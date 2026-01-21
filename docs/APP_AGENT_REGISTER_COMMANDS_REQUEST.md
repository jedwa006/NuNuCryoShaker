# Firmware Request: Implement READ_REGISTERS and WRITE_REGISTER Commands

**Date**: 2026-01-20
**From**: App Agent
**Priority**: Required for Register Editor feature

---

## Summary

The Android app has a new Register Editor screen that allows users to read and modify any Modbus register on the LC108/FT200 PID controllers via the tablet. This requires two new BLE commands to be implemented in firmware:

1. **READ_REGISTERS (0x0030)** - Read one or more consecutive registers
2. **WRITE_REGISTER (0x0031)** - Write a single register

---

## Why This Is Needed

The Register Editor allows technicians to:
- View and modify PID tuning parameters (P, I, D) without opening the equipment enclosure
- Adjust setpoint limits, alarm thresholds, and other configuration
- Debug controller behavior by reading status registers
- All from the wireless tablet via BLE

The app UI is complete and waiting for these firmware commands.

---

## Detailed Specification

Full command specifications are in:
**`/Users/joshuaedwards/Downloads/claudeShakerControl/docs/MCU_docs/FIRMWARE_REGISTER_COMMANDS_HANDOFF.md`**

### Quick Reference

**READ_REGISTERS (0x0030)**
- Request: `[controller_id(u8), start_address(u16 LE), count(u8)]` (4 bytes)
- Response: `[controller_id(u8), start_address(u16 LE), count(u8), values(u16 LE array)]`
- Supports reading 1-16 consecutive registers in one command

**WRITE_REGISTER (0x0031)**
- Request: `[controller_id(u8), address(u16 LE), value(u16 LE)]` (5 bytes)
- Response: `[controller_id(u8), address(u16 LE), value(u16 LE)]` (echo with verified value)
- Should read-back after write to verify the value took effect

---

## Firmware Files to Modify

| File | Changes Needed |
|------|----------------|
| `/Users/joshuaedwards/Documents/GitHub/NuNuCryoShaker/firmware/components/wire_protocol/include/wire_protocol.h` | Add CMD_READ_REGISTERS (0x0030) and CMD_WRITE_REGISTER (0x0031) defines |
| `/Users/joshuaedwards/Documents/GitHub/NuNuCryoShaker/firmware/components/ble_gatt/ble_gatt.c` | Add case handlers in command dispatch |
| `/Users/joshuaedwards/Documents/GitHub/NuNuCryoShaker/firmware/components/pid_controller/pid_controller.c` | May need to add/expose register read/write functions |
| `/Users/joshuaedwards/Documents/GitHub/NuNuCryoShaker/docs/90-command-catalog.md` | Document the new commands |

---

## App-Side Implementation (Already Complete)

| Component | Path |
|-----------|------|
| Command IDs | `/Users/joshuaedwards/Downloads/claudeShakerControl/app/src/main/kotlin/com/shakercontrol/app/data/ble/BleConstants.kt` (lines 73-75) |
| Repository Interface | `/Users/joshuaedwards/Downloads/claudeShakerControl/app/src/main/kotlin/com/shakercontrol/app/data/repository/MachineRepository.kt` (lines 104-120) |
| BLE Implementation | `/Users/joshuaedwards/Downloads/claudeShakerControl/app/src/main/kotlin/com/shakercontrol/app/data/repository/BleMachineRepository.kt` |
| Register Catalog | `/Users/joshuaedwards/Downloads/claudeShakerControl/app/src/main/kotlin/com/shakercontrol/app/domain/model/ModbusRegister.kt` |
| ViewModel | `/Users/joshuaedwards/Downloads/claudeShakerControl/app/src/main/kotlin/com/shakercontrol/app/ui/registers/RegisterEditorViewModel.kt` |
| UI Screen | `/Users/joshuaedwards/Downloads/claudeShakerControl/app/src/main/kotlin/com/shakercontrol/app/ui/registers/RegisterEditorScreen.kt` |

**Deep Links for Testing:**
```bash
adb shell am start -a android.intent.action.VIEW -d "shaker://registers/1" com.shakercontrol.app.debug
adb shell am start -a android.intent.action.VIEW -d "shaker://registers/2" com.shakercontrol.app.debug
adb shell am start -a android.intent.action.VIEW -d "shaker://registers/3" com.shakercontrol.app.debug
```

---

## Testing Checklist

Once implemented, please verify:

- [ ] READ_REGISTERS returns correct values for SV (addr 4), PV (addr 0), MODE (addr 13)
- [ ] READ_REGISTERS with count=3 returns 3 consecutive register values
- [ ] READ_REGISTERS returns TIMEOUT_DOWNSTREAM for offline controller
- [ ] WRITE_REGISTER successfully changes SV register
- [ ] WRITE_REGISTER read-back verification catches failed writes
- [ ] Both commands work without interfering with telemetry polling

---

## Error Codes to Use

| Status | Code | When to Use |
|--------|------|-------------|
| OK | 0x00 | Success |
| INVALID_ARGS | 0x02 | Bad controller_id (not 1-3), count > 16, or protected register |
| NOT_READY | 0x05 | PID module not initialized |
| TIMEOUT_DOWNSTREAM | 0x06 | RS-485 timeout (detail=0x0004 CONTROLLER_OFFLINE) |
| HW_FAULT | 0x04 | Write succeeded but read-back verification failed |

---

## Notes

- The app expects little-endian byte order for all multi-byte values
- Consider protecting RS-485 communication registers (addresses 49-51) from writes
- Read-after-write verification is important for catching silent controller rejections
- These commands should work alongside the existing telemetry polling without bus contention

Let me know when implemented and I'll test the full flow!