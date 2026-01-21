# Firmware Request: Implement Lazy Polling Mode

**Date**: 2026-01-20
**From**: App Agent
**Priority**: Quality of life improvement

---

## Summary

The Android app now has settings for "Lazy Polling" mode. This feature reduces the RS-485 PID polling frequency when the system is idle to:
1. Reduce coil whine from the PID controller PSUs (audible 1-2-3 cadence from polling each controller)
2. Reduce CPU load on the ESP32 when the system is idle

**Key requirement**: The lazy polling state should be persisted in NVS so it survives reboot.

---

## User Requirements

- When enabled, if the system is idle (not running, not in cooldown, not in any active state):
  - After the configured idle timeout (1-60 minutes), reduce PID polling from ~10Hz to ~0.2Hz (once per 5 seconds) or slower
  - The app will send the timeout value in minutes
- When disabled OR when the system becomes active (run starts, cooldown, etc.):
  - Immediately resume full-speed polling (~10Hz)
- The setting should persist across power cycles
- The app may not always be connected, so firmware must track idle time independently

---

## New BLE Command

### SET_LAZY_POLLING (0x0040)

Configure lazy polling mode.

**Request Payload (3 bytes):**
| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 1 | enabled | 0=disabled, 1=enabled |
| 1 | 2 | idle_timeout_minutes | Timeout in minutes before entering lazy mode (little-endian u16) |

**ACK Payload on Success (3 bytes):**
| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 1 | enabled | Echo of setting |
| 1 | 2 | idle_timeout_minutes | Echo of timeout |

**Example - Enable lazy polling with 3 minute timeout:**
```
Request:  cmd_id=0x0040, payload=[0x01, 0x03, 0x00]
          enabled=1, idle_timeout_minutes=3

Response: status=OK, payload=[0x01, 0x03, 0x00]
```

**Example - Disable lazy polling:**
```
Request:  cmd_id=0x0040, payload=[0x00, 0x00, 0x00]
          enabled=0, idle_timeout_minutes=0 (ignored when disabled)

Response: status=OK, payload=[0x00, 0x00, 0x00]
```

---

### GET_LAZY_POLLING (0x0041) - Optional

Read current lazy polling configuration (useful for syncing app state on connect).

**Request Payload: None**

**ACK Payload (4 bytes):**
| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 1 | enabled | Current setting (0/1) |
| 1 | 2 | idle_timeout_minutes | Configured timeout |
| 3 | 1 | is_currently_lazy | 1 if currently in lazy mode, 0 if full-speed |

---

## Implementation Notes

### 1. Idle Detection

The system is considered "idle" when:
- Machine state is IDLE (not RUNNING, COOLDOWN, etc.)
- No active alarms that would require monitoring
- No user interaction via BLE in the last N minutes (use the idle timeout)

### 2. Polling Rate Adjustment

When in lazy mode:
- Reduce PID polling from ~100ms interval to ~5000ms (5 seconds) or even ~10000ms
- This significantly reduces the audible whine from the PID controllers
- Telemetry can still be sent to the app at a reduced rate

When exiting lazy mode:
- Immediately restore full polling rate
- The transition should be fast (< 100ms) so the system is responsive when needed

### 3. NVS Persistence

Store in NVS:
- `lazy_polling_enabled` (bool)
- `lazy_polling_timeout_min` (u16)

On boot:
- Read settings from NVS
- If lazy polling is enabled and system starts idle, begin idle timer

### 4. State Machine

```
┌─────────────────┐
│  FULL_POLLING   │ ◄─── System active, run started, or user interaction
│  (10Hz rate)    │
└────────┬────────┘
         │ Idle for N minutes
         ▼
┌─────────────────┐
│  LAZY_POLLING   │
│  (0.2Hz rate)   │
└────────┬────────┘
         │ Activity detected
         ▼
    Back to FULL_POLLING
```

### 5. Activity Detection

Events that should exit lazy mode and restart the idle timer:
- BLE command received (any command)
- Run started
- Alarm triggered
- DI state change (door, E-stop, etc.)
- Manual relay control

---

## Testing Checklist

- [ ] SET_LAZY_POLLING command stores settings in NVS
- [ ] Settings persist across reboot
- [ ] System enters lazy mode after configured timeout when idle
- [ ] System immediately exits lazy mode when run starts
- [ ] System immediately exits lazy mode when BLE command received
- [ ] Coil whine is noticeably reduced in lazy mode
- [ ] Telemetry still works (at reduced rate) in lazy mode
- [ ] GET_LAZY_POLLING returns correct state (if implemented)

---

## App-Side Implementation (Already Complete)

| Component | Path |
|-----------|------|
| Preferences | `/Users/joshuaedwards/Downloads/claudeShakerControl/app/src/main/kotlin/com/shakercontrol/app/data/preferences/DevicePreferences.kt` |
| ViewModel | `/Users/joshuaedwards/Downloads/claudeShakerControl/app/src/main/kotlin/com/shakercontrol/app/ui/settings/SettingsViewModel.kt` |
| UI Screen | `/Users/joshuaedwards/Downloads/claudeShakerControl/app/src/main/kotlin/com/shakercontrol/app/ui/settings/SettingsScreen.kt` |

The app has:
- Toggle switch for enabling/disabling lazy polling
- Dropdown for idle timeout (1, 2, 3, 5, 10, 15, 30, 60 minutes)
- Settings persisted in DataStore
- TODO comments marking where to send commands to firmware

Once firmware implements SET_LAZY_POLLING, the app needs:
1. Add command ID 0x0040 to BleConstants.kt
2. Add `setLazyPolling(enabled: Boolean, timeoutMinutes: Int)` to MachineRepository
3. Call it from SettingsViewModel when settings change

---

## Firmware Reference Paths

| Component | Absolute Path |
|-----------|---------------|
| PID Controller | `/Users/joshuaedwards/Projects/NuNuCryoShaker/firmware/components/pid_controller/pid_controller.c` |
| BLE GATT | `/Users/joshuaedwards/Projects/NuNuCryoShaker/firmware/components/ble_gatt/ble_gatt.c` |
| Wire Protocol | `/Users/joshuaedwards/Projects/NuNuCryoShaker/firmware/components/wire_protocol/include/wire_protocol.h` |
| Machine State | `/Users/joshuaedwards/Projects/NuNuCryoShaker/firmware/components/machine_state/machine_state.c` |
| NVS Storage | Use `nvs_flash` component |

---

## Why This Matters

The RS-485 Modbus communication to the PID controllers causes an audible "coil whine" from the power supplies - a rhythmic 1-2-3 sound as each controller is polled in sequence. While acceptable during operation, this noise is annoying when the system is sitting idle. Reducing the polling frequency when idle eliminates this noise without impacting system responsiveness.