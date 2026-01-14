# MVP Scope (v0) — Tablet HMI over BLE

This document defines the minimum viable product (MVP) scope for v0 of the System HMI.
It exists to prevent scope creep and to provide an unambiguous “done” definition for an agentic coding effort.

Related docs:
- Scope & goals: `docs/00-scope-and-goals.md`
- GATT schema + UUIDs: `docs/20-gatt-schema.md`, `docs/80-gatt-uuids.md`
- Protocol catalog: `docs/90-command-catalog.md`
- Safety + lease policies: `docs/40-safety-heartbeat-and-policies.md`
- State machines: `docs/96-state-machine.md`
- UX requirements: `docs/98-operator-ux-requirements.md`
- Test plan: `docs/60-debugging-and-test-plan.md`
- Checklist gates: `docs/95-implementation-checklist.md`

---

## 1) MVP definition (v0)

### v0 must achieve
A technician can:
1) Connect the tablet to the ESP over BLE reliably.
2) View live status (DI/RO + alarms + 3 controller PV/SV/OP% + freshness).
3) Toggle CH1..CH8 relays with clear pending/confirmed behavior.
4) Set SV on controller #3 (and see the result reflected).
5) Start/Stop a run with lease-based “HMI LIVE” gating (Start requires LIVE).
6) Observe and acknowledge critical safety states (E-stop displayed as CRITICAL).

### v0 explicitly does not include
- Camera streaming
- Remote/cloud/MQTT features
- Full PID tuning parameter pages (beyond SV + mode basics)
- Full Modbus register editor UI (may be stubbed behind a capability flag)
- OTA update UI (placeholder allowed, no implementation required)

---

## 2) Feature list (MVP)

### 2.1 BLE connectivity and lifecycle
- Scan filtered by `SYS-CTRL-*` and/or service UUID
- Connect + discover required service/characteristics
- Subscribe:
  - Telemetry Stream (Notify)
  - Events + Acks (Indicate; Notify optional)
- OPEN_SESSION and KEEPALIVE loop (1 Hz)
- Deterministic reconnect:
  - backoff
  - re-discovery (or verify cached handles)
  - re-enable CCCD
  - re-open session

**Acceptance criteria**
- From app launch to LIVE state: < 10 seconds typical.
- After link drop, app returns to LIVE without restart: < 20 seconds typical.

---

### 2.2 Telemetry rendering (dashboard core)
Required displayed fields:
- Connection state (DISCONNECTED/SCANNING/CONNECTING/.../LIVE/DEGRADED)
- Device identity `SYS-CTRL-<shortid>`
- Firmware + protocol version (from Device Info)
- Run state (IDLE/RUNNING/etc.)
- Alarm summary + highest severity banner
- DI bits (DI1..DI8)
- RO bits (CH1..CH8)
- Controllers 1..3 panels:
  - PV, SV, OP%
  - mode
  - freshness indicator using age_ms: Fresh/Stale/Offline

**Acceptance criteria**
- Baseline telemetry cadence of ~10 Hz does not cause visible UI stutter.
- Change-driven updates appear within 100–200 ms typical on a good link.

---

### 2.3 Relay control (CH1..CH8)
- CH1..CH8 large toggle tiles.
- On tap:
  - send `SET_RELAY`
  - show Pending state immediately
  - clear Pending only when ACK received or telemetry confirms state
- If ACK timeout:
  - show Unconfirmed and offer Retry
- All channel indexing must be 1..8 (CH1..CH8) and match ro_bits mapping.

**Acceptance criteria**
- Relay toggle is confirmed within ~200 ms typical on good link.
- Mismatches or timeouts are visible to the operator (no silent failure).

---

### 2.4 SV set (controller #3)
- Numeric SV input dialog:
  - numeric keyboard
  - range validation (define min/max for v0; can be broad initially)
- Send `SET_SV` with `controller_id=3`
- Show Pending, then Confirmed / Failure with reason

**Acceptance criteria**
- When controller #3 is present, SV changes are reflected in telemetry within a reasonable interval (poll dependent).
- When controller #3 is absent, command returns TIMEOUT_DOWNSTREAM (or equivalent) and UI shows it clearly.

---

### 2.5 Start/Stop (lease gated)
- Start button:
  - disabled unless HMI is LIVE + policy conditions OK
  - if disabled, show why (HMI not live, interlock open, estop active)
- Stop button:
  - always available when connected (policy-dependent)
- ACK for Start/Stop must be **Indicate**.
- Event log records RUN_STARTED/RUN_STOPPED.

**Acceptance criteria**
- START_RUN rejected if lease invalid (REJECTED_POLICY).
- STOP_RUN confirmed via Indicate ACK.

---

### 2.6 Events & alarm log
- Events page lists:
  - time
  - severity
  - source (SYSTEM or controller 1..3)
  - event_id / description
- Alarm banner links to Events page.
- Provide basic filters:
  - severity
  - controller source

**Acceptance criteria**
- E-stop asserted/cleared events appear reliably.
- HMI connected/disconnected events appear reliably.

---

### 2.7 Diagnostics (v0 minimum)
Must display:
- RSSI
- MTU (if available)
- Telemetry rate estimate
- CRC failure count
- Last disconnect timestamp
- Session: lease_ms, last keepalive timestamp

Optional:
- Export logs (app logs + last N events + Device Info)

**Acceptance criteria**
- When something fails, operator can capture enough info to debug without re-running everything live.

---

## 3) Required command/event subset (v0)
From `docs/90-command-catalog.md`:

### Commands (must implement)
- OPEN_SESSION (0x0100)
- KEEPALIVE (0x0101)
- START_RUN (0x0102)
- STOP_RUN (0x0103)
- SET_RELAY (0x0001)
- SET_SV (0x0020)
- SET_MODE (0x0021) *(optional for v0; recommended if controller supports it)*

### Events (must implement)
- ESTOP_ASSERTED (0x1001) [CRITICAL, Indicate]
- ESTOP_CLEARED (0x1002) [Indicate recommended]
- HMI_CONNECTED (0x1100) [Notify]
- HMI_DISCONNECTED (0x1101) [Notify]
- RUN_STARTED (0x1200) [Notify]
- RUN_STOPPED (0x1201) [Notify]
- RS485_DEVICE_ONLINE/OFFLINE (0x1300/0x1301) [Notify]
- ALARM_LATCHED/CLEARED (0x1400/0x1401) [Notify/Indicate based on severity]

---

## 4) “Done” definition (v0 demo script)
A successful v0 demo includes:
1) Launch app → reach LIVE  
2) Toggle CH1..CH8 while observing telemetry + physical outputs  
3) Set SV on controller #3 and observe it reflected  
4) START_RUN requires LIVE, then starts; STOP_RUN stops  
5) Disconnect tablet mid-run; run continues; reconnect restores LIVE  
6) Simulate/trigger E-stop; app shows CRITICAL E-stop state reliably  

If all six steps succeed, v0 is complete.

---

## 5) Out-of-scope guardrails (do not implement in v0)
- No camera/video transport work
- No MQTT/cloud endpoints
- No advanced tuning pages beyond SV
- No general Modbus “write-anything” UI
- No over-engineered auth/crypto (local BLE only; revisit later if needed)
- No persistent multi-user roles/permissions (single operator model for v0)
