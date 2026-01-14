# Bring-Up Order & Execution Playbook

This document defines the most efficient bring-up order for the System HMI project and provides a step-by-step execution plan with gates, expected logs, and rollback points.

Recommended order: **Firmware (BLE + protocol) → App → Firmware hardware backends → End-to-end vertical slice**.

Related docs:
- GATT schema: `docs/20-gatt-schema.md`
- UUIDs: `docs/80-gatt-uuids.md`
- Protocol catalog: `docs/90-command-catalog.md`
- Safety + lease: `docs/40-safety-heartbeat-and-policies.md`
- Implementation checklist: `docs/95-implementation-checklist.md`
- State machines: `docs/96-state-machine.md`
- Action/behavior map: `docs/97-action-behavior-troubleshooting-map.md`
- Debug plan: `docs/60-debugging-and-test-plan.md`
- MVP scope: `docs/99-mvp-scope-v0.md`

---

## Phase 0 — One-time setup (repo + constants)

### Goals
- Ensure docs and constants are the single source of truth.
- Prevent churn caused by mismatched UUIDs or protocol drift.

### Steps
- [ ] Pin UUIDs in firmware from `docs/80-gatt-uuids.md`
- [ ] Create a shared constants file in firmware for:
  - service UUID + characteristic UUIDs
  - `proto_ver`
  - `msg_type` constants
  - `cmd_id` constants
  - `event_id` constants
- [ ] Create the same constant set in the app codebase.

### Accept
- nRF Connect shows correct UUIDs and properties match `docs/20-gatt-schema.md`.

### If error
See:
- `docs/80-gatt-uuids.md`
- `docs/20-gatt-schema.md`

---

## Phase 1 — Firmware bring-up: BLE + protocol skeleton (no real hardware yet)

### Goals
- Stabilize BLE + framing + session/lease + telemetry + acks/events with minimal variables.

### Implementation boundaries
In this phase, DI/RO and controller values can be **simulated**.
Do not integrate I2C expanders or RS-485 yet.

### Steps (in order)
1) **Advertising**
- [ ] Advertise `SYS-CTRL-<shortid>`
- [ ] Include service UUID if feasible

**Accept:** device is discoverable within 3–5 seconds typical.

2) **GATT server**
- [ ] Expose characteristics per `docs/20-gatt-schema.md`
- [ ] Implement Device Info read payload

**Accept:** nRF Connect can read Device Info.

3) **Telemetry stream**
- [ ] Emit TELEMETRY_SNAPSHOT at 10 Hz baseline
- [ ] Emit change-driven snapshots when internal “sim state” changes

**Accept:** stable notify stream for 60s, no stalls.

4) **Command RX + ACK policy**
- [ ] Parse COMMAND frames; validate CRC, length, and payload structure
- [ ] Implement ACK generation:
  - non-critical: Notify on Events + Acks
  - critical: Indicate on Events + Acks

**Accept:** command → ACK correlation via `acked_seq` works reliably.

5) **Session + lease**
- [ ] OPEN_SESSION returns session_id + lease_ms (Indicate recommended)
- [ ] KEEPALIVE updates last_seen
- [ ] Lease expiry:
  - sets HMI not live state
  - emits HMI_DISCONNECTED event

**Accept:** START_RUN rejects when lease invalid and succeeds when valid (simulated run state).

6) **Critical event path**
- [ ] Provide a test trigger (software flag) to emit ESTOP_ASSERTED via Indicate

**Accept:** nRF Connect receives the Indicate event.

### Expected firmware logs (examples)
- `BLE: advertising started name=SYS-CTRL-xxxx`
- `BLE: connected`
- `GATT: CCCD enabled telemetry notify`
- `GATT: CCCD enabled events indicate`
- `PROTO: rx cmd seq=..., cmd_id=OPEN_SESSION crc=OK`
- `PROTO: tx ack seq=... status=OK via=INDICATE session_id=... lease_ms=...`
- `PROTO: rx keepalive session_id=...`
- `LEASE: expired -> HMI_STALE`
- `EVENT: HMI_DISCONNECTED`

### If error
See:
- `docs/97-action-behavior-troubleshooting-map.md` (Connect/Subscribe/Session)
- `docs/90-command-catalog.md` (CRC/frames)
- `docs/60-debugging-and-test-plan.md`

---

## Phase 2 — App bring-up against simulated firmware

### Goals
- Make the app deterministic and stable before introducing hardware variability.

### Steps (in order)
1) **Permissions**
- [ ] Android runtime BLE permissions handled (Android 12+)
**Accept:** scan works on a fresh install.

2) **Scan filter**
- [ ] Filter by name prefix and/or service UUID
**Accept:** device appears reliably.

3) **Connect / discover**
- [ ] Connect and discover required UUIDs
- [ ] Fail fast with clear error if UUID missing
**Accept:** transition to DISCOVERING then SUBSCRIBING.

4) **Subscribe CCCD**
- [ ] Enable Telemetry Notify
- [ ] Enable Events/Acks Indicate (and Notify if used)
**Accept:** telemetry frames arrive and parse cleanly.

5) **OPEN_SESSION / KEEPALIVE**
- [ ] Send OPEN_SESSION and store session_id + lease_ms
- [ ] Start keepalive loop (1 Hz)
**Accept:** app reaches LIVE state and stays LIVE for > 2 minutes.

6) **Command queue**
- [ ] Send SET_RELAY (simulated), correlate ACKs, show pending/confirmed UI behavior
- [ ] Send START_RUN/STOP_RUN with Indicate ACK handling
**Accept:** pending states clear reliably and failures are visible.

7) **Reconnect**
- [ ] Force disconnect (toggle BLE off/on or walk away)
- [ ] App returns to LIVE with resubscribe + session reopen
**Accept:** no restart required.

### Expected app logs (examples)
- `PERM: granted`
- `SCAN: found SYS-CTRL-xxxx rssi=-55`
- `GATT: connected`
- `GATT: services discovered`
- `CCCD: telemetry notify enabled`
- `CCCD: events indicate enabled`
- `PROTO: rx telemetry ok rate=10Hz`
- `PROTO: tx OPEN_SESSION seq=...`
- `PROTO: rx ACK OPEN_SESSION status=OK lease_ms=3000`
- `KEEPALIVE: sent`
- `RECONNECT: backoff=2s`

### If error
See:
- `docs/96-state-machine.md`
- `docs/97-action-behavior-troubleshooting-map.md`

---

## Phase 3 — Firmware bring-up: hardware backends (keep BLE contract unchanged)

### Goals
- Replace simulated values with real I/O and RS-485 data without changing GATT or the protocol.

### Steps
1) **DI/RO integration**
- [ ] Implement CH1..CH8 output driver (Waveshare path)
- [ ] Implement DI1..DI8 reads
- [ ] Map to bitfields exactly as specified:
  - bit0 = channel 1, …, bit7 = channel 8

**Accept:** app toggles CHn and sees ro_bits update and physical output changes.

2) **RS-485 poll scheduler**
- [ ] Implement asynchronous polling for controllers 1..3
- [ ] Track `age_ms` per controller and include in telemetry
- [ ] Emit RS485_DEVICE_ONLINE/OFFLINE events

**Accept:** with only controller #3 present, telemetry shows #3 fresh and others stale/offline.

3) **Policy wiring**
- [ ] E-stop input integrated (physical)
- [ ] Interlocks integrated (if available in v0)
- [ ] START_RUN gating uses HMI live + interlocks + estop state

**Accept:** physical E-stop produces CRITICAL Indicate event and inhibits Start.

### If error
See:
- `docs/70-rs485-polling-strategy.md`
- `docs/98-operator-ux-requirements.md` (staleness rules)
- `docs/95-implementation-checklist.md` (B6/B7/E3)

---

## Phase 4 — End-to-end vertical slice (minimum demo)

### Goal
Prove the entire stack: app → firmware → hardware with the smallest meaningful feature set.

### Script
1) Launch app → reach LIVE  
2) Toggle **CH1**:
- physical output changes
- ro_bits updates
- ACK received

3) Set SV on controller #3:
- RS-485 write occurs
- telemetry reflects SV

4) START_RUN then STOP_RUN:
- Start requires LIVE
- ACK via Indicate
- run state visible

5) Disconnect tablet mid-run:
- run continues
- reconnect restores LIVE

6) Trigger E-stop:
- outputs safe
- CRITICAL Indicate event shown
- Start inhibited

### Done criteria
If the script completes without manual intervention (no restarts, no “mystery states”), the core architecture is validated.

---

## Recommended rollback points
- If BLE stability issues occur: revert to Phase 1 simulated firmware and validate with nRF Connect.
- If hardware issues occur: keep BLE/protocol stable, swap back to simulated sources to confirm app remains correct.

---

## Notes on efficiency
This order is intentionally chosen to avoid debugging three failure domains at once:
- BLE/GATT/platform quirks
- protocol correctness (seq/CRC/ACK policy)
- hardware timing and electrical realities (I2C expanders, optos, RS-485)

Stabilize the contract first, then attach physical reality behind stable interfaces.
