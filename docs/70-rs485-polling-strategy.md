# RS-485 Polling Strategy (3 controllers @ 9600 8N1)

## Context
The ESP32-S3 is RS-485 master on a small bus with 3 slave controllers:
- IDs: 1, 2, 3
- Typical test: only controller #3 present
- Serial: 9600 baud, 8N1
- Polling: asynchronous to reduce spam and avoid bus contention

## Goals
- Provide fresh PV/SV/OP% to the UI without saturating the bus
- Keep per-controller “age” visible so UI can show stale data gracefully
- Avoid coupling BLE update rate to RS-485 poll jitter

## Recommended approach
- Maintain a per-controller state struct:
  - latest PV/SV/OP/mode/alarms
  - timestamp_ms last_updated
  - comms status / error counters

- Poll schedule (example):
  - Round-robin each controller every 300–700 ms (tune)
  - If only controller #3 detected, poll it more frequently (e.g., 200–300 ms)
  - On errors, backoff and mark device stale

- Emit telemetry:
  - Baseline 10 Hz snapshot notify includes:
    - di_bits/ro_bits
    - alarm_bits
    - each controller block + age_ms

## Benefits
- BLE stays smooth and predictable at 10 Hz
- RS-485 is protected from “UI-driven” bursts
- UI can remain responsive even when one controller has comms issues
