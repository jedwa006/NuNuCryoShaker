# GATT UUIDs (Pinned)

This document pins the BLE UUIDs for the System Control Service and its characteristics.

## Policy: UUID stability
Once these UUIDs are published in firmware and used by an app, **do not change them**. Backward compatibility is handled by the framed wire protocol (`proto_ver`, `msg_type`, `payload_len`, etc.), not by changing UUIDs.

## Service + Characteristics

### Base Service: System Control Service
- **Service UUID**: `F0C5B4D2-3D1E-4A27-9B8A-2F0B3C4D5E60`

### Characteristics (within the service)
| Name | UUID | Properties | Usage |
|---|---|---|---|
| Device Info | `F0C5B4D2-3D1E-4A27-9B8A-2F0B3C4D5E61` | Read | Protocol version, firmware version, build id, capability flags |
| Telemetry Stream | `F0C5B4D2-3D1E-4A27-9B8A-2F0B3C4D5E62` | Notify | Periodic + change-driven status snapshots |
| Command RX | `F0C5B4D2-3D1E-4A27-9B8A-2F0B3C4D5E63` | Write, Write Without Response | App → ESP commands (framed) |
| Events + Acks | `F0C5B4D2-3D1E-4A27-9B8A-2F0B3C4D5E64` | Indicate, Notify | ESP → App events + command acknowledgements |
| Bulk Gateway (Future) | `F0C5B4D2-3D1E-4A27-9B8A-2F0B3C4D5E65` | Write, Notify | Future expansion: Modbus gateway, PID parameter tools, structured config |
| Diagnostic Log (Optional) | `F0C5B4D2-3D1E-4A27-9B8A-2F0B3C4D5E66` | Notify | Optional ASCII log stream for bring-up and field debugging |

## Standard Descriptors
- **CCCD** (Client Characteristic Configuration Descriptor): UUID `0x2902`
  - Present automatically for Notify/Indicate characteristics.
  - Client writes CCCD to enable notifications/indications.

## Advertising requirements (recommended)
- Device name prefix: `SYS-CTRL-<shortid>`
- Include the **service UUID** in advertising data if feasible (helps app filtering).

## Notes on placeholders in earlier docs
If `docs/20-gatt-schema.md` contains example UUIDs (e.g., `A0B0...` placeholders), replace them with the pinned UUIDs above.
