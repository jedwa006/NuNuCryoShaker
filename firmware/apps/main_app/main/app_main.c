#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "fw_version.h"
#include "bootctl.h"
#include "ble_gatt.h"
#include "telemetry.h"
#include "relay_ctrl.h"
#include "status_led.h"

static const char *TAG = "main_app";

void app_main(void)
{
    // Log firmware version first thing
    fw_version_log();

    // Initialize status LED first - show power on immediately
    esp_err_t ret = status_led_init();
    if (ret == ESP_OK) {
        status_led_set_state(LED_STATE_BOOT_POWER_ON);
    } else {
        ESP_LOGW(TAG, "Status LED init failed: %s", esp_err_to_name(ret));
    }

    // Hardware initialization phase
    status_led_set_state(LED_STATE_BOOT_HW_INIT);

    // NVS init (required before bootctl and BLE)
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize boot control (handles rollback validation + BOOT button monitoring)
    ESP_ERROR_CHECK(bootctl_init());

    // Initialize relay control (TCA9554 I/O expander for hardware relays)
    ret = relay_ctrl_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Relay control init failed: %s - continuing without hardware relay support",
                 esp_err_to_name(ret));
        // Continue anyway - software-only mode for testing
    }

    // BLE initialization phase
    status_led_set_state(LED_STATE_BOOT_BLE_INIT);

    // Initialize BLE GATT server
    ESP_ERROR_CHECK(ble_gatt_init());

    // Start telemetry generation (10Hz)
    ESP_ERROR_CHECK(telemetry_init());

    // Sync telemetry ro_bits with actual relay hardware state
    // This ensures the app sees the correct initial state on connect
    uint8_t initial_ro_bits = relay_ctrl_get_state();
    telemetry_set_ro_bits(initial_ro_bits);
    ESP_LOGI(TAG, "Initial relay state synced to telemetry: ro_bits=0x%02X", initial_ro_bits);

    // Boot complete - flash green 3x then transition to advertising mode
    status_led_set_state(LED_STATE_BOOT_COMPLETE);
    vTaskDelay(pdMS_TO_TICKS(700));  // Allow boot complete pattern to finish

    // Enter normal operation - idle advertising (cyan breathing)
    status_led_set_state(LED_STATE_IDLE_ADVERTISING);

    ESP_LOGI(TAG, "Main app running - BLE advertising started");
}
