# Firmware Bring-up & OTA Workflow

This repository contains two ESP-IDF projects under `firmware/apps` that share a common
partition table and configuration defaults. The recovery + OTA flow is working today and
**must remain stable**; the guidance below documents how to build, deploy, and verify it.

## Projects

- **`recovery_factory`** (`firmware/apps/recovery_factory`)
  - Factory recovery image that exposes a SoftAP + OTA portal for staging/activating firmware.
- **`main_app`** (`firmware/apps/main_app`)
  - Main firmware that runs in OTA slots (`ota_0`, `ota_1`) and can escape to recovery via
    BOOT long-press.

## Build (deterministic wrapper)

All builds should go through the wrapper so the sdkconfig defaults layering and partition
settings stay deterministic:

```bash
# Recovery factory image
./firmware/tools/idf recovery build

# Main application image
./firmware/tools/idf main build
```

If you need to fully regenerate `sdkconfig` from defaults (to avoid drift):

```bash
./firmware/tools/idf recovery reconfigure
./firmware/tools/idf main reconfigure
```

### Partition table intent

Partition table: `firmware/partitions/partitions_16mb_recovery_ota.csv`

- `factory` is the recovery/OTA portal (kept stable and never overwritten by OTA).
- `ota_0` and `ota_1` are the main firmware slots.
- `storage` is a persistent filesystem for settings/logs/assets.

Both projects pin the same partition table in their `CMakeLists.txt` and in
`sdkconfig.defaults.common` to avoid drift.

## OTA portal workflow (recovery_factory)

1. Connect to the SoftAP (`ESP32S3-RECOVERY`) and open `http://192.168.4.1/`.
2. **Stage**: Upload a firmware `.bin` to write into the next OTA slot.
3. **Status**: Use `/status` to confirm slot name, size, and SHA256.
4. **Activate**: Switches the boot partition to the staged slot and reboots.

The portal **never** writes over the factory/recovery partition.

## Main app workflow (ota slots)

- On boot, the main app calls `esp_ota_mark_app_valid_cancel_rollback()` to confirm the
  OTA slot is healthy (rollback-enabled flow).
- **BOOT long-press** (GPIO0, active-low) triggers a switch to the `factory` partition and
  reboots into recovery.
- The recovery portal can reboot back to the previous slot if the return label was stored
  in NVS.

> Note: A commented stub remains in `main_app` for a future dedicated GPIO. Do not change
> the BOOT long-press behavior without explicit approval.

## Security posture (current vs. hardened)

The OTA portal uses a **static token** (`OTA_TOKEN`) and static AP credentials
(`OTA_AP_SSID`, `OTA_AP_PASS`) in `recovery_factory/main/ota_portal.c`. This is a
local-maintenance placeholder and **not** hardened for production.

Hardening options (future work):
- Per-device tokens stored in NVS/secure storage.
- Rotate or disable the recovery AP in production.
- Add TLS via a local proxy or a secure provisioning channel.

## Gotchas & tips

- **NVS “no free pages”**: If you see `ESP_ERR_NVS_NO_FREE_PAGES`, erase NVS or use
  `reconfigure` to regenerate configs and flash cleanly.
- **Console selection**: Defaults use USB-Serial-JTAG (see `sdkconfig.defaults.common`).
  If you need GPIO UART, follow the commented block in that file.
- **Parttool usage**: `./firmware/tools/idf main install-ota0` or `install-ota1` writes
  the built `main_app.bin` directly to OTA slots without touching factory.

## References

- System docs: `/docs/README.md`
- Architecture overview: `/ARCHITECTURE.md`
