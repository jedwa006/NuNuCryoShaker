#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Relay Control Driver for Waveshare ESP32-S3-ETH-8DI-8RO
 *
 * Controls 8 relay outputs via TCA9554PWR I2C I/O expander.
 * - I2C Address: 0x20
 * - I2C Pins: GPIO42 (SDA), GPIO41 (SCL)
 * - Relay mapping: Bit 0 = RO1, Bit 7 = RO8
 */

/* Hardware configuration */
#define RELAY_I2C_SDA_PIN       42
#define RELAY_I2C_SCL_PIN       41
#define RELAY_I2C_FREQ_HZ       100000      /* 100 kHz standard mode */
#define RELAY_TCA9554_ADDR      0x20        /* 7-bit I2C address for relay outputs */
#define DI_TCA9534_ADDR         0x21        /* 7-bit I2C address for digital inputs */

/* TCA9554/TCA9534 Register addresses (compatible) */
#define TCA9554_REG_INPUT       0x00        /* Input port (read-only) */
#define TCA9554_REG_OUTPUT      0x01        /* Output port (read/write) */
#define TCA9554_REG_POLARITY    0x02        /* Polarity inversion */
#define TCA9554_REG_CONFIG      0x03        /* Configuration (0=output, 1=input) */

/* Relay state values for relay_ctrl_set() */
#define RELAY_STATE_OFF         0
#define RELAY_STATE_ON          1
#define RELAY_STATE_TOGGLE      2

/**
 * @brief Initialize the relay control driver
 *
 * Sets up I2C bus and configures TCA9554 pins as outputs.
 * All relays are initially OFF.
 *
 * @return ESP_OK on success
 */
esp_err_t relay_ctrl_init(void);

/**
 * @brief Set a single relay state
 *
 * @param relay_index Relay number 1-8
 * @param state 0=OFF, 1=ON, 2=TOGGLE
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if relay_index out of range
 */
esp_err_t relay_ctrl_set(uint8_t relay_index, uint8_t state);

/**
 * @brief Set multiple relays atomically using mask
 *
 * new_state = (current & ~mask) | (values & mask)
 *
 * @param mask Bitmask of relays to affect (bit 0 = RO1, etc.)
 * @param values Target state for affected relays
 * @return ESP_OK on success
 */
esp_err_t relay_ctrl_set_mask(uint8_t mask, uint8_t values);

/**
 * @brief Get current relay output state (cached)
 *
 * @return 8-bit value where bit N = relay N+1 state (1=ON, 0=OFF)
 */
uint8_t relay_ctrl_get_state(void);

/**
 * @brief Read actual hardware state from TCA9554 output register
 *
 * This reads the current value of the TCA9554 output register directly
 * from hardware, allowing verification of the actual relay states.
 *
 * @param state Pointer to store the 8-bit hardware state
 * @return ESP_OK on success
 */
esp_err_t relay_ctrl_read_hw_state(uint8_t *state);

/**
 * @brief Set all relays to a specific state
 *
 * @param state 8-bit value where bit N = relay N+1 state
 * @return ESP_OK on success
 */
esp_err_t relay_ctrl_set_all(uint8_t state);

/**
 * @brief Turn all relays off (safe state)
 *
 * @return ESP_OK on success
 */
esp_err_t relay_ctrl_all_off(void);

/**
 * @brief Read digital input state from TCA9534 at 0x21
 *
 * Reads the 8 digital inputs. On the Waveshare board:
 * - DI1-DI8 are directly connected to the TCA9534 pins
 * - Bit 0 = DI1, Bit 7 = DI8
 *
 * @param di_bits Pointer to store the 8-bit input state
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if TCA9534 not present
 */
esp_err_t relay_ctrl_read_di(uint8_t *di_bits);

/**
 * @brief Check if digital input hardware is available
 *
 * @return true if TCA9534 was found during init
 */
bool relay_ctrl_di_available(void);

#ifdef __cplusplus
}
#endif
