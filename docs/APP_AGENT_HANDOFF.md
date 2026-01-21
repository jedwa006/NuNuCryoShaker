# App Agent Handoff - Current MCU State

**Date**: 2026-01-20
**Firmware Version**: v0.4.1
**Firmware Branch**: `main`
**Status**: MCU implementation complete, ready for app integration

---

## Summary

Current firmware features:
- **v0.4.1**: PAUSED state for mid-run inspection
- **v0.4.0**: Safety Gate Framework with configurable subsystem capabilities
- **v0.3.x**: Lazy polling, generic register commands, session management

This handoff covers:
1. **NEW: PAUSED state commands** (0x0012-0x0013)
2. **Safety gate BLE commands** (0x0070-0x0073)
3. **Extended alarm_bits** (bits 9-14 for gate bypasses and probe errors)
4. **Hardware I/O mappings** (DI and RO channels for UI visualization)
5. **Lazy polling** (0x0060-0x0061)
6. **Integration tasks** for the app

---

## Part 1: PAUSED State (NEW in v0.4.1)

### Overview

The PAUSED state allows mid-run inspection without triggering a fault. When paused:
- Motor stops
- Door can be opened for inspection
- Cooling can optionally continue (LN2 valve stays open)
- Run timer is paused

### Machine State

`MACHINE_STATE_PAUSED = 7`

### Commands

#### CMD_PAUSE_RUN (0x0012)

Pause the current run.

**Request Payload (1 byte):**
```
Offset  Size  Field       Description
------  ----  ----------  ---------------------------
0       1     pause_mode  0=keep cooling, 1=stop cooling
```

**Pause Modes:**
| Mode | Value | Behavior |
|------|-------|----------|
| PAUSE_MODE_KEEP_COOLING | 0 | LN2 valve stays open, maintains cold |
| PAUSE_MODE_STOP_COOLING | 1 | LN2 valve closes, temperature rises |

**ACK:** Standard ACK. Returns error if not in RUNNING or PRECOOL state.

#### CMD_RESUME_RUN (0x0013)

Resume from paused state.

**Request:** Empty payload

**ACK:** Standard ACK. Returns error if:
- Not in PAUSED state
- Door is open (interlock check)

### App Integration

1. Add command constants:
   ```kotlin
   const val CMD_PAUSE_RUN = 0x0012
   const val CMD_RESUME_RUN = 0x0013
   ```

2. Add UI for pause/resume during RUNNING state

3. Handle PAUSED state in telemetry (machine_state = 7)

4. Show pause mode indicator (cooling active/stopped)

---

## Part 2: Hardware I/O Mappings

### Digital Inputs (di_bits)

These are the physical inputs from sensors - update your UI visualization to match.

| DI | Bit | Constant | Function | Active State |
|---:|---:|---|---|---|
| 1 | 0 | `ESTOP` | E-Stop button | LOW = active (normally closed) |
| 2 | 1 | `DOOR_CLOSED` | Door position sensor | HIGH = closed, LOW = open |
| 3 | 2 | `LN2_PRESENT` | LN2 supply sensor | HIGH = present |
| 4 | 3 | `MOTOR_FAULT` | Reserved (soft starter has no output) | - |
| 5-8 | 4-7 | - | Unused | - |

**App UI should show:**
- DI1 (bit 0): E-Stop indicator - RED when active (bit=0)
- DI2 (bit 1): Door status - GREEN when closed (bit=1), YELLOW/RED when open (bit=0)
- DI3 (bit 2): LN2 presence - BLUE/GREEN when present (bit=1)

### Relay Outputs (ro_bits)

These are the relay channels - update your UI toggles and indicators.

| CH | Bit | Constant | Function | Notes |
|---:|---:|---|---|---|
| 1 | 0 | `MAIN_CONTACTOR` | Motor circuit power | Contactor + soft starter |
| 2 | 1 | `MOTOR_START` | Soft starter START | Triggers soft starter |
| 3 | 2 | `HEATER_1` | Axle bearing heater | PID2 controlled |
| 4 | 3 | `HEATER_2` | Orbital bearing heater | PID3 controlled |
| 5 | 4 | `LN2_VALVE` | LN2 solenoid | PID1 controlled (chilldown) |
| 6 | 5 | `DOOR_LOCK` | Door lock solenoid | Locked during run |
| 7 | 6 | `CHAMBER_LIGHT` | Chamber lighting | User toggle |
| 8 | 7 | - | Unused | - |

**App channel constants should be:**
```kotlin
const val LIGHT_RELAY_CHANNEL = 7        // CH7 = Chamber light
const val DOOR_LOCK_RELAY_CHANNEL = 6    // CH6 = Door lock
const val LN2_VALVE_CHANNEL = 5          // CH5 = LN2 solenoid (chilldown)
```

---

## Part 3: Safety Gate Framework (v0.4.0)

### Concepts

**Capability Levels** - Configurable per subsystem, persists to NVS:
| Level | Value | Behavior |
|-------|-------|----------|
| NOT_PRESENT | 0 | Ignore completely - no gates checked |
| OPTIONAL | 1 | Faults generate warnings, don't block operations |
| REQUIRED | 2 | Faults block START_RUN, may trigger FAULT during run |

**Safety Gates** - Runtime conditions checked before/during operations:
- Gates can be ENABLED (checked) or BYPASSED (skipped) for development
- **E-Stop gate (ID 0) can NEVER be bypassed** - hardware safety requirement
- Gate bypasses reset on reboot (do not persist to NVS)

### Subsystem IDs

| ID | Subsystem | Default Capability |
|---:|-----------|-------------------|
| 0 | PID1 (LN2/Cold) | OPTIONAL |
| 1 | PID2 (Axle bearings) | REQUIRED |
| 2 | PID3 (Orbital bearings) | REQUIRED |
| 3 | DI1 (E-Stop) | REQUIRED (immutable) |
| 4 | DI2 (Door sensor) | REQUIRED |
| 5 | DI3 (LN2 present) | OPTIONAL |
| 6 | DI4 (Motor fault) | NOT_PRESENT |

### Gate IDs

| ID | Gate | Blocks | Can Bypass? |
|---:|------|--------|-------------|
| 0 | ESTOP | All operations | **NO** |
| 1 | DOOR_CLOSED | START_RUN | Yes |
| 2 | HMI_LIVE | START_RUN, continue RUN | Yes |
| 3 | PID1_ONLINE | START_RUN (if REQUIRED) | Yes |
| 4 | PID2_ONLINE | START_RUN (if REQUIRED) | Yes |
| 5 | PID3_ONLINE | START_RUN (if REQUIRED) | Yes |
| 6 | PID1_NO_PROBE_ERR | START_RUN (if REQUIRED) | Yes |
| 7 | PID2_NO_PROBE_ERR | START_RUN (if REQUIRED) | Yes |
| 8 | PID3_NO_PROBE_ERR | START_RUN (if REQUIRED) | Yes |

### Probe Error Detection

Probe errors are detected from PID PV values:
- **HHHH (Over-range)**: PV >= 500.0°C (5000 x10) - Any PID
- **LLLL (Under-range)**: PV <= -300.0°C (-3000 x10) - PID2 and PID3 only (not LN2)

The LN2 controller (PID 1) legitimately reads very low temperatures, so under-range is not an error for it.

---

## Part 4: Safety Gate BLE Commands

### CMD_GET_CAPABILITIES (0x0070)

Get current capability levels for all subsystems.

**Request:** Empty payload

**ACK Optional Data (8 bytes):**
```
Offset  Size  Field       Description
------  ----  ----------  ---------------------------
0       1     pid1_cap    PID1 capability (0-2)
1       1     pid2_cap    PID2 capability
2       1     pid3_cap    PID3 capability
3       1     di1_cap     E-Stop (always 2=REQUIRED)
4       1     di2_cap     Door sensor capability
5       1     di3_cap     LN2 sensor capability
6       1     di4_cap     Motor fault capability
7       1     reserved    Reserved
```

### CMD_SET_CAPABILITY (0x0071)

Set capability level for a subsystem. Persists to NVS.

**Request Payload (2 bytes):**
```
Offset  Size  Field           Description
------  ----  -------------   ---------------------------
0       1     subsystem_id    Subsystem ID (0-6)
1       1     capability      Capability level (0-2)
```

**ACK:** Standard ACK. Returns `INVALID_ARGS` if trying to change E-Stop (ID 3).

### CMD_GET_SAFETY_GATES (0x0072)

Get current safety gate states (enabled/bypassed) and status (passing/blocking).

**Request:** Empty payload

**ACK Optional Data (4 bytes):**
```
Offset  Size  Field         Description
------  ----  -----------   ---------------------------
0       2     gate_enable   Bitmask: 1=enabled, 0=bypassed (LE u16)
2       2     gate_status   Bitmask: 1=passing, 0=blocking (LE u16)
```

Bit positions correspond to Gate IDs (0-8).

### CMD_SET_SAFETY_GATE (0x0073)

Enable or bypass a safety gate. Does NOT persist to NVS.

**Request Payload (2 bytes):**
```
Offset  Size  Field     Description
------  ----  --------  ---------------------------
0       1     gate_id   Gate ID (0-8)
1       1     enabled   1=enable gate, 0=bypass gate
```

**ACK:** Standard ACK. Returns `INVALID_ARGS` for gate 0 (E-Stop cannot be bypassed).

---

## Part 5: Extended alarm_bits (u32)

New bits added in v0.4.0:

| Bit | Name | Meaning when set |
|----:|------|------------------|
| 9 | `GATE_DOOR_BYPASSED` | Door gate is bypassed |
| 10 | `GATE_HMI_BYPASSED` | HMI liveness gate is bypassed |
| 11 | `GATE_PID_BYPASSED` | Any PID gate is bypassed |
| 12 | `PID1_PROBE_ERROR` | PID1 has probe error (HHHH/LLLL) |
| 13 | `PID2_PROBE_ERROR` | PID2 has probe error |
| 14 | `PID3_PROBE_ERROR` | PID3 has probe error |

**App should:**
- Display warning indicators when gate bypass bits are set
- Show probe error indicators for each PID
- Consider adding a "Safety Gates Bypassed" banner when bits 9-11 are set

---

## Part 6: Lazy Polling (v0.3.7+)

### Overview

Reduce Modbus bus traffic and power consumption when no user is actively monitoring.

**Polling Rates:**
- Fast mode (default): 300ms per controller (~3.3 Hz)
- Lazy mode: 2000ms per controller (~0.5 Hz)

### Commands

#### CMD_SET_LAZY_POLL (0x0060)

**Request Payload (2 bytes):**
```
Offset  Size  Field        Description
------  ----  -----------  ---------------------------
0       1     enable       1=enable, 0=disable lazy polling
1       1     timeout_min  Idle timeout in minutes (1-255)
```

**Behavior:**
- Setting persists to NVS immediately
- Activity timer resets on any BLE command **EXCEPT KEEPALIVE**
- After timeout, polling rate reduces to lazy mode
- Any user command returns to fast mode

#### CMD_GET_LAZY_POLL (0x0061)

**Request:** Empty payload

**Response Payload (2 bytes):**
```
Offset  Size  Field        Description
------  ----  -----------  ---------------------------
0       1     enabled      1=enabled, 0=disabled
1       1     timeout_min  Current timeout setting
```

### Telemetry Integration

Run state telemetry (offset 9, 16 bytes) includes:
- `lazy_poll_active` (offset 13): 1=lazy mode, 0=fast mode
- `idle_timeout_min` (offset 14): Current timeout setting

---

## Part 7: App Integration Tasks

### Required Updates

1. **Add new command IDs to BleConstants.kt:**
   ```kotlin
   // PAUSED state (v0.4.1)
   const val CMD_PAUSE_RUN = 0x0012
   const val CMD_RESUME_RUN = 0x0013

   // Lazy polling (v0.3.7+)
   const val CMD_SET_LAZY_POLL = 0x0060
   const val CMD_GET_LAZY_POLL = 0x0061

   // Safety gates (v0.4.0)
   const val CMD_GET_CAPABILITIES = 0x0070
   const val CMD_SET_CAPABILITY = 0x0071
   const val CMD_GET_SAFETY_GATES = 0x0072
   const val CMD_SET_SAFETY_GATE = 0x0073
   ```

2. **Update alarm_bits parsing** to handle new bits 9-14

3. **Fix relay channel constants:**
   ```kotlin
   const val LIGHT_RELAY_CHANNEL = 7        // Was 6
   const val DOOR_LOCK_RELAY_CHANNEL = 6    // Was 5
   ```

4. **Add Service Mode settings panel** for capability configuration

5. **Add safety gate status display** in diagnostics/service view

### Deep Links for Testing

```bash
# Navigate to Service Mode
adb shell am start -a android.intent.action.VIEW -d "shaker://service" com.shakercontrol.app.debug

# Navigate to Registers for each PID
adb shell am start -a android.intent.action.VIEW -d "shaker://registers/1" com.shakercontrol.app.debug
adb shell am start -a android.intent.action.VIEW -d "shaker://registers/2" com.shakercontrol.app.debug
adb shell am start -a android.intent.action.VIEW -d "shaker://registers/3" com.shakercontrol.app.debug

# Navigate to Settings
adb shell am start -a android.intent.action.VIEW -d "shaker://settings" com.shakercontrol.app.debug
```

### Testing Checklist

**PAUSED State (v0.4.1):**
- [ ] Test CMD_PAUSE_RUN with keep_cooling mode
- [ ] Test CMD_PAUSE_RUN with stop_cooling mode
- [ ] Test CMD_RESUME_RUN when door is closed (should succeed)
- [ ] Test CMD_RESUME_RUN when door is open (should fail with interlock error)
- [ ] Verify machine_state=7 in telemetry when paused

**Hardware I/O:**
- [ ] Verify relay channel constants match hardware (Light=CH7, Door=CH6, LN2=CH5)
- [ ] Verify DI visualization matches hardware states

**Safety Gates (v0.4.0):**
- [ ] Test CMD_GET_CAPABILITIES returns correct values
- [ ] Test CMD_SET_CAPABILITY persists across reboot
- [ ] Test CMD_SET_CAPABILITY rejects changes to E-Stop (ID 3)
- [ ] Test CMD_GET_SAFETY_GATES returns gate enable and status bitmasks
- [ ] Test CMD_SET_SAFETY_GATE enables/bypasses gates (except E-Stop)
- [ ] Verify alarm_bits shows probe errors when PV is HHHH/LLLL
- [ ] Verify alarm_bits shows gate bypass indicators
- [ ] UI displays warning when safety gates are bypassed

**Lazy Polling (v0.3.7+):**
- [ ] Test CMD_SET_LAZY_POLL enables/disables lazy mode
- [ ] Verify lazy mode activates after idle timeout
- [ ] Verify KEEPALIVE does not reset idle timer
- [ ] Verify lazy_poll_active in telemetry

---

## Previous Features (Already Implemented)

### Generic Register Commands (v0.3.2+)
- `CMD_READ_REGISTERS (0x0030)` and `CMD_WRITE_REGISTER (0x0031)`
- Read 1-16 consecutive Modbus registers or write single register

### Session Management
- `CMD_OPEN_SESSION (0x0100)`, `CMD_KEEPALIVE (0x0101)`
- Session lease with automatic expiry

### Chilldown Mode

The MCU supports `RUN_MODE_PRECOOL_ONLY = 2` for chilldown:
1. Enters `PRECOOL` state
2. Enables LN2 valve, heaters, door lock
3. Waits for target temperature
4. Stops in `IDLE` when complete (doesn't start motor)

**For auto-start after chilldown**, the app can:
1. Send `START_RUN` with `run_mode = PRECOOL_ONLY`
2. Monitor telemetry for `machine_state = IDLE` (precool complete)
3. Send another `START_RUN` with `run_mode = NORMAL`

---

## Reference Paths

| File | Description |
|------|-------------|
| `firmware/components/safety_gate/` | Safety gate implementation |
| `firmware/components/wire_protocol/include/wire_protocol.h` | Wire protocol types and commands |
| `firmware/components/ble_gatt/ble_gatt.c` | BLE command handlers |
| `firmware/components/machine_state/include/machine_state.h` | DI/RO channel defines |
| `docs/90-command-catalog.md` | Complete protocol reference |
| `docs/SAFETY_GATE_FRAMEWORK.md` | Detailed safety gate design document |

---

## Important Notes

1. **All multi-byte values are little-endian**
2. **Temperature values are scaled ×10** (e.g., -150.0°C = -1500 as i16)
3. **Controller IDs are 1-based** (1, 2, or 3)
4. **Relay/DI channels are 1-based** in commands (1-8)
5. **Relay/DI bits are 0-based** in telemetry bitmasks (bit 0 = channel 1)
6. **E-Stop gate can NEVER be bypassed** - MCU will reject the command
7. **Capability levels persist to NVS** - survive reboots
8. **Gate bypasses do NOT persist** - reset to enabled on reboot for safety
