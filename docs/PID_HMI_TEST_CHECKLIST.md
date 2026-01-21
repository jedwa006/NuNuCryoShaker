# PID/HMI Integration Test Checklist

**Firmware Branch**: `feature/pid-hmi-integration`
**Firmware Version**: 0.3.0 (build 0x26012001)
**Flashed to**: ota_0 partition
**Date**: 2026-01-20

## Reference Documentation

| Document | Path | Description |
|----------|------|-------------|
| App Agent Handoff | [docs/APP_AGENT_HANDOFF.md](docs/APP_AGENT_HANDOFF.md) | Complete PID command reference for app integration |
| Wire Protocol | [docs/90-command-catalog.md](docs/90-command-catalog.md) | Frame format, CRC, all command/event definitions |
| GATT UUIDs | [docs/80-gatt-uuids.md](docs/80-gatt-uuids.md) | BLE service and characteristic UUIDs |
| State Machine | [docs/96-state-machine.md](docs/96-state-machine.md) | Machine state transitions |

## Firmware Source Files

| Component | Path |
|-----------|------|
| PID Controller | [firmware/components/pid_controller/](firmware/components/pid_controller/) |
| BLE GATT | [firmware/components/ble_gatt/ble_gatt.c](firmware/components/ble_gatt/ble_gatt.c) |
| Wire Protocol | [firmware/components/wire_protocol/include/wire_protocol.h](firmware/components/wire_protocol/include/wire_protocol.h) |
| Telemetry | [firmware/components/telemetry/telemetry.c](firmware/components/telemetry/telemetry.c) |

---

## Test Categories

### 1. BLE Connection & Telemetry

- [ ] **Connect to device** - Scan for "NuNu" device, connect via BLE
- [ ] **Subscribe to Telemetry** - Enable notifications on `...5E62`
- [ ] **Verify telemetry stream** - Should receive frames at ~10 Hz
- [ ] **Parse controller data** - Extract PV, SV, output%, mode, age_ms for each controller
- [ ] **Monitor alarm_bits** - Check bits 6-8 for per-controller fault status

### 2. Device Info

- [ ] **Read Device Info** - Read characteristic `...5E61`
- [ ] **Verify proto_ver** - Should be 0x01
- [ ] **Parse firmware version** - Extract major.minor.patch
- [ ] **Check capabilities** - Verify CAP_SUPPORTS_SESSION_LEASE (bit 0)

### 3. Session Management

- [ ] **OPEN_SESSION (0x0100)** - Send with client_nonce, receive session_id + lease_ms
- [ ] **KEEPALIVE (0x0101)** - Send periodically before lease expires
- [ ] **Verify session expiry** - Stop keepalives, verify HMI_NOT_LIVE alarm (bit 5) appears

### 4. PID Setpoint Control (CMD_SET_SV - 0x0020)

- [ ] **Set SV on controller 1** - Send `[0x01, sv_low, sv_high]` with desired temp×10
- [ ] **Verify ACK OK** - status=0x00
- [ ] **Verify telemetry update** - New SV appears in next telemetry frames
- [ ] **Test invalid controller_id** - Send controller_id=4, expect status=0x02 (INVALID_ARGS)
- [ ] **Test controller offline** - If controller offline, expect status=0x06 (TIMEOUT)

### 5. Controller Mode Control (CMD_SET_MODE - 0x0021)

- [ ] **Set mode to AUTO (2)** - Send `[controller_id, 0x02]`
- [ ] **Verify ACK OK**
- [ ] **Set mode to STOP (0)** - Send `[controller_id, 0x00]`
- [ ] **Verify telemetry** - Mode field updates
- [ ] **Test invalid mode** - Send mode=5, expect status=0x02

### 6. Force Poll (CMD_REQUEST_PV_SV_REFRESH - 0x0022)

- [ ] **Request refresh** - Send `[controller_id]`
- [ ] **Verify ACK OK**
- [ ] **Check age_ms** - Next telemetry should show low age_ms for that controller

### 7. PID Parameter Read/Write

#### Read PID Params (CMD_READ_PID_PARAMS - 0x0024)
- [ ] **Send read request** - `[controller_id]`
- [ ] **Parse ACK optional data** - Extract P_x10, I_time, D_time
- [ ] **Display in UI** - Show P, I, D values

#### Write PID Params (CMD_SET_PID_PARAMS - 0x0023)
- [ ] **Send write request** - `[controller_id, p_x10_low, p_x10_high, i_low, i_high, d_low, d_high]`
- [ ] **Verify ACK OK**
- [ ] **Read back** - Use READ_PID_PARAMS to verify values persisted

### 8. Auto-Tune Control

#### Start Auto-Tune (CMD_START_AUTOTUNE - 0x0025)
- [ ] **Send start command** - `[controller_id]`
- [ ] **Verify ACK OK**
- [ ] **Monitor status** - Check autotune status in controller polling

#### Stop Auto-Tune (CMD_STOP_AUTOTUNE - 0x0026)
- [ ] **Send stop command** - `[controller_id]`
- [ ] **Verify ACK OK**

### 9. Alarm Limits

#### Read Alarm Limits (CMD_READ_ALARM_LIMITS - 0x0028)
- [ ] **Send read request** - `[controller_id]`
- [ ] **Parse ACK optional data** - Extract AL1_x10, AL2_x10

#### Write Alarm Limits (CMD_SET_ALARM_LIMITS - 0x0027)
- [ ] **Send write request** - `[controller_id, al1_low, al1_high, al2_low, al2_high]`
- [ ] **Verify ACK OK**
- [ ] **Read back** - Verify values persisted

### 10. Error Handling

- [ ] **Session expired** - Send PID command without valid session, expect status=0x01
- [ ] **Invalid controller ID** - Send controller_id=0 or >3, expect status=0x02, detail=0x0005
- [ ] **Controller offline** - Send command to offline controller, expect status=0x06, detail=0x0004
- [ ] **Module not ready** - If PID module not init, expect status=0x05

---

## Telemetry Parsing Reference

### Controller Data Structure (10 bytes per controller)

```
Offset  Size  Field           Scaling
0       1     controller_id   1-3
1-2     2     pv_x10          ÷10 for °C
3-4     2     sv_x10          ÷10 for °C
5-6     2     op_x10          ÷10 for %
7       1     mode            0-3
8-9     2     age_ms          freshness in ms
```

### Alarm Bits (u32)

| Bit | Name | Check |
|-----|------|-------|
| 5 | HMI_NOT_LIVE | Session expired |
| 6 | PID1_FAULT | Controller 1 offline/alarm |
| 7 | PID2_FAULT | Controller 2 offline/alarm |
| 8 | PID3_FAULT | Controller 3 offline/alarm |

---

## Command Quick Reference

| cmd_id | Name | Payload |
|--------|------|---------|
| 0x0020 | SET_SV | controller_id(u8), sv_x10(i16) |
| 0x0021 | SET_MODE | controller_id(u8), mode(u8) |
| 0x0022 | REQUEST_PV_SV_REFRESH | controller_id(u8) |
| 0x0023 | SET_PID_PARAMS | controller_id(u8), p_x10(i16), i_time(u16), d_time(u16) |
| 0x0024 | READ_PID_PARAMS | controller_id(u8) |
| 0x0025 | START_AUTOTUNE | controller_id(u8) |
| 0x0026 | STOP_AUTOTUNE | controller_id(u8) |
| 0x0027 | SET_ALARM_LIMITS | controller_id(u8), al1_x10(i16), al2_x10(i16) |
| 0x0028 | READ_ALARM_LIMITS | controller_id(u8) |

---

## Notes for App Agent

1. **All multi-byte values are little-endian**
2. **Temperature values are scaled ×10** (e.g., -150.0°C = -1500 as i16)
3. **Controller IDs are 1-based** (1, 2, or 3)
4. **Session required** - Most commands need an active session (OPEN_SESSION first)
5. **ACK optional data** - READ commands return data in the ACK's optional_data field
6. **Force poll after write** - Call REQUEST_PV_SV_REFRESH after SET_SV for immediate UI update
