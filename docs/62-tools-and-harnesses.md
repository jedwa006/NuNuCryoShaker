# Tools & Harnesses (Bring-Up Accelerators)

This document defines lightweight tooling that makes Phase 1–2 bring-up fast and repeatable without contaminating production code.

Goals:
- Reduce debugging time by isolating failures (BLE vs protocol vs hardware).
- Provide deterministic “known good” tests and artifacts (logs, counters, scripts).
- Keep production code clean by gating test-only behaviors behind explicit flags.

Related docs:
- Bring-up playbook: `docs/61-bringup-order.md`
- Protocol catalog: `docs/90-command-catalog.md`
- State machines: `docs/96-state-machine.md`
- Debug plan: `docs/60-debugging-and-test-plan.md`

---

## 1) Firmware tools (ESP32-S3)

### 1.1 Firmware “Sim Harness” (recommended)
A build-time or runtime switch that routes telemetry sources and command handlers to a simulated backend.

**Name suggestion**
- `SIM_MODE` (build flag) and/or `sim_mode=true` (runtime)
- Companion: `HARDWARE_MODE` (default for production)

**Capabilities in SIM_MODE**
- Stable 10 Hz telemetry with deterministic values
- Ability to trigger events (E-stop, controller offline/online)
- Ability to mirror relay commands into `ro_bits` without touching physical relays
- Optional: synthetic RS-485 values with controllable staleness

**Why it helps**
- Lets the app team work without hardware connected.
- Lets you verify protocol + UI behaviors before touching I2C/RS-485 timing.

**Acceptance**
- App reaches LIVE and remains stable.
- Relay toggles and SV setpoints generate coherent ACKs and telemetry changes.

---

### 1.2 Firmware “Protocol Stats” counters (always-on, low cost)
Maintain basic counters and expose them either:
- via Device Info (additional fields) OR
- via Diagnostic Log characteristic OR
- via a dedicated “GET_STATS” command (future)

**Minimum counters**
- `rx_frames_total`
- `rx_crc_fail`
- `rx_len_fail`
- `tx_frames_total`
- `tx_indications_total`
- `tx_notifications_total`
- `cmd_acks_total`
- `cmd_timeouts_total` (if applicable)
- `telemetry_frames_sent`
- `ble_disconnects_total`
- `lease_expired_total`

**Why it helps**
- It is the fastest way to identify “this is a CRC issue” vs “this is a subscribe issue” vs “this is a throughput issue.”

---

### 1.3 Firmware “Self-test hooks” (explicit, safe)
Provide a test-only command or pin-triggered behavior to generate known events.

**Examples**
- `EVENT_TEST_TRIGGER` → emits:
  - HMI_CONNECTED (Notify)
  - ESTOP_ASSERTED (Indicate)
  - ESTOP_CLEARED (Indicate)
- `TELEMETRY_BURST` → send 20 telemetry frames quickly (to test parser robustness)
- `CONTROLLER_SIM_OFFLINE` → mark controller #3 offline and age_ms grows

**Guardrails**
- Only enabled in SIM_MODE or when a compile-time `ENABLE_TEST_HOOKS=1` is set.
- Never enabled by default in production builds.

---

## 2) App tools (Android)

### 2.1 “Protocol Test Page” (high value)
A hidden/diagnostics screen inside the app that allows:
- View connection state machine state
- View RSSI, MTU, telemetry rate, CRC failures (app-side)
- Send canned commands (SET_RELAY, OPEN_SESSION, KEEPALIVE, START/STOP)
- View raw frames (hex) and decoded summaries
- Trigger “re-subscribe CCCD” and “re-open session” actions

**Why it helps**
- Prevents you from debugging through the operator UI.
- Makes it trivial to reproduce issues and capture evidence.

**Acceptance**
- Any failure can be reproduced and diagnosed from this page without adding ad hoc prints.

---

### 2.2 “Golden Frame” vectors (must-have)
Store a small set of known-good frames (hex) from `docs/90-command-catalog.md` and validate:
- Frame builder output equals golden hex
- CRC16 implementation matches
- Decoder correctly parses telemetry/controller blocks

**Implementation approach**
- Unit tests (preferred)
- Or a debug tool within the app that compares against embedded vectors

**Why it helps**
- Eliminates entire classes of “endianness/CRC mismatch” bugs quickly.

---

### 2.3 “Link Health” monitor
Calculate in real time:
- telemetry frames/sec
- % frames failing validation (len/CRC)
- time since last telemetry
- time since last ACK
- command RTT distribution (min/mean/max)

Display on Diagnostics/Protocol Test Page.

---

## 3) External tools (developer workstation)

### 3.1 BLE sanity tool
- Use nRF Connect (mobile) for:
  - service discovery
  - CCCD enable
  - manual write to Command RX
  - observe notify/indicate streams

(Do not automate this initially; it’s your “truth source” during bring-up.)

### 3.2 Optional: CLI protocol exerciser (future)
A small script that connects and performs:
- subscribe
- open_session
- keepalive loop
- relay toggles
- dumps events

This is optional if the app Protocol Test Page exists; both together are powerful.

---

## 4) Minimal implementation design (keep code clean)

### 4.1 Firmware interface boundary
Create a stable interface in firmware:

- `IStateProvider`:
  - `get_di_bits()`
  - `get_ro_bits()`
  - `get_alarm_bits()`
  - `get_controller_snapshot(id)`
- `ICommandSink`:
  - `set_relay(index, state)`
  - `set_sv(controller_id, sv_x10)`
  - `start_run(mode)`
  - `stop_run(mode)`
- `IEventSource`:
  - `emit_event(...)`

Then implement:
- `SimBackend : IStateProvider, ICommandSink`
- `HardwareBackend : IStateProvider, ICommandSink`

BLE/protocol code depends only on these interfaces.

### 4.2 App layering boundary
Keep app structure:
- `BleTransport`
- `ProtocolCodec` (frames + CRC)
- `DeviceSession` (lease + state machine)
- `UiModel` (state for screens)

Diagnostics page reads from `DeviceSession` and `ProtocolCodec` stats.

---

## 5) Deliverables (what to implement first)

### Firmware (recommended order)
1) SIM_MODE telemetry + event test trigger
2) Command RX + ACK logic + session lease
3) Protocol stats counters + optional diagnostic log characteristic

### App (recommended order)
1) Golden frame vectors in unit tests
2) Minimal “Protocol Test Page” with:
   - connect/discover/subscribe
   - open session + keepalive
   - relay toggle
   - raw frame view
3) Link health metrics + export logs

---

## 6) Definition of done (tools)
Tools are “done” when:
- A new developer can connect to the ESP and validate:
  - GATT present
  - telemetry arriving
  - open_session + keepalive stable
  - relay command round-trip confirmed
- And can capture a log bundle sufficient to debug without additional instrumentation.
