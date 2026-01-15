#include <string.h>
#include <sys/param.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "mbedtls/sha256.h"

static const char *TAG = "ota_portal";

/* ========= User-configurable ========= */
#define OTA_AP_SSID      "ESP32S3-RECOVERY"
#define OTA_AP_PASS      "change-me-please"
#define OTA_AP_CHANNEL   6
#define OTA_AP_MAX_CONN  2

/* Simple header-based auth token (optional but recommended) */
#define OTA_TOKEN        "local-maint-token"

/* Store "return-to" partition label from main app (optional) */
#define NVS_NS           "bootctl"
#define NVS_KEY_RETURN   "return_lbl"

/* ========= Staging state (RAM only; cleared on reboot) ========= */
typedef struct {
    bool     valid;
    char     part_label[16];     // e.g. "ota_0"
    uint32_t bytes_written;
    uint32_t part_size;
    uint8_t  sha256[32];
} staged_update_t;

static staged_update_t g_stage = {0};

/* ========= Minimal HTML UI ========= */
static const char INDEX_HTML[] =
"<!doctype html><html><head><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>ESP Recovery OTA</title>"
"<style>"
" body{font-family:system-ui, -apple-system, sans-serif; margin:16px;}"
" button{margin:6px 6px 6px 0; padding:8px 12px;}"
" pre{background:#111; color:#eee; padding:12px; border-radius:8px; overflow:auto;}"
" code{background:#eee; padding:2px 4px; border-radius:4px;}"
"</style>"
"</head><body>"
"<h2>ESP Recovery OTA</h2>"
"<p>Upload a firmware <code>.bin</code> to stage it into the next OTA slot. Then click <b>Activate</b> to reboot into it.</p>"
"<p><small>This portal does not overwrite the factory/recovery partition.</small></p>"
"<input id='f' type='file' accept='.bin'/>"
"<div>"
"<button onclick='stage()'>Upload (Stage)</button>"
"<button onclick='activate()'>Activate & Reboot</button>"
"<button onclick='back()'>Reboot Back</button>"
"<button onclick='status()'>Refresh Status</button>"
"</div>"
"<pre id='o'>Ready.</pre>"
"<script>"
"const TOKEN = '" OTA_TOKEN "';"
"function setOut(s){ document.getElementById('o').textContent = s; }"
"async function status(){"
"  const r = await fetch('/status');"
"  setOut(await r.text());"
"}"
"async function stage(){"
"  const f = document.getElementById('f').files[0];"
"  if(!f){alert('Pick a .bin');return;}"
"  setOut('Staging '+f.name+' ('+f.size+' bytes)...\\n');"
"  const r = await fetch('/stage',{method:'POST',headers:{'X-OTA-Token':TOKEN},body:f});"
"  setOut(await r.text());"
"}"
"async function activate(){"
"  setOut('Activating staged firmware (if present)...\\n');"
"  const r = await fetch('/activate',{method:'POST',headers:{'X-OTA-Token':TOKEN}});"
"  setOut(await r.text());"
"}"
"async function back(){"
"  setOut('Rebooting back (if return label is stored)...\\n');"
"  const r = await fetch('/reboot_back',{method:'POST',headers:{'X-OTA-Token':TOKEN}});"
"  setOut(await r.text());"
"}"
"</script></body></html>";

/* ========= SoftAP init ========= */
static void wifi_init_softap(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = { 0 };
    strncpy((char *)wifi_config.ap.ssid, OTA_AP_SSID, sizeof(wifi_config.ap.ssid));
    strncpy((char *)wifi_config.ap.password, OTA_AP_PASS, sizeof(wifi_config.ap.password));
    wifi_config.ap.ssid_len = strlen(OTA_AP_SSID);
    wifi_config.ap.channel = OTA_AP_CHANNEL;
    wifi_config.ap.max_connection = OTA_AP_MAX_CONN;
    wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    if (strlen(OTA_AP_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "SoftAP started. SSID=%s IP=192.168.4.1", OTA_AP_SSID);
}

/* ========= Helpers ========= */
static esp_err_t send_text(httpd_req_t *req, const char *txt)
{
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req, txt, HTTPD_RESP_USE_STRLEN);
}

static bool token_ok(httpd_req_t *req)
{
    char token[64] = {0};
    if (httpd_req_get_hdr_value_str(req, "X-OTA-Token", token, sizeof(token)) != ESP_OK) {
        return false;
    }
    return (strcmp(token, OTA_TOKEN) == 0);
}

static void bytes_to_hex(const uint8_t *in, size_t in_len, char *out, size_t out_len)
{
    static const char *hex = "0123456789abcdef";
    size_t need = in_len * 2 + 1;
    if (out_len < need) {
        if (out_len > 0) out[0] = '\0';
        return;
    }
    for (size_t i = 0; i < in_len; i++) {
        out[i*2 + 0] = hex[(in[i] >> 4) & 0xF];
        out[i*2 + 1] = hex[(in[i] >> 0) & 0xF];
    }
    out[in_len*2] = '\0';
}

static bool nvs_get_return_label(char *out_lbl, size_t out_sz)
{
    if (!out_lbl || out_sz == 0) return false;
    out_lbl[0] = '\0';

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;

    size_t len = out_sz;
    esp_err_t err = nvs_get_str(h, NVS_KEY_RETURN, out_lbl, &len);
    nvs_close(h);

    return (err == ESP_OK && out_lbl[0] != '\0');
}

/* ========= HTTP handlers ========= */
static esp_err_t index_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t status_get_handler(httpd_req_t *req)
{
    char sha_hex[65] = {0};
    if (g_stage.valid) {
        bytes_to_hex(g_stage.sha256, sizeof(g_stage.sha256), sha_hex, sizeof(sha_hex));
    }

    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *boot    = esp_ota_get_boot_partition();

    char ret_lbl[16] = {0};
    bool have_ret = nvs_get_return_label(ret_lbl, sizeof(ret_lbl));

    char buf[896];
    snprintf(buf, sizeof(buf),
        "Recovery OTA Portal Status\n"
        "--------------------------\n"
        "Running partition: %s @ 0x%08" PRIx32 "\n"
        "Boot partition:    %s @ 0x%08" PRIx32 "\n"
        "Return label (NVS): %s\n"
        "\n"
        "Staged update:     %s\n"
        "  Partition:       %s\n"
        "  Bytes written:   %" PRIu32 "\n"
        "  Slot size:       %" PRIu32 "\n"
        "  Free after img:  %" PRIu32 "\n"
        "  Used:            %" PRIu32 "%%\n"
        "  SHA256:          %s\n",
        running ? running->label : "(unknown)", running ? running->address : 0,
        boot ? boot->label : "(unknown)", boot ? boot->address : 0,
        have_ret ? ret_lbl : "(none)",
        g_stage.valid ? "YES" : "NO",
        g_stage.valid ? g_stage.part_label : "(none)",
        g_stage.valid ? g_stage.bytes_written : 0,
        g_stage.valid ? g_stage.part_size : 0,
        g_stage.valid ? (g_stage.part_size - g_stage.bytes_written) : 0,
        g_stage.valid && g_stage.part_size ? (uint32_t)((100ULL * g_stage.bytes_written) / g_stage.part_size) : 0,
        g_stage.valid ? sha_hex : "(n/a)"
    );

    return send_text(req, buf);
}

/* Stage handler: write image to next OTA slot; DO NOT switch boot partition */
static esp_err_t stage_post_handler(httpd_req_t *req)
{
    if (!token_ok(req)) {
        httpd_resp_set_status(req, "401 Unauthorized");
        return send_text(req, "Unauthorized (missing/invalid X-OTA-Token)\n");
    }

    const esp_partition_t *update_part = esp_ota_get_next_update_partition(NULL);
    if (!update_part) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return send_text(req, "No OTA partition available\n");
    }

    // Safety: do not allow staging into factory/recovery by accident
    if (update_part->type != ESP_PARTITION_TYPE_APP ||
        !(update_part->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0 ||
          update_part->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1)) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return send_text(req, "Next update partition is not an OTA slot\n");
    }

    ESP_LOGI(TAG, "Staging to partition: %s @ 0x%lx, size=0x%lx",
             update_part->label, (unsigned long)update_part->address, (unsigned long)update_part->size);

    /* Enforce size if Content-Length is present */
    if (req->content_len > 0 && (size_t)req->content_len > update_part->size) {
        httpd_resp_set_status(req, "413 Payload Too Large");
        return send_text(req, "Firmware too large for OTA slot\n");
    }

    // Clear prior staging state
    memset(&g_stage, 0, sizeof(g_stage));

    esp_ota_handle_t ota_handle = 0;
    esp_err_t err = esp_ota_begin(update_part, OTA_SIZE_UNKNOWN, &ota_handle);
    if (err != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return send_text(req, "esp_ota_begin failed\n");
    }

    const int buf_sz = 4096;
    uint8_t *buf = (uint8_t *)malloc(buf_sz);
    if (!buf) {
        esp_ota_end(ota_handle);
        httpd_resp_set_status(req, "500 Internal Server Error");
        return send_text(req, "malloc failed\n");
    }

    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    if (mbedtls_sha256_starts(&sha, 0)) {
        free(buf);
        mbedtls_sha256_free(&sha);
        esp_ota_end(ota_handle);
        httpd_resp_set_status(req, "500 Internal Server Error");
        return send_text(req, "sha256 init failed\n");
    }

    uint32_t bytes_written = 0;
    int remaining = req->content_len;

    while (remaining > 0) {
        int to_read = MIN(remaining, buf_sz);
        int r = httpd_req_recv(req, (char *)buf, to_read);
        if (r < 0) {
            free(buf);
            mbedtls_sha256_free(&sha);
            esp_ota_end(ota_handle);
            httpd_resp_set_status(req, "500 Internal Server Error");
            return send_text(req, "httpd_req_recv failed\n");
        } else if (r == 0) {
            break; /* connection closed */
        }

        if (mbedtls_sha256_update(&sha, buf, (size_t)r)) {
            free(buf);
            mbedtls_sha256_free(&sha);
            esp_ota_end(ota_handle);
            httpd_resp_set_status(req, "500 Internal Server Error");
            return send_text(req, "sha256 update failed\n");
        }

        err = esp_ota_write(ota_handle, buf, r);
        if (err != ESP_OK) {
            free(buf);
            mbedtls_sha256_free(&sha);
            esp_ota_end(ota_handle);
            httpd_resp_set_status(req, "500 Internal Server Error");
            return send_text(req, "esp_ota_write failed\n");
        }

        bytes_written += (uint32_t)r;
        remaining -= r;
    }

    free(buf);

    if (mbedtls_sha256_finish(&sha, g_stage.sha256)) {
        mbedtls_sha256_free(&sha);
        esp_ota_end(ota_handle);
        httpd_resp_set_status(req, "500 Internal Server Error");
        return send_text(req, "sha256 finish failed\n");
    }
    mbedtls_sha256_free(&sha);

    err = esp_ota_end(ota_handle); /* validates image */
    if (err != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return send_text(req, "esp_ota_end failed (image invalid?)\n");
    }

    // Record staged info
    g_stage.valid = true;
    strncpy(g_stage.part_label, update_part->label, sizeof(g_stage.part_label));
    g_stage.part_label[sizeof(g_stage.part_label)-1] = '\0';
    g_stage.bytes_written = bytes_written;
    g_stage.part_size = (uint32_t)update_part->size;

    char sha_hex[65] = {0};
    bytes_to_hex(g_stage.sha256, sizeof(g_stage.sha256), sha_hex, sizeof(sha_hex));

    uint32_t free_after = (uint32_t)update_part->size - bytes_written;
    uint32_t used_pct = (update_part->size > 0)
        ? (uint32_t)((100ULL * bytes_written) / update_part->size)
        : 0;

    char resp[896];
    snprintf(resp, sizeof(resp),
        "STAGED OK\n"
        "  Slot:           %s\n"
        "  Slot address:   0x%08" PRIx32 "\n"
        "  Slot size:      %" PRIu32 " bytes\n"
        "  Image size:     %" PRIu32 " bytes\n"
        "  Free after img: %" PRIu32 " bytes\n"
        "  Used:           %" PRIu32 "%%\n"
        "  SHA256:         %s\n"
        "\n"
        "Next: click 'Activate & Reboot' to boot this image.\n",
        update_part->label,
        update_part->address,
        (uint32_t)update_part->size,
        bytes_written,
        free_after,
        used_pct,
        sha_hex
    );

    ESP_LOGI(TAG, "Staged image to %s: size=%" PRIu32 " sha256=%s", update_part->label, bytes_written, sha_hex);
    return send_text(req, resp);
}

/* Activate handler: switch boot partition to staged OTA slot and reboot */
static esp_err_t activate_post_handler(httpd_req_t *req)
{
    if (!token_ok(req)) {
        httpd_resp_set_status(req, "401 Unauthorized");
        return send_text(req, "Unauthorized (missing/invalid X-OTA-Token)\n");
    }

    if (!g_stage.valid || g_stage.part_label[0] == '\0') {
        httpd_resp_set_status(req, "409 Conflict");
        return send_text(req, "No staged firmware present. Upload first.\n");
    }

    const esp_partition_t *p = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, g_stage.part_label);

    if (!p) {
        httpd_resp_set_status(req, "404 Not Found");
        return send_text(req, "Staged partition not found in table\n");
    }

    esp_err_t err = esp_ota_set_boot_partition(p);
    if (err != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return send_text(req, "esp_ota_set_boot_partition failed\n");
    }

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "OK. Boot partition set. Rebooting now...\n");
    ESP_LOGW(TAG, "Activating %s and rebooting", p->label);

    vTaskDelay(pdMS_TO_TICKS(250));
    esp_restart();
    return ESP_OK;
}

/* Reboot back to the partition label stored by main_app */
static esp_err_t reboot_back_post_handler(httpd_req_t *req)
{
    if (!token_ok(req)) {
        httpd_resp_set_status(req, "401 Unauthorized");
        return send_text(req, "Unauthorized\n");
    }

    char lbl[16] = {0};
    if (!nvs_get_return_label(lbl, sizeof(lbl))) {
        httpd_resp_set_status(req, "404 Not Found");
        return send_text(req, "No return target stored\n");
    }

    const esp_partition_t *p = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, lbl);
    if (!p) {
        httpd_resp_set_status(req, "404 Not Found");
        return send_text(req, "Stored return partition not found\n");
    }

    ESP_ERROR_CHECK(esp_ota_set_boot_partition(p));
    httpd_resp_sendstr(req, "OK. Rebooting back...\n");
    vTaskDelay(pdMS_TO_TICKS(250));
    esp_restart();
    return ESP_OK;
}

static httpd_handle_t start_webserver(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.lru_purge_enable = true;
    cfg.max_uri_handlers = 10;
    cfg.stack_size = 8192;
    cfg.recv_wait_timeout = 15;
    cfg.send_wait_timeout = 15;

    httpd_handle_t server = NULL;
    ESP_ERROR_CHECK(httpd_start(&server, &cfg));

    httpd_register_uri_handler(server, &(httpd_uri_t){
        .uri="/", .method=HTTP_GET, .handler=index_get_handler, .user_ctx=NULL
    });

    httpd_register_uri_handler(server, &(httpd_uri_t){
        .uri="/status", .method=HTTP_GET, .handler=status_get_handler, .user_ctx=NULL
    });

    httpd_register_uri_handler(server, &(httpd_uri_t){
        .uri="/stage", .method=HTTP_POST, .handler=stage_post_handler, .user_ctx=NULL
    });

    httpd_register_uri_handler(server, &(httpd_uri_t){
        .uri="/activate", .method=HTTP_POST, .handler=activate_post_handler, .user_ctx=NULL
    });

    httpd_register_uri_handler(server, &(httpd_uri_t){
        .uri="/reboot_back", .method=HTTP_POST, .handler=reboot_back_post_handler, .user_ctx=NULL
    });

    ESP_LOGI(TAG, "HTTP server started");
    return server;
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    wifi_init_softap();
    start_webserver();

    ESP_LOGI(TAG, "Open http://192.168.4.1/ in a browser");
}
