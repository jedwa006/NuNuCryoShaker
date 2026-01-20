#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Machine State Manager for Cryogenic Shaker Ball Mill
 *
 * Implements the MCU-resident state machine that owns process control.
 * The app sends high-level commands (START_RUN, STOP_RUN) and observes state.
 * The MCU handles sequencing and can complete a run safely even if BLE disconnects.
 *
 * State diagram:
 *   IDLE → PRECOOL → RUNNING → STOPPING → IDLE
 *   Any → E_STOP (immediate on E-stop button)
 *   Any → FAULT (on hardware fault)
 *   IDLE ↔ SERVICE (manual control mode)
 */

/* Machine States */
typedef enum {
    MACHINE_STATE_IDLE      = 0,    /* Machine ready, outputs safe */
    MACHINE_STATE_PRECOOL   = 1,    /* Cooling chamber before motor start */
    MACHINE_STATE_RUNNING   = 2,    /* Motor active, temperature controlled */
    MACHINE_STATE_STOPPING  = 3,    /* Controlled shutdown sequence */
    MACHINE_STATE_E_STOP    = 4,    /* Emergency stopped, outputs latched safe */
    MACHINE_STATE_FAULT     = 5,    /* Hardware fault detected, outputs safe */
    MACHINE_STATE_SERVICE   = 6,    /* Manual control mode (app-controlled outputs) */
    MACHINE_STATE_MAX
} machine_state_t;

/* Run Modes (for START_RUN command) */
typedef enum {
    RUN_MODE_NORMAL         = 0,    /* Full precool + run cycle */
    RUN_MODE_DRY_RUN        = 1,    /* No high-power outputs (testing) */
    RUN_MODE_PRECOOL_ONLY   = 2,    /* Stop after precool complete */
} run_mode_t;

/* Stop Modes (for STOP_RUN command) */
typedef enum {
    STOP_MODE_NORMAL        = 0,    /* Graceful stop with thermal soak */
    STOP_MODE_ABORT         = 1,    /* Fast stop, maintain safe state */
} stop_mode_t;

/* Interlock bit definitions (for interlock_bits in telemetry) */
#define INTERLOCK_BIT_ESTOP         (1 << 0)    /* E-stop active (DI1 LOW) */
#define INTERLOCK_BIT_DOOR_OPEN     (1 << 1)    /* Door open (DI2 LOW) */
#define INTERLOCK_BIT_LN2_ABSENT    (1 << 2)    /* LN2 not present (DI3 LOW) - warning only */
#define INTERLOCK_BIT_MOTOR_FAULT   (1 << 3)    /* VFD fault (DI4 HIGH) */
#define INTERLOCK_BIT_HMI_STALE     (1 << 4)    /* HMI session not live */

/* Digital Input Channels (1-based indexing to match hardware labels) */
#define DI_ESTOP            1       /* DI1: E-Stop (LOW = active) */
#define DI_DOOR_CLOSED      2       /* DI2: Door position (HIGH = closed) */
#define DI_LN2_PRESENT      3       /* DI3: LN2 supply (HIGH = present) */
#define DI_MOTOR_FAULT      4       /* DI4: VFD fault (HIGH = fault) */

/* Relay Output Channels (1-based indexing to match hardware labels) */
#define RO_MAIN_CONTACTOR   1       /* CH1: Motor power enable */
#define RO_HEATER_1         2       /* CH2: Axle bearing heater */
#define RO_HEATER_2         3       /* CH3: Orbital bearing heater */
#define RO_LN2_VALVE        4       /* CH4: LN2 solenoid */
#define RO_DOOR_LOCK        5       /* CH5: Door lock solenoid */
#define RO_CHAMBER_LIGHT    6       /* CH6: Chamber lighting */

/* Run state information for telemetry */
typedef struct {
    machine_state_t state;              /* Current machine state */
    run_mode_t      run_mode;           /* Active run mode */
    uint32_t        run_elapsed_ms;     /* Time since run started (0 if not running) */
    uint32_t        run_remaining_ms;   /* Time until run completes (0 if no target) */
    int16_t         target_temp_x10;    /* Current target temperature × 10 */
    uint8_t         recipe_step;        /* Current recipe step (0-based) */
    uint8_t         interlock_bits;     /* Which interlocks are blocking start */
} machine_run_info_t;

/* State change callback type */
typedef void (*machine_state_cb_t)(machine_state_t old_state, machine_state_t new_state);

/**
 * @brief Initialize the machine state manager
 *
 * Sets initial state to IDLE and starts the state machine task.
 *
 * @return ESP_OK on success
 */
esp_err_t machine_state_init(void);

/**
 * @brief Get current machine state
 */
machine_state_t machine_state_get(void);

/**
 * @brief Get machine state as string (for logging)
 */
const char *machine_state_to_str(machine_state_t state);

/**
 * @brief Get current run information for telemetry
 */
void machine_state_get_run_info(machine_run_info_t *out_info);

/**
 * @brief Get current interlock status bits
 *
 * @return Bitmask of active interlocks (0 = all clear)
 */
uint8_t machine_state_get_interlocks(void);

/**
 * @brief Check if interlocks allow starting a run
 *
 * Checks E-stop (DI1), door (DI2), and HMI live status.
 * LN2 presence (DI3) generates a warning but doesn't block.
 *
 * @return true if start is allowed
 */
bool machine_state_start_allowed(void);

/**
 * @brief Request transition to PRECOOL/RUNNING state
 *
 * @param session_id Session ID for validation
 * @param mode Run mode (NORMAL, DRY_RUN, PRECOOL_ONLY)
 * @param target_temp_x10 Target temperature × 10 (optional, 0 for default)
 * @param run_duration_ms Run duration in ms (0 for indefinite)
 * @return ESP_OK if transition started
 *         ESP_ERR_INVALID_STATE if not in IDLE
 *         ESP_ERR_NOT_ALLOWED if interlocks prevent start
 */
esp_err_t machine_state_start_run(uint32_t session_id, run_mode_t mode,
                                   int16_t target_temp_x10, uint32_t run_duration_ms);

/**
 * @brief Request stop/abort of current run
 *
 * @param session_id Session ID for validation
 * @param mode Stop mode (NORMAL or ABORT)
 * @return ESP_OK if stop initiated
 */
esp_err_t machine_state_stop_run(uint32_t session_id, stop_mode_t mode);

/**
 * @brief Enter service mode (manual relay control)
 *
 * @param session_id Session ID for validation
 * @return ESP_OK if entered service mode
 *         ESP_ERR_INVALID_STATE if not in IDLE
 */
esp_err_t machine_state_enter_service(uint32_t session_id);

/**
 * @brief Exit service mode
 *
 * @param session_id Session ID for validation
 * @return ESP_OK if exited service mode
 */
esp_err_t machine_state_exit_service(uint32_t session_id);

/**
 * @brief Clear E-stop condition (requires E-stop released + HMI ack)
 *
 * @param session_id Session ID for validation
 * @return ESP_OK if cleared, ESP_ERR_INVALID_STATE if E-stop still active
 */
esp_err_t machine_state_clear_estop(uint32_t session_id);

/**
 * @brief Clear fault condition
 *
 * @param session_id Session ID for validation
 * @return ESP_OK if cleared, ESP_ERR_INVALID_STATE if fault still present
 */
esp_err_t machine_state_clear_fault(uint32_t session_id);

/**
 * @brief Register state change callback
 *
 * Called whenever the machine state changes. Can be used for event emission.
 *
 * @param cb Callback function (NULL to unregister)
 */
void machine_state_set_callback(machine_state_cb_t cb);

/**
 * @brief Read current digital input state
 *
 * Reads all 8 digital inputs and returns as bitmask.
 * bit 0 = DI1, bit 7 = DI8
 *
 * @return Digital input bitmask
 */
uint16_t machine_state_read_di_bits(void);

/**
 * @brief Force transition to safe state (called on critical errors)
 *
 * Immediately disables all outputs and transitions to FAULT state.
 */
void machine_state_force_safe(void);

#ifdef __cplusplus
}
#endif
