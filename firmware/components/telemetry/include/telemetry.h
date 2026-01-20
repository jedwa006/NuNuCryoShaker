#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Telemetry Generator
 * ===================
 * Generates TELEMETRY_SNAPSHOT frames at 10Hz and sends them via BLE.
 * For initial testing, generates mock data.
 */

#define TELEMETRY_INTERVAL_MS   100     // 10 Hz

/**
 * @brief Initialize and start the telemetry task
 *
 * Creates a FreeRTOS task that generates telemetry at 10Hz.
 *
 * @return ESP_OK on success
 */
esp_err_t telemetry_init(void);

/**
 * @brief Stop the telemetry task
 */
void telemetry_stop(void);

/**
 * @brief Set mock DI bits (for testing)
 */
void telemetry_set_di_bits(uint16_t bits);

/**
 * @brief Set mock RO bits (for testing)
 */
void telemetry_set_ro_bits(uint16_t bits);

/**
 * @brief Set mock alarm bits (for testing)
 */
void telemetry_set_alarm_bits(uint32_t bits);

/**
 * @brief Get current RO bits
 * @return Current relay output state (bit 0 = RO1, etc.)
 */
uint16_t telemetry_get_ro_bits(void);

/**
 * @brief Enable real PID controller data in telemetry
 *
 * When enabled, telemetry will read from pid_controller component
 * instead of using mock data. Call this after pid_controller_init().
 *
 * @param enable true to use real data, false for mock
 */
void telemetry_use_real_pid(bool enable);

/**
 * @brief Enable extended telemetry with machine state
 *
 * When enabled, telemetry frames will include machine_state run info.
 * Call this after machine_state_init().
 *
 * @param enable true to include machine state, false for basic telemetry
 */
void telemetry_use_machine_state(bool enable);

/**
 * @brief Get current alarm bits
 * @return Current alarm bitmask
 */
uint32_t telemetry_get_alarm_bits(void);

/**
 * @brief Get current DI bits
 * @return Current digital input bitmask
 */
uint16_t telemetry_get_di_bits(void);

#ifdef __cplusplus
}
#endif
