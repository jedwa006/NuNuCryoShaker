# Firmware Development Roadmap

## Current State (2026-01-19)
- ✅ Core BLE GATT server with custom wire protocol
- ✅ Session management with heartbeat/lease mechanism
- ✅ Relay control via TCA9554 I2C expander (8 relays)
- ✅ Telemetry streaming at 10Hz
- ✅ Boot control with OTA partition management
- ✅ RGB LED status indicator (status_led component)
- ✅ Recovery/Factory OTA portal via WiFi AP

## Next Steps

### Phase 1: RS-485/Modbus Integration (High Priority)
**Context**: 3x PID controllers (LC108) are now connected to RS-485 bus

1. **RS-485 Driver Setup**
   - Configure UART2 for RS-485 with direction control GPIO
   - Implement half-duplex TX/RX switching
   - Define baud rate, parity, stop bits per LC108 specs

2. **Modbus RTU Master Implementation**
   - Create `modbus_master` component
   - Implement CRC-16 calculation
   - Support function codes: 0x03 (read holding), 0x06 (write single), 0x10 (write multiple)
   - Polling scheduler with configurable intervals

3. **LC108 PID Controller Integration**
   - Create `pid_controller` component with LC108 register map
   - Read: PV (process value), SV (setpoint), output %, alarm status
   - Write: SV, PID parameters, control mode
   - Handle 3 devices on bus (addresses TBD)

4. **BLE Command Extension**
   - Add commands: READ_PID_STATUS, WRITE_PID_SETPOINT, READ_PID_CONFIG
   - Extend telemetry to include PID data
   - Consider dedicated PID telemetry characteristic

### Phase 2: App ↔ Firmware Collaboration

**Coordination Strategy for Dual-Agent Development:**

| Step | Firmware Agent | App Agent |
|------|---------------|-----------|
| 1 | Define wire protocol for PID commands | Wait |
| 2 | Implement commands, test with CLI/Python | Wait |
| 3 | Document command format & examples | Review docs |
| 4 | Provide test fixtures | Implement BLE service calls |
| 5 | Support integration testing | Test against firmware |

**Shared Artifacts:**
- `docs/90-command-catalog.md` - Command definitions (firmware maintains)
- `docs/PID_PROTOCOL.md` - PID-specific command specs (new)
- Test harness scripts in `firmware/tools/`

### Phase 3: Enhanced Status LED
1. Add LED states for RS-485/Modbus activity
2. LED_STATE_MODBUS_TX - brief flash on transmit
3. LED_STATE_MODBUS_ERROR - red blink on CRC/timeout
4. LED_STATE_PID_ALARM - yellow breathing when PID alarm active

### Phase 4: Production Hardening
1. Watchdog integration with status LED feedback
2. Error logging to NVS for post-mortem analysis
3. Configurable telemetry rate via BLE command
4. Power-on self-test (POST) sequence

## Known Issues
- Git repository has pack file corruption - recommend fresh clone
- main_app build may show "file truncated" linker error - fullclean fixes it

## Component Dependencies

```
main_app
├── ble_gatt (NimBLE)
│   └── status_led
├── session_mgr
│   └── wire_protocol
├── telemetry
├── relay_ctrl (I2C → TCA9554)
├── bootctl
└── [NEW] modbus_master
    └── pid_controller (LC108 x3)
```

## RS-485 Hardware Notes
- ESP32-S3-ETH-8DI-8RO has onboard RS-485 transceiver with isolation
- **TX GPIO: GPIO17** (UART TX)
- **RX GPIO: GPIO18** (UART RX)
- DE/RE GPIO: Likely auto-controlled by transceiver or GPIO46 (buzzer shares, needs verify)
- LC108 default baud: 9600, 8N1
- Modbus addresses: 1, 2, 3 (during bench test, only #3 may be present)
- See `docs/70-rs485-polling-strategy.md` for polling design
- See `docs/08-hardware-pid-lc108-stub.md` for controller details (needs register map)

## Collaboration Checkpoints

### Checkpoint A: PID Wire Protocol Review
- Firmware: Draft command definitions
- App: Review for UI integration needs
- Output: Approved PID_PROTOCOL.md

### Checkpoint B: Mock Data Testing
- Firmware: Implement commands with mock PID data
- App: Test BLE integration against mock responses
- Output: Basic end-to-end working

### Checkpoint C: Real Hardware Integration
- Firmware: Connect to real LC108 controllers
- App: Full UI for PID monitoring/control
- Output: MVP PID feature complete
