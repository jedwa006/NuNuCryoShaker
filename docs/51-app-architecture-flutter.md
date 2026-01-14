# Flutter App Architecture

## Recommended approach
Flutter can deliver fast UI iteration, but BLE stability depends on library quality and Android device variance.

Two common BLE library options:
- flutter_reactive_ble (often chosen for more explicit connection patterns)
- flutter_blue_plus (widely used; central role)

Choose one and standardize early.

## Module layout (suggested)
- `lib/ui/` screens/widgets
- `lib/state/` state mgmt (Riverpod/BLoC/etc.)
- `lib/ble/` BLE client wrapper (scan/connect/discover/subscribe/write)
- `lib/protocol/` frame encode/decode + CRC
- `lib/domain/` device model + command queue + policies
- `lib/logging/` structured logs + export

## Core workflow (same conceptual steps as Kotlin)
1. Request permissions
2. Scan filter: `SYS-CTRL-*`
3. Connect
4. Discover services/characteristics
5. Subscribe to:
   - Telemetry notify stream
   - Events/acks indicate stream
6. OPEN_SESSION
7. Keepalive loop (1 Hz)
8. Commands → ACK correlation via seq

## Reconnect strategy
Implement a deterministic state machine and avoid “UI-driven BLE calls.”
BLE operations should be driven by your `BleRepository` and exposed as streams.

## UI (tablet)
- Same page set as Kotlin; Flutter can implement a consistent layout across devices.

## Reference starting points
- Android BLE permissions:
  https://developer.android.com/develop/connectivity/bluetooth/bt-permissions
