# State Machines (App + ESP)

This document defines the expected state machines for:
- Android App (BLE Central / GATT Client)
- ESP32-S3 Firmware (BLE Peripheral / GATT Server)

These state machines exist to make reconnection and error handling deterministic.

Related docs:
- GATT Schema: `docs/20-gatt-schema.md`
- Protocol & commands: `docs/90-command-catalog.md`
- Safety + heartbeat policies: `docs/40-safety-heartbeat-and-policies.md`
- Implementation gates: `docs/95-implementation-checklist.md`

---

## 1) Android App State Machine (Central / Client)

### State enum (recommended)
- `DISCONNECTED`
- `PERMISSION_REQUIRED`
- `SCANNING`
- `DEVICE_SELECTED`
- `CONNECTING`
- `DISCOVERING`
- `SUBSCRIBING`
- `SESSION_OPENING`
- `LIVE`
- `DEGRADED` (connected but missing one or more streams / stale telemetry)
- `ERROR` (terminal until user action, e.g., incompatible protocol)

### Events that trigger transitions
- Permissions granted/denied
- Scan results present
- User selects device
- GATT connected/disconnected
- Services discovered / discovery failed
- CCCD subscribe success/failure
- OPEN_SESSION ACK received / rejected
- Telemetry received / telemetry timeout
- KEEPALIVE success / lease expired
- Protocol version incompatible

### Transition table (high-level)

#### DISCONNECTED → PERMISSION_REQUIRED
Condition: App starts and permissions not granted.

#### PERMISSION_REQUIRED → SCANNING
Condition: Permissions granted.

#### SCANNING → DEVICE_SELECTED
Condition: Filtered device(s) found and user selects one (or auto-select best RSSI).

#### DEVICE_SELECTED → CONNECTING
Action: Initiate GATT connect.

#### CONNECTING → DISCOVERING
Condition: GATT connected callback arrives.
Action: Start service discovery.

#### DISCOVERING → SUBSCRIBING
Condition: Required UUIDs found:
- Service: `...5E60`
- Device Info: `...5E61`
- Telemetry: `...5E62`
- Command RX: `...5E63`
- Events/Acks: `...5E64`

Action:
- Read Device Info
- Negotiate MTU (optional but recommended)
- Enable CCCD:
  - Telemetry Notify
  - Events/Acks Indicate (+ Notify if used)
  - Diagnostic Log Notify (optional)

#### SUBSCRIBING → SESSION_OPENING
Condition: Subscriptions enabled.
Action: Send `OPEN_SESSION`.

#### SESSION_OPENING → LIVE
Condition: Receive `COMMAND_ACK` for OPEN_SESSION (OK) containing:
- `session_id`
- `lease_ms`

Action:
- Start KEEPALIVE timer (1 Hz)
- Mark “HMI Live”

#### LIVE → DEGRADED
Condition (any):
- Telemetry not received for > 500 ms (configurable)
- Events stream not subscribed/working
- Too many CRC failures (threshold)
- Controller readings stale (age_ms exceeds limit)

Action:
- Continue attempting to recover streams (resubscribe, request snapshot)
- Keep UI usable, but highlight degraded state

#### DEGRADED → LIVE
Condition: Streams restored and telemetry healthy.

#### Any State → DISCONNECTED
Condition: GATT disconnected.
Action:
- Cancel keepalive
- Clear subscription flags
- Backoff and return to SCANNING (unless user requested manual connect)

#### Any State → ERROR
Condition:
- Protocol incompatible (proto_ver mismatch)
- Required characteristics missing
- Persistent subscribe failure beyond retry threshold

Action:
- Show actionable error and reference docs (see `docs/97-action-behavior-troubleshooting-map.md`).

### Backoff strategy (recommended)
On disconnect:
- Attempt reconnect after 1s, 2s, 4s, 8s… capped at 30s
- Reset backoff on a successful LIVE session

---

## 2) ESP Firmware State Machine (Peripheral / Server)

### State enum (recommended)
- `BOOTING`
- `ADVERTISING`
- `CONNECTED`
- `SUBSCRIBED` (CCCD enabled for required streams)
- `SESSION_ACTIVE`
- `HMI_LIVE` (lease valid)
- `HMI_STALE` (lease expired while still connected or disconnected)
- `FAULT` (hardware or safety fault / latched alarm)

### Key internal flags
- `cccd_telemetry_enabled`
- `cccd_events_indicate_enabled` / `cccd_events_notify_enabled`
- `session_id_valid`
- `last_keepalive_ms`
- `lease_ms`
- `run_state` (IDLE/RUNNING/etc.)
- `interlocks_ok`
- `estop_active`

### Expected transitions

#### BOOTING → ADVERTISING
Action:
- Initialize BLE stack and GATT server
- Begin advertising with `SYS-CTRL-<shortid>`

#### ADVERTISING → CONNECTED
Condition: Central connects.

#### CONNECTED → SUBSCRIBED
Condition: CCCD enabled for:
- Telemetry Notify
- Events/Acks Indicate (recommended)
Note: do not require telemetry subscription to accept OPEN_SESSION, but require Events/Acks Indicate for full “LIVE” semantics.

#### SUBSCRIBED → SESSION_ACTIVE
Condition: Receive OPEN_SESSION command (valid frame).
Action:
- Issue session_id + lease_ms in COMMAND_ACK (Indicate recommended)

#### SESSION_ACTIVE → HMI_LIVE
Condition: Receive KEEPALIVE within lease window.
Action:
- Mark “HMI live” (start permitted if other conditions OK)
- Emit EVENT: HMI_CONNECTED (Notify)

#### HMI_LIVE → HMI_STALE
Condition: Lease expiry (`now - last_keepalive_ms > lease_ms`)
Action:
- Mark “HMI not live”
- Emit EVENT: HMI_DISCONNECTED (Notify)
- Enforce policy:
  - START_RUN rejected when stale
  - If already RUNNING, continue unless other faults

#### Any → ADVERTISING
Condition: Central disconnects.
Action:
- Clear CCCD flags and session state (policy: may invalidate session_id)
- Advertise again
- Mark HMI_STALE until new session

#### Any → FAULT
Condition: E-stop active or critical interlock fault.
Action:
- Put outputs into safe state per system policy
- Emit EVENT critical transitions via Indicate (if connected)

---

## 3) Notes on determinism
- App must always:
  - Rediscover or validate characteristics after reconnect
  - Re-enable CCCD after reconnect
  - Re-open session and restart keepalive after reconnect
- ESP must:
  - Treat session state as scoped to the current connection (recommended)
  - Not assume CCCD remains enabled across connections
