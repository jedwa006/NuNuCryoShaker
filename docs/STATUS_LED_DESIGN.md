# Status LED Design Document

## Overview

The Waveshare ESP32-S3-ETH-8DI-8RO board includes a single WS2812 addressable RGB LED on **GPIO38** ("RGB Beads"). This document defines the color scheme, patterns, and implementation for using this LED as a system status indicator.

## Hardware

- **LED Type**: WS2812 (addressable RGB, single data line)
- **GPIO Pin**: 38
- **Driver**: ESP-IDF RMT (Remote Control Transceiver) peripheral
- **Component**: `espressif/led_strip` from ESP Component Registry

## Design Principles

1. **Clarity over complexity** - Each state should be instantly recognizable
2. **Color consistency** - Same color always means the same category of status
3. **Pattern differentiation** - Use blink patterns to distinguish sub-states within a color
4. **Non-intrusive** - Breathing/pulsing preferred over harsh blinking during normal operation
5. **Priority system** - Critical states override normal status indication

## Color Scheme

| Color | Meaning | Use Cases |
|-------|---------|-----------|
| **Blue** | Boot/Initialization | System starting up, self-test |
| **Cyan** | Network/BLE Activity | Advertising, connecting, data transfer |
| **Green** | Healthy/Ready | System operational, all OK |
| **Yellow/Amber** | Warning/Attention | Non-critical issue, session expiring |
| **Red** | Error/Fault | Critical error, hardware fault |
| **Magenta** | Service Mode | Manual override active |
| **White** | Special/User Action | Factory reset, firmware update |

## State Definitions

### Boot Sequence (Priority: Highest)

| Phase | Color | Pattern | Duration |
|-------|-------|---------|----------|
| Power On | Blue | Solid | 200ms |
| Hardware Init | Blue | Fast blink (100ms) | Until complete |
| BLE Init | Cyan | Fast blink (100ms) | Until complete |
| Boot Complete | Green | Flash 3x | 600ms total |

### Normal Operation (Priority: Low)

| State | Color | Pattern | Notes |
|-------|-------|---------|-------|
| Idle (no BLE) | Cyan | Slow breathe (2s cycle) | Advertising, waiting for connection |
| BLE Connected | Green | Slow breathe (3s cycle) | Session active, healthy |
| Session Warning | Yellow | Pulse (1s cycle) | Session lease approaching expiry |
| Service Mode | Magenta | Solid | Manual override active |

### Activity Indication (Priority: Medium)

| Event | Color | Pattern | Duration |
|-------|-------|---------|----------|
| Relay Toggle | White | Flash 1x | 50ms |
| Command Received | Cyan | Flash 1x | 30ms |
| Telemetry Burst | Green | Dim flash | 20ms (optional, may be too frequent) |

### Error States (Priority: High)

| Error | Color | Pattern | Notes |
|-------|-------|---------|-------|
| Hardware Fault | Red | Fast blink (200ms) | I2C failure, sensor error |
| BLE Disconnect | Yellow | Double blink | Connection lost |
| Critical Alarm | Red | Solid | E-stop, safety interlock |
| Watchdog Reset | Red | 3 slow blinks | After unexpected reset |

### Special Modes (Priority: Highest)

| Mode | Color | Pattern | Notes |
|------|-------|---------|-------|
| Firmware Update | White | Slow breathe | OTA in progress |
| Factory Reset | White | Fast blink | NVS erase in progress |
| Recovery Mode | Blue + Red | Alternating 500ms | Booted into recovery partition |

## Implementation Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    status_led.h                         │
├─────────────────────────────────────────────────────────┤
│ status_led_init()           - Initialize RMT driver     │
│ status_led_set_state()      - Set current state         │
│ status_led_set_color()      - Direct color control      │
│ status_led_flash()          - One-shot flash            │
│ status_led_start_pattern()  - Start repeating pattern   │
│ status_led_stop()           - Turn off LED              │
└─────────────────────────────────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────┐
│                   status_led.c                          │
├─────────────────────────────────────────────────────────┤
│ - FreeRTOS task for pattern execution                   │
│ - State machine for priority handling                   │
│ - RMT driver interface via led_strip component          │
│ - Breathing/pulsing via timer-based PWM simulation      │
└─────────────────────────────────────────────────────────┘
```

## API Design

```c
// LED states (ordered by priority - lower index = higher priority)
typedef enum {
    LED_STATE_OFF = 0,

    // Boot states (highest priority)
    LED_STATE_BOOT_POWER_ON,
    LED_STATE_BOOT_HW_INIT,
    LED_STATE_BOOT_BLE_INIT,
    LED_STATE_BOOT_COMPLETE,

    // Error states (high priority)
    LED_STATE_ERROR_CRITICAL,
    LED_STATE_ERROR_HW_FAULT,
    LED_STATE_ERROR_WATCHDOG,

    // Special modes
    LED_STATE_FIRMWARE_UPDATE,
    LED_STATE_FACTORY_RESET,
    LED_STATE_RECOVERY_MODE,

    // Activity (medium priority)
    LED_STATE_ACTIVITY_RELAY,
    LED_STATE_ACTIVITY_COMMAND,

    // Normal operation (low priority)
    LED_STATE_IDLE_ADVERTISING,
    LED_STATE_CONNECTED_HEALTHY,
    LED_STATE_CONNECTED_WARNING,
    LED_STATE_SERVICE_MODE,

    LED_STATE_MAX
} status_led_state_t;

// Initialize the status LED driver
esp_err_t status_led_init(void);

// Set the current LED state (will respect priority)
esp_err_t status_led_set_state(status_led_state_t state);

// Get current state
status_led_state_t status_led_get_state(void);

// Direct color control (bypasses state machine)
esp_err_t status_led_set_rgb(uint8_t r, uint8_t g, uint8_t b);

// One-shot flash (returns to previous state after)
esp_err_t status_led_flash(uint8_t r, uint8_t g, uint8_t b, uint32_t duration_ms);

// Stop LED (turn off)
void status_led_stop(void);
```

## Color Definitions (RGB values)

```c
#define LED_COLOR_OFF           0x00, 0x00, 0x00
#define LED_COLOR_BLUE          0x00, 0x00, 0xFF
#define LED_COLOR_CYAN          0x00, 0xFF, 0xFF
#define LED_COLOR_GREEN         0x00, 0xFF, 0x00
#define LED_COLOR_YELLOW        0xFF, 0xFF, 0x00
#define LED_COLOR_RED           0xFF, 0x00, 0x00
#define LED_COLOR_MAGENTA       0xFF, 0x00, 0xFF
#define LED_COLOR_WHITE         0xFF, 0xFF, 0xFF

// Dimmed versions for breathing effect peaks
#define LED_BRIGHTNESS_FULL     255
#define LED_BRIGHTNESS_DIM      64
#define LED_BRIGHTNESS_OFF      0
```

## Integration Points

### main_app.c
```c
void app_main(void) {
    // Very first thing - show power on
    status_led_init();
    status_led_set_state(LED_STATE_BOOT_POWER_ON);

    // NVS init
    status_led_set_state(LED_STATE_BOOT_HW_INIT);
    nvs_flash_init();

    // Boot control
    bootctl_init();

    // Relay control
    relay_ctrl_init();

    // BLE init
    status_led_set_state(LED_STATE_BOOT_BLE_INIT);
    ble_gatt_init();

    // Telemetry
    telemetry_init();

    // Boot complete - flash green 3x then go to advertising
    status_led_set_state(LED_STATE_BOOT_COMPLETE);
    vTaskDelay(pdMS_TO_TICKS(600));

    // Enter normal operation
    status_led_set_state(LED_STATE_IDLE_ADVERTISING);
}
```

### ble_gatt.c
```c
// On connection
status_led_set_state(LED_STATE_CONNECTED_HEALTHY);

// On disconnect
status_led_set_state(LED_STATE_IDLE_ADVERTISING);

// On command received (brief flash)
status_led_flash(LED_COLOR_CYAN, 30);
```

### session_mgr.c
```c
// Session warning (lease expiring)
status_led_set_state(LED_STATE_CONNECTED_WARNING);

// Session renewed
status_led_set_state(LED_STATE_CONNECTED_HEALTHY);
```

## Future Enhancements

1. **Android App Control**: Allow app to set custom colors/patterns for debugging
2. **Relay Activity Visualization**: Brief flash on relay state change
3. **Alarm Mapping**: Map specific alarms to specific blink patterns
4. **Brightness Control**: User-configurable brightness level
5. **Disable Option**: Allow LED to be disabled for installations where it's distracting

## References

- [ESP-IDF LED Strip Component](https://components.espressif.com/components/espressif/led_strip/)
- [ESP-IoT-Solution LED Indicator](https://docs.espressif.com/projects/esp-iot-solution/en/latest/display/led_indicator.html)
- [RGB LED Indicators for Embedded Systems](https://www.digikey.co.uk/en/articles/rgb-led-indicators-for-embedded-systems-and-displays)
