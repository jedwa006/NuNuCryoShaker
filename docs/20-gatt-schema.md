# BLE GATT Schema (Pinned)

This document defines the BLE GATT surface for the System HMI link.

## Roles
- **ESP32-S3 (Waveshare controller)**: BLE Peripheral, **GATT Server**
- **Android tablet**: BLE Central, **GATT Client**

## Design goals
- Keep the GATT surface **stable** (UUIDs do not change after publication).
- Carry application data using a **versioned framed protocol** (see `docs/30-wire-protocol.md` and `docs/90-command-catalog.md`).
- Separate concerns:
  - Telemetry: **Notify**
  - Commands: **Write / Write Without Response**
  - Critical events + critical acks: **Indicate**
  - Non-critical acks/events: **Notify**
  - Metadata: **Read**
  - Future expansion: **Bulk Gateway** characteristic

## UUIDs (source of truth)
All UUIDs are pinned and listed in:
- `docs/80-gatt-uuids.md`

This file duplicates them for convenience.

---

## Service: System Control Service
- **Service UUID**: `F0C5B4D2-3D1E-4A27-9B8A-2F0B3C4D5E60`

### Characteristics

#### 1) Device Info
- **UUID**: `F0C5B4D2-3D1E-4A27-9B8A-2F0B3C4D5E61`
- **Properties**: Read
- **Direction**: ESP → App
- **Purpose**: Identify firmware + protocol contract, enable feature negotiation.

Recommended payload (binary, little-endian):
- `proto_ver (u8)` (current = 0x01)
- `fw_major (u8) fw_minor (u8) fw_patch (u8)`
- `build_id (u32)` (or 8-byte hash)
- `cap_bits (u32)` (capability flags; see `docs/90-command-catalog.md`)

#### 2) Telemetry Stream
- **UUID**: `F0C5B4D2-3D1E-4A27-9B8A-2F0B3C4D5E62`
- **Properties**: Notify
- **Direction**: ESP → App
- **Purpose**: Periodic (~10 Hz) + change-driven status snapshots.

Notes:
- Baseline cadence: **10 Hz** (100 ms)
- Also send **immediately on change** (relay toggled, alarm edge, fresh RS-485 sample)
- Telemetry payload is framed (see `TELEMETRY_SNAPSHOT` in `docs/90-command-catalog.md`)

#### 3) Command RX
- **UUID**: `F0C5B4D2-3D1E-4A27-9B8A-2F0B3C4D5E63`
- **Properties**: Write, Write Without Response
- **Direction**: App → ESP
- **Purpose**: Send framed commands (relay control, session open/keepalive, run control, PID SV updates).

Notes:
- Use **Write Without Response** for fast user interactions (relay toggles, minor setpoints), with protocol-level ACK handling.
- Use **Write** (with response) selectively if you want immediate GATT-level confirmation (optional).

#### 4) Events + Acks
- **UUID**: `F0C5B4D2-3D1E-4A27-9B8A-2F0B3C4D5E64`
- **Properties**: Indicate, Notify
- **Direction**: ESP → App
- **Purpose**:
  - Command acknowledgements (`COMMAND_ACK`)
  - Events (`EVENT`), including alarms and state transitions

Policy:
- **Indicate** for critical items:
  - E-stop asserted/cleared
  - START/STOP/ABORT ACK
  - Latched CRITICAL alarm transitions
  - Session open ACK (recommended)
- **Notify** for non-critical ACKs and routine events.

#### 5) Bulk Gateway (Future Expansion)
- **UUID**: `F0C5B4D2-3D1E-4A27-9B8A-2F0B3C4D5E65`
- **Properties**: Write, Notify
- **Direction**: App ↔ ESP
- **Purpose**: Future “gateway” operations without changing GATT:
  - Modbus register tools (read/write batches)
  - PID tuning/config pages
  - Structured config blobs / parameter sets

Implementation guidance:
- Keep the same frame header and CRC rules.
- Introduce new `msg_type` values in the reserved ranges (see `docs/90-command-catalog.md`).

#### 6) Diagnostic Log (Optional)
- **UUID**: `F0C5B4D2-3D1E-4A27-9B8A-2F0B3C4D5E66`
- **Properties**: Notify
- **Direction**: ESP → App
- **Purpose**: Optional ASCII or framed debug logs for bring-up and field diagnostics.

Notes:
- Strongly recommended during early testing; can be disabled or rate-limited for production.

---

## Standard descriptors
- **CCCD (Client Characteristic Configuration Descriptor)**: UUID `0x2902`
  - Required for Notify/Indicate characteristics.
  - Client must write CCCD to enable:
    - Notify on Telemetry Stream
    - Indicate/Notify on Events + Acks
    - Notify on Diagnostic Log (if used)

---

## Advertising & discovery (recommended)
- Device name prefix: `SYS-CTRL-<shortid>`
- Advertise the **System Control Service UUID** if feasible to simplify filtering.
- Include a short manufacturer string/version if you already have that structure in firmware.

---

## Operational expectations
1. Client connects and discovers services/characteristics.
2. Client subscribes to Telemetry Stream (Notify).
3. Client subscribes to Events + Acks (Indicate + optional Notify).
4. Client sends `OPEN_SESSION` and begins `KEEPALIVE` loop (lease-based heartbeat).
5. Client sends user commands over Command RX and listens for ACKs/events.

For exact frame layouts and command/event lists:
- `docs/30-wire-protocol.md`
- `docs/90-command-catalog.md`
- `docs/40-safety-heartbeat-and-policies.md`
