x# Action → Expected Behavior → Troubleshooting Map

This document is a “behavior contract” plus a fast escalation guide.
Use it during bring-up, QA, and field debugging.

References:
- GATT schema: `docs/20-gatt-schema.md`
- UUIDs: `docs/80-gatt-uuids.md`
- Protocol & commands: `docs/90-command-catalog.md`
- Safety + lease: `docs/40-safety-heartbeat-and-policies.md`
- Checklist gates: `docs/95-implementation-checklist.md`
- State machines: `docs/96-state-machine.md`
- Debug plan: `docs/60-debugging-and-test-plan.md`

---

## 1) Connect / Subscribe / Session

### Action: Launch app (fresh install)
**Expected behavior**
- App requests BLE permissions (Android 12+).
- After grant, transitions to SCANNING.
See: `docs/96-state-machine.md` (PERMISSION_REQUIRED → SCANNING)

**If error**
- Permissions dialog never appears / scan empty:
  - See: `docs/60-debugging-and-test-plan.md` (Permissions pitfalls)
  - See Android permissions reference in `docs/20-gatt-schema.md`

---

### Action: Scan for device
**Expected behavior**
- Device appears as `SYS-CTRL-<shortid>`, ideally filtered by advertised service UUID.
- RSSI visible and stable.

**If error**
- Device not found:
  - Confirm ESP is advertising: `docs/95-implementation-checklist.md` (B1)
  - Validate with nRF Connect: `docs/60-debugging-and-test-plan.md`
  - Confirm name/service UUID: `docs/80-gatt-uuids.md`, `docs/20-gatt-schema.md`

---

### Action: Connect to device
**Expected behavior**
- App transitions CONNECTING → DISCOVERING.
- GATT connects and service discovery succeeds.

**If error**
- Connect hangs or disconnects immediately:
  - See state machine + backoff guidance: `docs/96-state-machine.md`
  - Verify ESP logs connection events: `docs/60-debugging-and-test-plan.md` (Logging)
  - Validate with nRF Connect (is the device connectable?)

---

### Action: Subscribe to Telemetry (Notify) and Events/Acks (Indicate)
**Expected behavior**
- CCCD writes succeed.
- App begins receiving:
  - TELEMETRY_SNAPSHOT at ~10 Hz
  - Events/Acks stream (idle until events/acks happen)

**If error**
- Telemetry never arrives:
  - Confirm CCCD enabled on `...5E62`: `docs/20-gatt-schema.md`, `docs/80-gatt-uuids.md`
  - Confirm ESP is sending telemetry frames: `docs/95-implementation-checklist.md` (B3)
  - Check app logs for subscribe success: `docs/60-debugging-and-test-plan.md`

- Events/Indications never arrive:
  - Confirm CCCD enabled on `...5E64` for Indicate
  - Confirm at least one test Indicate event emitted (e-stop simulation)
  - See: `docs/95-implementation-checklist.md` (B3)

---

### Action: OPEN_SESSION
**Expected behavior**
- App sends COMMAND `OPEN_SESSION (0x0100)`.
- ESP replies with COMMAND_ACK (Indicate recommended) including:
  - session_id
  - lease_ms
- App transitions to LIVE and starts KEEPALIVE 1 Hz.

**If error**
- ACK not received:
  - Verify app is subscribed to Events/Acks: `docs/96-state-machine.md`
  - Validate command frame CRC/endianness: `docs/90-command-catalog.md` (CRC rules)
  - Use nRF Connect to write OPEN_SESSION and observe ack (bring-up technique)

- ACK rejected (REJECTED_POLICY):
  - Confirm policy requirements: `docs/40-safety-heartbeat-and-policies.md`

---

### Action: KEEPALIVE
**Expected behavior**
- App sends KEEPALIVE every 1s.
- ESP maintains HMI_LIVE and does not emit HMI_DISCONNECTED.

**If error**
- Lease expires while connected:
  - Confirm keepalive loop not paused in background: `docs/60-debugging-and-test-plan.md`
  - Confirm session_id matches: `docs/90-command-catalog.md`
  - Confirm lease_ms reasonable (e.g., 3000 ms) and app interval is < lease_ms/2

---

## 2) Normal Operations (I/O, PID, run control)

### Action: Toggle relay CHn (SET_RELAY)
**Expected behavior**
- App writes COMMAND SET_RELAY (0x0001).
- ESP:
  - updates output
  - emits COMMAND_ACK (Notify acceptable)
  - telemetry reflects new ro_bits within 100–200 ms typical

**If error**
- Physical relay changes but UI doesn’t:
  - Telemetry subscription or parser issue:
    - `docs/95-implementation-checklist.md` (C4, C6)
    - `docs/90-command-catalog.md` (bit mapping)

- UI shows change but relay does not:
  - Firmware hardware abstraction path issue (I2C expander/opto):
    - `docs/95-implementation-checklist.md` (B7)

- ACK missing / command times out:
  - Confirm Events/Acks subscription: `docs/20-gatt-schema.md`
  - Verify CRC and seq correlation: `docs/90-command-catalog.md`

---

### Action: Set SV on controller #3 (SET_SV)
**Expected behavior**
- App writes SET_SV with controller_id=3.
- ESP performs RS-485 write to slave ID 3.
- ACK OK or TIMEOUT_DOWNSTREAM (if not present).
- Telemetry updates SV for controller 3 and age_ms resets.

**If error**
- TIMEOUT_DOWNSTREAM:
  - Controller not present or wiring/RS-485 issue:
    - `docs/70-rs485-polling-strategy.md`
    - `docs/60-debugging-and-test-plan.md` (RS-485 logging)

- SV changes but immediately reverts:
  - Device-side policy or controller mode issue:
    - confirm controller mode and register behavior
    - check downstream alarm state and write permissions

---

### Action: START_RUN
**Expected behavior**
- App must be LIVE (lease valid).
- ESP ACKs START_RUN via Indicate.
- Telemetry and EVENT show run state transition.

**If error**
- REJECTED_POLICY:
  - See: `docs/40-safety-heartbeat-and-policies.md` (start gating)
  - Common causes: HMI not live, interlock open, estop active.

---

### Action: STOP_RUN / ABORT
**Expected behavior**
- STOP/ABORT is always allowed when connected (policy choice; recommend allow even if lease near expiry).
- ACK via Indicate.
- Outputs transition to safe state per run policy.

**If error**
- No ACK:
  - Treat as critical:
    - verify Indicate subscription and link health
    - show UI “stop not confirmed” and keep retrying (bounded)

---

## 3) Faults / Alarms / E-stop

### Action: E-stop asserted
**Expected behavior**
- Outputs go safe immediately (independent of BLE).
- ESP emits EVENT ESTOP_ASSERTED via Indicate.
- App displays CRITICAL banner and inhibits start.

**If error**
- App doesn’t show estop:
  - confirm Indicate subscription and event decoding:
    - `docs/20-gatt-schema.md`, `docs/90-command-catalog.md`
  - confirm firmware emits the event:
    - `docs/95-implementation-checklist.md` (E3)

---

### Action: Tablet disconnect mid-run
**Expected behavior**
- Run continues (default policy).
- ESP logs and emits HMI_DISCONNECTED warning event.
- On reconnect, session is re-opened and control resumes.

**If error**
- Run stops unexpectedly:
  - check firmware policy implementation:
    - `docs/40-safety-heartbeat-and-policies.md`
  - verify “disconnect” not interpreted as fault

---

## 4) Data quality / performance

### Action: Observe telemetry for 60 seconds
**Expected behavior**
- Stable ~10 Hz baseline
- Minimal CRC failures
- UI remains smooth

**If error**
- Stutters / drops:
  - check MTU/packet sizing and notify rate
  - check that RS-485 polling does not block BLE task
  - see: `docs/60-debugging-and-test-plan.md`, `docs/70-rs485-polling-strategy.md`

---

## 5) “Where do I look?” quick index

- BLE discovery/subscription issues: `docs/20-gatt-schema.md`, `docs/80-gatt-uuids.md`, `docs/60-debugging-and-test-plan.md`
- Frame/CRC/seq mismatch: `docs/90-command-catalog.md`
- Start/stop/heartbeat policies: `docs/40-safety-heartbeat-and-policies.md`
- Reconnect and deterministic behavior: `docs/96-state-machine.md`
- RS-485 freshness/staleness: `docs/70-rs485-polling-strategy.md`
- Acceptance gates: `docs/95-implementation-checklist.md`
