#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Status LED Driver for Waveshare ESP32-S3-ETH-8DI-8RO
 *
 * Controls the onboard WS2812 RGB LED on GPIO38 for system status indication.
 * Uses ESP-IDF RMT peripheral via the led_strip component.
 *
 * Color Scheme:
 * - Blue:    Boot/Initialization
 * - Cyan:    Network/BLE Activity
 * - Green:   Healthy/Ready
 * - Yellow:  Warning/Attention
 * - Red:     Error/Fault
 * - Magenta: Service Mode
 * - White:   Special/User Action
 */

/* Hardware configuration */
#define STATUS_LED_GPIO         38
#define STATUS_LED_RMT_CHANNEL  0

/* LED states (ordered by display priority - errors shown over normal states) */
typedef enum {
    LED_STATE_OFF = 0,

    /* Normal operation (lowest priority) */
    LED_STATE_IDLE_ADVERTISING,     /* Cyan slow breathe - waiting for BLE connection */
    LED_STATE_CONNECTED_HEALTHY,    /* Green slow breathe - session active, all OK */
    LED_STATE_CONNECTED_WARNING,    /* Yellow pulse - session lease expiring */
    LED_STATE_SERVICE_MODE,         /* Magenta solid - manual override active */

    /* Activity indicators (medium priority, brief flashes) */
    LED_STATE_ACTIVITY_COMMAND,     /* Cyan flash - command received */
    LED_STATE_ACTIVITY_RELAY,       /* White flash - relay toggled */

    /* Special modes (high priority) */
    LED_STATE_FIRMWARE_UPDATE,      /* White slow breathe - OTA in progress */
    LED_STATE_FACTORY_RESET,        /* White fast blink - NVS erase */
    LED_STATE_RECOVERY_MODE,        /* Blue/Red alternating - recovery partition */

    /* Error states (higher priority) */
    LED_STATE_ERROR_DISCONNECT,     /* Yellow double blink - BLE disconnected */
    LED_STATE_ERROR_HW_FAULT,       /* Red fast blink - hardware failure */
    LED_STATE_ERROR_CRITICAL,       /* Red solid - critical alarm (e-stop) */
    LED_STATE_ERROR_WATCHDOG,       /* Red 3 slow blinks - after reset */

    /* Boot states (highest priority) */
    LED_STATE_BOOT_POWER_ON,        /* Blue solid - just powered on */
    LED_STATE_BOOT_HW_INIT,         /* Blue fast blink - hardware init */
    LED_STATE_BOOT_BLE_INIT,        /* Cyan fast blink - BLE init */
    LED_STATE_BOOT_COMPLETE,        /* Green flash 3x - boot complete */

    LED_STATE_MAX
} status_led_state_t;

/* Predefined colors - Note: This WS2812 appears to have R/G swapped
 * so we define colors with G and R positions swapped to compensate */
#define LED_COLOR_OFF       0, 0, 0
#define LED_COLOR_BLUE      0, 0, 255
#define LED_COLOR_CYAN      255, 0, 255       /* Should be 0,255,255 but R/G swapped */
#define LED_COLOR_GREEN     255, 0, 0         /* Should be 0,255,0 but R/G swapped */
#define LED_COLOR_YELLOW    200, 255, 0       /* Should be 255,200,0 but R/G swapped */
#define LED_COLOR_RED       0, 255, 0         /* Should be 255,0,0 but R/G swapped */
#define LED_COLOR_MAGENTA   0, 255, 255       /* Should be 255,0,255 but R/G swapped */
#define LED_COLOR_WHITE     255, 255, 255

/* Brightness levels */
#define LED_BRIGHTNESS_FULL 255
#define LED_BRIGHTNESS_MED  128
#define LED_BRIGHTNESS_DIM  32
#define LED_BRIGHTNESS_OFF  0

/**
 * @brief Initialize the status LED driver
 *
 * Sets up the RMT peripheral and LED strip driver.
 * Call this very early in app_main().
 *
 * @return ESP_OK on success
 */
esp_err_t status_led_init(void);

/**
 * @brief Set the current LED state
 *
 * Updates the LED to show the specified state pattern.
 * Higher priority states will override lower priority ones.
 *
 * @param state The state to display
 * @return ESP_OK on success
 */
esp_err_t status_led_set_state(status_led_state_t state);

/**
 * @brief Get the current LED state
 *
 * @return Current state enum value
 */
status_led_state_t status_led_get_state(void);

/**
 * @brief Set LED to a specific RGB color
 *
 * Direct color control, bypasses state machine.
 * Use for debugging or custom indications.
 *
 * @param r Red component (0-255)
 * @param g Green component (0-255)
 * @param b Blue component (0-255)
 * @return ESP_OK on success
 */
esp_err_t status_led_set_rgb(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Flash the LED briefly
 *
 * One-shot flash that returns to previous state after duration.
 * Useful for activity indication.
 *
 * @param r Red component
 * @param g Green component
 * @param b Blue component
 * @param duration_ms Flash duration in milliseconds
 * @return ESP_OK on success
 */
esp_err_t status_led_flash(uint8_t r, uint8_t g, uint8_t b, uint32_t duration_ms);

/**
 * @brief Turn off the LED
 */
void status_led_off(void);

/**
 * @brief Check if LED driver is initialized
 *
 * @return true if initialized
 */
bool status_led_is_initialized(void);

#ifdef __cplusplus
}
#endif
