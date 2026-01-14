# Kotlin (Native Android) App Architecture

## Recommended stack
- Kotlin + Coroutines + Flow
- Jetpack Compose (tablet UI)
- BLE using Android platform APIs (Bluetooth LE GATT)

## Module layout (suggested)
- `app/` UI + navigation
- `ble/` low-level BLE (scan/connect/discover/subscribe/write)
- `protocol/` frame encode/decode, CRC, message routing
- `domain/` device state model, command queue, policies
- `logging/` structured logs + export

## Core workflow
1. Request permissions (Android 12+ “Nearby devices”)
2. Scan for `SYS-CTRL-*`
3. Connect GATT
4. Discover services
5. Subscribe:
   - Telemetry (Notify)
   - Events/Acks (Indicate + optional Notify)
6. OPEN_SESSION → receive session_id
7. Start UI updates from telemetry stream
8. Keepalive loop (1 Hz)
9. Command dispatch:
   - Write COMMAND frames
   - Wait for ACK frames (notify/indicate)
   - Update UI state + error banners

## Reconnect strategy
- State machine:
  - DISCONNECTED → SCANNING → CONNECTING → DISCOVERING → SUBSCRIBED → LIVE
- On disconnect:
  - cancel keepalive
  - return to SCANNING with backoff
- On reconnect:
  - re-open session
  - resubscribe CCCD
  - resync state (request a snapshot if needed)

## UI (tablet)
- Dashboard:
  - system status, run state, alarm banner
  - 3 controller panels (IDs 1,2,3)
- I/O page:
  - CH1–CH8 relays (toggle + pending/confirmed state)
  - DI1–DI8 indicators
- Setpoint popups:
  - numeric entry + validation + apply

## References
- Android Bluetooth permissions:
  https://developer.android.com/develop/connectivity/bluetooth/bt-permissions
    