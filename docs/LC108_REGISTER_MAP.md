# LC108 / COM-800-C1 PID Controller Modbus Register Map

Reference for firmware Modbus RTU master implementation.

## Communication Settings
- **Baud**: 9600 (default, configurable via register 96)
- **Frame**: 8N1 (configurable via register 97)
- **Protocol**: Modbus RTU
- **Function Codes**: 0x03 (read holding), 0x06 (write single)

## Register Addressing
All registers are **1-based holding registers** (Modbus address = register - 1).

| Register | Name | Description | Scale | R/W |
|----------|------|-------------|-------|-----|
| 1 | PV | Process Value (measured temp) | x10 | R |
| 2 | MV1 | Output 1 % | x10 (0-1000 = 0-100%) | R |
| 3 | MV2 | Output 2 % | x10 | R |
| 4 | MVFB | Feedback % (valve position) | x10 | R |
| 5 | STATUS | Status/output bitfield | raw | R |
| 6 | SV | Main Setpoint | x10 | R/W |
| 8 | SV1 | Event SV1 | x10 | R/W |
| 9 | SV2 | Event SV2 | x10 | R/W |
| 10 | SV3 | Event SV3 | x10 | R/W |
| 11 | SV4 | Event SV4 | x10 | R/W |
| 13 | AT | Auto-tuning (0=OFF, 1=ON) | raw | R/W |
| 14 | MODE | Control mode | raw | R/W |
| 15 | AL1 | Alarm 1 setpoint | x10 | R/W |
| 16 | AL2 | Alarm 2 setpoint | x10 | R/W |

### PID Parameters (Group 1)
| Register | Name | Description | Scale | R/W |
|----------|------|-------------|-------|-----|
| 24 | SC | Input offset (PV offset) | x10 | R/W |
| 25 | P1 | Proportional gain | x10 | R/W |
| 26 | I1 | Integral time (seconds) | raw | R/W |
| 27 | D1 | Derivative time (seconds) | raw | R/W |
| 30 | CYT1 | Cycle time (seconds) | raw | R/W |
| 31 | HYS1 | Hysteresis | x10 | R/W |
| 32 | RST1 | Reset/offset | x10 | R/W |
| 33 | OPL1 | Output lower limit % | x10 | R/W |
| 34 | OPH1 | Output upper limit % | x10 | R/W |
| 35 | BUF1 | Soft-start % | x10 | R/W |

### Input Configuration
| Register | Name | Description | Scale | R/W |
|----------|------|-------------|-------|-----|
| 66 | INP1 | Input type (TC/RTD/analog) | raw | R/W |
| 67 | DP | Decimal point/resolution | raw | R/W |
| 68 | UNIT | Temperature unit (0=C, 1=F) | raw | R/W |
| 69 | LSPL | SV lower limit | x10 | R/W |
| 70 | USPL | SV upper limit | x10 | R/W |
| 71 | PVOS | Extra PV offset | x10 | R/W |
| 72 | PVFT | PV filter (0-60) | raw | R/W |

### Alarm Configuration
| Register | Name | Description | Scale | R/W |
|----------|------|-------------|-------|-----|
| 77 | ALD1 | Alarm 1 mode (0-23) | raw | R/W |
| 78 | AH1 | Alarm 1 hysteresis | x10 | R/W |
| 79 | ALT1 | Alarm 1 delay (seconds) | raw | R/W |
| 80 | ALD2 | Alarm 2 mode (0-23) | raw | R/W |
| 81 | AH2 | Alarm 2 hysteresis | x10 | R/W |
| 82 | ALT2 | Alarm 2 delay (seconds) | raw | R/W |

### Communication Settings
| Register | Name | Description | Values | R/W |
|----------|------|-------------|--------|-----|
| 95 | IDNO | Modbus slave address | 1-247 | R/W |
| 96 | BAUD | Baud rate index | 0=2400, 1=4800, 2=9600, 3=19200 | R/W |
| 97 | UCR | Parity/framing | 0=8N1, 1=8O1, 2=8E1 | R/W |

## Scaling Functions

```c
// Temperature: register value / 10.0
float decode_temp(int16_t raw) { return raw / 10.0f; }
int16_t encode_temp(float temp) { return (int16_t)(temp * 10.0f + 0.5f); }

// Percent: 0-1000 maps to 0.0-100.0%
float decode_percent(int16_t raw) { return raw / 10.0f; }
int16_t encode_percent(float pct) { return (int16_t)(pct * 10.0f + 0.5f); }
```

## Polling Strategy
For 3 controllers on the bus:
- Poll each controller every 300-500ms (round-robin)
- Read registers 1-6 in single request (PV, MV1, MV2, MVFB, STATUS, SV)
- Track last_updated_ms per controller for staleness detection
- On timeout/CRC error: mark stale, increment error counter, backoff

## Hardware Notes
- ESP32-S3-ETH-8DI-8RO RS-485: GPIO17 (TX), GPIO18 (RX)
- Half-duplex: DE/RE control may be auto or GPIO46
- Controller addresses: 1, 2, 3 (bench test may only have #3)
