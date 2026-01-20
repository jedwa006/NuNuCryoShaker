#include "telemetry.h"
#include "wire_protocol.h"
#include "ble_gatt.h"
#include "session_mgr.h"
#include "pid_controller.h"

#include "esp_log.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "telemetry";

static TaskHandle_t s_task_handle = NULL;
static bool s_running = false;
static uint16_t s_tx_seq = 0;

/* Telemetry state */
static uint16_t s_di_bits = 0;
static uint16_t s_ro_bits = 0;
static uint32_t s_alarm_bits = 0;

/* Use real PID controller data when available */
static bool s_use_real_pid = false;

/* Use machine state in telemetry when available */
static bool s_use_machine_state = false;

/* Forward declaration for machine state integration */
/* These are weak symbols that will resolve to actual functions when machine_state is linked */
__attribute__((weak)) void machine_state_get_run_info(void *out_info) {
    (void)out_info;
}

__attribute__((weak)) uint8_t machine_state_get(void) {
    return 0; /* MACHINE_STATE_IDLE */
}

/* Build controller data from PID controller state */
static uint8_t build_controller_data(wire_controller_data_t *out, uint8_t max_count)
{
    uint8_t count = 0;

    if (s_use_real_pid) {
        /* Get real data from PID controllers */
        for (int i = 0; i < max_count && i < PID_MAX_CONTROLLERS; i++) {
            pid_controller_t ctrl;
            if (pid_controller_get(i, &ctrl) == ESP_OK) {
                if (ctrl.state == PID_STATE_ONLINE || ctrl.state == PID_STATE_STALE) {
                    out[count].controller_id = ctrl.addr;
                    out[count].pv_x10 = (int16_t)(ctrl.data.pv * 10.0f);
                    out[count].sv_x10 = (int16_t)(ctrl.data.sv * 10.0f);
                    out[count].op_x10 = (uint16_t)(ctrl.data.output_pct * 10.0f);
                    out[count].mode = ctrl.data.mode;
                    out[count].age_ms = pid_controller_data_age_ms(ctrl.addr);
                    count++;
                }
            }
        }
        return count;
    }

    /* Fallback: mock data for testing without hardware */
    static int16_t pv_offset = 0;
    static int8_t pv_direction = 1;
    pv_offset += pv_direction;
    if (pv_offset > 20 || pv_offset < -20) {
        pv_direction = -pv_direction;
    }

    out[0].controller_id = 3;
    out[0].pv_x10 = 250 + pv_offset;
    out[0].sv_x10 = 300;
    out[0].op_x10 = 456;
    out[0].mode = CTRL_MODE_AUTO;
    out[0].age_ms = 50;

    return 1;
}

static void telemetry_task(void *arg)
{
    (void)arg;

    uint8_t frame[WIRE_MAX_FRAME_SIZE];
    wire_controller_data_t controllers[3];
    TickType_t last_wake = xTaskGetTickCount();

    ESP_LOGI(TAG, "Telemetry task started (real_pid=%d, machine_state=%d)",
             s_use_real_pid, s_use_machine_state);

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

        /* Check for PID controller alarms - use individual fault bits */
        if (s_use_real_pid && pid_controller_any_alarm()) {
            /* Set a generic PID fault bit - could be extended to per-controller */
            s_alarm_bits |= ALARM_BIT_PID1_FAULT;
        } else {
            s_alarm_bits &= ~(ALARM_BIT_PID1_FAULT | ALARM_BIT_PID2_FAULT | ALARM_BIT_PID3_FAULT);
        }

        /* Only send telemetry if connected and subscribed */
        if (ble_gatt_is_connected() && ble_gatt_telemetry_subscribed()) {
            /* Get timestamp in milliseconds */
            uint32_t timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000);

            /* Get controller data */
            uint8_t controller_count = build_controller_data(controllers, 3);

            size_t frame_len;

            if (s_use_machine_state) {
                /* Build extended telemetry with machine state */
                wire_telemetry_run_state_t run_state = {0};

                /* Get machine state info - use the actual struct type */
                /* We cast to void* because the weak symbol doesn't know the type */
                typedef struct {
                    uint8_t  state;
                    uint8_t  run_mode;
                    uint32_t run_elapsed_ms;
                    uint32_t run_remaining_ms;
                    int16_t  target_temp_x10;
                    uint8_t  recipe_step;
                    uint8_t  interlock_bits;
                } machine_run_info_internal_t;

                machine_run_info_internal_t info = {0};
                machine_state_get_run_info(&info);

                run_state.machine_state = info.state;
                run_state.run_elapsed_ms = info.run_elapsed_ms;
                run_state.run_remaining_ms = info.run_remaining_ms;
                run_state.target_temp_x10 = info.target_temp_x10;
                run_state.recipe_step = info.recipe_step;
                run_state.interlock_bits = info.interlock_bits;

                frame_len = wire_build_telemetry_ext(
                    frame, sizeof(frame),
                    s_tx_seq++,
                    timestamp_ms,
                    s_di_bits,
                    s_ro_bits,
                    s_alarm_bits,
                    controllers,
                    controller_count,
                    &run_state
                );
            } else {
                /* Build basic telemetry */
                frame_len = wire_build_telemetry(
                    frame, sizeof(frame),
                    s_tx_seq++,
                    timestamp_ms,
                    s_di_bits,
                    s_ro_bits,
                    s_alarm_bits,
                    controllers,
                    controller_count
                );
            }

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

uint32_t telemetry_get_alarm_bits(void)
{
    return s_alarm_bits;
}

uint16_t telemetry_get_di_bits(void)
{
    return s_di_bits;
}

void telemetry_use_real_pid(bool enable)
{
    s_use_real_pid = enable;
    ESP_LOGI(TAG, "Real PID data: %s", enable ? "enabled" : "disabled");
}

void telemetry_use_machine_state(bool enable)
{
    s_use_machine_state = enable;
    ESP_LOGI(TAG, "Machine state in telemetry: %s", enable ? "enabled" : "disabled");
}
