#include "bootctl.h"

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

static const char *TAG = "bootctl";

#define NVS_NS         "bootctl"
#define NVS_KEY_RETURN "return_lbl"

#define BOOT_GPIO            GPIO_NUM_0
#define BOOT_ACTIVE_LEVEL    0
#define BOOT_POLL_MS         25
#define BOOT_LONGPRESS_MS    2000

static TaskHandle_t s_bootctl_task = NULL;

esp_err_t bootctl_store_return_label(const char *label)
{
    if (!label || label[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open(%s) failed: %s", NVS_NS, esp_err_to_name(err));
        return err;
    }

    err = nvs_set_str(h, NVS_KEY_RETURN, label);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_set_str(%s) failed: %s", NVS_KEY_RETURN, esp_err_to_name(err));
        nvs_close(h);
        return err;
    }

    err = nvs_commit(h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_commit failed: %s", esp_err_to_name(err));
    }
    nvs_close(h);
    return err;
}

esp_err_t bootctl_get_return_label(char *out_label, size_t out_len)
{
    if (!out_label || out_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    out_label[0] = '\0';

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err != ESP_OK) {
        return err;
    }

    size_t len = out_len;
    err = nvs_get_str(h, NVS_KEY_RETURN, out_label, &len);
    nvs_close(h);

    if (err == ESP_OK && out_label[0] == '\0') {
        return ESP_ERR_NOT_FOUND;
    }
    return err;
}

static const esp_partition_t *find_factory_partition(void)
{
    const esp_partition_t *p =
        esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);

    if (!p) {
        p = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, "factory");
    }
    return p;
}

void bootctl_switch_to_recovery(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running && running->label[0] != '\0') {
        ESP_LOGI(TAG, "Stored return label: %s", running->label);
        bootctl_store_return_label(running->label);
    } else {
        ESP_LOGW(TAG, "Could not read running partition label");
    }

    const esp_partition_t *factory = find_factory_partition();
    if (!factory) {
        ESP_LOGE(TAG, "Factory partition not found; cannot switch to recovery.");
        return;
    }

    ESP_LOGW(TAG, "Switching boot partition to FACTORY (recovery) and rebooting...");

    esp_err_t err = esp_ota_set_boot_partition(factory);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition(factory) failed: %s", esp_err_to_name(err));
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
}

esp_err_t bootctl_mark_app_valid(void)
{
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Health checks OK; marking app valid");
    } else if (err == ESP_ERR_OTA_ROLLBACK_INVALID_STATE) {
        ESP_LOGI(TAG, "App not in pending-verify state (ok)");
        err = ESP_OK;
    } else {
        ESP_LOGW(TAG, "esp_ota_mark_app_valid_cancel_rollback returned: %s", esp_err_to_name(err));
    }
    return err;
}

static void bootctl_task(void *arg)
{
    (void)arg;

    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        bootctl_switch_to_recovery();
    }
}

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

                vTaskDelay(pdMS_TO_TICKS(250));
                vTaskSuspend(NULL);
            }
        } else {
            held = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(BOOT_POLL_MS));
    }
}

esp_err_t bootctl_init(void)
{
    bootctl_mark_app_valid();

    BaseType_t ok = xTaskCreatePinnedToCore(
        bootctl_task,
        "bootctl",
        8192,
        NULL,
        10,
        &s_bootctl_task,
        0
    );
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create bootctl task");
        return ESP_FAIL;
    }

    ok = xTaskCreatePinnedToCore(
        boot_button_task,
        "boot_btn",
        4096,
        NULL,
        9,
        NULL,
        0
    );
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create boot_btn task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Boot control initialized");
    return ESP_OK;
}
