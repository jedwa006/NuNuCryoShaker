# Transport Decision: BLE vs Bluetooth Classic (BR/EDR)

## Executive summary
We will use **BLE (GATT)** for the tablet ↔ ESP link.

**Bluetooth Classic is not supported on ESP32-S3**, so BR/EDR options (e.g., SPP “serial over Bluetooth Classic”) are not available on this platform.

## Why BLE is a good fit for this HMI
BLE is well-suited to:
- Small, structured telemetry updates (Notify)
- Discrete commands with acknowledgements (Write + Notify/Indicate)
- Proximity/local control (tablet in the same room)

We will design for:
- Baseline periodic telemetry (e.g., 10 Hz)
- Event-driven updates (send immediately on state changes)
- Critical events via Indicate (link-layer acknowledged)

## BLE limitations / mitigations
### 1) BLE is not a good fit for video streaming
Mitigation: plan camera streaming over Ethernet/Wi-Fi later; keep BLE for control.

### 2) BLE reliability varies across Android devices
Mitigation:
- Implement a robust connection state machine
- Use a versioned wire protocol with sequence numbers + CRC
- Use session/lease heartbeat for operator gating policies
- Provide logs + test tooling (nRF Connect validation, etc.)

## References
- Espressif ESP-IDF Bluetooth Overview for ESP32-S3 (LE-only; Classic not supported):
  https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/bluetooth.html
