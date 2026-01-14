# System HMI (Android Tablet ↔ ESP32-S3 over BLE)

Tablet-based HMI for controlling and monitoring the “System” instrument via **Bluetooth Low Energy (BLE)** using a Waveshare ESP32-S3 POE ETH 8DI 8RO controller board.

The ESP32-S3 is the authoritative controller (“device in command”). The tablet provides a near-real-time operational display and issues operator commands (Start/Stop, relay control, setpoints), with a protocol designed for future expansion (PID tuning pages, Modbus tools, event logs).

## Documentation
All design, protocol, safety, UX, hardware, and implementation guidance lives in the docs index:

- **Docs index**: [`docs/README.md`](./docs/README.md)

## MVP hardware (current target)
- **Tablet HMI**: Lenovo Tab Plus (Model **ZADX0091SE**), **11.5" 2000×1200**
- **Controller**: Waveshare ESP32-S3-(POE-)ETH-8DI-8RO (8DI + 8RO + RS-485 + Ethernet reserved)

Hardware references (details live in `docs/`):
- [`docs/05-hardware-overview.md`](./docs/05-hardware-overview.md)
- [`docs/06-hardware-tablet-lenovo-tab-plus.md`](./docs/06-hardware-tablet-lenovo-tab-plus.md)
- [`docs/07-hardware-controller-waveshare-esp32-s3-eth-8di-8ro.md`](./docs/07-hardware-controller-waveshare-esp32-s3-eth-8di-8ro.md)
- [`docs/08-hardware-pid-lc108-stub.md`](./docs/08-hardware-pid-lc108-stub.md)

## Repository layout (proposed)
This repo is expected to evolve into a multi-component system. A typical layout:

- `docs/` — design + protocol + safety + testing documentation
- `firmware/` — ESP32-S3 firmware (BLE GATT server, RS-485 master, DI/RO control)
- `app-android/` — Android HMI app (Kotlin/Compose or Flutter; choose one)
- `tools/` — bring-up tools (scripts, protocol testers, log parsers)
- `hardware/` — wiring diagrams, pin maps, mechanical notes (optional)

## Transport note (important)
ESP32-S3 supports **Bluetooth Low Energy** and does not support Bluetooth Classic (BR/EDR). BLE is the intended control-plane transport for this project.

## Contributing
See [`CONTRIBUTING.md`](./CONTRIBUTING.md).

## Architecture
See [`ARCHITECTURE.md`](./ARCHITECTURE.md).

## License
TBD.
