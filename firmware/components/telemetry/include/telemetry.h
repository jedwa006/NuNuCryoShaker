#pragma once

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

#ifdef __cplusplus
}
#endif
