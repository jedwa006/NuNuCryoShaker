# Docs Index — System HMI (Android Tablet ↔ ESP32-S3 over BLE)

This folder contains the transport, protocol, safety, UX, and implementation documentation for the “System” instrument HMI.

## Start here (recommended reading order)
1. [Scope & Goals](./00-scope-and-goals.md)
2. [MVP Scope (v0)](./99-mvp-scope-v0.md)
3. [Transport Decision: BLE vs Classic (ESP32-S3 = BLE only)](./10-transport-choice-ble-vs-classic.md)
4. [BLE GATT Schema (Pinned)](./20-gatt-schema.md)
5. [Pinned GATT UUIDs](./80-gatt-uuids.md)
6. [Wire Protocol (Framing)](./30-wire-protocol.md)
7. [Command Catalog & Protocol Reference](./90-command-catalog.md)
8. [Safety + Heartbeat (Lease) Policies](./40-safety-heartbeat-and-policies.md)

## Hardware (read next)
- [05-hardware-overview.md](05-hardware-overview.md)
- [06-hardware-tablet-lenovo-tab-plus.md](06-hardware-tablet-lenovo-tab-plus.md)
- [07-hardware-controller-waveshare-esp32-s3-eth-8di-8ro.md](07-hardware-controller-waveshare-esp32-s3-eth-8di-8ro.md)
- [08-hardware-pid-lc108-stub.md](08-hardware-pid-lc108-stub.md)

## Implementation guidance
- [Implementation Checklist (ESP + App gates)](./95-implementation-checklist.md)
- [State Machines (App + ESP)](./96-state-machine.md)
- [Action → Expected Behavior → Troubleshooting Map](./97-action-behavior-troubleshooting-map.md)
- [Operator UX Requirements (Tablet HMI)](./98-operator-ux-requirements.md)
- [Debug & Diagnostics Access (App + Firmware)](./63-debug-and-diagnostics-access.md)
- [Debugging & Test Plan](./60-debugging-and-test-plan.md)
- [RS-485 Polling Strategy (3 controllers @ 9600 8N1)](./70-rs485-polling-strategy.md)

## App implementation paths
- [Kotlin (Native Android) App Architecture](./50-app-architecture-kotlin.md)
- [Flutter App Architecture](./51-app-architecture-flutter.md)
- [Flutter vs Kotlin — Decision Guide](./55-flutter-vs-kotlin-decision.md)

## Notes
- UUIDs are pinned in [`80-gatt-uuids.md`](./80-gatt-uuids.md). Do not change them after publication.
- Backward compatibility is maintained via the framed protocol and `proto_ver`, not by changing UUIDs.
