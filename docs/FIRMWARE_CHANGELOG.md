# Firmware Changelog

## [Unreleased] - 2026-01-19

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

## Git Best Practices for This Project

### Branch Strategy
- **main**: Stable, deployable code only. Protected branch.
- **feature/\***: Feature development branches (e.g., `feature/firmware-led-and-components`)
- **fix/\***: Bug fix branches
- Always create PRs for merging to main; never push directly.

### Commit Hygiene
1. **Commit frequently**: Push after each logical unit of work completes
2. **Atomic commits**: One feature/fix per commit when possible
3. **Descriptive messages**: First line summarizes (50 chars), body explains why
4. **Push regularly**: Don't accumulate local commits - push after each working state

### Before Starting Work
```bash
git fetch origin
git status                    # Check for uncommitted changes
git pull origin main          # If on main
```

### During Development
```bash
# After each milestone:
git add -A
git commit -m "Brief description of change"
git push origin <branch-name>
```

### Preventing Corruption
- **Push often**: Remote serves as backup; local-only commits are vulnerable
- **Avoid large binary files**: Use Git LFS if needed
- **Clean builds**: Use `fullclean` before major operations to avoid stale artifacts
- **Fresh clone test**: Periodically verify the repo builds from a fresh clone

### Recovery from Corruption
If you encounter pack file errors or "file truncated" messages:
1. Clone fresh to a temporary location
2. Copy uncommitted changes from corrupted repo
3. Commit and push from the fresh clone
4. Replace the corrupted local repo with the fresh one
5. Preserve machine-specific files (e.g., `local.env`)

### Pre-Session Checklist
- [ ] `git status` - verify clean state
- [ ] `git pull` - sync with remote
- [ ] Check branch name matches intended work
- [ ] Note current HEAD commit for reference

### Post-Session Checklist
- [ ] All changes committed with clear messages
- [ ] Pushed to remote
- [ ] Update changelog/roadmap if applicable
- [ ] Create PR if feature is complete
