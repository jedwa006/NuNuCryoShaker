#include "telemetry.h"
#include "wire_protocol.h"
#include "ble_gatt.h"
#include "session_mgr.h"

#include "esp_log.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "telemetry";

static TaskHandle_t s_task_handle = NULL;
static bool s_running = false;
static uint16_t s_tx_seq = 0;

/* Mock telemetry state */
static uint16_t s_di_bits = 0;
static uint16_t s_ro_bits = 0;
static uint32_t s_alarm_bits = 0;

/* Mock controller data - simulates a single temperature controller */
static wire_controller_data_t s_mock_controllers[1] = {
    {
        .controller_id = 3,     // Controller #3 per spec
        .pv_x10 = 250,          // 25.0°C
        .sv_x10 = 300,          // 30.0°C
        .op_x10 = 456,          // 45.6%
        .mode = CTRL_MODE_AUTO,
        .age_ms = 50,
    }
};

static void telemetry_task(void *arg)
{
    (void)arg;

    uint8_t frame[WIRE_MAX_FRAME_SIZE];
    TickType_t last_wake = xTaskGetTickCount();

    ESP_LOGI(TAG, "Telemetry task started");

    while (s_running) {
        /* Check session expiry */
        if (session_mgr_check_expiry()) {
            /* Session became stale - set HMI_NOT_LIVE alarm */
            s_alarm_bits |= ALARM_BIT_HMI_NOT_LIVE;
            ESP_LOGW(TAG, "Session expired - HMI_NOT_LIVE set");
        }

        /* Clear HMI_NOT_LIVE if session is live */
        if (session_mgr_is_live()) {
            s_alarm_bits &= ~ALARM_BIT_HMI_NOT_LIVE;
        }

        /* Only send telemetry if connected and subscribed */
        if (ble_gatt_is_connected() && ble_gatt_telemetry_subscribed()) {
            /* Get timestamp in milliseconds */
            uint32_t timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000);

            /* Simulate some variation in PV */
            static int16_t pv_offset = 0;
            static int8_t pv_direction = 1;
            pv_offset += pv_direction;
            if (pv_offset > 20 || pv_offset < -20) {
                pv_direction = -pv_direction;
            }
            s_mock_controllers[0].pv_x10 = 250 + pv_offset;

            /* Simulate age variation */
            s_mock_controllers[0].age_ms = 30 + (timestamp_ms % 40);

            /* Build telemetry frame */
            size_t frame_len = wire_build_telemetry(
                frame, sizeof(frame),
                s_tx_seq++,
                timestamp_ms,
                s_di_bits,
                s_ro_bits,
                s_alarm_bits,
                s_mock_controllers,
                1  /* controller_count */
            );

            if (frame_len > 0) {
                esp_err_t err = ble_gatt_send_telemetry(frame, frame_len);
                if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
                    ESP_LOGW(TAG, "Failed to send telemetry: %s", esp_err_to_name(err));
                }
            }
        }

        /* Sleep until next interval */
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TELEMETRY_INTERVAL_MS));
    }

    ESP_LOGI(TAG, "Telemetry task stopped");
    s_task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t telemetry_init(void)
{
    if (s_task_handle != NULL) {
        ESP_LOGW(TAG, "Telemetry already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    s_running = true;
    s_tx_seq = 0;

    BaseType_t ok = xTaskCreatePinnedToCore(
        telemetry_task,
        "telemetry",
        4096,
        NULL,
        5,      /* Lower priority than BLE */
        &s_task_handle,
        0
    );

    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create telemetry task");
        s_running = false;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Telemetry initialized (interval=%dms)", TELEMETRY_INTERVAL_MS);
    return ESP_OK;
}

void telemetry_stop(void)
{
    s_running = false;
    /* Task will delete itself */
}

void telemetry_set_di_bits(uint16_t bits)
{
    s_di_bits = bits;
}

void telemetry_set_ro_bits(uint16_t bits)
{
    s_ro_bits = bits;
}

void telemetry_set_alarm_bits(uint32_t bits)
{
    s_alarm_bits = bits;
}

uint16_t telemetry_get_ro_bits(void)
{
    return s_ro_bits;
}
