# App Agent Session Prompt - Safety Gate Integration & Testing

Copy and paste this prompt to start an APP agent session for the ShakerControl Android app.

---

## Session Context

You are working on the **ShakerControl Android app** - the tablet UI for the NuNu CryoShaker ball mill. The MCU firmware has been updated to v0.4.0 with the new Safety Gate Framework. Your tasks are to:

1. **Integrate new safety gate BLE commands** (0x0070-0x0073)
2. **Fix relay channel constants** to match hardware
3. **Update alarm_bits parsing** for new bits 9-14
4. **Extend deep links framework** for automated testing
5. **Bump app version** to match firmware v0.4.0

---

## Hardware I/O Mappings (CRITICAL - Fix These)

### Relay Outputs (ro_bits)
The app currently has WRONG channel constants. Fix them:

```kotlin
// WRONG (current):
const val LIGHT_RELAY_CHANNEL = 6
const val DOOR_LOCK_RELAY_CHANNEL = 5

// CORRECT:
const val LIGHT_RELAY_CHANNEL = 7        // CH7 = Chamber light
const val DOOR_LOCK_RELAY_CHANNEL = 6    // CH6 = Door lock
const val LN2_VALVE_CHANNEL = 5          // CH5 = LN2 solenoid (chilldown)
```

### Full Channel Map
| CH | Bit | Function | Notes |
|---:|---:|---|---|
| 1 | 0 | Motor contactor | Controlled by state machine |
| 2 | 1 | Soft starter START | Controlled by state machine |
| 3 | 2 | Heater 1 (Axle) | PID2 controlled |
| 4 | 3 | Heater 2 (Orbital) | PID3 controlled |
| 5 | 4 | LN2 valve | PID1 controlled (chilldown) |
| 6 | 5 | Door lock | Locked during run |
| 7 | 6 | Chamber light | User toggle |

### Digital Inputs (di_bits)
| DI | Bit | Function | Active State |
|---:|---:|---|---|
| 1 | 0 | E-Stop | LOW = active |
| 2 | 1 | Door closed | HIGH = closed |
| 3 | 2 | LN2 present | HIGH = present |

---

## New BLE Commands to Implement

### Safety Gate Commands (0x0070-0x0073)

Add these command IDs to `BleConstants.kt`:
```kotlin
const val CMD_GET_CAPABILITIES = 0x0070
const val CMD_SET_CAPABILITY = 0x0071
const val CMD_GET_SAFETY_GATES = 0x0072
const val CMD_SET_SAFETY_GATE = 0x0073
```

### Command Details

**CMD_GET_CAPABILITIES (0x0070)**
- Request: Empty
- Response: 8 bytes - capability levels for each subsystem (0=NOT_PRESENT, 1=OPTIONAL, 2=REQUIRED)

**CMD_SET_CAPABILITY (0x0071)**
- Request: `subsystem_id(u8)`, `capability(u8)`
- Subsystem IDs: 0=PID1, 1=PID2, 2=PID3, 3=E-Stop(immutable), 4=Door, 5=LN2, 6=Motor

**CMD_GET_SAFETY_GATES (0x0072)**
- Request: Empty
- Response: 4 bytes - `gate_enable(u16 LE)`, `gate_status(u16 LE)` bitmasks

**CMD_SET_SAFETY_GATE (0x0073)**
- Request: `gate_id(u8)`, `enabled(u8)`
- Gate IDs: 0=ESTOP(cannot bypass), 1=DOOR, 2=HMI, 3-5=PID_ONLINE, 6-8=PID_NO_PROBE_ERR

---

## Extended alarm_bits (u32)

Update telemetry parsing for new bits:

```kotlin
// Existing bits (0-8) - no changes needed

// New bits (9-14) - ADD THESE:
const val ALARM_GATE_DOOR_BYPASSED = 1 shl 9
const val ALARM_GATE_HMI_BYPASSED = 1 shl 10
const val ALARM_GATE_PID_BYPASSED = 1 shl 11
const val ALARM_PID1_PROBE_ERROR = 1 shl 12
const val ALARM_PID2_PROBE_ERROR = 1 shl 13
const val ALARM_PID3_PROBE_ERROR = 1 shl 14
```

**UI should show warnings when:**
- Any bit 9-11 is set: "Safety gate bypassed" warning banner
- Any bit 12-14 is set: Probe error indicator next to affected PID

---

## Deep Links Framework

Extend the deep links for automated testing:

```kotlin
// Existing deep links
"shaker://run" -> RunScreen
"shaker://registers/{controllerId}" -> RegisterEditorScreen
"shaker://settings" -> SettingsScreen
"shaker://service" -> ServiceModeScreen

// Add these new deep links:
"shaker://diagnostics" -> DiagnosticsScreen (show DI/RO states, alarm bits)
"shaker://safety" -> SafetyGateScreen (show capabilities, gate states)
"shaker://test/relay/{channel}/{state}" -> Directly toggle relay (for testing)
"shaker://test/capability/{subsystem}/{level}" -> Set capability level
"shaker://test/gate/{gateId}/{enabled}" -> Enable/bypass safety gate
```

### Test Commands via ADB
```bash
# Test light toggle
adb shell am start -a android.intent.action.VIEW -d "shaker://test/relay/7/1" com.shakercontrol.app.debug
adb shell am start -a android.intent.action.VIEW -d "shaker://test/relay/7/0" com.shakercontrol.app.debug

# Test door lock toggle
adb shell am start -a android.intent.action.VIEW -d "shaker://test/relay/6/1" com.shakercontrol.app.debug

# Navigate to safety gate screen
adb shell am start -a android.intent.action.VIEW -d "shaker://safety" com.shakercontrol.app.debug

# Set PID1 capability to OPTIONAL (1)
adb shell am start -a android.intent.action.VIEW -d "shaker://test/capability/0/1" com.shakercontrol.app.debug

# Bypass door gate (gate 1)
adb shell am start -a android.intent.action.VIEW -d "shaker://test/gate/1/0" com.shakercontrol.app.debug
```

---

## Version Update

Bump app version to match firmware:
- `versionCode`: increment
- `versionName`: "0.4.0" or matching format

---

## Testing Checklist

After making changes, verify:

- [ ] Light toggle (CH7) works via UI
- [ ] Door lock toggle (CH6) works via UI
- [ ] LN2 valve/chilldown (CH5) triggers correct relay
- [ ] DI visualization shows correct states (E-Stop, Door, LN2)
- [ ] New alarm bits (9-14) display warnings in UI
- [ ] CMD_GET_CAPABILITIES returns subsystem levels
- [ ] CMD_SET_CAPABILITY changes persist across reconnect
- [ ] CMD_GET_SAFETY_GATES returns gate enable/status
- [ ] CMD_SET_SAFETY_GATE can bypass gates (except E-Stop)
- [ ] Deep links work for all test scenarios
- [ ] App version reflects v0.4.0

---

## Reference Documentation

Full details in the firmware repository:
- `/Users/joshuaedwards/Projects/NuNuCryoShaker/docs/APP_AGENT_HANDOFF.md`
- `/Users/joshuaedwards/Projects/NuNuCryoShaker/docs/90-command-catalog.md`
- `/Users/joshuaedwards/Projects/NuNuCryoShaker/docs/SAFETY_GATE_FRAMEWORK.md`

App repository:
- `/Users/joshuaedwards/Downloads/claudeShakerControl/`

---

## Important Notes

1. **All multi-byte values are little-endian**
2. **Temperature values are scaled ×10** (e.g., -150.0°C = -1500)
3. **Controller IDs are 1-based** (1, 2, 3)
4. **Relay/DI channels are 1-based** in commands (1-8)
5. **Relay/DI bits are 0-based** in telemetry bitmasks
6. **E-Stop gate (ID 0) can NEVER be bypassed**
