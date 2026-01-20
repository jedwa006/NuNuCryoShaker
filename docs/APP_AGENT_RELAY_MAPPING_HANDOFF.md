# App Agent Handoff: Relay Channel Mapping Fix

**Date:** 2026-01-20
**Firmware Version:** v0.3.10 (build 0x26012011)
**Status:** MCU relay handling verified, app needs channel constant updates

---

## Summary

The MCU relay handling (`SET_RELAY` command) is fully working. However, the app has **incorrect relay channel constants** that need to be updated.

---

## Relay Channel Mapping (MCU - Authoritative)

| Channel | MCU Define | Function | Notes |
|---------|------------|----------|-------|
| CH1 | `RO_MAIN_CONTACTOR` | Motor circuit power | Contactor + soft starter circuit |
| CH2 | `RO_MOTOR_START` | Soft starter START | Triggers soft starter |
| CH3 | `RO_HEATER_1` | Axle bearing heater | PID-controlled |
| CH4 | `RO_HEATER_2` | Orbital bearing heater | PID-controlled |
| CH5 | `RO_LN2_VALVE` | LN2 solenoid | Cryogenic valve |
| CH6 | `RO_DOOR_LOCK` | Door lock solenoid | Safety interlock |
| CH7 | `RO_CHAMBER_LIGHT` | Chamber lighting | User-controlled |

Source: `/Users/joshuaedwards/Documents/GitHub/NuNuCryoShaker/firmware/components/machine_state/include/machine_state.h:64-70`

---

## Required App Changes

### 1. Fix Relay Channel Constants

**Current (Wrong) in RunViewModel.kt:179-182:**
```kotlin
companion object {
    const val LIGHT_RELAY_CHANNEL = 6        // WRONG
    const val DOOR_LOCK_RELAY_CHANNEL = 5    // WRONG
}
```

**Correct:**
```kotlin
companion object {
    const val LIGHT_RELAY_CHANNEL = 7        // CH7 = Chamber light
    const val DOOR_LOCK_RELAY_CHANNEL = 6    // CH6 = Door lock
}
```

### 2. Verify SET_RELAY Command ID

The MCU uses `CMD_SET_RELAY = 0x0001` (not 0x0030).

Check `/Users/joshuaedwards/Downloads/claudeShakerControl/app/src/main/kotlin/com/shakercontrol/app/data/ble/BleConstants.kt` to ensure the app is using the correct command ID.

Wire protocol reference: `/Users/joshuaedwards/Documents/GitHub/NuNuCryoShaker/firmware/components/wire_protocol/include/wire_protocol.h:37`

---

## SET_RELAY Command Format

**Command ID:** 0x0001

**Request payload (2 bytes):**
```
Offset  Size  Field         Description
------  ----  -----------   ---------------------------
0       1     relay_index   Channel number (1-8)
1       1     state         0=OFF, 1=ON, 2=TOGGLE
```

**Response:** Standard ACK with `CMD_STATUS_OK` (0x00)

**Example:** Turn on chamber light (CH7)
- `relay_index = 7`
- `state = 1`

---

## Chilldown Implementation

The MCU already supports a pre-cool only mode. To implement "chilldown" in the app:

### Option 1: Use Existing START_RUN with PRECOOL_ONLY Mode

The MCU supports `RUN_MODE_PRECOOL_ONLY = 2` which:
1. Enters `PRECOOL` state
2. Enables LN2 valve, heaters, door lock
3. Waits for target temperature
4. Stops in `IDLE` when complete (doesn't start motor)

**START_RUN Command (0x0102):**
```
Offset  Size  Field              Description
------  ----  ----------------   ---------------------------
0       4     session_id         Current session ID
4       1     run_mode           2 = PRECOOL_ONLY
5       2     target_temp_x10    Target temp × 10 (optional)
7       4     run_duration_ms    0 for chilldown (no time limit)
```

### Option 2: Interim Solution (Current App Approach)

The app's current approach of just enabling PID 1 (LN2) in AUTO mode is acceptable for now, but doesn't provide:
- Door lock engagement
- Proper state machine tracking
- Auto-start after chilldown

### "Start After Chill" Feature

For auto-start after chilldown, the app could:
1. Send `START_RUN` with `run_mode = PRECOOL_ONLY`
2. Monitor telemetry for `machine_state = IDLE` (precool complete)
3. When detected, send another `START_RUN` with `run_mode = NORMAL`

The MCU doesn't currently have a built-in "auto-start after precool" flag, but this can be handled app-side.

---

## Door Lock Safety Considerations

The MCU state machine automatically manages the door lock:

| State | Door Lock |
|-------|-----------|
| IDLE | OFF (unlocked) |
| PRECOOL | ON (locked) |
| RUNNING | ON (locked) |
| STOPPING | ON until complete, then OFF |
| E_STOP | ON (locked) |
| SERVICE | Manual control allowed |

**Current Behavior:**
- Manual door lock toggle via SET_RELAY works in any state
- No safety interlock preventing unlock during run (could be added)

**Recommended:**
- App should check machine state before allowing door unlock
- Consider adding MCU-side rejection of door unlock during RUNNING/PRECOOL

---

## Testing Checklist

- [ ] Light toggle sends SET_RELAY(7, 1/0) - verify CH7 relay toggles
- [ ] Door toggle sends SET_RELAY(6, 1/0) - verify CH6 relay toggles
- [ ] Telemetry `ro_bits` reflects actual relay state after toggle
- [ ] Verify SET_RELAY command ID is 0x0001 (not 0x0030)

---

## Files to Update

**App:**
- `/Users/joshuaedwards/Downloads/claudeShakerControl/app/src/main/kotlin/com/shakercontrol/app/ui/run/RunViewModel.kt:179-182` - Fix channel constants
- Check command ID in BLE constants file

**MCU (No changes needed):**
- Relay mapping defined in `machine_state.h:64-70`
- SET_RELAY handler in `ble_gatt.c:423-470`