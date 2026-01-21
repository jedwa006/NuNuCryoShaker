# Command Catalog & Protocol Reference

This document defines the wire protocol carried over BLE characteristics:
- App → ESP: writes to **Command RX** characteristic
- ESP → App: Notifies/Indicates on **Telemetry Stream** and **Events + Acks**

## 1) Framing (all messages)

### Endianness
- **Little-endian** for multi-byte integers unless stated otherwise.

### Frame layout
All frames share a common header and CRC:

| Field | Type | Size | Notes |
|---|---:|---:|---|
| proto_ver | u8 | 1 | Current = `0x01` |
| msg_type | u8 | 1 | See msg types below |
| seq | u16 | 2 | Sender-chosen sequence number |
| payload_len | u16 | 2 | Length of payload in bytes |
| payload | bytes | N | Message-specific |
| crc16 | u16 | 2 | CRC of header+payload (little-endian) |

### CRC16
- **CRC-16/CCITT-FALSE**
  - Poly: `0x1021`
  - Init: `0xFFFF`
  - RefIn/RefOut: false
  - XorOut: `0x0000`

Compute CRC over:
- `proto_ver .. payload` (i.e., header + payload, excluding crc16 field)

### Scaling conventions
- PV/SV values: **x10** (e.g., 123.4 → 1234)
- OP%: **x10** (e.g., 87.6% → 876)

### Indexing conventions (human-friendly)
- Relay indexing in protocol uses **1..8** (maps to CH1..CH8 on the board label)
- Digital input indexing uses **1..8** (maps to DI1..DI8)
- Bitfields (di_bits / ro_bits):
  - bit0 = channel 1
  - bit1 = channel 2
  - ...
  - bit7 = channel 8

### Digital Input Mapping (di_bits)
| DI | Bit | Name | Function | Active State |
|---:|---:|---|---|---|
| 1 | 0 | ESTOP | E-Stop button | LOW = active (normally closed) |
| 2 | 1 | DOOR_CLOSED | Door position sensor | HIGH = closed, LOW = open |
| 3 | 2 | LN2_PRESENT | LN2 supply sensor | HIGH = present |
| 4 | 3 | MOTOR_FAULT | Reserved (soft starter has no output) | - |
| 5-8 | 4-7 | - | Unused | - |

### Relay Output Mapping (ro_bits)
| CH | Bit | Name | Function | Notes |
|---:|---:|---|---|---|
| 1 | 0 | MAIN_CONTACTOR | Motor circuit power | Contactor + soft starter |
| 2 | 1 | MOTOR_START | Soft starter START | Triggers soft starter |
| 3 | 2 | HEATER_1 | Axle bearing heater | PID2 controlled |
| 4 | 3 | HEATER_2 | Orbital bearing heater | PID3 controlled |
| 5 | 4 | LN2_VALVE | LN2 solenoid | PID1 controlled (chilldown) |
| 6 | 5 | DOOR_LOCK | Door lock solenoid | Locked during run |
| 7 | 6 | CHAMBER_LIGHT | Chamber lighting | User toggle |
| 8 | 7 | - | Unused | - |

---

## 2) Message types (msg_type)

| msg_type | Name | Direction | Typical BLE property |
|---:|---|---|---|
| 0x01 | TELEMETRY_SNAPSHOT | ESP → App | Notify |
| 0x10 | COMMAND | App → ESP | Write / Write No Resp |
| 0x11 | COMMAND_ACK | ESP → App | Notify for non-critical, Indicate for critical |
| 0x20 | EVENT | ESP → App | Notify for normal, Indicate for critical |

Reserved for future:
- 0x30..0x3F: Bulk gateway responses / async read results
- 0x40..0x4F: File/config operations
- 0xF0..0xFF: Debug/diagnostic frames

---

## 3) Telemetry: TELEMETRY_SNAPSHOT (0x01)

### Purpose
Periodic (~10 Hz) status plus change-driven immediate updates.

### Payload layout
| Field | Type | Size |
|---|---:|---:|
| timestamp_ms | u32 | 4 |
| di_bits | u16 | 2 |
| ro_bits | u16 | 2 |
| alarm_bits | u32 | 4 |
| controller_count | u8 | 1 |
| controllers[i].controller_id | u8 | 1 |
| controllers[i].pv_x10 | i16 | 2 |
| controllers[i].sv_x10 | i16 | 2 |
| controllers[i].op_x10 | u16 | 2 |
| controllers[i].mode | u8 | 1 |
| controllers[i].age_ms | u16 | 2 |

### Controller IDs
- Valid: **1, 2, 3**
- Typical bring-up: only #3 present

### Extended Telemetry (Machine State)
When machine state is enabled, the following fields are appended after controller data:

| Field | Type | Size | Notes |
|---|---:|---:|---|
| machine_state | u8 | 1 | Current state (see below) |
| run_elapsed_ms | u32 | 4 | Time since run started |
| run_remaining_ms | u32 | 4 | Estimated time remaining |
| target_temp_x10 | i16 | 2 | Target temperature × 10 |
| recipe_step | u8 | 1 | Current recipe step (0 if none) |
| interlock_bits | u8 | 1 | Which interlocks are active |

### Machine State Values
| Value | State | Description |
|---:|---|---|
| 0 | IDLE | Ready for commands |
| 1 | PRECOOL | Cooling to target temperature |
| 2 | RUNNING | Motor active, milling in progress |
| 3 | STOPPING | Thermal soak before idle |
| 4 | E_STOP | Emergency stop active |
| 5 | FAULT | Fault condition (motor, door, etc.) |
| 6 | SERVICE | Manual relay control mode |

### Interlock Bits
| Bit | Name | Description |
|---:|---|---|
| 0 | ESTOP | E-stop button pressed (DI1 LOW) |
| 1 | DOOR_OPEN | Door interlock open (DI2 LOW) |
| 2 | LN2_ABSENT | LN2 level low (DI3 LOW) |
| 3 | MOTOR_FAULT | Motor fault signal (DI4 HIGH) |
| 4 | HMI_STALE | No recent keepalive from app |

---

## 4) Commands: COMMAND (0x10)

### COMMAND payload layout
| Field | Type | Size |
|---|---:|---:|
| cmd_id | u16 | 2 |
| flags | u16 | 2 |
| cmd_payload | bytes | N |

`flags` is currently reserved; set to 0.

### Command IDs (cmd_id)

#### Session + lease (heartbeat)
| cmd_id | Name | Payload |
|---:|---|---|
| 0x0100 | OPEN_SESSION | `client_nonce(u32)` |
| 0x0101 | KEEPALIVE | `session_id(u32)` |
| 0x0102 | START_RUN | `session_id(u32)`, `run_mode(u8)`, `target_temp_x10(i16)`, `run_duration_ms(u32)` |
| 0x0103 | STOP_RUN | `session_id(u32)`, `stop_mode(u8)` |
| 0x0110 | ENABLE_SERVICE_MODE | `session_id(u32)` |
| 0x0111 | DISABLE_SERVICE_MODE | `session_id(u32)` |
| 0x0112 | CLEAR_ESTOP | `session_id(u32)` |
| 0x0113 | CLEAR_FAULT | `session_id(u32)` |

Recommended modes:
- `run_mode`:
  - 0 = NORMAL (precool → run → stop)
  - 1 = PRECOOL_ONLY (precool → stop, no motor run)
  - 2 = SKIP_PRECOOL (immediate motor run)
- `stop_mode`:
  - 0 = NORMAL_STOP (thermal soak, then idle)
  - 1 = ABORT (fast stop, immediate idle)

#### I/O control
| cmd_id | Name | Payload |
|---:|---|---|
| 0x0001 | SET_RELAY | `relay_index(u8 1..8)`, `state(u8)` |
| 0x0002 | SET_RELAY_MASK | `mask(u8)`, `values(u8)` |
| 0x0003 | PULSE_RELAY | `relay_index(u8)`, `pulse_ms(u16)` |

State encoding for SET_RELAY:
- 0 = OFF
- 1 = ON
- 2 = TOGGLE

#### PID / controller interaction (logical level; transported via ESP)
| cmd_id | Name | Payload |
|---:|---|---|
| 0x0020 | SET_SV | `controller_id(u8 1..3)`, `sv_x10(i16)` |
| 0x0021 | SET_MODE | `controller_id(u8 1..3)`, `mode(u8)` |
| 0x0022 | REQUEST_PV_SV_REFRESH | `controller_id(u8 1..3)` |

Controller mode encoding (generic; map as needed):
- 0 = STOP
- 1 = MANUAL
- 2 = AUTO
- 3 = PROGRAM

#### Safety gate configuration (v0.4+)
| cmd_id | Name | Payload |
|---:|---|---|
| 0x0070 | GET_CAPABILITIES | none |
| 0x0071 | SET_CAPABILITY | `subsystem_id(u8)`, `capability(u8)` |
| 0x0072 | GET_SAFETY_GATES | none |
| 0x0073 | SET_SAFETY_GATE | `gate_id(u8)`, `enabled(u8)` |

**Subsystem IDs:**
- 0 = PID1 (LN2/Cold)
- 1 = PID2 (Axle bearings)
- 2 = PID3 (Orbital bearings)
- 3 = DI1 (E-Stop) - always REQUIRED, cannot be changed
- 4 = DI2 (Door sensor)
- 5 = DI3 (LN2 present)
- 6 = DI4 (Motor fault)

**Capability levels:**
- 0 = NOT_PRESENT (ignore completely)
- 1 = OPTIONAL (faults generate warnings, don't block)
- 2 = REQUIRED (faults block operations)

**Gate IDs:**
- 0 = ESTOP (cannot be bypassed)
- 1 = DOOR_CLOSED
- 2 = HMI_LIVE
- 3 = PID1_ONLINE
- 4 = PID2_ONLINE
- 5 = PID3_ONLINE
- 6 = PID1_NO_PROBE_ERR
- 7 = PID2_NO_PROBE_ERR
- 8 = PID3_NO_PROBE_ERR

**Notes:**
- Capability levels persist to NVS (survive reboots)
- Gate bypasses do NOT persist (reset to enabled on reboot for safety)
- E-Stop gate (ID 0) can never be bypassed

#### Maintenance / diagnostics (optional in v0)
| cmd_id | Name | Payload |
|---:|---|---|
| 0x00F0 | REQUEST_SNAPSHOT_NOW | none |
| 0x00F1 | CLEAR_WARNINGS | none |
| 0x00F2 | CLEAR_LATCHED_ALARMS | none *(should be policy-gated)* |

---

## 5) Acknowledgements: COMMAND_ACK (0x11)

### Payload layout
| Field | Type | Size |
|---|---:|---:|
| acked_seq | u16 | 2 |
| cmd_id | u16 | 2 |
| status | u8 | 1 |
| detail | u16 | 2 |
| optional_data | bytes | N |

### Status codes
- 0 = OK
- 1 = REJECTED_POLICY
- 2 = INVALID_ARGS
- 3 = BUSY
- 4 = HW_FAULT
- 5 = NOT_READY
- 6 = TIMEOUT_DOWNSTREAM (e.g., RS-485 timeout)

### `detail` subcodes (examples)
- 0x0000 = none
- 0x0001 = session invalid / expired
- 0x0002 = interlock open (door, coolant, etc.)
- 0x0003 = estop active
- 0x0004 = controller offline
- 0x0005 = parameter out of range

### Optional data conventions
- OPEN_SESSION ACK (OK) should include:
  - `session_id(u32)`
  - `lease_ms(u16)`

Critical acks (Start/Stop/Abort, E-stop state transitions) should be sent via **Indicate**.

---

## 6) Events: EVENT (0x20)

### Payload layout
| Field | Type | Size |
|---|---:|---:|
| event_id | u16 | 2 |
| severity | u8 | 1 |
| source | u8 | 1 |
| data | bytes | N |

Severity:
- 0 = INFO
- 1 = WARN
- 2 = ALARM
- 3 = CRITICAL

Source:
- 0 = SYSTEM
- 1..3 = controller_id

### Event IDs (initial set)
| event_id | Name | Severity | Source | Data |
|---:|---|---|---:|---|
| 0x1001 | ESTOP_ASSERTED | CRITICAL | 0 | none |
| 0x1002 | ESTOP_CLEARED | INFO | 0 | none |
| 0x1100 | HMI_CONNECTED | INFO | 0 | optional |
| 0x1101 | HMI_DISCONNECTED | WARN | 0 | optional |
| 0x1200 | RUN_STARTED | INFO | 0 | none |
| 0x1201 | RUN_STOPPED | INFO | 0 | none |
| 0x1202 | RUN_ABORTED | ALARM | 0 | none (fault/e-stop during run) |
| 0x1203 | PRECOOL_COMPLETE | INFO | 0 | none (target temp reached) |
| 0x1204 | STATE_CHANGED | varies | 0 | `old_state(u8)`, `new_state(u8)` |
| 0x1300 | RS485_DEVICE_ONLINE | INFO | 1..3 | `controller_id(u8)` |
| 0x1301 | RS485_DEVICE_OFFLINE | WARN/ALARM | 1..3 | `controller_id(u8)` |
| 0x1400 | ALARM_LATCHED | ALARM/CRITICAL | 0 or 1..3 | `alarm_bits(u32)` |
| 0x1401 | ALARM_CLEARED | INFO/WARN | 0 or 1..3 | `alarm_bits(u32)` |

### STATE_CHANGED Severity
The severity of STATE_CHANGED events depends on the new state:
- CRITICAL if new_state = E_STOP
- ALARM if new_state = FAULT
- WARN if new_state = STOPPING
- INFO otherwise

Critical events (ESTOP_ASSERTED, RUN_ABORTED, STATE_CHANGED to E_STOP/FAULT) must use **Indicate**.

---

## 7) Alarm bitfield (alarm_bits u32)

Bit allocations:
- bit0: ESTOP_ACTIVE
- bit1: DOOR_INTERLOCK_OPEN
- bit2: OVER_TEMP
- bit3: RS485_FAULT
- bit4: POWER_FAULT
- bit5: HMI_NOT_LIVE (start inhibited)
- bit6: PID1_FAULT
- bit7: PID2_FAULT
- bit8: PID3_FAULT
- bit9: GATE_DOOR_BYPASSED (door safety gate bypassed)
- bit10: GATE_HMI_BYPASSED (HMI liveness gate bypassed)
- bit11: GATE_PID_BYPASSED (any PID online/probe gate bypassed)
- bit12: PID1_PROBE_ERROR (HHHH/LLLL detected)
- bit13: PID2_PROBE_ERROR (HHHH/LLLL detected)
- bit14: PID3_PROBE_ERROR (HHHH/LLLL detected)
- bits15..31: reserved

---

## 8) Capability flags (cap_bits u32) in Device Info

- bit0: SUPPORTS_SESSION_LEASE
- bit1: SUPPORTS_EVENT_LOG
- bit2: SUPPORTS_BULK_GATEWAY
- bit3: SUPPORTS_MODBUS_TOOLS
- bit4: SUPPORTS_PID_TUNING
- bit5: SUPPORTS_OTA (future)
- bits6..31: reserved

---

## 9) Worked frame examples (hex, CRC included)

All examples use:
- `proto_ver = 0x01`
- CRC-16/CCITT-FALSE as specified above
- Little-endian multi-byte fields

### Example A — COMMAND: SET_RELAY (CH1 ON)
- COMMAND seq = 0x0001
- cmd_id = 0x0001
- flags = 0x0000
- payload: relay_index=1, state=1

Hex:
01 10 01 00 06 00 01 00 00 00 01 01 8F 5B

### Example B — COMMAND_ACK: SET_RELAY OK
- ACK seq = 0x0001 (implementation choice; may differ)
- acked_seq = 0x0001
- cmd_id = 0x0001
- status = 0 OK
- detail = 0

Hex:
01 11 01 00 07 00 01 00 01 00 00 00 00 98 22

### Example C — COMMAND: OPEN_SESSION (nonce = 0xDEADBEEF)
- COMMAND seq = 0x0002
- cmd_id = 0x0100
- flags = 0x0000
- client_nonce = EF BE AD DE

Hex:
01 10 02 00 08 00 00 01 00 00 EF BE AD DE 14 C4

### Example D — COMMAND_ACK: OPEN_SESSION OK (session_id=0x12345678, lease_ms=3000)
- ACK seq = 0x0002
- acked_seq = 0x0002
- cmd_id = 0x0100
- status = OK
- detail = 0
- session_id = 78 56 34 12
- lease_ms = B8 0B

Hex:
01 11 02 00 0D 00 02 00 00 01 00 00 00 78 56 34 12 B8 0B 41 C4

### Example E — COMMAND: KEEPALIVE (session_id=0x12345678)
- COMMAND seq = 0x0003
- cmd_id = 0x0101
- session_id = 78 56 34 12

Hex:
01 10 03 00 08 00 01 01 00 00 78 56 34 12 23 A4

### Example F — COMMAND: START_RUN (NORMAL)
- COMMAND seq = 0x0004
- cmd_id = 0x0102
- session_id = 78 56 34 12
- run_mode = 01

Hex:
01 10 04 00 09 00 02 01 00 00 78 56 34 12 01 4A F9

### Example G — EVENT: ESTOP_ASSERTED (CRITICAL, SYSTEM)
- EVENT seq = 0x1000
- event_id = 0x1001
- severity = 3 (CRITICAL)
- source = 0 (SYSTEM)
- data = state=1

Hex:
01 20 00 10 05 00 01 10 03 00 01 DF 89

### Example H — TELEMETRY_SNAPSHOT (1 controller: #3)
- TELEMETRY seq = 0x2000
- timestamp_ms = 123456
- di_bits = 0b00000101
- ro_bits = 0b00000001
- alarm_bits = 0
- controller_count = 1
- controller #3: pv=25.0C, sv=30.0C, op=45.6%, mode=2 (AUTO), age_ms=120

Hex:
01 01 00 20 17 00 40 E2 01 00 05 00 01 00 00 00 00 00 01 03 FA 00 2C 01 C8 01 02 78 00 AC 2D
