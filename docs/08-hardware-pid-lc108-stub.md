# Hardware: PID Controllers (LC108) — Stub

This doc is a placeholder until the definitive LC108 manual + register map is added.

## 1. Inventory (current system)
- Qty: 3 controllers
- RS-485 network: multi-drop, ESP32 is Modbus RTU master
- Controller Modbus IDs:
  - PID #1: 1
  - PID #2: 2
  - PID #3: 3
  - Note: during bench testing, if only one is present, it may be configured as ID #3.

## 2. Serial settings (current)
- 9600 baud
- 8N1
- Polling strategy: asynchronous polling to reduce bus spam (implementation detail in firmware)

## 3. To be filled (when manual is attached)
- Register map (PV, SV, output %, mode bits, alarms)
- Write safety rules (what registers are safe to write while “running”)
- Device-specific timing constraints (minimum inter-frame delay, recommended poll rate)
- Scaling/units conventions (e.g., PV/SV scaling, decimal places)

## 4. Physical wiring notes (RS-485)
- Confirm termination:
  - Terminate at the physical ends of the bus only.
  - The ESP32-side termination strategy should be documented (jumper / fixed / external).
- Confirm reference/grounding strategy for the isolated RS-485 interface.

## 5. App relevance
The app should not speak Modbus directly in the MVP; it speaks BLE to the ESP32. PID pages will be “logical controls” that the ESP maps to Modbus reads/writes.
