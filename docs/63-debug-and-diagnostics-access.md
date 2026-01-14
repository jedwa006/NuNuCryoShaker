# Debug & Diagnostics Access (App + Firmware) — Policy Locked

This document defines the **chosen** diagnostics access policy for the project.
It is intended to make development fast, keep the operator UI clean, and preserve field supportability without shipping unsafe test hooks.

Related docs:
- Tools & harnesses: `docs/62-tools-and-harnesses.md`
- State machines: `docs/96-state-machine.md`
- Debug plan: `docs/60-debugging-and-test-plan.md`
- Safety policies: `docs/40-safety-heartbeat-and-policies.md`

---

## 1) Policy (what we will do)

### 1.1 Android App
- Diagnostics UI is **always compiled into the app**.
- **Debug builds:** Diagnostics entry is **visible** in navigation (one tap).
- **Release builds:** Diagnostics entry is **hidden behind a deliberate gesture**, with a confirmation modal.
- Release Diagnostics is **read-only by default** and includes **Export Log Bundle**.

Debug-only additions (allowed):
- “Protocol Test Page” actions (send canned commands, force resubscribe, etc.) may be enabled in debug builds only.

### 1.2 Firmware (ESP32-S3)
- **Always-on**: lightweight counters/stats are included in all builds (safe and low overhead).
- **Compile-time only (debug firmware builds):**
  - `SIM_MODE` (simulated backend)
  - `ENABLE_TEST_HOOKS` (synthetic events, stress tools)
- Production firmware must ship with:
  - `SIM_MODE=0`
  - `ENABLE_TEST_HOOKS=0`
- **No runtime toggles** for sim/test behaviors in production builds.

---

## 2) Rationale

1) Debug builds must be frictionless:
- visible Diagnostics avoids wasting time during development.

2) Release builds must remain supportable without reinstalling a debug APK:
- hidden Diagnostics enables field troubleshooting without cluttering operator UI.

3) Production safety and correctness:
- firmware test hooks are compile-time gated to prevent accidental exposure in production binaries.

---

## 3) Android App: Diagnostics entry definition

### 3.1 Debug builds (visible entry)
- A “Diagnostics” item exists in the main navigation (tab, drawer, or menu).
- If a Protocol Test Page exists, it is reachable from Diagnostics.

### 3.2 Release builds (hidden entry)
Diagnostics is accessible via a deliberate gesture. Choose one and implement consistently:

**Gesture standard (recommended):**
- Tap the **version string 7×** on the About panel
  - This is a common Android convention and is easy to explain to support users.

Alternative (allowed if preferred):
- Long-press app title for 3 seconds, then tap 5×

### 3.3 Confirmation modal (release builds)
On entry to Diagnostics in release builds:
- Show a confirmation modal:
  - “Diagnostics is for support and advanced troubleshooting. Continue?”
- Provide Continue / Cancel.

---

## 4) Android App: Diagnostics content (minimum for v0)

### 4.1 Read-only status (release + debug)
Must display:
- Connection state (DISCONNECTED/SCANNING/CONNECTING/DISCOVERING/SUBSCRIBING/SESSION_OPENING/LIVE/DEGRADED/ERROR)
- Device identity `SYS-CTRL-<shortid>`
- Firmware + protocol version (from Device Info)
- RSSI
- MTU (if available)
- Telemetry rate estimate (frames/sec)
- Frames received counters:
  - total frames
  - CRC failures
  - length/parse failures
- Last telemetry timestamp
- Last ACK timestamp
- Session info:
  - lease_ms
  - last keepalive timestamp

### 4.2 Export Log Bundle (release + debug)
Provide “Export log bundle” that includes:
- app logs (recent window)
- last N events (decoded list)
- Device Info snapshot (fw/proto/cap bits)
- link health summary (RSSI/MTU/rates/counters)

### 4.3 Debug-only actions (debug builds only)
Allowed in debug builds:
- “Protocol Test Page” actions:
  - send canned commands (OPEN_SESSION/KEEPALIVE/SET_RELAY/START/STOP)
  - force re-subscribe CCCD
  - force re-open session
  - force reconnect
- Raw frame hex view (in/out) and decoder output

In release builds:
- Do not include command-sending controls unless explicitly approved later.

---

## 5) Firmware: flags and build policy

### 5.1 Compile-time flags
- `SIM_MODE`
  - Telemetry sources and command effects are simulated (no real I/O)
- `ENABLE_TEST_HOOKS`
  - Enables test-only triggers (synthetic E-stop, burst telemetry, controller offline simulation)

### 5.2 Production build requirements
Production builds must have:
- `SIM_MODE=0`
- `ENABLE_TEST_HOOKS=0`

### 5.3 Always-on firmware stats
Include basic counters in all builds (safe):
- `rx_frames_total`
- `rx_crc_fail`
- `rx_len_fail`
- `tx_frames_total`
- `tx_indications_total`
- `tx_notifications_total`
- `telemetry_frames_sent`
- `ble_disconnects_total`
- `lease_expired_total`

Expose stats via:
- Diagnostic log stream (optional), or
- future GET_STATS command, or
- extended Device Info payload (if stable and bounded)

---

## 6) Acceptance criteria

App:
- Debug build: Diagnostics is reachable in one tap.
- Release build: Diagnostics is reachable via the gesture within 10 seconds.
- Export log bundle produces a shareable artifact.

Firmware:
- Test hooks cannot be executed in production builds (not compiled in).
- Stats counters increment correctly and can be surfaced for diagnostics.

---

## 7) Documentation references
- When updating Diagnostics features, update:
  - `docs/62-tools-and-harnesses.md`
  - `docs/60-debugging-and-test-plan.md`
  - This document: `docs/63-debug-and-diagnostics-access.md`
