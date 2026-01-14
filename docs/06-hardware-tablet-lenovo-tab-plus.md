# Hardware: Tablet (Lenovo Tab Plus)

## 1. Device identification
- **Device**: Lenovo Tab Plus  
- **Model**: ZADX0091SE  
- **Role**: Primary local HMI (touch UI) over BLE to the ESP32-S3 controller

## 2. Key specifications (UI layout + BLE assumptions)

### Display
- **Size**: 11.5"
- **Resolution**: 2000 × 1200 (2K)
- **Refresh rate**: 90 Hz
- **Pixel density**: 204 PPI

### Wireless connectivity
- **Wi-Fi**: Wi-Fi 5 (802.11ac 1×1)
- **Bluetooth**: Bluetooth 5.2 (BLE-capable)

### OS
- **Android**: Android 14 or later (confirm exact version on your unit)

## 3. “Fill-me” fields (lock these from your device)
Capture these from **Settings → About tablet** (and paste into this file for permanence):

- Android version:
- Security patch level:
- Build number:
- BLE / Bluetooth version (as reported):
- Developer options enabled? (Y/N):
- Display scaling / font scaling:
- Any kiosk / lock-task requirements:

Optional (if you can use ADB):
- `adb shell wm size`:
- `adb shell wm density`:

## 4. UI implications (practical guidance)

### Layout constraints for 2000×1200
- Aspect ratio is approximately **5:3** (landscape-first).
- Prefer “panel” layouts: a persistent status bar + large tiles/cards.
- Use large typography for PV/SV/OP% blocks; avoid dense tables for primary ops.

### Touch targets
- Minimum touch target sizing: **48 dp** or larger for primary controls.
- Avoid tight spacing between adjacent toggles (gloves and fast taps happen).

### Input behavior
- Prefer native numeric entry (no custom keypad required for v0):
  - **Kotlin/Compose**: numeric keyboard (`KeyboardType.Number`)
  - **Flutter**: `TextInputType.number` + input formatters
- Validate before send (min/max, units) and show “Pending / Confirmed / Unconfirmed” states.

### Performance notes
- Telemetry target of ~10 Hz is appropriate, but avoid visibly “flickering” UI:
  - Update values, but don’t animate every tick.
  - Keep expensive layouts stable (avoid rebuilding whole screen on each sample).

## 5. Connectivity test checklist (tablet-specific)

### BLE behavior validation
- App reaches **LIVE** state reliably after cold start.
- Reconnect works after:
  - Screen off/on
  - App background/foreground
  - Leaving BLE range and returning
  - ESP reboot

### Operator-mode validation
- Confirm whether kiosk/lock-task is desired:
  - If yes, document the target kiosk approach (Android pinned app / dedicated kiosk launcher).

## 6. References
This file should reference (or attach) the authoritative Lenovo spec sheet used to confirm:
- display: 11.5" 2000×1200, 90 Hz, 204 PPI
- wireless: Wi-Fi 5 + Bluetooth 5.2
- OS: Android 14+

(Keep the source document link here once you’ve saved it in the repo or pinned it in notes.)
