# Debugging & Test Plan

## Tools
- nRF Connect (phone/tablet) for BLE validation:
  - service discovery
  - CCCD subscriptions
  - raw characteristic read/write
- Serial logs from ESP (USB/UART)

## Bring-up sequence
1. ESP firmware: advertise service + DeviceInfo read works
2. Telemetry notify: confirm stable 10 Hz + change-driven bursts
3. Commands:
   - SET_RELAY (CH1..CH8)
   - SET_SV for controller #3
4. Acks/events:
   - verify ACK correlation via seq
   - verify Indicate path for critical events
5. Session + lease:
   - OPEN_SESSION
   - KEEPALIVE
   - lease expiry behavior (warning event)
6. App:
   - connect/disconnect loops
   - background/foreground behavior
   - reconnection with resubscribe + session reopen

## Logging requirements
### ESP
- connection events
- subscribe events (CCCD enabled)
- command received (cmd_id, seq, result)
- rs-485 poll schedule + outcomes
- event emission (type, severity)

### App
- permission state
- scan results (filtered)
- connect/discover timings
- subscribe success/failure
- telemetry rate, dropped frames (CRC fail)
- command round-trip time (write → ack)

## Acceptance targets (v0)
- Cold start to LIVE state in < 10 seconds (typical)
- Telemetry stable at 10 Hz with no UI stutter
- Relay toggles feel immediate:
  - UI reflects change within 200 ms typical on a good link
- Reconnect:
  - recovers automatically within 10–20 seconds typical after link loss
  - resubscribes and reopens session deterministically

## Known pitfalls
- Android permission changes (Android 12+ runtime permissions for BLE)
- BLE CCCD not enabled after reconnect unless explicitly re-written
- Device-specific BLE stack quirks → mitigate via deterministic state machine

## Reference
- Android Bluetooth permissions:
  https://developer.android.com/develop/connectivity/bluetooth/bt-permissions
