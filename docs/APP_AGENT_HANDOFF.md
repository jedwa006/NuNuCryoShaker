# App Agent Handoff - Lazy Polling Telemetry + Alarm Architecture

**Date**: 2026-01-20
**Firmware Branch**: `feature/pid-hmi-integration`
**Firmware Version**: 0.3.4 (build 0x26012005)
**Flashed to**: ota_0 partition

---

## Summary

Two items for app-side integration:

1. **Lazy polling state now in telemetry** - MCU reports configuration in every packet
2. **Alarm handling architecture** - MCU reports real-time state; app handles history/latching

---

## Part 1: Lazy Polling Telemetry (NEW in v0.3.4)

### New Telemetry Fields

The `wire_telemetry_run_state_t` structure now includes:

| Offset | Field | Type | Description |
|--------|-------|------|-------------|
| +13 | `lazy_poll_active` | u8 | 1 = lazy polling active, 0 = fast polling |
| +14 | `idle_timeout_min` | u8 | Configured idle timeout in minutes (0 = disabled) |

**Full `wire_telemetry_run_state_t` structure (16 bytes):**
```
| machine_state (u8)     | offset 0  |
| run_elapsed_ms (u32)   | offset 1  |
| run_remaining_ms (u32) | offset 5  |
| target_temp_x10 (i16)  | offset 9  |
| recipe_step (u8)       | offset 11 |
| interlock_bits (u8)    | offset 12 |
| lazy_poll_active (u8)  | offset 13 |  <-- NEW
| idle_timeout_min (u8)  | offset 14 |  <-- NEW
```

### MCU as Source of Truth

The MCU stores the idle timeout in NVS (persists across reboots). The app should:

1. **On first connect:**
   - Read `idle_timeout_min` from the first telemetry packet
   - Update local UI to reflect MCU's current setting
   - If app has a different desired value, send `CMD_SET_IDLE_TIMEOUT`

2. **When user changes timeout in app:**
   - Send `CMD_SET_IDLE_TIMEOUT (0x0040)` with new value
   - Wait for ACK (status OK confirms NVS write succeeded)
   - Verify by checking next telemetry packet's `idle_timeout_min`

3. **Displaying lazy mode status:**
   - Use `lazy_poll_active` to show current polling mode
   - Changes automatically based on activity timer

### Why MCU Might "Default to 5 Minutes"

If the app never sends `CMD_SET_IDLE_TIMEOUT` after connecting, the MCU uses its stored value (or default of 5 minutes if NVS is empty). The app should query the telemetry and update if needed.

---

## Part 2: Alarm Architecture

### How MCU Alarms Work

The MCU reports alarms via **two separate mechanisms**:

#### 1. Telemetry `alarm_bits` (u32) - Real-time State

**Sent:** Every telemetry packet (10 Hz)
**Behavior:** Reports **current** conditions - NOT latched

```c
ALARM_BIT_ESTOP_ACTIVE   = 0x0001  // E-stop currently pressed
ALARM_BIT_DOOR_INTERLOCK = 0x0002  // Door currently open during run
ALARM_BIT_OVER_TEMP      = 0x0004  // Over-temperature condition
ALARM_BIT_RS485_FAULT    = 0x0008  // RS-485 communication fault
ALARM_BIT_POWER_FAULT    = 0x0010  // Power supply fault
ALARM_BIT_HMI_NOT_LIVE   = 0x0020  // Session expired/stale
ALARM_BIT_PID1_FAULT     = 0x0040  // PID controller 1 offline/alarm
ALARM_BIT_PID2_FAULT     = 0x0080  // PID controller 2 offline/alarm
ALARM_BIT_PID3_FAULT     = 0x0100  // PID controller 3 offline/alarm
```

**Key Point:** These bits **clear automatically** when the condition resolves.

#### 2. Events - One-shot Notifications

**Sent:** When state transitions occur (E-stop pressed, run started, etc.)
**Behavior:** Fire once per transition, include severity level

```c
EVENT_ESTOP_ASSERTED    = 0x1001  // E-stop engaged
EVENT_ESTOP_CLEARED     = 0x1002  // E-stop released
EVENT_RUN_STARTED       = 0x1200
EVENT_RUN_STOPPED       = 0x1201
EVENT_RUN_ABORTED       = 0x1202  // Run terminated by fault
EVENT_STATE_CHANGED     = 0x1204  // Generic state change
```

### Why Alarms Appear Briefly Then Disappear

**This is correct MCU behavior.** The MCU reports truthfully:

1. **Brief PID offline:** RS-485 poll failed → `PID1_FAULT` set → Next poll succeeds → `PID1_FAULT` cleared
2. **Session expiry race:** Keepalive arrives just as timer expires → `HMI_NOT_LIVE` set briefly → Session revives → Cleared

The MCU doesn't latch alarms because it doesn't know if this is a "real" problem or a transient glitch.

### Required App-Side Implementation

**The app MUST implement alarm history tracking:**

```swift
// Pseudocode - Alarm History Manager
struct AlarmHistoryEntry {
    let timestamp: Date
    let alarmBit: UInt32
    let wasAsserted: Bool  // true = alarm set, false = alarm cleared
}

class AlarmHistoryManager {
    private var history: [AlarmHistoryEntry] = []
    private var previousAlarmBits: UInt32 = 0
    private var unacknowledgedAlarms: Set<UInt32> = []

    func processTelemetry(_ telemetry: TelemetryPacket) {
        let current = telemetry.alarmBits
        let changed = current ^ previousAlarmBits

        for bit in 0..<32 {
            let mask: UInt32 = 1 << bit
            if (changed & mask) != 0 {
                let isNowSet = (current & mask) != 0

                // Record the transition
                history.append(AlarmHistoryEntry(
                    timestamp: Date(),
                    alarmBit: mask,
                    wasAsserted: isNowSet
                ))

                // Track unacknowledged alarms
                if isNowSet {
                    unacknowledgedAlarms.insert(mask)
                }
            }
        }
        previousAlarmBits = current
    }

    func acknowledgeAlarm(_ alarmBit: UInt32) {
        unacknowledgedAlarms.remove(alarmBit)
    }

    func hasUnacknowledgedAlarms() -> Bool {
        return !unacknowledgedAlarms.isEmpty
    }
}
```

### Recommended Alarm UI Pattern

1. **Active Alarms Banner:**
   - Show currently active alarms from `alarm_bits`
   - Red indicator while any bit is set
   - Clears automatically when conditions resolve

2. **Alarm History Log:**
   - Record every transition (set/clear)
   - Include timestamp
   - Keep for session or persist to storage
   - Allow user to view past events

3. **Unacknowledged Alarms Badge:**
   - Even if `alarm_bits` is now 0, show that an alarm occurred
   - Require user to explicitly dismiss
   - Example: "PID1_FAULT occurred at 10:23:45 (now cleared)"

### Events vs Telemetry for Alarms

| Mechanism | Use Case | Reliability |
|-----------|----------|-------------|
| `alarm_bits` in telemetry | Continuous monitoring (primary) | High (10 Hz) |
| Events | Immediate UI triggers (supplementary) | Medium (may miss if BLE busy) |

**Recommendation:** Use telemetry `alarm_bits` as **primary source**. Events supplement but don't rely on them for complete history.

---

## Previous Versions

### v0.3.3: Lazy Polling Configuration
- Added `CMD_SET_IDLE_TIMEOUT (0x0040)` and `CMD_GET_IDLE_TIMEOUT (0x0041)`
- NVS persistence of idle timeout setting
- Activity tracking (any BLE command resets timer)

### v0.3.2: Generic Register Commands
- `CMD_READ_REGISTERS (0x0030)` - Read 1-16 consecutive Modbus registers
- `CMD_WRITE_REGISTER (0x0031)` - Write single register with verification

### v0.3.1: PID Polling Fixes
- MODE register now polled alongside PV/SV
- Read-after-write verification for SET_SV and SET_MODE

---

## Reference Documentation (Absolute Paths)

| Document | Path |
|----------|------|
| **This Handoff** | `/Users/joshuaedwards/Documents/GitHub/NuNuCryoShaker/docs/APP_AGENT_HANDOFF.md` |
| Wire Protocol Types | `/Users/joshuaedwards/Documents/GitHub/NuNuCryoShaker/firmware/components/wire_protocol/include/wire_protocol.h` |
| PID Controller Header | `/Users/joshuaedwards/Documents/GitHub/NuNuCryoShaker/firmware/components/pid_controller/include/pid_controller.h` |
| PID Controller Impl | `/Users/joshuaedwards/Documents/GitHub/NuNuCryoShaker/firmware/components/pid_controller/pid_controller.c` |
| Telemetry | `/Users/joshuaedwards/Documents/GitHub/NuNuCryoShaker/firmware/components/telemetry/telemetry.c` |
| BLE GATT | `/Users/joshuaedwards/Documents/GitHub/NuNuCryoShaker/firmware/components/ble_gatt/ble_gatt.c` |
| Machine State | `/Users/joshuaedwards/Documents/GitHub/NuNuCryoShaker/firmware/components/machine_state/machine_state.c` |
| Session Manager | `/Users/joshuaedwards/Documents/GitHub/NuNuCryoShaker/firmware/components/session_mgr/session_mgr.c` |
| Firmware Version | `/Users/joshuaedwards/Documents/GitHub/NuNuCryoShaker/firmware/components/version/include/fw_version.h` |

---

## Testing Checklist

### Lazy Polling
- [ ] App reads `idle_timeout_min` from telemetry on connect
- [ ] App updates local setting from MCU value
- [ ] App sends `CMD_SET_IDLE_TIMEOUT` when user changes setting
- [ ] MCU persists setting to NVS (survives reboot)
- [ ] `lazy_poll_active` reflects current polling mode in UI

### Alarm History
- [ ] App records alarm transitions from telemetry
- [ ] Brief alarms (100-500ms) are captured in history
- [ ] User can view alarm history log
- [ ] Unacknowledged alarms persist until dismissed
- [ ] Current `alarm_bits` drives active alarm indicator

---

## Command Quick Reference

| Command | ID | Payload | ACK Data |
|---------|-----|---------|----------|
| SET_IDLE_TIMEOUT | 0x0040 | `timeout_minutes (u8)` | None |
| GET_IDLE_TIMEOUT | 0x0041 | None | `timeout_minutes (u8)` |
| SET_SV | 0x0020 | `ctrl_id, sv_x10` | None |
| SET_MODE | 0x0021 | `ctrl_id, mode` | None |
| READ_REGISTERS | 0x0030 | `ctrl_id, addr, count` | `ctrl_id, addr, count, values[]` |
| WRITE_REGISTER | 0x0031 | `ctrl_id, addr, value` | `ctrl_id, addr, verified_value` |

---

## Important Notes

1. **All multi-byte values are little-endian**
2. **Temperature values are scaled ×10** (e.g., -150.0°C = -1500 as i16)
3. **Controller IDs are 1-based** (1, 2, or 3)
4. **MCU is source of truth** for idle timeout - read from telemetry, write to change
5. **Alarm bits are real-time state** - app must track history separately
