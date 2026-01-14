# Operator UX Requirements (Tablet HMI)

This document defines the operator-facing UX requirements for the System HMI on an Android tablet (Lenovo, 1900×1200).
It is intentionally framework-agnostic so Kotlin/Compose or Flutter implementations converge on the same behavior.

Related docs:
- State machines: `docs/96-state-machine.md`
- Action/behavior map: `docs/97-action-behavior-troubleshooting-map.md`
- Safety + lease policies: `docs/40-safety-heartbeat-and-policies.md`
- Protocol catalog: `docs/90-command-catalog.md`
- Debug plan: `docs/60-debugging-and-test-plan.md`

---

## 1) UX goals

### Primary goals
- Provide a “near real time” operational display (status + alarms + run state).
- Make common actions (relay toggles, setpoint changes, start/stop) fast and unambiguous.
- Ensure safety intent is reflected clearly:
  - “Start is not permitted unless HMI is LIVE.”
  - Physical E-stop and interlocks are authoritative and must be unmistakable on-screen.

### Secondary goals
- Support expansion into:
  - PID tuning pages
  - Modbus register tools
  - Event log review/export
- Enable rapid field diagnosis (operator can report clear states/screenshots/log export).

---

## 2) Layout & interaction standards (tablet)

### Touch targets
- Minimum touch target: **48 dp** (preferred larger for critical controls).
- Avoid small toggles and dense tables for primary operations.

### Readability
- Large numeric values for PV/SV/OP% (dashboard-first).
- Use text + icons; do not rely on color alone (color-blind safe).

### Latency and responsiveness
- Baseline telemetry updates at ~10 Hz should feel “live,” but the UI should not visibly “flicker.”
- Prefer smoothing/holding of numeric values (display latest sample; do not animate every tick unnecessarily).
- Operator actions should produce immediate feedback:
  - show **Pending** state within one frame of the tap
  - show **Confirmed** state upon ACK/telemetry update

---

## 3) Global UI elements (always visible)

### Top status bar (persistent)
Must include:
- **Connection state indicator** (one of the defined states; see section 6)
- **Device identity**: `SYS-CTRL-<shortid>`
- **HMI Live** indicator (lease valid / invalid)
- **Run state**: IDLE / RUNNING / STOPPING / FAULT
- **Alarm summary**: count + highest severity

### Alarm banner (persistent when active)
- Appears immediately when any WARN/ALARM/CRITICAL event occurs.
- For CRITICAL (e.g., E-stop asserted):
  - banner must be dominant and sticky
  - clear action text: “E-STOP ACTIVE — outputs safe — clear physical E-stop to proceed”
- Banner must support tapping to open the Alarm/Events page.

### Command pending indicator
- When a command is sent and not yet ACKed:
  - show a small “pending” indicator at the point of interaction (tile/button row)
  - optionally show a global “pending operations” counter

---

## 4) Screen set (v0 + expansion)

### v0 required screens
1) **Connect / Device Select**
   - scan results with RSSI
   - filter by `SYS-CTRL-*` and/or service UUID
   - connect button and last-known device shortcut

2) **Dashboard**
   - run state
   - alarm banner
   - DI/RO summary
   - three controller panels (IDs 1,2,3):
     - PV, SV, OP%
     - mode indicator
     - “age_ms” freshness indicator (e.g., Fresh / Stale / Offline)

3) **I/O Panel**
   - Relays CH1..CH8:
     - large toggle tiles
     - current state, pending state, faulted state
   - Digital Inputs DI1..DI8:
     - indicator tiles
     - optional label overrides

4) **Setpoint / Control page**
   - set SV for a selected controller (default to #3 in early testing)
   - mode changes (STOP/MANUAL/AUTO/PROGRAM as applicable)
   - numeric entry via native keypad (see section 5)

5) **Events / Alarm Log**
   - chronological list with severity + source (SYSTEM or controller 1..3)
   - quick filters: WARN/ALARM/CRITICAL, by controller, by time window
   - “export logs” action (file share intent)

### Expansion screens (planned)
- PID tuning/config page (read/write params)
- Modbus register tool (read/write with safeguards)
- Diagnostics page (BLE RSSI, MTU, telemetry rate, CRC failures, command RTT)

---

## 5) Input behaviors (numeric entry, toggles, confirmations)

### Numeric entry (SV, thresholds, timers)
Requirements:
- Use a numeric-first input method (native keypad / numeric keyboard).
- Validate before sending:
  - allowed range (min/max)
  - units shown adjacent (°C, %, ms, etc.)
  - step/resolution shown where relevant
- Provide:
  - **Apply** (sends command)
  - **Cancel**
- After Apply:
  - show Pending state until ACK
  - on success: confirm visually and update displayed SV
  - on rejection: show reason (policy/invalid/interlock)

### Relay toggles
- Default behavior: optimistic UI is allowed but must be visually distinct.
  - Example: on tap, show “Pending ON” immediately
  - Confirm ON only after ACK and/or telemetry matches expected state
- If ACK times out:
  - show “Unconfirmed” and offer “Retry”
  - do not silently revert without indicating uncertainty

### Critical actions (Start/Stop/Abort, Clear latched alarms)
- Start:
  - must be disabled unless HMI LIVE + policy conditions OK (interlocks OK, no estop).
  - if disabled, UI must show *why* (e.g., “HMI not live”, “Door interlock open”).
- Stop/Abort:
  - Stop should always be quickly accessible when connected.
  - Abort may require a confirmation step depending on system policy.
- Critical actions must show acknowledgement semantics:
  - “Sending…”
  - “Confirmed” (Indicate ACK)
  - “Not confirmed” (link issue) with clear escalation instructions

---

## 6) Connection state model (UI-facing)

The UI must use deterministic states and present them consistently (colors optional, text mandatory):

- **DISCONNECTED**: not connected; show “Connect” call-to-action
- **SCANNING**: scanning for devices; show spinner and results
- **CONNECTING**: attempting GATT connect
- **DISCOVERING**: discovering services/characteristics
- **SUBSCRIBING**: enabling CCCD for telemetry and events/acks
- **SESSION_OPENING**: sending OPEN_SESSION; awaiting ACK
- **LIVE**: session open; keepalive active; telemetry healthy
- **DEGRADED**: connected but not fully healthy (telemetry stale, events stream down, CRC high)
- **ERROR**: incompatible protocol or missing characteristics; requires user action

Each non-LIVE state must present a short operator-relevant explanation.
Detailed technical reasons belong on the Diagnostics page.

---

## 7) Freshness and staleness rules

### Telemetry freshness
- If no telemetry frames received for > 500 ms (tunable):
  - UI transitions to DEGRADED
  - show “Telemetry stale” indicator

### Controller freshness (IDs 1..3)
- Use `age_ms` per controller:
  - Fresh: < 1000 ms
  - Stale: 1000–5000 ms
  - Offline: > 5000 ms or explicit offline event
- UI must show freshness and avoid implying current values are live when they are stale.

---

## 8) Safety semantics in the UI

### Start gating
- UI must reflect the lease-based policy:
  - “Start requires HMI LIVE”
  - show lease status and last keepalive time (in Diagnostics)

### Mid-run disconnect behavior
- If tablet disconnects mid-run:
  - run continues (default policy)
  - UI, on reconnect, must show a warning event occurred and current run state

### E-stop
- E-stop must be visually unmistakable.
- E-stop events must be treated as CRITICAL and sticky until cleared.

---

## 9) Diagnostics and supportability (field-ready)

### Minimum diagnostics (v0)
- BLE RSSI
- Connection state
- MTU (if available)
- Telemetry rate estimate (frames/sec)
- CRC failures count
- Command round-trip time (approx)
- Last disconnect reason (if known)
- Session info: session_id (masked), lease_ms, last keepalive timestamp

### Export / sharing
- Provide “Export logs” that bundles:
  - app logs
  - recent events list
  - current device info (fw version, proto ver, cap bits)
This enables fast support from an engineer/agent without reproducing everything live.

---

## 10) Consistency requirements (across Kotlin/Flutter)
- Same screen names and core layouts
- Same command semantics (pending/confirmed/unconfirmed)
- Same state machine labels
- Same error messages for common policy rejections (session invalid, interlock open, estop active)
- Same channel indexing (CH1..CH8, DI1..DI8)
