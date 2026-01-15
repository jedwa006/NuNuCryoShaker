# Firmware

This directory contains two ESP-IDF projects:

- `apps/recovery_factory`: the factory recovery/OTA portal image that stays in the factory slot.
- `apps/main_app`: the normal application image that runs from OTA slots.

For broader system context and hardware notes, see the top-level [`/docs`](../docs) directory.

## Build + flash with `./firmware/tools/idf`

Use the wrapper script so ESP-IDF setup, partition tooling, and serial defaults are consistent.

### Recovery (factory) project

```bash
./firmware/tools/idf recovery build
./firmware/tools/idf recovery flash -p /dev/ttyUSB0
./firmware/tools/idf recovery monitor -p /dev/ttyUSB0
```

### Main app project

```bash
./firmware/tools/idf main build
./firmware/tools/idf main flash -p /dev/ttyUSB0
./firmware/tools/idf main monitor -p /dev/ttyUSB0
```

### Helpful wrapper commands

```bash
# Force sdkconfig regeneration from defaults (handy after changing console options)
./firmware/tools/idf recovery reconfigure
./firmware/tools/idf main reconfigure

# Install the main app into a specific OTA slot (uses parttool under the hood)
./firmware/tools/idf main install-ota0 -p /dev/ttyUSB0
./firmware/tools/idf main install-ota1 -p /dev/ttyUSB0
```

The wrapper can read `ESPPORT` and `ESPBAUD` from `firmware/tools/local.env`, or you can pass `-p` directly to each command. The OTA installers (`install-ota0`/`install-ota1`) use `parttool.py` and never overwrite the factory recovery partition.

## Partition map intent

The custom partition table is designed for recovery-first OTA workflows:

- **factory**: stable recovery image (the OTA portal).
- **ota_0 / ota_1**: primary application slots for staged OTA updates.
- **storage**: LittleFS data partition for settings/logs/assets.

See `firmware/partitions/partitions_16mb_recovery_ota.csv` for the exact layout.

## OTA portal workflow (recovery image)

The recovery portal hosts a local OTA UI and API. The high-level flow is:

1. **Stage**: Upload a `.bin` to `/stage`, which writes the image into the next OTA slot without switching boot partitions.
2. **Status**: Query `/status` to confirm which slot is staged and how much was written.
3. **Activate**: Call `/activate` to switch the boot partition to the staged slot and reboot.

This portal explicitly avoids touching the factory partition, so recovery remains available.

## Security note (recovery OTA portal defaults)

The recovery portal ships with static token and AP credentials intended only as local-maintenance placeholders. These constants live in `firmware/apps/recovery_factory/main/ota_portal.c` (see the token/AP SSID and password definitions near the portal config). For production deployments, replace them with hardened options such as unique per-device tokens, rotating WPA2-PSK credentials, disabling the captive portal/UI once provisioning is complete, and/or placing the portal behind a TLS-terminating proxy to avoid exposing plaintext endpoints.

## Main app workflow

- On boot, the main app calls `esp_ota_mark_app_valid_cancel_rollback()` to mark the OTA image as valid when rollback is enabled.
- A long-press of the **BOOT** button (GPIO0, active-low) triggers a switch back to the factory recovery partition.

## Common gotchas

- **NVS “no free pages”**: both images handle `ESP_ERR_NVS_NO_FREE_PAGES` by erasing and reinitializing NVS, but if you see it during bring-up, expect a full NVS erase on next boot.
- **Serial console selection**: use `./firmware/tools/idf <app> reconfigure` to regenerate `sdkconfig` from defaults if console or UART settings drift.
- **Parttool usage**: `install-ota0`/`install-ota1` are the safe path for programming OTA slots without overwriting factory recovery; they require a serial port (`-p` or `ESPPORT`).
