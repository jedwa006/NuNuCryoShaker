#include "safety_gate.h"
#include "pid_controller.h"
#include "machine_state.h"
#include "session_mgr.h"

#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "safety_gate";

/* NVS namespace and keys */
#define NVS_NAMESPACE       "safety"
#define NVS_KEY_CAP_PID1    "cap_pid1"
#define NVS_KEY_CAP_PID2    "cap_pid2"
#define NVS_KEY_CAP_PID3    "cap_pid3"
#define NVS_KEY_CAP_DI_DOOR "cap_di_door"
#define NVS_KEY_CAP_DI_LN2  "cap_di_ln2"
#define NVS_KEY_CAP_DI_MOT  "cap_di_motor"

/* Default capability levels */
static const capability_level_t s_default_caps[SUBSYS_MAX] = {
    [SUBSYS_PID1]       = CAP_OPTIONAL,     /* LN2 PID - monitoring only */
    [SUBSYS_PID2]       = CAP_REQUIRED,     /* Axle bearings - critical */
    [SUBSYS_PID3]       = CAP_REQUIRED,     /* Orbital bearings - critical */
    [SUBSYS_DI_ESTOP]   = CAP_REQUIRED,     /* E-Stop - always required */
    [SUBSYS_DI_DOOR]    = CAP_REQUIRED,     /* Door sensor */
    [SUBSYS_DI_LN2]     = CAP_OPTIONAL,     /* LN2 present - advisory */
    [SUBSYS_DI_MOTOR]   = CAP_NOT_PRESENT,  /* Motor fault - not connected */
};

/* Current capability levels (loaded from NVS or defaults) */
static capability_level_t s_caps[SUBSYS_MAX];

/* Gate enable flags (bit N = gate N enabled) */
/* All gates enabled by default, bypasses do NOT persist */
static uint16_t s_gate_enable_mask = 0xFFFF;

/* Initialized flag */
static bool s_initialized = false;

/* ============================================================================
 * NVS HELPERS
 * ============================================================================ */

static void load_capabilities_from_nvs(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);

    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No NVS namespace '%s', using defaults", NVS_NAMESPACE);
        for (int i = 0; i < SUBSYS_MAX; i++) {
            s_caps[i] = s_default_caps[i];
        }
        return;
    }

    /* Load each capability, falling back to default if not found */
    uint8_t val;

    if (nvs_get_u8(nvs, NVS_KEY_CAP_PID1, &val) == ESP_OK) {
        s_caps[SUBSYS_PID1] = (capability_level_t)val;
    } else {
        s_caps[SUBSYS_PID1] = s_default_caps[SUBSYS_PID1];
    }

    if (nvs_get_u8(nvs, NVS_KEY_CAP_PID2, &val) == ESP_OK) {
        s_caps[SUBSYS_PID2] = (capability_level_t)val;
    } else {
        s_caps[SUBSYS_PID2] = s_default_caps[SUBSYS_PID2];
    }

    if (nvs_get_u8(nvs, NVS_KEY_CAP_PID3, &val) == ESP_OK) {
        s_caps[SUBSYS_PID3] = (capability_level_t)val;
    } else {
        s_caps[SUBSYS_PID3] = s_default_caps[SUBSYS_PID3];
    }

    /* E-Stop is always REQUIRED, never load from NVS */
    s_caps[SUBSYS_DI_ESTOP] = CAP_REQUIRED;

    if (nvs_get_u8(nvs, NVS_KEY_CAP_DI_DOOR, &val) == ESP_OK) {
        s_caps[SUBSYS_DI_DOOR] = (capability_level_t)val;
    } else {
        s_caps[SUBSYS_DI_DOOR] = s_default_caps[SUBSYS_DI_DOOR];
    }

    if (nvs_get_u8(nvs, NVS_KEY_CAP_DI_LN2, &val) == ESP_OK) {
        s_caps[SUBSYS_DI_LN2] = (capability_level_t)val;
    } else {
        s_caps[SUBSYS_DI_LN2] = s_default_caps[SUBSYS_DI_LN2];
    }

    if (nvs_get_u8(nvs, NVS_KEY_CAP_DI_MOT, &val) == ESP_OK) {
        s_caps[SUBSYS_DI_MOTOR] = (capability_level_t)val;
    } else {
        s_caps[SUBSYS_DI_MOTOR] = s_default_caps[SUBSYS_DI_MOTOR];
    }

    nvs_close(nvs);

    ESP_LOGI(TAG, "Loaded capabilities from NVS: PID1=%d PID2=%d PID3=%d Door=%d LN2=%d Motor=%d",
             s_caps[SUBSYS_PID1], s_caps[SUBSYS_PID2], s_caps[SUBSYS_PID3],
             s_caps[SUBSYS_DI_DOOR], s_caps[SUBSYS_DI_LN2], s_caps[SUBSYS_DI_MOTOR]);
}

static esp_err_t save_capability_to_nvs(subsystem_id_t subsys, capability_level_t level)
{
    const char *key = NULL;

    switch (subsys) {
        case SUBSYS_PID1:       key = NVS_KEY_CAP_PID1; break;
        case SUBSYS_PID2:       key = NVS_KEY_CAP_PID2; break;
        case SUBSYS_PID3:       key = NVS_KEY_CAP_PID3; break;
        case SUBSYS_DI_DOOR:    key = NVS_KEY_CAP_DI_DOOR; break;
        case SUBSYS_DI_LN2:     key = NVS_KEY_CAP_DI_LN2; break;
        case SUBSYS_DI_MOTOR:   key = NVS_KEY_CAP_DI_MOT; break;
        default:
            return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_u8(nvs, key, (uint8_t)level);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }

    nvs_close(nvs);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save capability: %s", esp_err_to_name(err));
    }

    return err;
}

/* ============================================================================
 * PROBE ERROR DETECTION
 * ============================================================================ */

bool safety_gate_pid_has_probe_error(uint8_t pid_id)
{
    if (pid_id < 1 || pid_id > 3) {
        return false;
    }

    pid_controller_t ctrl;
    if (pid_controller_get_by_addr(pid_id, &ctrl) != ESP_OK) {
        /* Can't read PID - not a probe error per se */
        return false;
    }

    /* Check if PID is even online */
    if (ctrl.state != PID_STATE_ONLINE && ctrl.state != PID_STATE_STALE) {
        return false;  /* Offline is handled by PID_ONLINE gate */
    }

    int16_t pv_x10 = (int16_t)(ctrl.data.pv * 10.0f);

    /* Over-range (HHHH) - applies to all PIDs */
    if (pv_x10 >= PROBE_ERROR_HIGH_THRESHOLD_X10) {
        return true;
    }

    /* Under-range (LLLL) - only for heater PIDs (2, 3), not LN2 PID (1) */
    if (pid_id != 1 && pv_x10 <= PROBE_ERROR_LOW_THRESHOLD_X10) {
        return true;
    }

    return false;
}

uint8_t safety_gate_get_probe_error_flags(void)
{
    uint8_t flags = 0;

    if (safety_gate_pid_has_probe_error(1)) flags |= (1 << 0);
    if (safety_gate_pid_has_probe_error(2)) flags |= (1 << 1);
    if (safety_gate_pid_has_probe_error(3)) flags |= (1 << 2);

    return flags;
}

/* ============================================================================
 * GATE CHECKING
 * ============================================================================ */

static bool check_gate_condition(gate_id_t gate)
{
    uint8_t interlocks = machine_state_get_interlocks();

    switch (gate) {
        case GATE_ESTOP:
            return (interlocks & INTERLOCK_BIT_ESTOP) == 0;

        case GATE_DOOR_CLOSED:
            return (interlocks & INTERLOCK_BIT_DOOR_OPEN) == 0;

        case GATE_HMI_LIVE:
            return session_mgr_is_live();

        case GATE_PID1_ONLINE: {
            pid_controller_t ctrl;
            if (pid_controller_get_by_addr(1, &ctrl) != ESP_OK) return false;
            return ctrl.state == PID_STATE_ONLINE || ctrl.state == PID_STATE_STALE;
        }

        case GATE_PID2_ONLINE: {
            pid_controller_t ctrl;
            if (pid_controller_get_by_addr(2, &ctrl) != ESP_OK) return false;
            return ctrl.state == PID_STATE_ONLINE || ctrl.state == PID_STATE_STALE;
        }

        case GATE_PID3_ONLINE: {
            pid_controller_t ctrl;
            if (pid_controller_get_by_addr(3, &ctrl) != ESP_OK) return false;
            return ctrl.state == PID_STATE_ONLINE || ctrl.state == PID_STATE_STALE;
        }

        case GATE_PID1_NO_PROBE_ERR:
            return !safety_gate_pid_has_probe_error(1);

        case GATE_PID2_NO_PROBE_ERR:
            return !safety_gate_pid_has_probe_error(2);

        case GATE_PID3_NO_PROBE_ERR:
            return !safety_gate_pid_has_probe_error(3);

        case GATE_RESERVED:
            return true;  /* Reserved gate always passes */

        default:
            return true;
    }
}

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

esp_err_t safety_gate_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    /* Load capabilities from NVS */
    load_capabilities_from_nvs();

    /* All gates start enabled (bypasses reset on boot for safety) */
    s_gate_enable_mask = 0xFFFF;

    s_initialized = true;

    ESP_LOGI(TAG, "Safety gate module initialized");
    return ESP_OK;
}

capability_level_t safety_gate_get_capability(subsystem_id_t subsys)
{
    if (subsys >= SUBSYS_MAX) {
        return CAP_NOT_PRESENT;
    }
    return s_caps[subsys];
}

esp_err_t safety_gate_set_capability(subsystem_id_t subsys, capability_level_t level)
{
    if (subsys >= SUBSYS_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    /* E-Stop capability cannot be changed */
    if (subsys == SUBSYS_DI_ESTOP) {
        ESP_LOGW(TAG, "Cannot change E-Stop capability (always REQUIRED)");
        return ESP_ERR_INVALID_ARG;
    }

    /* Validate level */
    if (level > CAP_REQUIRED) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Save to NVS */
    esp_err_t err = save_capability_to_nvs(subsys, level);
    if (err != ESP_OK) {
        return err;
    }

    /* Update in-memory value */
    s_caps[subsys] = level;

    ESP_LOGI(TAG, "Set capability: subsys=%d level=%d", subsys, level);
    return ESP_OK;
}

void safety_gate_get_all_capabilities(uint8_t *out_caps)
{
    for (int i = 0; i < SUBSYS_MAX; i++) {
        out_caps[i] = (uint8_t)s_caps[i];
    }
}

bool safety_gate_is_enabled(gate_id_t gate)
{
    if (gate >= GATE_MAX) {
        return true;
    }
    return (s_gate_enable_mask & (1 << gate)) != 0;
}

esp_err_t safety_gate_set_enabled(gate_id_t gate, bool enabled)
{
    if (gate >= GATE_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    /* E-Stop gate cannot be bypassed */
    if (gate == GATE_ESTOP && !enabled) {
        ESP_LOGW(TAG, "Cannot bypass E-Stop gate");
        return ESP_ERR_INVALID_ARG;
    }

    if (enabled) {
        s_gate_enable_mask |= (1 << gate);
        ESP_LOGI(TAG, "Gate %d enabled", gate);
    } else {
        s_gate_enable_mask &= ~(1 << gate);
        ESP_LOGW(TAG, "Gate %d BYPASSED (development mode)", gate);
    }

    return ESP_OK;
}

uint16_t safety_gate_get_enable_mask(void)
{
    return s_gate_enable_mask;
}

uint16_t safety_gate_get_status_mask(void)
{
    uint16_t status = 0;

    for (int i = 0; i < GATE_MAX; i++) {
        gate_status_t gs = safety_gate_check((gate_id_t)i);
        if (gs == GATE_STATUS_PASSING || gs == GATE_STATUS_BYPASSED || gs == GATE_STATUS_NA) {
            status |= (1 << i);
        }
    }

    return status;
}

gate_status_t safety_gate_check(gate_id_t gate)
{
    if (gate >= GATE_MAX) {
        return GATE_STATUS_NA;
    }

    /* Check if gate is bypassed (except E-Stop which is never bypassable) */
    if (gate != GATE_ESTOP && !safety_gate_is_enabled(gate)) {
        return GATE_STATUS_BYPASSED;
    }

    /* For PID gates, check if subsystem is NOT_PRESENT */
    subsystem_id_t related_subsys = SUBSYS_MAX;
    switch (gate) {
        case GATE_PID1_ONLINE:
        case GATE_PID1_NO_PROBE_ERR:
            related_subsys = SUBSYS_PID1;
            break;
        case GATE_PID2_ONLINE:
        case GATE_PID2_NO_PROBE_ERR:
            related_subsys = SUBSYS_PID2;
            break;
        case GATE_PID3_ONLINE:
        case GATE_PID3_NO_PROBE_ERR:
            related_subsys = SUBSYS_PID3;
            break;
        case GATE_DOOR_CLOSED:
            related_subsys = SUBSYS_DI_DOOR;
            break;
        default:
            break;
    }

    if (related_subsys < SUBSYS_MAX && s_caps[related_subsys] == CAP_NOT_PRESENT) {
        return GATE_STATUS_NA;
    }

    /* Check the actual condition */
    if (check_gate_condition(gate)) {
        return GATE_STATUS_PASSING;
    } else {
        return GATE_STATUS_BLOCKING;
    }
}

bool safety_gate_can_start_run(int8_t *out_blocking_gate)
{
    /* E-Stop always checked (never bypassable) */
    if (safety_gate_check(GATE_ESTOP) == GATE_STATUS_BLOCKING) {
        if (out_blocking_gate) *out_blocking_gate = GATE_ESTOP;
        return false;
    }

    /* Door gate */
    if (s_caps[SUBSYS_DI_DOOR] != CAP_NOT_PRESENT) {
        gate_status_t gs = safety_gate_check(GATE_DOOR_CLOSED);
        if (gs == GATE_STATUS_BLOCKING) {
            if (out_blocking_gate) *out_blocking_gate = GATE_DOOR_CLOSED;
            return false;
        }
    }

    /* HMI gate */
    gate_status_t hmi_gs = safety_gate_check(GATE_HMI_LIVE);
    if (hmi_gs == GATE_STATUS_BLOCKING) {
        if (out_blocking_gate) *out_blocking_gate = GATE_HMI_LIVE;
        return false;
    }

    /* PID gates - only check if subsystem is REQUIRED */
    for (int pid_id = 1; pid_id <= 3; pid_id++) {
        subsystem_id_t subsys = SUBSYS_PID1 + (pid_id - 1);

        if (s_caps[subsys] != CAP_REQUIRED) {
            continue;  /* Skip if NOT_PRESENT or OPTIONAL */
        }

        /* Check online gate */
        gate_id_t online_gate = GATE_PID1_ONLINE + (pid_id - 1);
        gate_status_t gs = safety_gate_check(online_gate);
        if (gs == GATE_STATUS_BLOCKING) {
            if (out_blocking_gate) *out_blocking_gate = (int8_t)online_gate;
            return false;
        }

        /* Check probe error gate */
        gate_id_t probe_gate = GATE_PID1_NO_PROBE_ERR + (pid_id - 1);
        gs = safety_gate_check(probe_gate);
        if (gs == GATE_STATUS_BLOCKING) {
            if (out_blocking_gate) *out_blocking_gate = (int8_t)probe_gate;
            return false;
        }
    }

    if (out_blocking_gate) *out_blocking_gate = -1;
    return true;
}

bool safety_gate_can_enable_pid(uint8_t pid_id, int8_t *out_blocking_gate)
{
    if (pid_id < 1 || pid_id > 3) {
        if (out_blocking_gate) *out_blocking_gate = -1;
        return false;
    }

    /* E-Stop always checked */
    if (safety_gate_check(GATE_ESTOP) == GATE_STATUS_BLOCKING) {
        if (out_blocking_gate) *out_blocking_gate = GATE_ESTOP;
        return false;
    }

    /* Check online gate (must be online to enable) */
    gate_id_t online_gate = GATE_PID1_ONLINE + (pid_id - 1);
    if (check_gate_condition(online_gate) == false) {
        if (out_blocking_gate) *out_blocking_gate = (int8_t)online_gate;
        return false;
    }

    /* Check probe error gate (if enabled) */
    gate_id_t probe_gate = GATE_PID1_NO_PROBE_ERR + (pid_id - 1);
    gate_status_t gs = safety_gate_check(probe_gate);
    if (gs == GATE_STATUS_BLOCKING) {
        if (out_blocking_gate) *out_blocking_gate = (int8_t)probe_gate;
        return false;
    }

    if (out_blocking_gate) *out_blocking_gate = -1;
    return true;
}
