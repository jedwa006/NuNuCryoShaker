# App Agent Handoff - Firmware Version Display

**Date**: 2026-01-19
**Firmware Branch**: `feature/firmware-led-and-components` (ready for PR to main)
**Firmware Version**: 0.2.0 (build 0x26011901)

## Summary

The firmware now has centralized version management. The app can read the firmware version via BLE to display it in the UI (e.g., settings screen, about page, or connection status).

## BLE Device Info Characteristic

**Characteristic UUID**: `F0C5B4D2-3D1E-4A27-9B8A-2F0B3C4D5E61`
**Properties**: Read
**Size**: 12 bytes

### Payload Format (little-endian)

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 1 | proto_ver | Wire protocol version (currently 0x01) |
| 1 | 1 | fw_major | Firmware major version |
| 2 | 1 | fw_minor | Firmware minor version |
| 3 | 1 | fw_patch | Firmware patch version |
| 4-7 | 4 | build_id | Build ID (little-endian u32) |
| 8-11 | 4 | cap_bits | Capability flags (little-endian u32) |

### Example Response

For firmware 0.2.0 (build 0x26011901):
```
[0x01, 0x00, 0x02, 0x00, 0x01, 0x19, 0x01, 0x26, 0x01, 0x00, 0x00, 0x00]
  │     │     │     │     └──────────────────┘     └──────────────────┘
  │     │     │     │           build_id              capability bits
  │     │     │     └── patch (0)
  │     │     └── minor (2)
  │     └── major (0)
  └── proto_ver (1)
```

### Parsing Code (Dart/Flutter example)

```dart
class FirmwareInfo {
  final int protoVersion;
  final String version;  // "0.2.0"
  final int buildId;     // 0x26011901
  final int capabilities;

  FirmwareInfo.fromBytes(List<int> data) :
    protoVersion = data[0],
    version = '${data[1]}.${data[2]}.${data[3]}',
    buildId = data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24),
    capabilities = data[8] | (data[9] << 8) | (data[10] << 16) | (data[11] << 24);

  String get buildIdHex => buildId.toRadixString(16).padLeft(8, '0');
  String get fullVersion => '$version+$buildIdHex';  // "0.2.0+26011901"
}
```

### Capability Flags

| Bit | Flag | Description |
|-----|------|-------------|
| 0 | CAP_SUPPORTS_SESSION_LEASE | Session/heartbeat mechanism (enabled) |
| 1 | CAP_SUPPORTS_EVENT_LOG | Event logging (future) |
| 2 | CAP_SUPPORTS_BULK_GATEWAY | Bulk data transfer (future) |
| 3 | CAP_SUPPORTS_MODBUS_TOOLS | Modbus debugging tools (future) |
| 4 | CAP_SUPPORTS_PID_TUNING | PID parameter tuning (future) |
| 5 | CAP_SUPPORTS_OTA | OTA update support (future) |

## Integration Tasks for App

1. **Read Device Info on Connect**
   - After BLE connection established, read the Device Info characteristic
   - Parse the 12-byte response per format above

2. **Display Firmware Version**
   - Settings/About screen: "Firmware: 0.2.0"
   - Optional: Show build ID for debugging: "0.2.0+26011901"

3. **Connection Status Enhancement** (optional)
   - Show version in connection status area
   - Could help users identify if firmware needs update

4. **Version Compatibility Check** (future)
   - If proto_ver doesn't match expected, warn user
   - Could prompt for firmware update if version too old

## Reference Files

- Firmware version header: `firmware/components/version/include/fw_version.h`
- BLE GATT implementation: `firmware/components/ble_gatt/ble_gatt.c` (lines 71-82)
- BLE characteristic UUIDs: `firmware/components/ble_gatt/include/ble_gatt.h`
- Wire protocol spec: `docs/90-command-catalog.md`

## Testing

After flashing updated firmware:
1. Connect from app
2. Read characteristic `...5E61`
3. Verify response parses to version 0.2.0, build 0x26011901

## Notes

- The firmware must be reflashed for version 0.2.0 to be reported
- Previous firmware reported 0.1.0 (build 0x00000001)
- Build ID format is `0xYYMMDDNN` (year, month, day, build number)
