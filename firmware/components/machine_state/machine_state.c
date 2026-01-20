#include "machine_state.h"
#include "relay_ctrl.h"
#include "session_mgr.h"
#include "telemetry.h"
#include "wire_protocol.h"
#include "pid_controller.h"
#include "ble_gatt.h"
#include "safety_gate.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "machine_state";

/* Digital Input GPIO mapping for Waveshare ESP32-S3-ETH-8DI-8RO */
/* Note: Actual GPIO pins depend on hardware - these are placeholders */
/* DI1-DI8 typically connect through TCA9534 I2C expander at address 0x21 */
#define DI_EXPANDER_ADDR    0x21    /* Separate from relay expander at 0x20 */

/* State machine task parameters */
#define STATE_TASK_STACK_SIZE   4096
#define STATE_TASK_PRIORITY     6       /* Higher than telemetry (5) */
#define STATE_POLL_INTERVAL_MS  50      /* 20 Hz state machine tick */

/* Precool parameters */
#define PRECOOL_TARGET_TEMP_X10     (-500)  /* -50.0°C default precool target */
#define PRECOOL_TIMEOUT_MS          (300000) /* 5 minute precool timeout */
#define PRECOOL_TEMP_TOLERANCE_X10  50      /* ±5°C tolerance for temp reached */

/* Stopping phase parameters */
#define STOPPING_SOAK_TIME_MS       (30000) /* 30 second thermal soak */

/* State machine state */
static machine_state_t s_state = MACHINE_STATE_IDLE;
static run_mode_t s_run_mode = RUN_MODE_NORMAL;
static int64_t s_run_start_us = 0;
static uint32_t s_run_duration_ms = 0;
static int16_t s_target_temp_x10 = 0;
static int64_t s_state_enter_us = 0;

/* Digital input state (cached from last read) */
static uint16_t s_di_bits = 0xFF;   /* All HIGH = safe default */

/* Mutex for state access */
static SemaphoreHandle_t s_mutex = NULL;

/* State change callback */
static machine_state_cb_t s_state_callback = NULL;

/* Task handle */
static TaskHandle_t s_task_handle = NULL;
static bool s_running = false;

/* Event sequence counter */
static uint16_t s_event_seq = 0;

/* State name strings */
static const char *state_names[] = {
    [MACHINE_STATE_IDLE]     = "IDLE",
    [MACHINE_STATE_PRECOOL]  = "PRECOOL",
    [MACHINE_STATE_RUNNING]  = "RUNNING",
    [MACHINE_STATE_STOPPING] = "STOPPING",
    [MACHINE_STATE_E_STOP]   = "E_STOP",
    [MACHINE_STATE_FAULT]    = "FAULT",
    [MACHINE_STATE_SERVICE]  = "SERVICE",
};

/* PID controller address for chamber temperature */
#define CHAMBER_PID_ADDR    1

/* Forward declarations */
static void state_task(void *arg);
static void transition_to(machine_state_t new_state);
static void set_outputs_safe(void);
static void update_di_bits(void);
static bool check_estop_active(void);
static bool check_door_open(void);
static bool check_motor_fault(void);
static bool check_ln2_present(void);
static bool get_chamber_temp(int16_t *temp_x10);

const char *machine_state_to_str(machine_state_t state)
{
    if (state < MACHINE_STATE_MAX) {
        return state_names[state];
    }
    return "UNKNOWN";
}

esp_err_t machine_state_init(void)
{
    if (s_mutex != NULL) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    /* Initialize state */
    s_state = MACHINE_STATE_IDLE;
    s_state_enter_us = esp_timer_get_time();

    /* Read initial DI state */
    update_di_bits();

    /* Check if E-stop is active on startup */
    if (check_estop_active()) {
        ESP_LOGW(TAG, "E-Stop active on startup");
        s_state = MACHINE_STATE_E_STOP;
        set_outputs_safe();
    }

    /* Start state machine task */
    s_running = true;
    BaseType_t ok = xTaskCreatePinnedToCore(
        state_task,
        "machine_state",
        STATE_TASK_STACK_SIZE,
        NULL,
        STATE_TASK_PRIORITY,
        &s_task_handle,
        0
    );

    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create state task");
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Machine state initialized: state=%s", machine_state_to_str(s_state));
    return ESP_OK;
}

machine_state_t machine_state_get(void)
{
    return s_state;
}

void machine_state_get_run_info(machine_run_info_t *out_info)
{
    if (out_info == NULL) return;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    out_info->state = s_state;
    out_info->run_mode = s_run_mode;
    out_info->target_temp_x10 = s_target_temp_x10;
    out_info->recipe_step = 0;  /* Not implemented yet */
    out_info->interlock_bits = machine_state_get_interlocks();

    /* Calculate elapsed/remaining time */
    if (s_run_start_us > 0 && (s_state == MACHINE_STATE_PRECOOL ||
                                s_state == MACHINE_STATE_RUNNING)) {
        int64_t elapsed_us = esp_timer_get_time() - s_run_start_us;
        out_info->run_elapsed_ms = (uint32_t)(elapsed_us / 1000);

        if (s_run_duration_ms > 0 && out_info->run_elapsed_ms < s_run_duration_ms) {
            out_info->run_remaining_ms = s_run_duration_ms - out_info->run_elapsed_ms;
        } else {
            out_info->run_remaining_ms = 0;
        }
    } else {
        out_info->run_elapsed_ms = 0;
        out_info->run_remaining_ms = 0;
    }

    xSemaphoreGive(s_mutex);
}

uint8_t machine_state_get_interlocks(void)
{
    uint8_t interlocks = 0;

    if (check_estop_active()) {
        interlocks |= INTERLOCK_BIT_ESTOP;
    }
    if (check_door_open()) {
        interlocks |= INTERLOCK_BIT_DOOR_OPEN;
    }
    if (!check_ln2_present()) {
        interlocks |= INTERLOCK_BIT_LN2_ABSENT;
    }
    /* Motor fault check disabled - soft starter has no fault output.
     * INTERLOCK_BIT_MOTOR_FAULT reserved for future accelerometer-based detection.
     * if (check_motor_fault()) {
     *     interlocks |= INTERLOCK_BIT_MOTOR_FAULT;
     * }
     */
    if (!session_mgr_is_live()) {
        interlocks |= INTERLOCK_BIT_HMI_STALE;
    }

    return interlocks;
}

bool machine_state_start_allowed(void)
{
    /* Use the safety gate framework for comprehensive start checks.
     * This evaluates E-stop, door, HMI, and PID-related gates based on
     * configured capability levels and gate bypass states. */
    return safety_gate_can_start_run(NULL);
}

esp_err_t machine_state_start_run(uint32_t session_id, run_mode_t mode,
                                   int16_t target_temp_x10, uint32_t run_duration_ms)
{
    /* Validate session */
    if (!session_mgr_is_valid(session_id)) {
        ESP_LOGW(TAG, "START_RUN rejected: invalid session");
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    /* Check current state */
    if (s_state != MACHINE_STATE_IDLE) {
        ESP_LOGW(TAG, "START_RUN rejected: not in IDLE (state=%s)",
                 machine_state_to_str(s_state));
        xSemaphoreGive(s_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    /* Check interlocks */
    if (!machine_state_start_allowed()) {
        uint8_t interlocks = machine_state_get_interlocks();
        ESP_LOGW(TAG, "START_RUN rejected: interlocks=0x%02X", interlocks);
        xSemaphoreGive(s_mutex);
        return ESP_ERR_NOT_ALLOWED;
    }

    /* Warn if LN2 not present but don't block */
    if (!check_ln2_present()) {
        ESP_LOGW(TAG, "Warning: LN2 not present, cooling may be impaired");
    }

    /* Store run parameters */
    s_run_mode = mode;
    s_target_temp_x10 = (target_temp_x10 != 0) ? target_temp_x10 : PRECOOL_TARGET_TEMP_X10;
    s_run_duration_ms = run_duration_ms;
    s_run_start_us = esp_timer_get_time();

    /* Transition to PRECOOL */
    transition_to(MACHINE_STATE_PRECOOL);

    ESP_LOGI(TAG, "Run started: mode=%d target_temp=%d.%d run_duration=%lums",
             mode, s_target_temp_x10 / 10, abs(s_target_temp_x10 % 10),
             (unsigned long)run_duration_ms);

    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t machine_state_stop_run(uint32_t session_id, stop_mode_t mode)
{
    /* Validate session */
    if (!session_mgr_is_valid(session_id)) {
        ESP_LOGW(TAG, "STOP_RUN rejected: invalid session");
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    /* Check if we're in a stoppable state */
    if (s_state != MACHINE_STATE_PRECOOL &&
        s_state != MACHINE_STATE_RUNNING) {
        ESP_LOGW(TAG, "STOP_RUN ignored: state=%s", machine_state_to_str(s_state));
        xSemaphoreGive(s_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    if (mode == STOP_MODE_ABORT) {
        /* Fast stop - go directly to safe state */
        ESP_LOGW(TAG, "ABORT requested - immediate stop");
        set_outputs_safe();
        transition_to(MACHINE_STATE_IDLE);
    } else {
        /* Normal stop - go through STOPPING phase */
        transition_to(MACHINE_STATE_STOPPING);
    }

    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t machine_state_enter_service(uint32_t session_id)
{
    if (!session_mgr_is_valid(session_id)) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    if (s_state != MACHINE_STATE_IDLE) {
        ESP_LOGW(TAG, "Cannot enter SERVICE: not in IDLE");
        xSemaphoreGive(s_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    transition_to(MACHINE_STATE_SERVICE);

    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t machine_state_exit_service(uint32_t session_id)
{
    if (!session_mgr_is_valid(session_id)) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    if (s_state != MACHINE_STATE_SERVICE) {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    /* Turn off all relays when leaving service mode */
    relay_ctrl_all_off();
    telemetry_set_ro_bits(0);

    transition_to(MACHINE_STATE_IDLE);

    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t machine_state_clear_estop(uint32_t session_id)
{
    if (!session_mgr_is_valid(session_id)) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    if (s_state != MACHINE_STATE_E_STOP) {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    /* Check that E-stop is actually released */
    if (check_estop_active()) {
        ESP_LOGW(TAG, "Cannot clear E-stop: still active");
        xSemaphoreGive(s_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    transition_to(MACHINE_STATE_IDLE);
    ESP_LOGI(TAG, "E-stop cleared");

    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t machine_state_clear_fault(uint32_t session_id)
{
    if (!session_mgr_is_valid(session_id)) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    if (s_state != MACHINE_STATE_FAULT) {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    /* Check that fault condition is resolved */
    if (check_motor_fault()) {
        ESP_LOGW(TAG, "Cannot clear fault: motor fault still active");
        xSemaphoreGive(s_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    transition_to(MACHINE_STATE_IDLE);
    ESP_LOGI(TAG, "Fault cleared");

    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

void machine_state_set_callback(machine_state_cb_t cb)
{
    s_state_callback = cb;
}

uint16_t machine_state_read_di_bits(void)
{
    update_di_bits();
    return s_di_bits;
}

void machine_state_force_safe(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    set_outputs_safe();
    transition_to(MACHINE_STATE_FAULT);
    xSemaphoreGive(s_mutex);
}

/* ===== Internal Functions ===== */

/**
 * @brief Emit an event via BLE GATT
 */
static void emit_event(uint16_t event_id, uint8_t severity, const uint8_t *data, size_t data_len)
{
    uint8_t buf[64];
    size_t frame_len = wire_build_event(buf, sizeof(buf), s_event_seq++,
                                         event_id, severity, 0, data, data_len);
    if (frame_len > 0) {
        esp_err_t err = ble_gatt_send_event(buf, frame_len, severity >= EVENT_SEVERITY_ALARM);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "Failed to send event 0x%04X: %s", event_id, esp_err_to_name(err));
        }
    }
}

/**
 * @brief Emit a state change event with old and new state in payload
 */
static void emit_state_change_event(machine_state_t old_state, machine_state_t new_state)
{
    uint8_t payload[2] = { (uint8_t)old_state, (uint8_t)new_state };

    /* Determine severity based on new state */
    uint8_t severity = EVENT_SEVERITY_INFO;
    if (new_state == MACHINE_STATE_E_STOP) {
        severity = EVENT_SEVERITY_CRITICAL;
    } else if (new_state == MACHINE_STATE_FAULT) {
        severity = EVENT_SEVERITY_ALARM;
    } else if (new_state == MACHINE_STATE_STOPPING) {
        severity = EVENT_SEVERITY_WARN;
    }

    emit_event(EVENT_STATE_CHANGED, severity, payload, sizeof(payload));
}

static void transition_to(machine_state_t new_state)
{
    machine_state_t old_state = s_state;

    if (old_state == new_state) {
        return;
    }

    ESP_LOGI(TAG, "State transition: %s -> %s",
             machine_state_to_str(old_state), machine_state_to_str(new_state));

    s_state = new_state;
    s_state_enter_us = esp_timer_get_time();

    /* Execute entry actions for new state */
    switch (new_state) {
        case MACHINE_STATE_IDLE:
            set_outputs_safe();
            s_run_start_us = 0;
            break;

        case MACHINE_STATE_PRECOOL:
            /* Lock door, start cooling */
            relay_ctrl_set(RO_DOOR_LOCK, RELAY_STATE_ON);
            relay_ctrl_set(RO_LN2_VALVE, RELAY_STATE_ON);
            /* Heaters controlled by PID - enable them */
            relay_ctrl_set(RO_HEATER_1, RELAY_STATE_ON);
            relay_ctrl_set(RO_HEATER_2, RELAY_STATE_ON);
            /* Energize motor circuit (contactor + soft starter power) but don't start yet */
            relay_ctrl_set(RO_MAIN_CONTACTOR, RELAY_STATE_ON);
            telemetry_set_ro_bits(relay_ctrl_get_state());
            break;

        case MACHINE_STATE_RUNNING:
            /* Trigger soft starter to start motor */
            relay_ctrl_set(RO_MOTOR_START, RELAY_STATE_ON);
            telemetry_set_ro_bits(relay_ctrl_get_state());
            break;

        case MACHINE_STATE_STOPPING:
            /* Stop motor via soft starter (contactor stays on during soak) */
            relay_ctrl_set(RO_MOTOR_START, RELAY_STATE_OFF);
            relay_ctrl_set(RO_HEATER_1, RELAY_STATE_OFF);
            relay_ctrl_set(RO_HEATER_2, RELAY_STATE_OFF);
            /* Keep door locked and LN2 off during soak */
            relay_ctrl_set(RO_LN2_VALVE, RELAY_STATE_OFF);
            telemetry_set_ro_bits(relay_ctrl_get_state());
            break;

        case MACHINE_STATE_E_STOP:
        case MACHINE_STATE_FAULT:
            set_outputs_safe();
            break;

        case MACHINE_STATE_SERVICE:
            /* All relays available for manual control */
            break;

        default:
            break;
    }

    /* Notify callback */
    if (s_state_callback != NULL) {
        s_state_callback(old_state, new_state);
    }

    /* Emit state change event via BLE */
    emit_state_change_event(old_state, new_state);

    /* Emit specific events for key transitions */
    if (new_state == MACHINE_STATE_E_STOP) {
        emit_event(EVENT_ESTOP_ASSERTED, EVENT_SEVERITY_CRITICAL, NULL, 0);
    } else if (old_state == MACHINE_STATE_E_STOP && new_state == MACHINE_STATE_IDLE) {
        emit_event(EVENT_ESTOP_CLEARED, EVENT_SEVERITY_INFO, NULL, 0);
    }

    if (old_state == MACHINE_STATE_IDLE && new_state == MACHINE_STATE_PRECOOL) {
        emit_event(EVENT_RUN_STARTED, EVENT_SEVERITY_INFO, NULL, 0);
    } else if (old_state == MACHINE_STATE_PRECOOL && new_state == MACHINE_STATE_RUNNING) {
        emit_event(EVENT_PRECOOL_COMPLETE, EVENT_SEVERITY_INFO, NULL, 0);
    } else if (new_state == MACHINE_STATE_IDLE &&
               (old_state == MACHINE_STATE_STOPPING || old_state == MACHINE_STATE_RUNNING)) {
        emit_event(EVENT_RUN_STOPPED, EVENT_SEVERITY_INFO, NULL, 0);
    } else if (new_state == MACHINE_STATE_FAULT || new_state == MACHINE_STATE_E_STOP) {
        if (old_state == MACHINE_STATE_RUNNING || old_state == MACHINE_STATE_PRECOOL) {
            emit_event(EVENT_RUN_ABORTED, EVENT_SEVERITY_ALARM, NULL, 0);
        }
    }
}

static void set_outputs_safe(void)
{
    ESP_LOGI(TAG, "Setting outputs to safe state");

    /* Turn off all relays except chamber light (user preference) */
    relay_ctrl_set(RO_MOTOR_START, RELAY_STATE_OFF);    /* Stop motor first */
    relay_ctrl_set(RO_MAIN_CONTACTOR, RELAY_STATE_OFF); /* Then kill power circuit */
    relay_ctrl_set(RO_HEATER_1, RELAY_STATE_OFF);
    relay_ctrl_set(RO_HEATER_2, RELAY_STATE_OFF);
    relay_ctrl_set(RO_LN2_VALVE, RELAY_STATE_OFF);
    relay_ctrl_set(RO_DOOR_LOCK, RELAY_STATE_OFF);
    /* Keep light state as-is or turn off */

    telemetry_set_ro_bits(relay_ctrl_get_state());
}

static void update_di_bits(void)
{
    /* Read from hardware via relay_ctrl (which manages I2C bus) */
    uint8_t di_byte = 0;
    esp_err_t ret = relay_ctrl_read_di(&di_byte);

    if (ret == ESP_OK) {
        s_di_bits = di_byte;
    } else {
        /* If read fails, keep previous value and log warning */
        ESP_LOGW(TAG, "DI read failed: %s - keeping previous state", esp_err_to_name(ret));
    }

    /* Update telemetry with current DI state */
    telemetry_set_di_bits(s_di_bits);
}

static bool check_estop_active(void)
{
    /* E-Stop is active-LOW: DI1 LOW = E-stop pressed */
    return (s_di_bits & (1 << (DI_ESTOP - 1))) == 0;
}

static bool check_door_open(void)
{
    /* Door closed is HIGH: DI2 LOW = door open */
    return (s_di_bits & (1 << (DI_DOOR_CLOSED - 1))) == 0;
}

static bool check_motor_fault(void)
{
    /* Motor fault check disabled - soft starter has no fault output signal.
     * DI4 is reserved for future use if we add a VFD with fault output.
     * Always returns false (no fault) for now. */
    (void)DI_MOTOR_FAULT;  /* Suppress unused warning */
    return false;
}

static bool check_ln2_present(void)
{
    /* LN2 present is HIGH: DI3 HIGH = LN2 available */
    return (s_di_bits & (1 << (DI_LN2_PRESENT - 1))) != 0;
}

/**
 * @brief Get current chamber temperature from PID controller
 *
 * @param temp_x10 Output: temperature in tenths of degree C
 * @return true if valid temperature was read, false if controller offline/stale
 */
static bool get_chamber_temp(int16_t *temp_x10)
{
    pid_controller_t ctrl;

    /* Get PID controller 1 (chamber temperature) */
    if (pid_controller_get_by_addr(CHAMBER_PID_ADDR, &ctrl) != ESP_OK) {
        return false;
    }

    /* Check if data is fresh */
    if (ctrl.state != PID_STATE_ONLINE) {
        ESP_LOGD(TAG, "PID controller %d not online (state=%d)", CHAMBER_PID_ADDR, ctrl.state);
        return false;
    }

    /* Convert from float to x10 integer */
    *temp_x10 = (int16_t)(ctrl.data.pv * 10.0f);
    return true;
}

static void state_task(void *arg)
{
    (void)arg;

    TickType_t last_wake = xTaskGetTickCount();

    ESP_LOGI(TAG, "State machine task started");

    while (s_running) {
        /* Read digital inputs */
        update_di_bits();

        xSemaphoreTake(s_mutex, portMAX_DELAY);

        /* Check for E-stop - highest priority, any state */
        if (check_estop_active() && s_state != MACHINE_STATE_E_STOP) {
            ESP_LOGE(TAG, "E-STOP ACTIVATED!");
            transition_to(MACHINE_STATE_E_STOP);
        }

        /* Check for motor fault - triggers FAULT from most states */
        if (check_motor_fault() &&
            s_state != MACHINE_STATE_E_STOP &&
            s_state != MACHINE_STATE_FAULT &&
            s_state != MACHINE_STATE_IDLE &&
            s_state != MACHINE_STATE_SERVICE) {
            ESP_LOGE(TAG, "Motor fault detected!");
            transition_to(MACHINE_STATE_FAULT);
        }

        /* Check door interlock during run */
        if (check_door_open() &&
            (s_state == MACHINE_STATE_RUNNING || s_state == MACHINE_STATE_PRECOOL)) {
            ESP_LOGE(TAG, "Door opened during run - stopping!");
            /* Door open during run is a fault condition */
            transition_to(MACHINE_STATE_FAULT);
        }

        /* State-specific logic */
        int64_t now_us = esp_timer_get_time();
        int64_t state_duration_ms = (now_us - s_state_enter_us) / 1000;

        switch (s_state) {
            case MACHINE_STATE_PRECOOL: {
                /* Check actual temperature from PID controller */
                int16_t current_temp_x10;
                bool temp_valid = get_chamber_temp(&current_temp_x10);

                if (temp_valid) {
                    /* Check if we've reached target temperature (within tolerance) */
                    int16_t temp_diff = current_temp_x10 - s_target_temp_x10;
                    if (temp_diff < 0) temp_diff = -temp_diff;  /* abs() */

                    if (temp_diff <= PRECOOL_TEMP_TOLERANCE_X10) {
                        ESP_LOGI(TAG, "Precool target reached: current=%d.%d target=%d.%d",
                                 current_temp_x10 / 10, abs(current_temp_x10 % 10),
                                 s_target_temp_x10 / 10, abs(s_target_temp_x10 % 10));
                        if (s_run_mode == RUN_MODE_PRECOOL_ONLY) {
                            transition_to(MACHINE_STATE_STOPPING);
                        } else {
                            transition_to(MACHINE_STATE_RUNNING);
                        }
                        break;
                    }

                    /* Log progress periodically (every 5 seconds) */
                    if ((state_duration_ms % 5000) < STATE_POLL_INTERVAL_MS) {
                        ESP_LOGI(TAG, "Precool: current=%d.%d target=%d.%d diff=%d.%d",
                                 current_temp_x10 / 10, abs(current_temp_x10 % 10),
                                 s_target_temp_x10 / 10, abs(s_target_temp_x10 % 10),
                                 temp_diff / 10, temp_diff % 10);
                    }
                }

                /* Check if precool timeout exceeded */
                if (state_duration_ms > PRECOOL_TIMEOUT_MS) {
                    if (temp_valid) {
                        ESP_LOGW(TAG, "Precool timeout at temp=%d.%d (target=%d.%d) - proceeding anyway",
                                 current_temp_x10 / 10, abs(current_temp_x10 % 10),
                                 s_target_temp_x10 / 10, abs(s_target_temp_x10 % 10));
                    } else {
                        ESP_LOGW(TAG, "Precool timeout (no valid temp reading) - proceeding anyway");
                    }

                    if (s_run_mode == RUN_MODE_PRECOOL_ONLY) {
                        transition_to(MACHINE_STATE_STOPPING);
                    } else {
                        transition_to(MACHINE_STATE_RUNNING);
                    }
                }
                break;
            }

            case MACHINE_STATE_RUNNING: {
                /* Check run duration timeout */
                if (s_run_duration_ms > 0) {
                    int64_t run_elapsed_ms = (now_us - s_run_start_us) / 1000;
                    if (run_elapsed_ms >= s_run_duration_ms) {
                        ESP_LOGI(TAG, "Run duration complete");
                        transition_to(MACHINE_STATE_STOPPING);
                    }
                }

                /* Check for HMI stale - continue to safe stop */
                if (!session_mgr_is_live()) {
                    ESP_LOGW(TAG, "HMI disconnected during run - safe stop");
                    transition_to(MACHINE_STATE_STOPPING);
                }
                break;
            }

            case MACHINE_STATE_STOPPING: {
                /* Wait for thermal soak period */
                if (state_duration_ms > STOPPING_SOAK_TIME_MS) {
                    ESP_LOGI(TAG, "Thermal soak complete");
                    transition_to(MACHINE_STATE_IDLE);
                }
                break;
            }

            default:
                break;
        }

        xSemaphoreGive(s_mutex);

        /* Sleep until next tick */
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(STATE_POLL_INTERVAL_MS));
    }

    ESP_LOGI(TAG, "State machine task stopped");
    s_task_handle = NULL;
    vTaskDelete(NULL);
}
