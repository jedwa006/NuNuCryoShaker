# Wire Protocol (over BLE Characteristics)

## Why a framed protocol (instead of “one characteristic per value”)
- Avoids proliferation of characteristics as the system grows
- Provides versioning, CRC, sequence numbers, and extensibility
- Cleanly supports 3 downstream controllers and future expansion (PID tuning, Modbus tools)

## Conventions
- Endianness: Little-endian
- Scaling:
  - PV/SV: `x10` (e.g., 123.4 -> 1234)
  - OP%: `x10` (e.g., 87.6% -> 876)
- All frames include:
  - `proto_ver (u8)`
  - `msg_type (u8)`
  - `seq (u16)`
  - `payload_len (u16)`
  - `payload (bytes)`
  - `crc16 (u16)` over header+payload

## Message Types
### 0x01 — TELEMETRY_SNAPSHOT (Notify)
Purpose: periodic and change-driven status update.

Suggested payload:
- `timestamp_ms (u32)`
- `di_bits (u16)`   (bits 0..7 used)
- `ro_bits (u16)`   (bits 0..7 used)
- `alarm_bits (u32)` (system-level alarms)
- `controller_count (u8)`  (0..3)
- Repeated `controller_count` times:
  - `controller_id (u8)` (1,2,3)
  - `pv_x10 (i16)`
  - `sv_x10 (i16)`
  - `op_x10 (u16)`
  - `mode (u8)`  (enum)
  - `age_ms (u16)` (how old the RS-485 reading is)

Notes:
- Even if only controller #3 is present during testing, keep the structure multi-controller-ready.

### 0x10 — COMMAND (Write)
Purpose: app → ESP command requests.

Payload:
- `cmd_id (u16)`
- `flags (u16)` (optional)
- `cmd_payload (...)`

Common cmd_id set:
- 0x0001 SET_RELAY: `{ relay_index(u8), state(u8) }`
- 0x0002 SET_SV: `{ controller_id(u8), sv_x10(i16) }`
- 0x0003 SET_MODE: `{ controller_id(u8), mode(u8) }`
- 0x0100 OPEN_SESSION: `{ client_nonce(u32) }`
- 0x0101 KEEPALIVE: `{ session_id(u32) }`
- 0x0102 START_RUN: `{ session_id(u32), run_mode(u8) }`
- 0x0103 STOP_RUN: `{ session_id(u32), stop_mode(u8) }`

### 0x11 — COMMAND_ACK (Notify or Indicate)
Purpose: ESP → app command acknowledgement.

Payload:
- `acked_seq (u16)` (the COMMAND seq)
- `cmd_id (u16)`
- `status (u8)` enum:
  - 0 OK
  - 1 REJECTED_POLICY
  - 2 INVALID_ARGS
  - 3 BUSY
  - 4 HW_FAULT
  - 5 NOT_READY
- `detail (u16)` (subcode)
- `optional_data (...)`

Rule:
- Critical commands (Start/Stop/Abort, safety transitions) should ACK via **Indicate**.

### 0x20 — EVENT (Notify or Indicate)
Purpose: ESP → app events (alarms, state changes, logs).

Payload:
- `event_id (u16)`
- `severity (u8)` (INFO/WARN/ALARM/CRITICAL)
- `source (u8)` (0=system, 1..3=controller_id)
- `data (...)`

Critical events (E-stop asserted) should use **Indicate**.

## CRC16
- Choose one CRC16 and document it (e.g., CRC-16/CCITT-FALSE).
- Implement on both sides.

## Versioning policy
- `proto_ver` changes only when breaking.
- Backward-compatible additions:
  - add new msg_type
  - add new cmd_id
  - extend payload with `payload_len` guards

## Human-readable mode (optional)
For early bring-up, you may also implement a debug mode:
- A characteristic that outputs ASCII logs
- Or a “diagnostic frame” msg_type with simple key/value pairs

This is optional; binary framing is the primary contract.
