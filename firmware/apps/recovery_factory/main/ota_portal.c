#include <string.h>
#include <sys/param.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"

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

/* ========= Minimal HTML UI ========= */
static const char INDEX_HTML[] =
"<!doctype html><html><head><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>ESP Recovery OTA</title></head><body>"
"<h2>ESP Recovery OTA</h2>"
"<p>Select a firmware <code>.bin</code> and upload.</p>"
"<input id='f' type='file' accept='.bin'/>"
"<button onclick='u()'>Upload</button>"
"<pre id='o'></pre>"
"<script>"
"async function u(){"
" const f=document.getElementById('f').files[0];"
" if(!f){alert('Pick a .bin');return;}"
" const o=document.getElementById('o');"
" o.textContent='Uploading '+f.name+' ('+f.size+' bytes)...\\n';"
" const r=await fetch('/update',{method:'POST',headers:{'X-OTA-Token':'" OTA_TOKEN "'},body:f});"
" o.textContent+=await r.text();"
"}"
"</script></body></html>";

/* ========= SoftAP init (based on ESP-IDF softAP example patterns) ========= */
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

/* ========= HTTP handlers ========= */
static esp_err_t index_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t update_post_handler(httpd_req_t *req)
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

    ESP_LOGI(TAG, "Writing to partition: %s @ 0x%lx, size=0x%lx",
             update_part->label, (unsigned long)update_part->address, (unsigned long)update_part->size);

    /* Enforce size if Content-Length is present */
    if (req->content_len > 0 && (size_t)req->content_len > update_part->size) {
        httpd_resp_set_status(req, "413 Payload Too Large");
        return send_text(req, "Firmware too large for OTA slot\n");
    }

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

    int remaining = req->content_len;
    while (remaining > 0) {
        int to_read = MIN(remaining, buf_sz);
        int r = httpd_req_recv(req, (char *)buf, to_read);
        if (r < 0) {
            free(buf);
            esp_ota_end(ota_handle);
            httpd_resp_set_status(req, "500 Internal Server Error");
            return send_text(req, "httpd_req_recv failed\n");
        } else if (r == 0) {
            break; /* connection closed */
        }

        err = esp_ota_write(ota_handle, buf, r);
        if (err != ESP_OK) {
            free(buf);
            esp_ota_end(ota_handle);
            httpd_resp_set_status(req, "500 Internal Server Error");
            return send_text(req, "esp_ota_write failed\n");
        }
        remaining -= r;
    }

    free(buf);

    err = esp_ota_end(ota_handle); /* validates image */
    if (err != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return send_text(req, "esp_ota_end failed (image invalid?)\n");
    }

    err = esp_ota_set_boot_partition(update_part);
    if (err != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return send_text(req, "esp_ota_set_boot_partition failed\n");
    }

    /* Respond before reboot */
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "OK. Update written. Rebooting into new firmware...\n");
    ESP_LOGW(TAG, "Rebooting now");
    vTaskDelay(pdMS_TO_TICKS(250));
    esp_restart();
    return ESP_OK;
}

/* Optional: go back to “previous app” if main app stored a return label */
static esp_err_t reboot_back_post_handler(httpd_req_t *req)
{
    if (!token_ok(req)) {
        httpd_resp_set_status(req, "401 Unauthorized");
        return send_text(req, "Unauthorized\n");
    }

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        httpd_resp_set_status(req, "404 Not Found");
        return send_text(req, "No return target stored\n");
    }

    char lbl[16] = {0};
    size_t len = sizeof(lbl);
    esp_err_t err = nvs_get_str(h, NVS_KEY_RETURN, lbl, &len);
    nvs_close(h);
    if (err != ESP_OK || lbl[0] == '\0') {
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
    cfg.max_uri_handlers = 8;
    cfg.stack_size = 8192;         /* bump for upload handler */
    cfg.recv_wait_timeout = 15;
    cfg.send_wait_timeout = 15;

    httpd_handle_t server = NULL;
    ESP_ERROR_CHECK(httpd_start(&server, &cfg));

    httpd_uri_t uri_index = {
        .uri      = "/",
        .method   = HTTP_GET,
        .handler  = index_get_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &uri_index);

    httpd_uri_t uri_update = {
        .uri      = "/update",
        .method   = HTTP_POST,
        .handler  = update_post_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &uri_update);

    httpd_uri_t uri_back = {
        .uri      = "/reboot_back",
        .method   = HTTP_POST,
        .handler  = reboot_back_post_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &uri_back);

    ESP_LOGI(TAG, "HTTP server started");
    return server;
}

void app_main(void)
{
    /* NVS is required by Wi-Fi and useful for any boot control */
    ESP_ERROR_CHECK(nvs_flash_init());

    wifi_init_softap();
    start_webserver();

    ESP_LOGI(TAG, "Open http://192.168.4.1/ in a browser");
}
