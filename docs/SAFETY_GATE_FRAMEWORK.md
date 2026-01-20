# Safety Gate Framework Design

**Date:** 2026-01-20
**Version:** Draft 1.0
**Status:** Design Phase

---

## Overview

This document defines the safety gate framework that governs how the MCU enforces operational safety based on subsystem capabilities, current state, and configurable overrides.

### Design Goals

1. **MCU is authoritative** - App provides UX, MCU enforces safety
2. **Configurable capability levels** - Each subsystem can be NOT_PRESENT, OPTIONAL, or REQUIRED
3. **Runtime safety gates** - Conditions that must be met for operations (start, enable PID, etc.)
4. **Development overrides** - Ability to bypass specific gates for testing
5. **Transparent state** - All gate states readable via BLE for app display

---

## Capability Levels

Each subsystem has a capability level stored in NVS:

| Level | Value | Meaning |
|-------|-------|---------|
| NOT_PRESENT | 0 | Not installed, ignore completely |
| OPTIONAL | 1 | Present but not critical. Faults generate warnings, don't block operations |
| REQUIRED | 2 | Critical for operation. Faults block start, may trigger E-stop during run |

### Subsystems

| Subsystem | ID | Default Level | Notes |
|-----------|-----|---------------|-------|
| PID 1 (LN2/Cold) | 0 | OPTIONAL | Temperature monitoring only, valve controlled by relay |
| PID 2 (Axle bearings) | 1 | REQUIRED | Heater control, critical for bearing protection |
| PID 3 (Orbital bearings) | 2 | REQUIRED | Heater control, critical for bearing protection |
| DI1 (E-Stop) | 3 | REQUIRED | Always required, cannot be overridden |
| DI2 (Door sensor) | 4 | REQUIRED | Safety interlock |
| DI3 (LN2 present) | 5 | OPTIONAL | Advisory only |
| DI4 (Motor fault) | 6 | NOT_PRESENT | Soft starter has no fault output - reserved for future |

---

## Safety Gates

Safety gates are conditions that must pass for certain operations. Each gate can be:
- **ENABLED** (default) - Gate is checked and can block operations
- **BYPASSED** - Gate check is skipped (for development/testing)

### Gate Definitions

| Gate ID | Name | Condition | Blocks | Can Bypass? |
|---------|------|-----------|--------|-------------|
| 0 | ESTOP | DI1 not active | All operations | **NO** |
| 1 | DOOR_CLOSED | DI2 high (closed) | START_RUN | Yes |
| 2 | HMI_LIVE | BLE session active | START_RUN, continue RUN | Yes |
| 3 | PID1_ONLINE | PID1 state == ONLINE | START_RUN (if REQUIRED) | Yes |
| 4 | PID2_ONLINE | PID2 state == ONLINE | START_RUN (if REQUIRED) | Yes |
| 5 | PID3_ONLINE | PID3 state == ONLINE | START_RUN (if REQUIRED) | Yes |
| 6 | PID1_NO_PROBE_ERR | PID1 PV in range | START_RUN (if REQUIRED) | Yes |
| 7 | PID2_NO_PROBE_ERR | PID2 PV in range | START_RUN (if REQUIRED) | Yes |
| 8 | PID3_NO_PROBE_ERR | PID3 PV in range | START_RUN (if REQUIRED) | Yes |
| 9 | (Reserved) | - | - | - |

**Notes:**
- E-STOP gate (ID 0) can NEVER be bypassed. This is a hardware safety requirement.
- Gate 9 (Motor Fault) is reserved for future use - soft starter has no fault output signal.

---

## Gate Evaluation Logic

### For START_RUN Command

```
function can_start_run():
    # E-stop always checked (never bypassable)
    if gate_estop_active():
        return BLOCKED_ESTOP

    # Check bypassable gates
    if gate_enabled(DOOR_CLOSED) and door_open():
        return BLOCKED_DOOR_OPEN

    if gate_enabled(HMI_LIVE) and not hmi_live():
        return BLOCKED_HMI_STALE

    # Note: Motor fault gate disabled - soft starter has no fault output
    # if gate_enabled(MOTOR_NO_FAULT) and motor_fault():
    #     return BLOCKED_MOTOR_FAULT

    # Check PID gates based on capability level
    for pid_id in [1, 2, 3]:
        cap = get_capability(pid_id)
        if cap == REQUIRED:
            if gate_enabled(PID_ONLINE[pid_id]) and pid_offline(pid_id):
                return BLOCKED_PID_OFFLINE
            if gate_enabled(PID_NO_PROBE_ERR[pid_id]) and pid_probe_error(pid_id):
                return BLOCKED_PROBE_ERROR

    return OK
```

### For SET_MODE(AUTO) Command

```
function can_enable_pid(pid_id):
    # E-stop always checked
    if gate_estop_active():
        return BLOCKED_ESTOP

    # PID must be online
    if pid_offline(pid_id):
        return BLOCKED_PID_OFFLINE

    # Check probe error gate (can be bypassed)
    if gate_enabled(PID_NO_PROBE_ERR[pid_id]) and pid_probe_error(pid_id):
        return BLOCKED_PROBE_ERROR

    return OK
```

### During Active Run

```
function check_run_faults():
    # E-stop always triggers immediate E_STOP state
    if gate_estop_active():
        transition_to(E_STOP)
        return

    # Check gates that trigger FAULT during run
    if gate_enabled(DOOR_CLOSED) and door_open():
        transition_to(FAULT)
        return

    # Note: Motor fault gate disabled - soft starter has no fault output
    # if gate_enabled(MOTOR_NO_FAULT) and motor_fault():
    #     transition_to(FAULT)
    #     return

    # Check REQUIRED PIDs during run
    for pid_id in [1, 2, 3]:
        cap = get_capability(pid_id)
        if cap == REQUIRED:
            if gate_enabled(PID_ONLINE[pid_id]) and pid_offline(pid_id):
                transition_to(FAULT)
                return
            if gate_enabled(PID_NO_PROBE_ERR[pid_id]) and pid_probe_error(pid_id):
                transition_to(FAULT)
                return
```

---

## Probe Error Detection

Probe errors are detected from PID PV values:

| Condition | Threshold | Applies To |
|-----------|-----------|------------|
| HHHH (Over-range) | PV >= 500.0°C (5000 x10) | All PIDs |
| LLLL (Under-range) | PV <= -300.0°C (-3000 x10) | PID 2, 3 only (not LN2) |

The LN2 controller (PID 1) legitimately reads very low temperatures, so under-range is not an error for it.

---

## BLE Commands

### CMD_GET_CAPABILITIES (0x0070)

Get current capability levels for all subsystems.

**Request:** Empty

**Response payload (8 bytes):**
```
Offset  Size  Field           Description
------  ----  -------------   ---------------------------
0       1     pid1_cap        PID1 capability (0=NOT_PRESENT, 1=OPTIONAL, 2=REQUIRED)
1       1     pid2_cap        PID2 capability
2       1     pid3_cap        PID3 capability
3       1     di1_cap         E-Stop DI capability (always 2=REQUIRED)
4       1     di2_cap         Door sensor capability
5       1     di3_cap         LN2 sensor capability
6       1     di4_cap         Motor fault capability
7       1     reserved        Reserved for expansion
```

### CMD_SET_CAPABILITY (0x0071)

Set capability level for a subsystem. Persists to NVS.

**Request payload (2 bytes):**
```
Offset  Size  Field           Description
------  ----  -------------   ---------------------------
0       1     subsystem_id    Subsystem ID (0-6)
1       1     capability      Capability level (0-2)
```

**Response:** Standard ACK. Returns INVALID_ARGS if trying to change E-Stop (ID 3).

### CMD_GET_SAFETY_GATES (0x0072)

Get current safety gate states (enabled/bypassed) and status (passing/blocking).

**Request:** Empty

**Response payload (4 bytes):**
```
Offset  Size  Field           Description
------  ----  -------------   ---------------------------
0       2     gate_enable     Bitmask: 1=gate enabled, 0=bypassed (LE u16)
1       2     gate_status     Bitmask: 1=gate passing, 0=blocking (LE u16)
```

Bit positions correspond to Gate IDs (0-9).

### CMD_SET_SAFETY_GATE (0x0073)

Enable or bypass a safety gate. Does NOT persist to NVS (resets on reboot).

**Request payload (2 bytes):**
```
Offset  Size  Field           Description
------  ----  -------------   ---------------------------
0       1     gate_id         Gate ID (0-9)
1       1     enabled         1=enable gate, 0=bypass gate
```

**Response:** Standard ACK. Returns INVALID_ARGS for gate 0 (E-Stop cannot be bypassed).

---

## Telemetry Extension

Add safety state to telemetry for real-time visibility.

### Option A: Extend alarm_bits (u32)

Current alarm bits (0-8) are used. Add new bits:

| Bit | Name | Meaning when set |
|-----|------|------------------|
| 9 | GATE_DOOR_BYPASSED | Door gate is bypassed |
| 10 | GATE_HMI_BYPASSED | HMI gate is bypassed |
| 11 | GATE_PID_BYPASSED | Any PID gate bypassed |
| 12 | PID1_PROBE_ERROR | PID1 has probe error |
| 13 | PID2_PROBE_ERROR | PID2 has probe error |
| 14 | PID3_PROBE_ERROR | PID3 has probe error |

### Option B: Add safety_state to run_state struct

Add 2 bytes to `wire_telemetry_run_state_t`:

```c
typedef struct __attribute__((packed)) {
    // ... existing fields (16 bytes)
    uint16_t gate_status;    // Bitmask of gate pass/fail status
} wire_telemetry_run_state_ext_t;  // Total: 18 bytes
```

**Recommendation:** Use Option A (extend alarm_bits) for backwards compatibility.

---

## NVS Storage

Capabilities are stored in NVS namespace "safety":

| Key | Type | Description |
|-----|------|-------------|
| `cap_pid1` | u8 | PID1 capability level |
| `cap_pid2` | u8 | PID2 capability level |
| `cap_pid3` | u8 | PID3 capability level |
| `cap_di2` | u8 | Door sensor capability |
| `cap_di3` | u8 | LN2 sensor capability |
| `cap_di4` | u8 | Motor fault capability |

Note: E-Stop (DI1) capability is always REQUIRED and not stored.

Safety gate bypasses are NOT persisted (reset to enabled on reboot for safety).

---

## UI Representation (App Side)

### Service Mode Settings Panel

```
┌─────────────────────────────────────────────────┐
│ Safety Configuration                             │
├─────────────────────────────────────────────────┤
│                                                  │
│ Subsystem Capabilities                           │
│ ┌─────────────────────────────────────────────┐ │
│ │ PID 1 (LN2)        [Not Present ▼]          │ │
│ │ PID 2 (Axle)       [Required ▼]             │ │
│ │ PID 3 (Orbital)    [Required ▼]             │ │
│ │ Door Sensor        [Required ▼]             │ │
│ │ LN2 Sensor         [Optional ▼]             │ │
│ │ Motor Fault Input  [Required ▼]             │ │
│ └─────────────────────────────────────────────┘ │
│                                                  │
│ Safety Gates (Development Only)                  │
│ ┌─────────────────────────────────────────────┐ │
│ │ ⬤ E-Stop          [Always Enabled] BLOCKED  │ │
│ │ ○ Door Closed     [Enabled ▼]      PASSING  │ │
│ │ ○ HMI Live        [Bypassed ▼]     BYPASSED │ │
│ │ ○ PID 2 Online    [Enabled ▼]      PASSING  │ │
│ │ ○ PID 2 No Error  [Enabled ▼]      BLOCKED  │ │
│ │ ...                                          │ │
│ └─────────────────────────────────────────────┘ │
│                                                  │
│ ⚠ Warning: Bypassing safety gates may cause     │
│   equipment damage or injury.                   │
│                                                  │
└─────────────────────────────────────────────────┘
```

### Tree View (Optional)

```
┌─ START_RUN Blocked ──────────────────────────────┐
│                                                   │
│  ✓ E-Stop Released                               │
│  ✓ Door Closed                                   │
│  ✓ HMI Connected                                 │
│  ✗ PID 2 (Axle) ─────────────────────┐          │
│      ✓ Online                         │          │
│      ✗ No Probe Error (HHHH detected) │ REQUIRED │
│  ✓ PID 3 (Orbital) ──────────────────┘          │
│      ✓ Online                                    │
│      ✓ No Probe Error                            │
│  ⊘ PID 1 (LN2) ─────────── OPTIONAL (ignored)   │
│                                                   │
│  [Bypass PID 2 Error Gate]  [Cancel]  [Start]    │
└───────────────────────────────────────────────────┘
```

---

## Testing Criteria

### Capability Level Tests

| Test | Expected Result |
|------|-----------------|
| Set PID1 to NOT_PRESENT, PID1 offline | START_RUN allowed |
| Set PID1 to OPTIONAL, PID1 offline | START_RUN allowed (warning only) |
| Set PID1 to REQUIRED, PID1 offline | START_RUN blocked |
| Set PID1 to REQUIRED, PID1 probe error | START_RUN blocked |
| Capability persists across reboot | Values match after power cycle |

### Safety Gate Tests

| Test | Expected Result |
|------|-----------------|
| Bypass door gate, door open | START_RUN allowed |
| Enable door gate, door open | START_RUN blocked |
| Cannot bypass E-Stop gate | Command rejected |
| Gate bypass resets on reboot | All gates enabled after power cycle |
| During run, REQUIRED PID goes offline | Transition to FAULT |
| During run, OPTIONAL PID goes offline | Run continues, warning logged |

### Probe Error Tests

| Test | Expected Result |
|------|-----------------|
| PID2 PV = 800°C | Probe error detected (HHHH) |
| PID2 PV = -350°C | Probe error detected (LLLL) |
| PID1 PV = -196°C | No probe error (valid LN2 temp) |
| Probe error on REQUIRED PID | START blocked, SET_MODE(AUTO) blocked |
| Probe error on OPTIONAL PID | Warning only, operations allowed |

---

## Implementation Phases

### Phase 1: Core Framework
- [ ] Add safety_gate component with NVS storage
- [ ] Implement capability level storage/retrieval
- [ ] Implement gate enable/bypass tracking
- [ ] Add probe error detection to PID controller
- [ ] Integrate gate checks into machine_state

### Phase 2: BLE Commands
- [ ] CMD_GET_CAPABILITIES (0x0070)
- [ ] CMD_SET_CAPABILITY (0x0071)
- [ ] CMD_GET_SAFETY_GATES (0x0072)
- [ ] CMD_SET_SAFETY_GATE (0x0073)

### Phase 3: Telemetry
- [ ] Add probe error bits to alarm_bits
- [ ] Add gate bypass status bits to alarm_bits
- [ ] Update app to display new alarm bits

### Phase 4: App Integration
- [ ] Service mode settings panel for capabilities
- [ ] Safety gate status display
- [ ] Gate bypass controls (with warnings)
- [ ] Tree view for start gating visualization

---

## Summary

This framework provides:

1. **Flexibility** - Each subsystem can be NOT_PRESENT, OPTIONAL, or REQUIRED
2. **Safety** - E-Stop is always enforced, cannot be bypassed
3. **Development support** - Other gates can be temporarily bypassed
4. **Transparency** - All state visible via BLE commands and telemetry
5. **Persistence** - Capability levels persist, gate bypasses reset on reboot
6. **App integration** - Clear API for service mode configuration UI
