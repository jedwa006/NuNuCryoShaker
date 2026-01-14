# System HMI (Android Tablet ↔ ESP32-S3 over BLE)

Tablet-based HMI for controlling and monitoring the “System” instrument via **Bluetooth Low Energy (BLE)** using a Waveshare ESP32-S3 POE ETH 8DI 8RO controller board.

The ESP32-S3 is the authoritative controller (“device in command”). The tablet provides a near-real-time operational display and issues operator commands (Start/Stop, relay control, setpoints), with a protocol designed for future expansion (PID tuning pages, Modbus tools, event logs).

## Documentation
All design, protocol, safety, UX, and implementation guidance lives in the docs index:

- **Docs index**: [`docs/README.md`](./docs/README.md)

Start with:
- **MVP scope (v0)**: [`docs/99-mvp-scope-v0.md`](./docs/99-mvp-scope-v0.md)
- **GATT schema (pinned)**: [`docs/20-gatt-schema.md`](./docs/20-gatt-schema.md)
- **Protocol + commands**: [`docs/90-command-catalog.md`](./docs/90-command-catalog.md)
- **Implementation checklist**: [`docs/95-implementation-checklist.md`](./docs/95-implementation-checklist.md)

## Repository layout (proposed)
This repo is expected to evolve into a multi-component system. A typical layout:

- `docs/` — design + protocol + safety + testing documentation
- `firmware/` — ESP32-S3 firmware (BLE GATT server, RS-485 master, DI/RO control)
- `app-android/` — Android HMI app (Kotlin/Compose or Flutter; choose one)
- `tools/` — bring-up tools (scripts, protocol testers, log parsers)
- `hardware/` — board notes, wiring diagrams, pin maps (optional)

## Transport note (important)
ESP32-S3 supports **Bluetooth LE** and does **not** support Bluetooth Classic (BR/EDR). BLE is the intended control-plane transport for this project.

Reference: https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/bluetooth.html

## Contributing
See [`CONTRIBUTING.md`](./CONTRIBUTING.md).

## Architecture
See [`ARCHITECTURE.md`](./ARCHITECTURE.md).

## License
TBD.
