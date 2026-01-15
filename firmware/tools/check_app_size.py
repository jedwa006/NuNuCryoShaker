#!/usr/bin/env python3
import os, sys

OTA_SLOT_BYTES = 0x4C0000
SAFETY_MARGIN  = 256 * 1024
MAX_BYTES      = OTA_SLOT_BYTES - SAFETY_MARGIN

bin_path = sys.argv[1] if len(sys.argv) > 1 else "build/main_app.bin"

size = os.path.getsize(bin_path)
print(f"App binary: {size} bytes")
print(f"Budget:     {MAX_BYTES} bytes (slot {OTA_SLOT_BYTES} - margin {SAFETY_MARGIN})")

if size > MAX_BYTES:
    print("ERROR: app binary exceeds OTA slot budget.")
    sys.exit(2)

print("OK: app binary within OTA slot budget.")
