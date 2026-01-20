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
#include "pid_controller.h"
#include "machine_state.h"

static const char *TAG = "main_app";

/* State change callback - update LED and emit events */
static void on_state_change(machine_state_t old_state, machine_state_t new_state)
{
    ESP_LOGI(TAG, "Machine state: %s -> %s",
             machine_state_to_str(old_state),
             machine_state_to_str(new_state));

    /* Update status LED based on new state */
    switch (new_state) {
        case MACHINE_STATE_IDLE:
            if (ble_gatt_is_connected()) {
                status_led_set_state(LED_STATE_CONNECTED_HEALTHY);
            } else {
                status_led_set_state(LED_STATE_IDLE_ADVERTISING);
            }
            break;

        case MACHINE_STATE_PRECOOL:
        case MACHINE_STATE_RUNNING:
            /* Could add a specific "running" LED pattern */
            status_led_set_state(LED_STATE_CONNECTED_HEALTHY);
            break;

        case MACHINE_STATE_STOPPING:
            status_led_set_state(LED_STATE_CONNECTED_WARNING);
            break;

        case MACHINE_STATE_E_STOP:
            status_led_set_state(LED_STATE_ERROR_CRITICAL);
            break;

        case MACHINE_STATE_FAULT:
            status_led_set_state(LED_STATE_ERROR_HW_FAULT);
            break;

        case MACHINE_STATE_SERVICE:
            status_led_set_state(LED_STATE_SERVICE_MODE);
            break;

        default:
            break;
    }
}

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

    // Initialize PID controller manager (RS-485 Modbus to LC108 controllers)
    // Uses default config: addresses 1, 2, 3 polled every 300ms
    ret = pid_controller_init(NULL);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "PID controller manager initialized");
        // Enable real PID data in telemetry
        telemetry_use_real_pid(true);
    } else {
        ESP_LOGW(TAG, "PID controller init failed: %s - using mock data",
                 esp_err_to_name(ret));
    }

    // Initialize machine state manager (MCU-resident state machine)
    ret = machine_state_init();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Machine state manager initialized");
        // Register state change callback for LED updates
        machine_state_set_callback(on_state_change);
        // Enable machine state in telemetry
        telemetry_use_machine_state(true);
    } else {
        ESP_LOGW(TAG, "Machine state init failed: %s - state machine disabled",
                 esp_err_to_name(ret));
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

    // Sync telemetry di_bits from machine state (which reads hardware)
    uint16_t initial_di_bits = machine_state_read_di_bits();
    telemetry_set_di_bits(initial_di_bits);
    ESP_LOGI(TAG, "Initial DI state synced to telemetry: di_bits=0x%04X", initial_di_bits);

    // Boot complete - flash green 3x then transition to advertising mode
    status_led_set_state(LED_STATE_BOOT_COMPLETE);
    vTaskDelay(pdMS_TO_TICKS(700));  // Allow boot complete pattern to finish

    // Enter normal operation - idle advertising (cyan breathing)
    status_led_set_state(LED_STATE_IDLE_ADVERTISING);

    ESP_LOGI(TAG, "Main app running - BLE advertising started (state=%s)",
             machine_state_to_str(machine_state_get()));
}
