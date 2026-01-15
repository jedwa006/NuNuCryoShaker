#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "main_app";
#define NVS_NS           "bootctl"
#define NVS_KEY_RETURN   "return_lbl"

/* Store which partition we’re currently running from, so factory can reboot back */
static void store_return_partition_label(void)
{
    const esp_partition_t *run = esp_ota_get_running_partition();
    if (!run) return;

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;

    /* run->label is typically "ota_0" or "ota_1" once OTA is in use */
    nvs_set_str(h, NVS_KEY_RETURN, run->label);
    nvs_commit(h);
    nvs_close(h);
}

static void maybe_confirm_ota_and_cancel_rollback(void)
{
    const esp_partition_t *run = esp_ota_get_running_partition();
    if (!run) return;

    esp_ota_img_states_t st = ESP_OTA_IMG_UNDEFINED;
    if (esp_ota_get_state_partition(run, &st) != ESP_OK) return;

    if (st == ESP_OTA_IMG_PENDING_VERIFY) {
        /* TODO: replace with real health checks:
           - init critical peripherals
           - confirm watchdogs running
           - confirm storage mounts
           - confirm main control loop started
        */
        bool ok = true;

        if (ok) {
            ESP_LOGI(TAG, "Health checks OK; marking app valid");
            ESP_ERROR_CHECK(esp_ota_mark_app_valid_cancel_rollback());
        } else {
            ESP_LOGE(TAG, "Health checks FAILED; rolling back on reboot");
            ESP_ERROR_CHECK(esp_ota_mark_app_invalid_rollback_and_reboot());
        }
    }
}

void reboot_to_factory_recovery(void)
{
    const esp_partition_t *factory = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
    if (!factory) {
        ESP_LOGE(TAG, "No factory partition found");
        return;
    }

    store_return_partition_label(); /* optional convenience */
    ESP_LOGW(TAG, "Rebooting into factory recovery (%s @ 0x%lx)",
             factory->label, (unsigned long)factory->address);

    ESP_ERROR_CHECK(esp_ota_set_boot_partition(factory));
    esp_restart();
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());

    /* Rollback flow: on first boot of a new OTA image, mark it valid once healthy */
    maybe_confirm_ota_and_cancel_rollback();

    /* Your normal app init continues here... */
    ESP_LOGI(TAG, "Main app running");
}
