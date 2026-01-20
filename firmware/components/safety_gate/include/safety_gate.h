#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file safety_gate.h
 * @brief Safety Gate Framework for Cryogenic Shaker Ball Mill
 *
 * Provides configurable capability levels for subsystems and bypassable safety
 * gates for development/testing. MCU is the authoritative safety enforcer.
 *
 * Capability Levels:
 *   NOT_PRESENT (0) - Subsystem not installed, ignore completely
 *   OPTIONAL (1)    - Present but not critical, faults generate warnings
 *   REQUIRED (2)    - Critical for operation, faults block start/trigger E-stop
 *
 * Safety Gates:
 *   Each gate is a condition that must pass for certain operations.
 *   Gates can be ENABLED (checked) or BYPASSED (skipped) for development.
 *   E-Stop gate (ID 0) can NEVER be bypassed.
 */

/* ============================================================================
 * CAPABILITY LEVELS
 * ============================================================================ */

typedef enum {
    CAP_NOT_PRESENT = 0,    /* Not installed, ignore completely */
    CAP_OPTIONAL    = 1,    /* Present but not critical (warnings only) */
    CAP_REQUIRED    = 2,    /* Critical for operation (faults block/trigger) */
} capability_level_t;

/* Subsystem IDs for capability configuration */
typedef enum {
    SUBSYS_PID1         = 0,    /* PID 1 (LN2/Cold) */
    SUBSYS_PID2         = 1,    /* PID 2 (Axle bearings) */
    SUBSYS_PID3         = 2,    /* PID 3 (Orbital bearings) */
    SUBSYS_DI_ESTOP     = 3,    /* E-Stop input (always REQUIRED) */
    SUBSYS_DI_DOOR      = 4,    /* Door position sensor */
    SUBSYS_DI_LN2       = 5,    /* LN2 present sensor */
    SUBSYS_DI_MOTOR     = 6,    /* Motor fault input (reserved) */
    SUBSYS_MAX
} subsystem_id_t;

/* ============================================================================
 * SAFETY GATES
 * ============================================================================ */

/* Gate IDs */
typedef enum {
    GATE_ESTOP              = 0,    /* E-Stop not active (NEVER bypassable) */
    GATE_DOOR_CLOSED        = 1,    /* Door is closed */
    GATE_HMI_LIVE           = 2,    /* BLE session is active */
    GATE_PID1_ONLINE        = 3,    /* PID1 responding to RS-485 */
    GATE_PID2_ONLINE        = 4,    /* PID2 responding to RS-485 */
    GATE_PID3_ONLINE        = 5,    /* PID3 responding to RS-485 */
    GATE_PID1_NO_PROBE_ERR  = 6,    /* PID1 PV in valid range */
    GATE_PID2_NO_PROBE_ERR  = 7,    /* PID2 PV in valid range */
    GATE_PID3_NO_PROBE_ERR  = 8,    /* PID3 PV in valid range */
    GATE_RESERVED           = 9,    /* Reserved for future (motor fault) */
    GATE_MAX
} gate_id_t;

/* Gate status (result of checking a gate condition) */
typedef enum {
    GATE_STATUS_PASSING     = 0,    /* Condition is met */
    GATE_STATUS_BLOCKING    = 1,    /* Condition not met, would block */
    GATE_STATUS_BYPASSED    = 2,    /* Gate is bypassed, not checked */
    GATE_STATUS_NA          = 3,    /* Not applicable (subsystem NOT_PRESENT) */
} gate_status_t;

/* ============================================================================
 * PROBE ERROR THRESHOLDS
 * ============================================================================ */

/* Over-range (HHHH): sensor disconnected or way out of range */
#define PROBE_ERROR_HIGH_THRESHOLD_X10  5000    /* 500.0°C */

/* Under-range (LLLL): sensor shorted or way out of range */
/* Note: Only applies to heater PIDs (2, 3), not LN2 PID (1) */
#define PROBE_ERROR_LOW_THRESHOLD_X10   (-3000) /* -300.0°C */

/* ============================================================================
 * API FUNCTIONS
 * ============================================================================ */

/**
 * @brief Initialize the safety gate module
 *
 * Loads capability levels from NVS. Gate bypasses are NOT loaded (always reset
 * to enabled on boot for safety).
 *
 * @return ESP_OK on success
 */
esp_err_t safety_gate_init(void);

/* ===== Capability Management ===== */

/**
 * @brief Get capability level for a subsystem
 *
 * @param subsys Subsystem ID
 * @return Capability level (NOT_PRESENT, OPTIONAL, REQUIRED)
 */
capability_level_t safety_gate_get_capability(subsystem_id_t subsys);

/**
 * @brief Set capability level for a subsystem
 *
 * Persists to NVS. E-Stop (SUBSYS_DI_ESTOP) is always REQUIRED and cannot
 * be changed.
 *
 * @param subsys Subsystem ID
 * @param level Capability level
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if trying to change E-Stop
 */
esp_err_t safety_gate_set_capability(subsystem_id_t subsys, capability_level_t level);

/**
 * @brief Get all capability levels as a packed byte array
 *
 * @param out_caps Output array (SUBSYS_MAX bytes)
 */
void safety_gate_get_all_capabilities(uint8_t *out_caps);

/* ===== Gate Management ===== */

/**
 * @brief Check if a gate is enabled (not bypassed)
 *
 * @param gate Gate ID
 * @return true if gate is enabled, false if bypassed
 */
bool safety_gate_is_enabled(gate_id_t gate);

/**
 * @brief Enable or bypass a safety gate
 *
 * Gate bypasses do NOT persist to NVS (reset on reboot for safety).
 * E-Stop gate (GATE_ESTOP) cannot be bypassed.
 *
 * @param gate Gate ID
 * @param enabled true to enable gate, false to bypass
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG for E-Stop bypass attempt
 */
esp_err_t safety_gate_set_enabled(gate_id_t gate, bool enabled);

/**
 * @brief Get gate enable bitmask
 *
 * @return Bitmask where bit N = gate N enabled (1) or bypassed (0)
 */
uint16_t safety_gate_get_enable_mask(void);

/**
 * @brief Get gate status bitmask (current pass/fail state)
 *
 * This evaluates all gates and returns their current status.
 * Bit N = 1 means gate N is passing (or bypassed), 0 means blocking.
 *
 * @return Bitmask where bit N = gate N passing/bypassed (1) or blocking (0)
 */
uint16_t safety_gate_get_status_mask(void);

/**
 * @brief Check a specific gate's current status
 *
 * @param gate Gate ID
 * @return Gate status (PASSING, BLOCKING, BYPASSED, or NA)
 */
gate_status_t safety_gate_check(gate_id_t gate);

/* ===== Probe Error Detection ===== */

/**
 * @brief Check if a PID has a probe error (HHHH/LLLL)
 *
 * @param pid_id PID ID (1, 2, or 3)
 * @return true if probe error detected
 */
bool safety_gate_pid_has_probe_error(uint8_t pid_id);

/**
 * @brief Get probe error flags for all PIDs
 *
 * @return Bitmask: bit 0 = PID1, bit 1 = PID2, bit 2 = PID3
 */
uint8_t safety_gate_get_probe_error_flags(void);

/* ===== Start Validation ===== */

/**
 * @brief Check if START_RUN is allowed based on safety gates
 *
 * Evaluates all relevant gates considering capability levels.
 * Returns the first blocking gate ID, or -1 if start is allowed.
 *
 * @param out_blocking_gate If not NULL, receives the ID of the blocking gate
 * @return true if start is allowed, false if blocked
 */
bool safety_gate_can_start_run(int8_t *out_blocking_gate);

/**
 * @brief Check if SET_MODE(AUTO) is allowed for a specific PID
 *
 * @param pid_id PID ID (1, 2, or 3)
 * @param out_blocking_gate If not NULL, receives the ID of the blocking gate
 * @return true if allowed, false if blocked
 */
bool safety_gate_can_enable_pid(uint8_t pid_id, int8_t *out_blocking_gate);

#ifdef __cplusplus
}
#endif
