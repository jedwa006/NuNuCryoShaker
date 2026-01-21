# Firmware Changelog

## [v0.4.1] - 2026-01-20

### Added
- **PAUSED state**: New machine state for mid-run inspection
  - `MACHINE_STATE_PAUSED (7)` - Motor stopped, door unlocked, cooling optional
  - `CMD_PAUSE_RUN (0x0012)` - Pause with keep_cooling or stop_cooling mode
  - `CMD_RESUME_RUN (0x0013)` - Resume from paused state with door interlock check
  - Pause mode options: `PAUSE_MODE_KEEP_COOLING (0)`, `PAUSE_MODE_STOP_COOLING (1)`
  - Door can be opened during pause for inspection without triggering fault

### Technical Notes
- Pause preserves pre-pause state (RUNNING or PRECOOL) for correct resume
- Resume checks door is closed before re-engaging
- Run timer paused during PAUSED state

---

## [v0.4.0] - 2026-01-20

### Added
- **safety_gate component**: Configurable safety gate framework for operational safety
  - Subsystem capability levels: NOT_PRESENT (0), OPTIONAL (1), REQUIRED (2)
  - 9 safety gates: E-Stop, Door Closed, HMI Live, PID1/2/3 Online, PID1/2/3 No Probe Error
  - E-Stop gate (ID 0) can NEVER be bypassed - hardware safety requirement
  - Gate bypasses reset on reboot for safety; capability levels persist via NVS
  - Probe error detection: HHHH (≥500°C) and LLLL (≤-300°C, except PID1/LN2)
  - Integrated with machine_state for START_RUN blocking and run fault detection

- **BLE Commands for Safety Gate**:
  - `CMD_GET_CAPABILITIES (0x0070)` - Get subsystem capability levels (8 bytes)
  - `CMD_SET_CAPABILITY (0x0071)` - Set capability level for a subsystem (persists to NVS)
  - `CMD_GET_SAFETY_GATES (0x0072)` - Get gate enable/status bitmasks (4 bytes)
  - `CMD_SET_SAFETY_GATE (0x0073)` - Enable/bypass a safety gate (rejects E-Stop bypass)

- **Extended alarm_bits (bits 9-14)**:
  - bit 9: `GATE_DOOR_BYPASSED` - Door gate is bypassed
  - bit 10: `GATE_HMI_BYPASSED` - HMI gate is bypassed
  - bit 11: `GATE_PID_BYPASSED` - Any PID gate is bypassed
  - bit 12: `PID1_PROBE_ERROR` - PID1 has probe error
  - bit 13: `PID2_PROBE_ERROR` - PID2 has probe error
  - bit 14: `PID3_PROBE_ERROR` - PID3 has probe error

### Changed
- **telemetry.c**: Added `update_safety_gate_alarm_bits()` to report probe errors and gate bypasses
- **recovery_factory CMakeLists.txt**: Excluded `safety_gate` and `machine_state` from recovery build
- **machine_state**: Now checks safety gates before allowing START_RUN

### Technical Notes
- Subsystem IDs: PID1(0), PID2(1), PID3(2), DI_ESTOP(3), DI_DOOR(4), DI_LN2(5), DI_MOTOR(6)
- Gate IDs: ESTOP(0), DOOR_CLOSED(1), HMI_LIVE(2), PID1_ONLINE(3), PID2_ONLINE(4), PID3_ONLINE(5), PID1_NO_PROBE_ERR(6), PID2_NO_PROBE_ERR(7), PID3_NO_PROBE_ERR(8)
- NVS namespace "safety" stores capability levels (keys: `cap_pid1`, `cap_pid2`, etc.)
- Default capabilities: E-Stop=REQUIRED (immutable), Door=REQUIRED, LN2=OPTIONAL, Motor=NOT_PRESENT

---

## [v0.3.x] - 2026-01-19 to 2026-01-20

### v0.3.10 - Relay Mapping Verification
- Verified relay channel mapping (CH1-CH7)
- Documented relay functions in machine_state.h

### v0.3.8 - Telemetry Serialization Fix
- Fixed `wire_build_telemetry_ext()` to serialize all 16 bytes of run state
- `lazy_poll_active` and `idle_timeout_min` now correctly included in telemetry

### v0.3.7 - KEEPALIVE Activity Fix
- Fixed KEEPALIVE resetting activity timer (prevented lazy polling activation)

### v0.3.4 - Lazy Polling Telemetry
- Added `lazy_poll_active` and `idle_timeout_min` to telemetry run state struct
- NVS persistence of idle timeout setting

### v0.3.2 - Generic Register Commands
- `CMD_READ_REGISTERS (0x0030)` - Read 1-16 consecutive Modbus registers
- `CMD_WRITE_REGISTER (0x0031)` - Write single register with verification

### v0.3.1 - PID Polling Fixes
- MODE register now polled alongside PV/SV
- Read-after-write verification for SET_SV and SET_MODE

---

## [v0.2.0] - 2026-01-19

### Added
- **status_led component**: New RGB LED status indicator using WS2812 on GPIO38
  - 17 LED states covering boot sequence, operation, errors, and special modes
  - Breathing effect for idle/connected states
  - Blinking patterns for boot sequence and errors
  - FreeRTOS task for async pattern execution
  - Flash timer for one-shot activity indicators

### Changed
- **recovery_factory CMakeLists.txt**: Added `EXCLUDE_COMPONENTS` to prevent building
  BLE-dependent components (ble_gatt, session_mgr, wire_protocol, telemetry, relay_ctrl,
  status_led, bootctl) that aren't needed for the minimal OTA portal
- **main_app/app_main.c**: Integrated status_led boot sequence
  - LED_STATE_BOOT_POWER_ON (blue solid) on startup
  - LED_STATE_BOOT_HW_INIT (blue blink) during hardware init
  - LED_STATE_BOOT_BLE_INIT (cyan blink) during BLE init
  - LED_STATE_BOOT_COMPLETE (green flash 3x) then transition to advertising
- **ble_gatt.c**: Added status_led state transitions on connect/disconnect
  - LED_STATE_CONNECTED_HEALTHY on client connect
  - LED_STATE_ERROR_DISCONNECT on client disconnect

### Fixed
- **WS2812 R/G color swap**: Hardware has R and G channels swapped; compensated
  by defining color macros with G and R positions swapped in status_led.h

### Technical Notes
- status_led uses `espressif/led_strip` component (v2.5.0+)
- Pattern task runs on CPU0 at priority 3 with 2KB stack
- Breathing effect uses triangle wave approximation of sine
- Recovery app idf_component.yml added to pull led_strip dependency

## Flash Layout (as of this session)
- **factory** (0x50000): recovery_factory app - WiFi OTA portal
- **ota_0** (0x250000): main_app - Primary BLE relay controller
- **ota_1** (0x710000): Alternate OTA slot (unused)
- Boot partition set to ota_0 via otatool

## Session Summary
1. Implemented status_led component with 17 states
2. Fixed WS2812 R/G color swap at hardware level
3. Integrated LED status into boot sequence and BLE events
4. Fixed recovery_factory build by excluding BLE components
5. Clean flash: erase → recovery to factory → main_app to ota_0
6. Verified LED states: cyan breathing (advertising) → green breathing (connected)
7. Verified BLE communication with tablet app - all systems nominal
8. Recovered from git pack file corruption via fresh clone workflow
9. Created feature branch `feature/firmware-led-and-components` with clean history
10. Verified fresh clone builds successfully (recovery: 799KB, main: 573KB)

---

## Development Reference

For complete agent instructions, build commands, and development workflow, see:
- **[CLAUDE.md](../CLAUDE.md)** - Agent instructions and quick reference
- **[CONTRIBUTING.md](../CONTRIBUTING.md)** - Contribution guidelines

### Quick Build Reference (use wrapper script!)
```bash
cd ~/Documents/GitHub/NuNuCryoShaker/firmware

./tools/idf main build          # Build main_app
./tools/idf main flash          # Flash to device
./tools/idf main monitor        # Serial monitor
./tools/idf main install-ota0   # Safe install to ota_0
./tools/idf main fullclean      # Clean build directory
./tools/idf main reconfigure    # Regenerate sdkconfig

./tools/idf recovery build      # Build recovery app
```

### Git Quick Reference
```bash
# Start feature
git checkout main && git pull
git checkout -b feature/my-feature

# Commit and push
git add -A
git commit -m "feat(component): Description"
git push -u origin feature/my-feature

# Create PR
gh pr create --title "feat: Description" --body "Summary..."
```
