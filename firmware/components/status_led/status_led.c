#include "status_led.h"

#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "led_strip.h"

static const char *TAG = "status_led";

/* LED strip handle */
static led_strip_handle_t s_led_strip = NULL;

/* Current state */
static status_led_state_t s_current_state = LED_STATE_OFF;
static bool s_initialized = false;

/* Pattern task handle */
static TaskHandle_t s_pattern_task = NULL;
static volatile bool s_pattern_running = false;

/* Flash timer for one-shot flashes */
static TimerHandle_t s_flash_timer = NULL;
static status_led_state_t s_pre_flash_state = LED_STATE_OFF;

/* Pattern parameters */
typedef struct {
    uint8_t r, g, b;
    uint16_t on_ms;
    uint16_t off_ms;
    uint8_t repeat_count;   /* 0 = infinite */
    bool breathing;         /* Use breathing effect */
    uint16_t breathe_period_ms;
} led_pattern_t;

/* State to pattern mapping */
static const led_pattern_t s_state_patterns[] = {
    [LED_STATE_OFF]                 = {LED_COLOR_OFF,      0, 0, 0, false, 0},

    /* Normal operation */
    [LED_STATE_IDLE_ADVERTISING]    = {LED_COLOR_CYAN,     0, 0, 0, true, 2000},
    [LED_STATE_CONNECTED_HEALTHY]   = {LED_COLOR_GREEN,    0, 0, 0, true, 3000},
    [LED_STATE_CONNECTED_WARNING]   = {LED_COLOR_YELLOW,   0, 0, 0, true, 1000},
    [LED_STATE_SERVICE_MODE]        = {LED_COLOR_MAGENTA,  0, 0, 0, false, 0},  /* Solid */

    /* Activity (brief, handled specially) */
    [LED_STATE_ACTIVITY_COMMAND]    = {LED_COLOR_CYAN,     30, 0, 1, false, 0},
    [LED_STATE_ACTIVITY_RELAY]      = {LED_COLOR_WHITE,    50, 0, 1, false, 0},

    /* Special modes */
    [LED_STATE_FIRMWARE_UPDATE]     = {LED_COLOR_WHITE,    0, 0, 0, true, 2000},
    [LED_STATE_FACTORY_RESET]       = {LED_COLOR_WHITE,    100, 100, 0, false, 0},
    [LED_STATE_RECOVERY_MODE]       = {LED_COLOR_BLUE,     500, 500, 0, false, 0}, /* Alternates with red */

    /* Errors */
    [LED_STATE_ERROR_DISCONNECT]    = {LED_COLOR_YELLOW,   100, 200, 2, false, 0}, /* Double blink */
    [LED_STATE_ERROR_HW_FAULT]      = {LED_COLOR_RED,      200, 200, 0, false, 0},
    [LED_STATE_ERROR_CRITICAL]      = {LED_COLOR_RED,      0, 0, 0, false, 0},     /* Solid */
    [LED_STATE_ERROR_WATCHDOG]      = {LED_COLOR_RED,      300, 700, 3, false, 0},

    /* Boot sequence */
    [LED_STATE_BOOT_POWER_ON]       = {LED_COLOR_BLUE,     0, 0, 0, false, 0},     /* Solid */
    [LED_STATE_BOOT_HW_INIT]        = {LED_COLOR_BLUE,     100, 100, 0, false, 0},
    [LED_STATE_BOOT_BLE_INIT]       = {LED_COLOR_CYAN,     100, 100, 0, false, 0},
    [LED_STATE_BOOT_COMPLETE]       = {LED_COLOR_GREEN,    150, 150, 3, false, 0},
};

/**
 * @brief Set LED color directly
 */
static esp_err_t led_set_color(uint8_t r, uint8_t g, uint8_t b)
{
    if (s_led_strip == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = led_strip_set_pixel(s_led_strip, 0, r, g, b);
    if (ret != ESP_OK) {
        return ret;
    }

    return led_strip_refresh(s_led_strip);
}

/**
 * @brief Apply brightness to a color
 */
static void apply_brightness(uint8_t *r, uint8_t *g, uint8_t *b, uint8_t brightness)
{
    *r = (*r * brightness) / 255;
    *g = (*g * brightness) / 255;
    *b = (*b * brightness) / 255;
}

/**
 * @brief Calculate breathing brightness (sine-like curve)
 */
static uint8_t breathing_brightness(uint32_t elapsed_ms, uint32_t period_ms)
{
    /* Simple triangle wave for breathing effect */
    uint32_t half_period = period_ms / 2;
    uint32_t pos = elapsed_ms % period_ms;

    if (pos < half_period) {
        /* Ramping up */
        return (uint8_t)((pos * LED_BRIGHTNESS_FULL) / half_period);
    } else {
        /* Ramping down */
        return (uint8_t)(((period_ms - pos) * LED_BRIGHTNESS_FULL) / half_period);
    }
}

/**
 * @brief Pattern execution task
 */
static void pattern_task(void *arg)
{
    (void)arg;
    TickType_t last_wake = xTaskGetTickCount();
    uint32_t elapsed_ms = 0;
    uint8_t blink_count = 0;
    bool led_on = true;
    status_led_state_t last_logged_state = LED_STATE_MAX;

    while (s_pattern_running) {
        status_led_state_t state = s_current_state;

        if (state >= LED_STATE_MAX) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        const led_pattern_t *pattern = &s_state_patterns[state];

        /* Debug: log state changes */
        if (state != last_logged_state) {
            ESP_LOGI(TAG, "Pattern task: state=%d, r=%d g=%d b=%d, breathing=%d, on_ms=%d",
                     state, pattern->r, pattern->g, pattern->b,
                     pattern->breathing, pattern->on_ms);
            last_logged_state = state;
        }

        if (pattern->breathing && pattern->breathe_period_ms > 0) {
            /* Breathing mode */
            uint8_t brightness = breathing_brightness(elapsed_ms, pattern->breathe_period_ms);
            uint8_t r = pattern->r, g = pattern->g, b = pattern->b;
            apply_brightness(&r, &g, &b, brightness);
            led_set_color(r, g, b);

            elapsed_ms += 20;
            vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(20));

        } else if (pattern->on_ms > 0) {
            /* Blinking mode */
            if (led_on) {
                /* Special case: recovery mode alternates blue/red */
                if (state == LED_STATE_RECOVERY_MODE) {
                    if (blink_count % 2 == 0) {
                        led_set_color(LED_COLOR_BLUE);
                    } else {
                        led_set_color(LED_COLOR_RED);
                    }
                } else {
                    led_set_color(pattern->r, pattern->g, pattern->b);
                }
                vTaskDelay(pdMS_TO_TICKS(pattern->on_ms));
                led_on = false;
            } else {
                led_set_color(LED_COLOR_OFF);
                vTaskDelay(pdMS_TO_TICKS(pattern->off_ms));
                led_on = true;
                blink_count++;

                /* Check repeat count */
                if (pattern->repeat_count > 0 && blink_count >= pattern->repeat_count) {
                    /* Pattern complete - go back to previous state or off */
                    /* For boot complete, transition to advertising */
                    if (state == LED_STATE_BOOT_COMPLETE) {
                        s_current_state = LED_STATE_IDLE_ADVERTISING;
                    } else if (state == LED_STATE_ERROR_DISCONNECT) {
                        s_current_state = LED_STATE_IDLE_ADVERTISING;
                    } else if (state == LED_STATE_ERROR_WATCHDOG) {
                        s_current_state = LED_STATE_IDLE_ADVERTISING;
                    }
                    blink_count = 0;
                    elapsed_ms = 0;
                }
            }
            last_wake = xTaskGetTickCount();

        } else {
            /* Solid color */
            led_set_color(pattern->r, pattern->g, pattern->b);
            vTaskDelay(pdMS_TO_TICKS(100));
            last_wake = xTaskGetTickCount();
        }
    }

    ESP_LOGI(TAG, "Pattern task stopped");
    vTaskDelete(NULL);
}

/**
 * @brief Flash timer callback
 */
static void flash_timer_callback(TimerHandle_t timer)
{
    /* Restore previous state */
    s_current_state = s_pre_flash_state;
    ESP_LOGD(TAG, "Flash complete, restored state %d", s_current_state);
}

esp_err_t status_led_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing status LED on GPIO%d", STATUS_LED_GPIO);

    /* Configure LED strip */
    led_strip_config_t strip_config = {
        .strip_gpio_num = STATUS_LED_GPIO,
        .max_leds = 1,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags = {
            .invert_out = false,
        },
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000, /* 10 MHz */
        .flags = {
            .with_dma = false,
        },
    };

    esp_err_t ret = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_led_strip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LED strip: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Clear LED initially */
    led_strip_clear(s_led_strip);

    /* Create flash timer */
    s_flash_timer = xTimerCreate(
        "led_flash",
        pdMS_TO_TICKS(100),
        pdFALSE,  /* One-shot */
        NULL,
        flash_timer_callback
    );

    if (s_flash_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create flash timer");
        return ESP_FAIL;
    }

    /* Start pattern task */
    s_pattern_running = true;
    BaseType_t ok = xTaskCreatePinnedToCore(
        pattern_task,
        "status_led",
        2048,
        NULL,
        3,  /* Low priority */
        &s_pattern_task,
        0
    );

    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create pattern task");
        s_pattern_running = false;
        return ESP_FAIL;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Status LED initialized");

    return ESP_OK;
}

esp_err_t status_led_set_state(status_led_state_t state)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (state >= LED_STATE_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_current_state != state) {
        ESP_LOGD(TAG, "State: %d -> %d", s_current_state, state);
        s_current_state = state;
    }

    return ESP_OK;
}

status_led_state_t status_led_get_state(void)
{
    return s_current_state;
}

esp_err_t status_led_set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    return led_set_color(r, g, b);
}

esp_err_t status_led_flash(uint8_t r, uint8_t g, uint8_t b, uint32_t duration_ms)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Save current state */
    s_pre_flash_state = s_current_state;

    /* Set color directly */
    led_set_color(r, g, b);

    /* Start timer to restore */
    xTimerChangePeriod(s_flash_timer, pdMS_TO_TICKS(duration_ms), 0);
    xTimerStart(s_flash_timer, 0);

    return ESP_OK;
}

void status_led_off(void)
{
    if (s_initialized) {
        s_current_state = LED_STATE_OFF;
        led_set_color(LED_COLOR_OFF);
    }
}

bool status_led_is_initialized(void)
{
    return s_initialized;
}
