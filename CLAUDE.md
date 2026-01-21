# Claude Code Agent Instructions - NuNuCryoShaker Firmware

## Project Overview

This is the **ESP32-S3 firmware** for the NuNu CryoShaker cryogenic ball mill controller. The firmware manages:
- BLE GATT server for tablet app communication
- RS-485 Modbus RTU to LC108 PID temperature controllers (3 units)
- 8-channel relay control via I2C expander (TCA9554)
- 8-channel digital input via I2C expander (TCA9534)
- Safety gate framework for operational safety
- Machine state management (IDLE, PRECOOL, RUNNING, PAUSED, etc.)

**Current Version:** v0.4.0 (build 0x26012001)

---

## Repository Structure

```
NuNuCryoShaker/
├── firmware/
│   ├── apps/
│   │   ├── main_app/          # Primary BLE relay controller
│   │   └── recovery_factory/  # WiFi OTA recovery portal
│   ├── components/            # Shared ESP-IDF components
│   │   ├── ble_gatt/          # BLE GATT server & command handlers
│   │   ├── machine_state/     # State machine (IDLE, RUNNING, etc.)
│   │   ├── modbus_master/     # RS-485 Modbus RTU driver
│   │   ├── pid_controller/    # LC108 PID polling & management
│   │   ├── relay_ctrl/        # I2C relay expander driver
│   │   ├── safety_gate/       # Safety gate framework
│   │   ├── session_mgr/       # BLE session & keepalive
│   │   ├── status_led/        # WS2812 RGB status LED
│   │   ├── telemetry/         # Telemetry packet builder
│   │   ├── wire_protocol/     # Wire protocol types & CRC
│   │   ├── bootctl/           # OTA boot control
│   │   └── version/           # Firmware version defines
│   └── tools/
│       ├── idf                # Build/flash wrapper script (USE THIS!)
│       ├── local.env          # Local serial port config (not committed)
│       └── check_app_size.py  # Binary size checker
├── docs/                      # Protocol specs, hardware docs, handoffs
└── .github/workflows/         # CI/CD (firmware-build.yml)
```

---

## Build & Flash Commands

**IMPORTANT: Always use the wrapper script `./tools/idf` for all build operations!**

```bash
cd ~/Documents/GitHub/NuNuCryoShaker/firmware

# Build
./tools/idf main build              # Build main_app
./tools/idf recovery build          # Build recovery_factory

# Flash (requires device connected)
./tools/idf main flash              # Flash main_app to default partition
./tools/idf main install-ota0       # Write main_app to ota_0 (safer)
./tools/idf recovery flash          # Flash recovery to factory partition

# Monitor
./tools/idf main monitor            # Serial monitor for main_app
./tools/idf recovery monitor        # Serial monitor for recovery

# Clean
./tools/idf main fullclean          # Full clean of main_app build
./tools/idf main reconfigure        # Fullclean + regenerate sdkconfig

# Other
./tools/idf main menuconfig         # Open sdkconfig menu
./tools/idf main erase-flash        # Erase entire flash
```

### Serial Port Configuration

The wrapper reads `firmware/tools/local.env` for serial port settings:
```bash
ESPPORT=/dev/cu.usbmodem2101
ESPBAUD=115200
```

To find your ESP device:
```bash
ls /dev/cu.usb*
```

---

## Git Workflow & Best Practices

### Branch Strategy
- **main**: Stable, deployable code. Protected branch.
- **feature/***: Feature development (e.g., `feature/pause-resume`)
- **fix/***: Bug fixes
- **docs/***: Documentation-only changes

### Creating a Feature Branch
```bash
git checkout main
git pull origin main
git checkout -b feature/my-feature
```

### Commit Guidelines
- Commit frequently after each logical unit of work
- Use conventional commit format:
  - `feat(component): Add new feature`
  - `fix(component): Fix specific bug`
  - `docs: Update documentation`
  - `refactor(component): Refactor without behavior change`
- Push after each working state - don't accumulate local commits

### Creating a PR
```bash
git push -u origin feature/my-feature
gh pr create --title "feat: Description" --body "## Summary\n..."
```

### After PR Merge
```bash
git checkout main
git pull origin main
git branch -d feature/my-feature  # Delete local branch
```

---

## Key Files Reference

| Purpose | File |
|---------|------|
| BLE command handlers | `firmware/components/ble_gatt/ble_gatt.c` |
| Wire protocol types | `firmware/components/wire_protocol/include/wire_protocol.h` |
| State machine | `firmware/components/machine_state/machine_state.c` |
| Safety gates | `firmware/components/safety_gate/safety_gate.c` |
| PID polling | `firmware/components/pid_controller/pid_controller.c` |
| Relay control | `firmware/components/relay_ctrl/relay_ctrl.c` |
| Command catalog | `docs/90-command-catalog.md` |
| GATT UUIDs | `docs/80-gatt-uuids.md` |
| Hardware I/O map | `docs/APP_AGENT_HANDOFF.md` |

---

## Hardware I/O Mappings

### Digital Inputs (di_bits)
| DI | Bit | Function | Active State |
|---:|---:|----------|--------------|
| 1 | 0 | E-Stop | LOW = active (NC) |
| 2 | 1 | Door closed | HIGH = closed |
| 3 | 2 | LN2 present | HIGH = present |
| 4-8 | 3-7 | Unused | - |

### Relay Outputs (ro_bits)
| CH | Bit | Function |
|---:|---:|----------|
| 1 | 0 | Main contactor |
| 2 | 1 | Motor START |
| 3 | 2 | Heater 1 (Axle) |
| 4 | 3 | Heater 2 (Orbital) |
| 5 | 4 | LN2 valve |
| 6 | 5 | Door lock |
| 7 | 6 | Chamber light |

### PID Controllers
| ID | Address | Function |
|---:|--------:|----------|
| 1 | 1 | LN2/Cold (optional) |
| 2 | 2 | Axle bearings (required) |
| 3 | 3 | Orbital bearings (required) |

---

## Safety Gate Framework (v0.4.0)

### Capability Levels
- `NOT_PRESENT (0)`: Ignore completely
- `OPTIONAL (1)`: Faults warn but don't block
- `REQUIRED (2)`: Faults block operations

### Gate IDs
| ID | Gate | Can Bypass? |
|---:|------|-------------|
| 0 | E-Stop | **NO** |
| 1 | Door Closed | Yes |
| 2 | HMI Live | Yes |
| 3-5 | PID 1-3 Online | Yes |
| 6-8 | PID 1-3 No Probe Error | Yes |

### Key Safety Rules
- E-Stop gate can NEVER be bypassed
- Gate bypasses reset on reboot (do not persist)
- Capability levels persist to NVS

---

## Common Tasks

### Adding a New BLE Command
1. Add command ID to `wire_protocol.h`
2. Add case handler in `ble_gatt.c` `handle_command_write()`
3. Document in `docs/90-command-catalog.md`
4. Test with tablet app or nRF Connect

### Modifying State Machine
1. Update states/transitions in `machine_state.h`
2. Implement in `machine_state.c`
3. Update `docs/96-state-machine.md`
4. Test all affected transitions

### Adding a Safety Gate
1. Add gate ID to `safety_gate.h`
2. Implement check logic in `safety_gate.c`
3. Update `docs/APP_AGENT_HANDOFF.md`

---

## Troubleshooting

### Build Fails with "Dependencies lock doesn't exist"
```bash
rm -rf firmware/apps/main_app/build
rm -rf firmware/apps/main_app/managed_components
./tools/idf main build
```

### Git Corruption / Timeouts
The repo is in `~/Documents/GitHub/` which may sync with iCloud. If git operations timeout:
1. Wait for iCloud sync to complete
2. Or temporarily disable iCloud for Documents folder
3. As last resort, fresh clone and copy changes

### Flash Fails "No serial data received"
1. Hold BOOT button on ESP32-S3
2. Press and release RESET while holding BOOT
3. Release BOOT
4. Retry flash command

### Monitor Shows Garbled Output
Check baud rate matches (115200 default):
```bash
./tools/idf main monitor
```
Press Ctrl+] to exit monitor.

---

## Related Repositories

- **Android App**: `/Users/joshuaedwards/Downloads/claudeShakerControl`
  - Kotlin/Compose tablet UI
  - Separate Claude Code agent context

---

## CI/CD

GitHub Actions workflow (`.github/workflows/firmware-build.yml`):
- Triggers on push to main, dev, feature/* branches
- Builds both main_app and recovery_factory
- Uploads artifacts with firmware version
- Runs on `espressif/idf:v5.5.2` container

---

## Session Handoff Notes

When ending a session, update `docs/FIRMWARE_CHANGELOG.md` with:
- What was completed
- Any known issues
- Next steps

For complex in-progress work, create a handoff doc in `docs/SESSION_HANDOFF_YYYYMMDD.md`.
