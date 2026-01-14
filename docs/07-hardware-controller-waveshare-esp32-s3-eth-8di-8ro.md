# Hardware: Controller (Waveshare ESP32-S3-ETH-8DI-8RO / PoE Variant)

## 1. What this board provides
Waveshare describes the ESP32-S3-ETH-8DI-8RO as an industrial 8-channel relay controller with Wi-Fi, BLE, RS-485, and Ethernet, plus isolation and a wide input voltage range. Relay contact rating is listed as ≤10A @ 250VAC or ≤10A @ 30VDC, and supply input as 7–36V (or 5V via USB-C). The board uses optocoupler/digital/power isolation and includes status indicators.  

## 2. Major onboard components (relevant to firmware expectations)
- **TCA9554PWR** I/O expander used for relay control.
- **W5500** Ethernet controller (10/100) connected via SPI.

These are important because they explain why “relay state” is not simply a direct ESP32 GPIO.

## 3. Interfaces and pin mappings (from Waveshare)
### Digital inputs (8-ch)
- DI1..DI8 detection pins:
  - GPIO4, GPIO5, GPIO6, GPIO7, GPIO8, GPIO9, GPIO10, GPIO11

### RS-485
- RS-485 TX: **GPIO17** (UART TX)
- RS-485 RX: **GPIO18** (UART RX)

### Ethernet (W5500 SPI)
- GPIO12 ETH_INT
- GPIO13 ETH_MOSI
- GPIO14 ETH_MISO
- GPIO15 ETH_SCLK
- GPIO16 ETH_CS

### Boot/diagnostic signals
- BOOT button: **GPIO0**
- RGB LED: GPIO38
- Buzzer: GPIO46

## 4. Implications for the app + BLE model
- The app should treat “relay outputs” and “digital inputs” as logical channels (CH1..CH8), irrespective of how firmware reads them (GPIO vs I/O expander).
- Firmware should be responsible for debouncing/filtering DI lines (if needed) and mapping raw I/O into stable “status bits” exported over BLE.

## 5. Reference diagrams (Waveshare)
If you want a canonical picture of the internal architecture and I/O mapping, Waveshare publishes:
- annotated board resources photo
- linkage/control diagram
- logic diagram

(links included in `docs/05-hardware-overview.md`)

## References
- Waveshare wiki: features, electrical specs, components, and pin mappings.
