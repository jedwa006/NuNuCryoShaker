#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file pid_controller.h
 * @brief LC108 PID Controller Interface
 *
 * Provides high-level access to LC108/COM-800-C1 PID temperature controllers
 * via Modbus RTU. Handles register mapping, scaling, and polling.
 */

/* Number of PID controllers on the bus */
#define PID_MAX_CONTROLLERS     3

/* Polling configuration */
#define PID_POLL_INTERVAL_MS        300     /* Poll each controller every N ms (fast mode) */
#define PID_POLL_INTERVAL_SLOW_MS   2000    /* Poll interval in lazy mode (0.5 Hz per controller) */
#define PID_STALE_THRESHOLD_MS      2000    /* Data considered stale after this */

/* Lazy polling defaults */
#define PID_IDLE_TIMEOUT_DEFAULT    5       /* Default idle timeout in minutes */
#define PID_IDLE_TIMEOUT_DISABLED   0       /* 0 = always poll at full rate */

/* LC108 Register Addresses (0-based for Modbus, 1-based in datasheet) */
#define LC108_REG_PV        0   /* Process Value, x10 */
#define LC108_REG_MV1       1   /* Output 1 %, x10 */
#define LC108_REG_MV2       2   /* Output 2 %, x10 */
#define LC108_REG_MVFB      3   /* Feedback %, x10 */
#define LC108_REG_STATUS    4   /* Status bitfield */
#define LC108_REG_SV        5   /* Setpoint, x10 */
#define LC108_REG_AT        12  /* Auto-tune (0=OFF, 1=ON) */
#define LC108_REG_MODE      13  /* Control mode */
#define LC108_REG_AL1       14  /* Alarm 1 setpoint, x10 */
#define LC108_REG_AL2       15  /* Alarm 2 setpoint, x10 */

/* PID parameters */
#define LC108_REG_P1        24  /* P gain, x10 */
#define LC108_REG_I1        25  /* I time (seconds) */
#define LC108_REG_D1        26  /* D time (seconds) */

/* Limits */
#define LC108_REG_LSPL      68  /* SV lower limit, x10 */
#define LC108_REG_USPL      69  /* SV upper limit, x10 */

/* Status bits */
#define PID_STATUS_ALARM1       (1 << 0)
#define PID_STATUS_ALARM2       (1 << 1)
#define PID_STATUS_OUTPUT1      (1 << 2)
#define PID_STATUS_OUTPUT2      (1 << 3)
#define PID_STATUS_AUTOTUNE     (1 << 4)

/* Controller state */
typedef enum {
    PID_STATE_UNKNOWN = 0,      /* Never polled successfully */
    PID_STATE_ONLINE,           /* Responding normally */
    PID_STATE_STALE,            /* Data is old but was valid */
    PID_STATE_OFFLINE,          /* Stopped responding */
} pid_state_t;

/* Live data from a single controller */
typedef struct {
    float pv;                   /* Process value (temperature) */
    float sv;                   /* Setpoint */
    float output_pct;           /* Output 1 % */
    uint16_t status;            /* Status bitfield */
    uint8_t mode;               /* Control mode */
    bool alarm1;                /* Alarm 1 active */
    bool alarm2;                /* Alarm 2 active */
} pid_live_data_t;

/* Controller info */
typedef struct {
    uint8_t addr;               /* Modbus address (1-247) */
    pid_state_t state;          /* Current state */
    pid_live_data_t data;       /* Latest data */
    uint32_t last_update_ms;    /* Timestamp of last successful poll */
    uint32_t error_count;       /* Consecutive error count */
    uint32_t total_polls;       /* Total poll attempts */
    uint32_t total_errors;      /* Total errors */
} pid_controller_t;

/* Configuration */
typedef struct {
    uint8_t addresses[PID_MAX_CONTROLLERS];  /* Modbus addresses */
    uint8_t count;                           /* Number of controllers */
    uint32_t poll_interval_ms;               /* Polling interval */
} pid_config_t;

/* Default configuration: addresses 1, 2, 3 */
#define PID_CONFIG_DEFAULT() { \
    .addresses = {1, 2, 3}, \
    .count = 3, \
    .poll_interval_ms = PID_POLL_INTERVAL_MS, \
}

/**
 * @brief Initialize PID controller manager
 *
 * Starts background polling task.
 *
 * @param config Configuration (NULL for defaults)
 * @return ESP_OK on success
 */
esp_err_t pid_controller_init(const pid_config_t *config);

/**
 * @brief Deinitialize PID controller manager
 */
void pid_controller_deinit(void);

/**
 * @brief Get controller info by index
 *
 * @param index Controller index (0 to count-1)
 * @param out Output controller info
 * @return ESP_OK if valid, ESP_ERR_INVALID_ARG if index out of range
 */
esp_err_t pid_controller_get(uint8_t index, pid_controller_t *out);

/**
 * @brief Get controller info by Modbus address
 *
 * @param addr Modbus address
 * @param out Output controller info
 * @return ESP_OK if found
 */
esp_err_t pid_controller_get_by_addr(uint8_t addr, pid_controller_t *out);

/**
 * @brief Write setpoint to controller
 *
 * @param addr Modbus address
 * @param sv_celsius Setpoint in degrees C
 * @return ESP_OK on success
 */
esp_err_t pid_controller_set_sv(uint8_t addr, float sv_celsius);

/**
 * @brief Read PID parameters from controller
 *
 * @param addr Modbus address
 * @param p_gain Output: P gain
 * @param i_time Output: I time (seconds)
 * @param d_time Output: D time (seconds)
 * @return ESP_OK on success
 */
esp_err_t pid_controller_read_params(uint8_t addr, float *p_gain,
                                      uint16_t *i_time, uint16_t *d_time);

/**
 * @brief Start auto-tune on controller
 *
 * @param addr Modbus address
 * @return ESP_OK on success
 */
esp_err_t pid_controller_start_autotune(uint8_t addr);

/**
 * @brief Stop auto-tune on controller
 *
 * @param addr Modbus address
 * @return ESP_OK on success
 */
esp_err_t pid_controller_stop_autotune(uint8_t addr);

/**
 * @brief Check if any controller has an active alarm
 *
 * @return true if any alarm is active
 */
bool pid_controller_any_alarm(void);

/**
 * @brief Get age of data in milliseconds
 *
 * @param addr Modbus address
 * @return Age in ms, or UINT32_MAX if never updated
 */
uint32_t pid_controller_data_age_ms(uint8_t addr);

/**
 * @brief Force immediate poll of a controller
 *
 * Useful after writing a setpoint to get updated data quickly.
 *
 * @param addr Modbus address
 * @return ESP_OK on success
 */
esp_err_t pid_controller_force_poll(uint8_t addr);

/**
 * @brief Write PID parameters to controller
 *
 * @param addr Modbus address
 * @param p_gain P gain (e.g., 5.0 for 50.0% proportional band)
 * @param i_time I time in seconds (0 = disabled)
 * @param d_time D time in seconds (0 = disabled)
 * @return ESP_OK on success
 */
esp_err_t pid_controller_write_params(uint8_t addr, float p_gain,
                                       uint16_t i_time, uint16_t d_time);

/**
 * @brief Set controller operating mode
 *
 * @param addr Modbus address
 * @param mode 0=STOP, 1=MANUAL, 2=AUTO, 3=PROGRAM
 * @return ESP_OK on success
 */
esp_err_t pid_controller_set_mode(uint8_t addr, uint8_t mode);

/**
 * @brief Set alarm setpoints
 *
 * @param addr Modbus address
 * @param alarm1_celsius Alarm 1 setpoint in degrees C
 * @param alarm2_celsius Alarm 2 setpoint in degrees C
 * @return ESP_OK on success
 */
esp_err_t pid_controller_set_alarm_limits(uint8_t addr, float alarm1_celsius,
                                           float alarm2_celsius);

/**
 * @brief Check if controller is currently auto-tuning
 *
 * @param addr Modbus address
 * @return true if auto-tune is active
 */
bool pid_controller_is_autotuning(uint8_t addr);

/**
 * @brief Read alarm setpoints from controller
 *
 * @param addr Modbus address
 * @param alarm1_celsius Output: Alarm 1 setpoint
 * @param alarm2_celsius Output: Alarm 2 setpoint
 * @return ESP_OK on success
 */
esp_err_t pid_controller_read_alarm_limits(uint8_t addr, float *alarm1_celsius,
                                            float *alarm2_celsius);

/**
 * @brief Get controller count
 *
 * @return Number of configured controllers
 */
uint8_t pid_controller_get_count(void);

/**
 * @brief Read consecutive Modbus registers from a controller
 *
 * Provides direct register access for HMI register editor.
 *
 * @param addr Modbus address (1-3)
 * @param start_reg Starting register address
 * @param count Number of registers to read (1-16)
 * @param values Output array (must be at least count elements)
 * @return ESP_OK on success, ESP_ERR_TIMEOUT on RS-485 timeout
 */
esp_err_t pid_controller_read_registers(uint8_t addr, uint16_t start_reg,
                                         uint8_t count, uint16_t *values);

/**
 * @brief Write a single Modbus register to a controller
 *
 * Provides direct register access for HMI register editor.
 * Includes read-after-write verification.
 *
 * @param addr Modbus address (1-3)
 * @param reg Register address
 * @param value Value to write
 * @param verified_value Output: value read back after write (can be NULL)
 * @return ESP_OK on success, ESP_ERR_INVALID_RESPONSE if verification fails
 */
esp_err_t pid_controller_write_register(uint8_t addr, uint16_t reg,
                                         uint16_t value, uint16_t *verified_value);

/**
 * @brief Set idle timeout for lazy polling
 *
 * When set, polling rate reduces after no activity for this many minutes.
 * Setting persists to NVS.
 *
 * @param minutes Idle timeout in minutes (0 = disabled, always fast poll)
 * @return ESP_OK on success
 */
esp_err_t pid_controller_set_idle_timeout(uint8_t minutes);

/**
 * @brief Get current idle timeout setting
 *
 * @return Idle timeout in minutes (0 = disabled)
 */
uint8_t pid_controller_get_idle_timeout(void);

/**
 * @brief Signal user activity to reset idle timer
 *
 * Call this when any BLE command is received to prevent lazy polling.
 */
void pid_controller_signal_activity(void);

/**
 * @brief Check if currently in lazy polling mode
 *
 * @return true if idle timeout has expired and polling is slowed
 */
bool pid_controller_is_lazy_polling(void);

#ifdef __cplusplus
}
#endif
