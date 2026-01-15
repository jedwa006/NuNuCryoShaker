# Firmware Refactor Plan (Conservative)

This plan inventories redundancy between the two ESP-IDF projects and proposes
incremental, low-risk improvements without altering the current recovery + OTA behavior.
For the broader 3-step audit process, see `firmware/docs/FIRMWARE_AUDIT_PLAN.md`.

## Current redundancy (inventory)

- **Module inventory (current)**
  - `apps/main_app/main/app_main.c`
    - BOOT long-press watcher task (`boot_button_task`) that notifies boot control.
    - Boot control task (`bootctl_task`) that stores the return label and switches to
      factory recovery.
    - Rollback/health marking via `esp_ota_mark_app_valid_cancel_rollback()`.
  - `apps/recovery_factory/main/ota_portal.c`
    - SoftAP bring-up + HTTP server endpoints (`/`, `/status`, `/stage`, `/activate`,
      `/reboot_back`).
    - OTA staging with SHA256 verification and slot size guardrails.
    - Return label lookup in NVS for reboot-back behavior.

- **Boot control NVS access**: `bootctl` namespace and `return_lbl` key exist in both apps.
- **NVS init boilerplate**: identical init/erase flow in both apps.
- **Logging conventions**: tags differ (`main_app`, `ota_portal`) but no shared format/levels.
- **OTA partition safety checks**: recovery app contains OTA slot safety logic that could be
  shared with main app utilities later.
- **UI text + token handling**: portal HTML and token checks are embedded in one file.

## Recommended componentization (future, staged)

> Do **not** move everything at once; keep recovery + OTA stable.

1. **`bootctl` helper component**
   - Shared helpers for `store_return_label()`, `get_return_label()` and
     `find_factory_partition()`.
   - Keeps BOOT long-press behavior intact while avoiding duplicate NVS handling.

2. **`portal_ui` helper (recovery only)**
   - Extracts HTML, token handling, and response formatting into a small helper
     to keep `ota_portal.c` focused on OTA mechanics.

3. **`ota_helpers` utilities**
   - Reusable guardrails for OTA partition checks and size calculation.

## Risks & rollback strategy

- **Risk**: accidental changes to partition selection or boot flags.
  - **Mitigation**: never change partition table, and keep boot logic in place.
- **Risk**: regressions in recovery portal responses.
  - **Mitigation**: keep existing endpoints and payload formats; add tests only when stable.
- **Rollback**: revert to the prior commit (no data migrations planned).

## Incremental milestones

### Milestone 0 (this PR)
- Documentation updates (firmware README + refactor plan).
- Agent work log scaffolding.
- Minimal CI build workflow.
- Small safety fix (avoid aborts during HTTP requests).

### Milestone 1 (1–2 PRs)
- Introduce `bootctl` helper component; migrate NVS return label functions.
- Add shared logging conventions (tag naming + log level notes).

### Milestone 2 (1–3 PRs)
- Extract portal UI/text responses into a helper.
- Add optional JSON status endpoint (non-breaking, keep existing text).

### Milestone 3 (1–2 PRs)
- Consolidate OTA partition safety helpers and optional size checks shared by both apps.

## What will not change without explicit approval

- BOOT long-press behavior as the recovery escape mechanism.
- Partition table layout or slot sizes.
- Removal of tracked `sdkconfig` or defaults.
