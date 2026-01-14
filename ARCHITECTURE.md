# Architecture Overview

This document describes the high-level architecture for the System HMI and controller.

## 1) System overview

### Components
- **ESP32-S3 Controller (Waveshare POE ETH 8DI 8RO)**
  - Real-time-ish I/O control (relays/DI)
  - RS-485 master for downstream controllers (IDs 1,2,3 @ 9600 8N1)
  - BLE Peripheral / GATT Server: publishes the HMI contract
  - Maintains safety policies and interlock gating

- **Android Tablet HMI**
  - BLE Central / GATT Client
  - Touch operator UI (dashboard, I/O, SV, Start/Stop, events)
  - Lease-based heartbeat with deterministic reconnect
  - Operator-facing alarms and diagnostic tools

### Data/control flow
- Tablet ↔ ESP: BLE GATT, framed protocol over a small set of characteristics
- ESP ↔ Controllers: RS-485 polling and write operations (asynchronous scheduling)
- Future: Ethernet reserved for MQTT/camera/remote features (out of v0 scope)

## 2) BLE design

### Why BLE GATT
- Stable service/characteristic contract (UUIDs pinned)
- Efficient Notify stream for telemetry
- Indicate for critical events/acks
- Versioned framing allows feature growth without changing GATT

Reference docs:
- `docs/20-gatt-schema.md`
- `docs/80-gatt-uuids.md`
- `docs/90-command-catalog.md`

## 3) Protocol design

### Framed messages
All data exchanged is framed with:
- proto_ver, msg_type, seq, payload_len, payload, crc16

Benefits:
- Deterministic decoding and validation (CRC)
- Correlate commands to ACKs (seq)
- Allow optional features via new msg_types/cmd_ids
- Maintain backward compatibility with payload_len guards

See:
- `docs/90-command-catalog.md`
- `docs/30-wire-protocol.md`

## 4) Safety & lease (heartbeat)

### Session + lease model
- App must OPEN_SESSION and maintain KEEPALIVE to be considered “HMI LIVE”
- START_RUN is gated on “HMI LIVE” and interlocks
- Mid-run disconnect does not force stop (default policy); logs warning and continues unless faults

See:
- `docs/40-safety-heartbeat-and-policies.md`
- `docs/96-state-machine.md`

## 5) App architecture (framework-agnostic)

The app should be layered:
1. BLE transport layer (scan/connect/discover/subscribe/write)
2. Protocol layer (frames, CRC, command mapping)
3. Domain model (device state, command queue, policies)
4. UI layer (dashboard, I/O, dialogs, events)

State machine is explicit and drives BLE operations (not UI callbacks).

See:
- `docs/50-app-architecture-kotlin.md`
- `docs/51-app-architecture-flutter.md`
- `docs/96-state-machine.md`

## 6) Firmware architecture (ESP)

Firmware responsibilities:
- BLE GATT server
- Protocol parsing and command handling
- Safe output defaults, interlock gating, E-stop handling
- RS-485 poll scheduler and device state cache
- Telemetry emission (10 Hz baseline + change-driven)
- Event emission (critical via Indicate)
- Hardware abstraction (I2C expanders/opto paths hidden behind stable DI/RO bitfields)

See:
- `docs/95-implementation-checklist.md`
- `docs/70-rs485-polling-strategy.md`

## 7) Acceptance gates

v0 acceptance is defined by:
- `docs/99-mvp-scope-v0.md` (demo script)
- `docs/95-implementation-checklist.md` (engineering gates)
- `docs/97-action-behavior-troubleshooting-map.md` (expected behaviors)

## 8) Future expansion (non-v0)
- Bulk Gateway characteristic for Modbus tooling and PID parameter pages
- Ethernet/Wi-Fi for camera streaming and MQTT
- OTA updates (capability-flag gated)
- Role-based operator permissions (if needed)
