# Implementation Checklist (ESP Firmware + Android App)

This checklist is intended to be used as an engineering “gate list” for bring-up and iterative development.
Each step has concrete acceptance criteria.

---

## A. Protocol & Contract Gates (do first)

### A1) UUIDs pinned and referenced consistently
- [ ] `docs/80-gatt-uuids.md` contains the final UUIDs (no placeholders)
- [ ] `docs/20-gatt-schema.md` matches pinned UUIDs exactly
- [ ] Firmware uses the same UUIDs; app uses the same UUIDs

**Accept:** nRF Connect shows the service and all expected characteristics with correct properties.

### A2) Framed protocol implementation stubs exist on both sides
- [ ] Frame parser validates: `proto_ver`, `payload_len`, CRC16
- [ ] Frame builder on app side produces the same CRC16 and endianness
- [ ] Unknown `msg_type` / `cmd_id` are handled gracefully (ignored + logged)

**Accept:** A set of synthetic frames can be round-tripped without CRC mismatch.

---

## B. ESP Firmware Bring-Up (BLE-only path)

### B1) Advertising
- [ ] Device advertises with name `SYS-CTRL-<shortid>`
- [ ] Advertising includes the System Control Service UUID (recommended)

**Accept:** App can filter scan results and reliably find the device.

### B2) GATT server availability
- [ ] Service and all characteristics are present:
  - Device Info (Read)
  - Telemetry Stream (Notify)
  - Command RX (Write, Write Without Response)
  - Events + Acks (Indicate, Notify)
  - Optional Diagnostic Log (Notify)
- [ ] Device Info returns a coherent payload (proto_ver, fw version, cap_bits)

**Accept:** nRF Connect can read Device Info, subscribe to Telemetry Stream, and subscribe to Events + Acks.

### B3) Notifications and indications
- [ ] Telemetry Stream sends frames at baseline 10 Hz (100 ms)
- [ ] Telemetry Stream also sends “change-driven” updates immediately
- [ ] Events + Acks supports both Notify and Indicate
- [ ] At least one critical event path uses Indicate (e.g., simulated E-stop)

**Accept:** Client receives stable 10 Hz telemetry without stalls; critical Indicate events arrive reliably.

### B4) Command RX handling + ACK policy
- [ ] Firmware receives COMMAND frames and validates CRC and payload length
- [ ] Firmware responds with COMMAND_ACK:
  - Non-critical: Notify
  - Critical (Start/Stop/Abort, E-stop transitions): Indicate
- [ ] Command correlation uses `acked_seq` and `cmd_id`

**Accept:** A relay toggle command produces a matching ACK and the telemetry reflects the new state.

### B5) Session + Lease (“heartbeat”) policy
- [ ] OPEN_SESSION returns session_id + lease_ms (recommended via Indicate ACK)
- [ ] KEEPALIVE updates last_seen timestamp
- [ ] Lease expiry sets “HMI not live” internal state and emits:
  - EVENT: HMI_DISCONNECTED (WARN)
- [ ] START_RUN is policy-gated: rejected unless lease valid + interlocks OK

**Accept:** START_RUN fails when the app is disconnected or lease expired, and succeeds when lease valid.

### B6) RS-485 integration (3 controllers: IDs 1,2,3 @ 9600 8N1)
- [ ] Poll scheduling is asynchronous; no UI-driven bus bursts
- [ ] Per-controller “age_ms” is tracked and included in telemetry
- [ ] Offline/online transitions emit events
- [ ] Typical test case (only controller #3 present) works without code changes

**Accept:** Telemetry shows controller #3 with fresh PV/SV/OP%; controllers #1/#2 show stale/offline but do not break the UI.

### B7) Hardware abstraction (I2C expanders + opto paths)
- [ ] Firmware exposes DI/RO as stable bitfields independent of hardware path
- [ ] Channel mapping is consistent with labeling:
  - CH1..CH8 = relay_index 1..8
  - DI1..DI8 = input_index 1..8

**Accept:** Toggling CH1 changes ro_bits bit0; DI1 changes di_bits bit0.

---

## C. Android App Bring-Up (choose Kotlin or Flutter)

### C1) Permissions gate (Android 12+)
- [ ] App requests required runtime permissions for scanning/connecting
- [ ] User denial is handled gracefully (no crash, clear instruction)

**Accept:** Fresh install → user can grant permissions → scan results appear.

### C2) Scan and filter
- [ ] Scan lists only devices matching:
  - name prefix `SYS-CTRL-`
  - OR advertised service UUID (preferred)
- [ ] UI shows RSSI and device ID (shortid)

**Accept:** Device appears reliably within a few seconds.

### C3) Connect & discover
- [ ] Connect initiates GATT connection
- [ ] Services and characteristics discovered
- [ ] App verifies presence of required UUIDs (and fails with a clear message if missing)

**Accept:** App reaches a “DISCOVERED” state and lists characteristic availability.

### C4) Subscribe to streams
- [ ] Enable Telemetry Stream Notify (write CCCD)
- [ ] Enable Events + Acks Indicate (and Notify if you use both)
- [ ] Confirm subscription success via internal state/log

**Accept:** Telemetry frames arrive; events/acks arrive.

### C5) OPEN_SESSION + KEEPALIVE loop
- [ ] App sends OPEN_SESSION, validates ACK, stores `session_id` and `lease_ms`
- [ ] App starts KEEPALIVE every 1 second
- [ ] App detects lease failure conditions (no ACKs/events, disconnect) and updates UI state

**Accept:** App shows “LIVE” only when session is open and keepalive is active.

### C6) Command queue + ACK correlation
- [ ] App increments `seq` for each command
- [ ] App tracks outstanding commands by seq
- [ ] App matches COMMAND_ACK `acked_seq` to update UI and clear outstanding commands
- [ ] Retries:
  - For non-critical commands: retry on timeout (bounded retries)
  - For critical commands: require Indicate ACK or show failure clearly

**Accept:** Relay toggles feel immediate and are confirmed via ACK; failures produce clear errors.

### C7) Reconnect state machine
- [ ] Deterministic state machine:
  - DISCONNECTED → SCANNING → CONNECTING → DISCOVERING → SUBSCRIBED → SESSION_OPEN → LIVE
- [ ] On disconnect:
  - cancel keepalive
  - clear subscriptions state
  - attempt reconnect with backoff
- [ ] On reconnect:
  - rediscover (or validate cached handles)
  - re-enable CCCD
  - re-open session

**Accept:** Turning BLE off/on or walking away and back causes recovery to LIVE without restarting the app.

---

## D. Performance Targets (pragmatic v0)

### D1) Telemetry cadence
- [ ] Baseline 10 Hz telemetry stable
- [ ] Change-driven update arrives within ~100–200 ms typical

**Accept:** UI appears “responsive” without excessive bus/CPU usage.

### D2) Command responsiveness
- [ ] Relay toggle: operator action → UI confirmation within ~200 ms typical on good link
- [ ] START/STOP: ACK via Indicate and reflected in telemetry promptly

**Accept:** Controls feel immediate; critical commands are reliably acknowledged.

---

## E. Safety Policy Gates

### E1) Start gating
- [ ] START_RUN rejected unless:
  - session lease valid
  - no critical alarms
  - interlocks satisfied

**Accept:** Cannot start without “HMI live.”

### E2) Mid-run disconnect behavior
- [ ] If HMI disconnects mid-run:
  - run continues (default policy)
  - event logged + warning alarm shown
- [ ] Reconnect restores LIVE and allows operator interaction again

**Accept:** No nuisance aborts on transient tablet disconnects.

### E3) E-stop behavior
- [ ] Physical E-stop remains authoritative (outside BLE)
- [ ] E-stop events emitted to app via Indicate (must-land)
- [ ] Outputs return to safe state per firmware policy

**Accept:** E-stop always works regardless of BLE/app state; app reliably shows E-stop state.

---

## F. Tooling & Diagnostics

### F1) Field-debug logging
- [ ] ESP logs connection/subscription/command outcomes
- [ ] App logs scan/connect/discover/subscribe/command RTT/CRC failures
- [ ] Optional Diagnostic Log characteristic enabled during bring-up

**Accept:** When something fails, logs clearly indicate which gate broke and why.

---

## Appendix: Suggested v0 acceptance demo
1) Fresh boot ESP + open app → reaches LIVE  
2) Toggle CH1..CH8 and verify telemetry + physical outputs  
3) Display DI1..DI8 changes  
4) Set SV on controller #3, verify RS-485 action + reflected PV/SV  
5) START_RUN requires LIVE; then disconnect tablet mid-run → run continues; reconnect restores control  
6) Simulate E-stop event → app receives critical Indicate event and UI shows CRITICAL state
