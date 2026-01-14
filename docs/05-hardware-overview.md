# Hardware Overview (System HMI + Controller)

This document defines the physical “System” components for the MVP and how they connect. It is intentionally scoped to:
- Tablet HMI device (Android)
- Main controller (Waveshare ESP32-S3-ETH-8DI-8RO / POE-variant)
- RS-485 field devices (LC108 PID controllers; details stubbed)
- Power/relay I/O and digital inputs (as exposed by the controller hardware)

If you want “how we talk over BLE,” see:
- `docs/20-gatt-schema.md`
- `docs/30-wire-protocol.md`

---

## 1. System block diagram (MVP)

```text
[Lenovo Android Tablet]
   |
   |  BLE (GATT): status/controls/ack/notify
   v
[ESP32-S3 Controller: Waveshare ETH-8DI-8RO]
   | \
   |  \  (future) Ethernet: MQTT / OTA / camera stream / remote UI
   |
   +-- RS-485 (Modbus RTU master) --> [LC108 PID #1 addr=1]
   |                                --> [LC108 PID #2 addr=2]
   |                                --> [LC108 PID #3 addr=3]
   |
   +-- 8 Relay Outputs (isolated)
   +-- 8 Digital Inputs (isolated)
