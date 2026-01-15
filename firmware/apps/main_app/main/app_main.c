#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"

#include "nvs.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"

static const char *TAG = "main_app";

/* ===================== Boot control NVS ===================== */
#define NVS_NS         "bootctl"
#define NVS_KEY_RETURN "return_lbl"

/* ===================== BOOT button handling =====================
   On most ESP32-S3 boards, BOOT is GPIO0 and active-low.
   If Waveshare routes it differently, adjust BOOT_GPIO accordingly.
*/
#define BOOT_GPIO            GPIO_NUM_0
#define BOOT_ACTIVE_LEVEL    0
#define BOOT_POLL_MS         25
#define BOOT_LONGPRESS_MS    2000

/* Future option: use a dedicated GPIO instead of BOOT
   (e.g., through your isolated DIN path). Leave commented for now.
*/
// #define USE_DEDICATED_OTA_GPIO 1
// #define OTA_GPIO              GPIO_NUM_21
// #define OTA_ACTIVE_LEVEL      1

static TaskHandle_t s_bootctl_task = NULL;

/* ===================== Helpers ===================== */

static void store_return_label(const char *lbl)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open(%s) failed: %s", NVS_NS, esp_err_to_name(err));
        return;
    }

    err = nvs_set_str(h, NVS_KEY_RETURN, lbl);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_set_str(%s) failed: %s", NVS_KEY_RETURN, esp_err_to_name(err));
        nvs_close(h);
        return;
    }

    err = nvs_commit(h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_commit failed: %s", esp_err_to_name(err));
    }
    nvs_close(h);
}

static const esp_partition_t *find_factory_partition(void)
{
    const esp_partition_t *p =
        esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);

    if (!p) {
        // Fallback by label (only if you truly need it)
        p = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, "factory");
    }
    return p;
}

/* ===================== Boot control task ===================== */

static void bootctl_task(void *arg)
{
    (void)arg;

    for (;;) {
        // Wait for a "go to recovery" request from the button task
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        const esp_partition_t *running = esp_ota_get_running_partition();
        if (running && running->label[0] != '\0') {
            ESP_LOGI(TAG, "Stored return label: %s", running->label);
            store_return_label(running->label);
        } else {
            ESP_LOGW(TAG, "Could not read running partition label");
        }

        const esp_partition_t *factory = find_factory_partition();
        if (!factory) {
            ESP_LOGE(TAG, "Factory partition not found; cannot switch to recovery.");
            continue;
        }

        ESP_LOGW(TAG, "Switching boot partition to FACTORY (recovery) and rebooting...");

        // Validation + boot selection (keep this OUT of the small button task stack)
        esp_err_t err = esp_ota_set_boot_partition(factory);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_set_boot_partition(factory) failed: %s", esp_err_to_name(err));
            continue;
        }

        vTaskDelay(pdMS_TO_TICKS(200));
        esp_restart();
    }
}

/* ===================== BOOT button task ===================== */

static void boot_button_task(void *arg)
{
    (void)arg;

    gpio_config_t io = {
        .pin_bit_mask = 1ULL << BOOT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));

    const int needed_ticks = BOOT_LONGPRESS_MS / BOOT_POLL_MS;
    int held = 0;

    for (;;) {
        int lvl = gpio_get_level(BOOT_GPIO);

        if (lvl == BOOT_ACTIVE_LEVEL) {
            held++;
            if (held >= needed_ticks) {
                ESP_LOGW(TAG, "BOOT long-press detected (%d ms)", BOOT_LONGPRESS_MS);

                if (s_bootctl_task) {
                    xTaskNotifyGive(s_bootctl_task);
                } else {
                    ESP_LOGE(TAG, "bootctl task handle is NULL");
                }

                // One-shot: give time for logs to flush, then suspend this watcher
                vTaskDelay(pdMS_TO_TICKS(250));
                vTaskSuspend(NULL);
            }
        } else {
            held = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(BOOT_POLL_MS));
    }
}

/* ===================== Health / rollback handling ===================== */

static void mark_app_valid_if_needed(void)
{
    // If rollback is enabled and this boot is "pending verify", mark it valid here.
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Health checks OK; marking app valid");
    } else if (err == ESP_ERR_OTA_ROLLBACK_INVALID_STATE) {
        ESP_LOGI(TAG, "App not in pending-verify state (ok): %s", esp_err_to_name(err));
    } else {
        ESP_LOGW(TAG, "esp_ota_mark_app_valid_cancel_rollback returned: %s", esp_err_to_name(err));
    }
}

void app_main(void)
{
    // NVS init
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Mark valid early (supports rollback-enabled OTA workflow)
    mark_app_valid_if_needed();

    // Boot control task (bigger stack; does NVS + esp_ota_set_boot_partition + reboot)
    BaseType_t ok = xTaskCreatePinnedToCore(
        bootctl_task,
        "bootctl",
        8192,
        NULL,
        10,
        &s_bootctl_task,
        0
    );
    ESP_ERROR_CHECK(ok == pdPASS ? ESP_OK : ESP_FAIL);

    // BOOT button watcher (small stack; only detects press and notifies bootctl)
    ok = xTaskCreatePinnedToCore(
        boot_button_task,
        "boot_btn",
        4096,
        NULL,
        9,
        NULL,
        0
    );
    ESP_ERROR_CHECK(ok == pdPASS ? ESP_OK : ESP_FAIL);

    ESP_LOGI(TAG, "Main app running");
}
