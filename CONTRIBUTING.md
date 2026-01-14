# Contributing Guide

This project is a safety-adjacent instrument control system. Contributions should prioritize determinism, testability, and traceability.

## Ground rules
- Prefer small, reviewable changes over large refactors.
- Do not change published BLE UUIDs after release. UUIDs are pinned in `docs/80-gatt-uuids.md`.
- Maintain backward compatibility via the framed protocol (proto versioning, message types, payload length guards).

## Development flow
1. Create a feature branch from `main`:
   - `feature/<short-topic>`
   - `fix/<short-topic>`
   - `docs/<short-topic>`
2. Make changes with clear commits.
3. Update documentation alongside code changes (protocol/GATT changes require doc updates).
4. Submit a PR with:
   - Summary of change
   - Test evidence (logs, screenshots, or tool output)
   - Updated docs links if applicable

## Required documentation updates (when applicable)
### If you modify BLE surface or protocol
You must update:
- `docs/20-gatt-schema.md`
- `docs/80-gatt-uuids.md` (only if initially pinning; avoid changing later)
- `docs/90-command-catalog.md`
- `docs/30-wire-protocol.md` (if framing changes)
- `docs/96-state-machine.md` / `docs/97-action-behavior-troubleshooting-map.md` (if behavior changes)

### If you modify safety behavior
You must update:
- `docs/40-safety-heartbeat-and-policies.md`
- `docs/98-operator-ux-requirements.md` (operator-facing behavior)
- `docs/95-implementation-checklist.md` (gates/acceptance)

## Testing expectations
Before merging, show evidence for at least the relevant subset of the v0 demo script:
- `docs/99-mvp-scope-v0.md` → section “Done definition (v0 demo script)”

At minimum for BLE changes:
- nRF Connect service discovery screenshot and/or log
- Telemetry stability (10 Hz baseline) for 60 seconds
- One command round-trip with ACK
- Reconnect behavior demonstrated

## Logging requirements
Changes should not reduce observability.
- Firmware should log connection/subscription/command outcomes.
- App should log scan/connect/discover/subscribe outcomes and command RTT/CRC failures.

## Coding standards (general)
- Prefer explicit state machines over implicit UI-driven BLE logic.
- Avoid “magic numbers” for command IDs, event IDs, bitfields; keep them centralized.
- Use capability flags (`cap_bits`) to gate optional features.

## Safety note
BLE and the HMI are not primary safety systems. Physical interlocks and E-stop remain authoritative.
Code must not introduce unsafe “default ON” behaviors or bypass interlocks.

## Questions / clarifications
If uncertain whether a change affects safety policy, treat it as safety-relevant and update the associated docs and test evidence.
