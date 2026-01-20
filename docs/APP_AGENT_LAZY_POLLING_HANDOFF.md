# App Agent Handoff: Lazy Polling Integration

**Date:** 2026-01-20
**Firmware Version:** v0.3.8 (build 0x26012009)
**Status:** MCU implementation complete, ready for app integration testing

---

## Summary

The lazy polling feature is fully implemented in firmware. A bug was identified and fixed where KEEPALIVE commands were resetting the idle timer, preventing lazy polling from ever activating while the app was connected.

---

## Lazy Polling Overview

### Purpose
Reduce Modbus bus traffic and power consumption when no user is actively monitoring the device. The PID controllers are polled at a reduced rate after a configurable idle timeout.

### Polling Rates
- **Fast mode (default):** 300ms per controller (~3.3 Hz per controller)
- **Lazy mode:** 2000ms per controller (~0.5 Hz per controller)

### Activation Logic
1. User configures idle timeout via BLE command (0 = disabled, 1-255 minutes)
2. Setting is persisted to NVS (survives reboots)
3. Activity timer resets on any BLE command **EXCEPT KEEPALIVE** (fixed in v0.3.7)
4. After idle timeout expires with no activity, polling rate reduces to lazy mode
5. Any user command (not KEEPALIVE) immediately returns to fast mode

---

## Telemetry Integration

### Run State Telemetry (Offset 9, 16 bytes)

The `lazy_poll_active` and `idle_timeout_min` fields are included in every telemetry packet:

```
Offset  Size  Field                Description
------  ----  -------------------  ----------------------------------
0       1     machine_state        Current state machine state
1       4     run_elapsed_ms       Time since run started (LE u32)
5       4     run_remaining_ms     Time until run ends (LE u32)
9       2     target_temp_x10      Target temp × 10 (LE i16)
11      1     recipe_step          Current recipe step index
12      1     interlock_bits       Safety interlock status
13      1     lazy_poll_active     1 = lazy mode active, 0 = fast mode
14      1     idle_timeout_min     Current idle timeout setting (minutes)
15      1     reserved             Padding (always 0)
------  ----  -------------------  ----------------------------------
Total: 16 bytes
```

### Parsing Example (Dart)
```dart
// In telemetry parser, after extracting run state at offset 9:
final lazyPollActive = runStateBytes[13] == 1;
final idleTimeoutMin = runStateBytes[14];
```

---

## BLE Commands

### CMD_SET_LAZY_POLL (0x0060)

**Request payload (2 bytes):**
```
Offset  Size  Field          Description
------  ----  -------------  ---------------------------
0       1     enable         1 = enable, 0 = disable lazy polling
1       1     timeout_min    Idle timeout in minutes (1-255, ignored if enable=0)
```

**Response:** Standard ACK with `CMD_STATUS_OK` (0x00)

**Behavior:**
- `enable=1, timeout_min=5` → Enable lazy polling, activate after 5 minutes idle
- `enable=0, timeout_min=X` → Disable lazy polling (X ignored), always fast poll
- Setting persists to NVS immediately
- Takes effect immediately (timer starts from current moment)

### CMD_GET_LAZY_POLL (0x0061)

**Request payload:** None (0 bytes)

**Response payload (2 bytes):**
```
Offset  Size  Field          Description
------  ----  -------------  ---------------------------
0       1     enabled        1 = enabled, 0 = disabled
1       1     timeout_min    Current timeout setting
```

---

## Bug Fixes (v0.3.7 and v0.3.8)

### Bug 1: KEEPALIVE Resetting Activity Timer (Fixed in v0.3.7)

**Problem:** KEEPALIVE commands were resetting the activity timer, preventing lazy polling from ever activating while the app maintained a session.

**Root Cause:** In `ble_gatt.c`, `pid_controller_signal_activity()` was called for ALL commands before the switch statement.

**Fix:** Activity signal is now skipped for KEEPALIVE:
```c
if (cmd_id != CMD_KEEPALIVE) {
    pid_controller_signal_activity();
}
```

### Bug 2: Telemetry Missing lazy_poll_active and idle_timeout_min (Fixed in v0.3.8)

**Problem:** App reported "No run state data" warning because `lazy_poll_active` and `idle_timeout_min` were not being serialized into the telemetry payload.

**Root Cause:** In `wire_protocol.c`, the `wire_build_telemetry_ext()` function was only serializing 14 bytes of the 16-byte `wire_telemetry_run_state_t` struct - it was missing the last two fields.

**Old code (lines 283-301):**
```c
payload[offset++] = run_state->recipe_step;
payload[offset++] = run_state->interlock_bits;
// MISSING: lazy_poll_active and idle_timeout_min!
```

**Fixed code:**
```c
payload[offset++] = run_state->recipe_step;
payload[offset++] = run_state->interlock_bits;
payload[offset++] = run_state->lazy_poll_active;   // offset 13
payload[offset++] = run_state->idle_timeout_min;   // offset 14
```

### Expected Behavior Now
- App sends KEEPALIVE every few seconds to maintain session
- KEEPALIVE does NOT reset idle timer
- User interactions (reading temps, changing setpoints, etc.) DO reset idle timer
- After configured idle timeout with no user interaction, lazy mode activates
- Any user command (not KEEPALIVE) immediately returns to fast mode
- **Telemetry now includes all 16 bytes of run state, including lazy_poll_active and idle_timeout_min**

---

## Testing Checklist

### Basic Functionality
- [ ] Fresh boot with lazy polling disabled → stays in fast mode indefinitely
- [ ] Fresh boot with lazy polling enabled (1 min) → enters lazy mode after 1 min
- [ ] Telemetry shows correct `lazy_poll_active` (0 or 1)
- [ ] Telemetry shows correct `idle_timeout_min`

### With App Connected
- [ ] Connect app, enable lazy polling (1 min timeout)
- [ ] Wait 1+ minutes with no interaction (KEEPALIVE still running)
- [ ] Verify `lazy_poll_active` = 1 in telemetry
- [ ] Verify poll rate slowed (visible in diagnostics or MCU logs)
- [ ] Send any user command (e.g., read PID params)
- [ ] Verify `lazy_poll_active` = 0 (returns to fast mode)

### Persistence
- [ ] Set lazy polling enabled, timeout = 3 minutes
- [ ] Reboot device
- [ ] Read settings via CMD_GET_LAZY_POLL
- [ ] Verify settings restored (enabled=1, timeout=3)

### Edge Cases
- [ ] Timeout = 0 (disabled) → never enters lazy mode
- [ ] Disconnect app, reconnect → settings should persist
- [ ] Multiple rapid connect/disconnect cycles → no crash or state corruption

---

## Relevant Source Files

| File | Description |
|------|-------------|
| `firmware/components/pid_controller/pid_controller.c` | Lazy polling state machine, NVS persistence |
| `firmware/components/pid_controller/include/pid_controller.h` | API definitions, timing constants |
| `firmware/components/ble_gatt/ble_gatt.c` | BLE command handlers, activity signaling (fixed in v0.3.7) |
| `firmware/components/telemetry/telemetry.c` | Telemetry packet builder |
| `firmware/components/wire_protocol/include/wire_protocol.h` | Wire protocol structs |

---

## MCU-Side Verification Complete

The MCU implementation has been verified:

1. ✅ **Telemetry** - Run state includes `lazy_poll_active` (offset 13) and `idle_timeout_min` (offset 14)
2. ✅ **Serialization** - All 16 bytes of run state now written to telemetry payload (fixed in v0.3.8)
3. ✅ **NVS Persistence** - Idle timeout saved/loaded from NVS key `pid_idle_timeout`
4. ✅ **Activity Tracking** - Fixed to exclude KEEPALIVE from resetting timer (v0.3.7)
5. ✅ **State Machine** - `check_lazy_polling_state()` correctly compares idle time to timeout
6. ✅ **Commands** - `CMD_SET_LAZY_POLL` and `CMD_GET_LAZY_POLL` implemented

The app should now be able to:
1. Configure lazy polling settings via BLE
2. Monitor lazy polling state via telemetry (no more "No run state data" warning)
3. See lazy mode activate after the configured idle period (even while connected)
4. Display "SLOW" (yellow) when lazy mode is active, "FAST" (green) when normal
