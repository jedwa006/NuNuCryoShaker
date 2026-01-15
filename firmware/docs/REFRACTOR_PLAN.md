# Refactor Plan (main_app + recovery_factory)

## Goals
- Reduce duplicated boot/OTA logic while **preserving current OTA behavior** (factory recovery stays intact; staging does not switch boot partitions until activation).【F:firmware/README.md†L46-L64】
- Keep changes conservative: isolate shared helpers without altering boot flows or HTTP endpoints.【F:firmware/README.md†L54-L70】【F:firmware/apps/recovery_factory/main/ota_portal.c†L224-L405】

## Redundancy inventory (current overlap)
### Boot control + partition selection
- Both images rely on ESP-IDF OTA APIs (`esp_ota_set_boot_partition`, `esp_ota_get_running_partition`, `esp_partition_find_first`).【F:firmware/apps/main_app/main/app_main.c†L66-L112】【F:firmware/apps/recovery_factory/main/ota_portal.c†L188-L405】
- Both store/read a **return partition label** in NVS (`bootctl/return_lbl`) to support returning from recovery to the prior app slot.【F:firmware/apps/main_app/main/app_main.c†L18-L64】【F:firmware/apps/recovery_factory/main/ota_portal.c†L36-L178】

### NVS init + error handling
- Both apps initialize NVS and handle `ESP_ERR_NVS_NO_FREE_PAGES` by erasing and reinitializing.【F:firmware/apps/main_app/main/app_main.c†L148-L165】【F:firmware/apps/recovery_factory/main/ota_portal.c†L492-L501】

### OTA workflow semantics
- Recovery portal stages firmware in an OTA slot without switching boot partitions, then activates via `esp_ota_set_boot_partition` and reboot.【F:firmware/apps/recovery_factory/main/ota_portal.c†L224-L405】
- Main app marks OTA image valid on boot when rollback is enabled (`esp_ota_mark_app_valid_cancel_rollback`).【F:firmware/apps/main_app/main/app_main.c†L142-L176】

### Logging + small helper utilities
- Both apps format OTA status or logs referencing partition labels and addresses (manual string formatting / logging).【F:firmware/apps/main_app/main/app_main.c†L84-L112】【F:firmware/apps/recovery_factory/main/ota_portal.c†L188-L218】

## Recommended componentization candidates
> Keep these helpers **thin wrappers** around existing behavior to avoid OTA regressions.

### 1) Boot control helpers (shared)
- **NVS boot label helpers**: `bootctl_store_return_label()` and `bootctl_load_return_label()` to centralize the `bootctl/return_lbl` key handling used by both images.【F:firmware/apps/main_app/main/app_main.c†L24-L64】【F:firmware/apps/recovery_factory/main/ota_portal.c†L36-L178】
- **Partition lookup helpers**: `bootctl_find_factory_partition()` and `bootctl_find_label_partition()` to keep the partition selection logic consistent across apps.【F:firmware/apps/main_app/main/app_main.c†L66-L78】【F:firmware/apps/recovery_factory/main/ota_portal.c†L387-L436】
- **Boot switch utility**: `bootctl_set_boot_partition_and_reboot()` to standardize logging + reboot delay (currently repeated in both apps).【F:firmware/apps/main_app/main/app_main.c†L102-L112】【F:firmware/apps/recovery_factory/main/ota_portal.c†L397-L405】

### 2) OTA portal UI utilities (recovery-only, but modularized)
- **HTML template + token header parsing**: move constants (`INDEX_HTML`, `OTA_TOKEN`) and the `token_ok()` helper into a dedicated module for clarity and testability.【F:firmware/apps/recovery_factory/main/ota_portal.c†L24-L140】
- **Shared response helpers**: `send_text()` and `bytes_to_hex()` are generic utilities that can be grouped into a small `http_utils.c` under recovery_factory (not necessarily shared with main_app).【F:firmware/apps/recovery_factory/main/ota_portal.c†L128-L166】

### 3) OTA staging workflow helpers (recovery-only)
- **Staging lifecycle** (init/reset/commit) can be wrapped to reduce local state complexity while keeping the same `/stage` and `/activate` semantics.【F:firmware/apps/recovery_factory/main/ota_portal.c†L224-L374】

## Risks and rollback strategy
### Risks
- **OTA slot selection regressions** if shared helpers accidentally select the factory slot or change staging behavior (current safety checks block staging into factory).【F:firmware/apps/recovery_factory/main/ota_portal.c†L238-L244】
- **Rollback and validation behavior changes** if the main app’s call to `esp_ota_mark_app_valid_cancel_rollback()` is moved or altered.【F:firmware/apps/main_app/main/app_main.c†L142-L176】
- **NVS data loss** if NVS handling changes; both apps currently erase/reinit when `ESP_ERR_NVS_NO_FREE_PAGES` is detected.【F:firmware/apps/main_app/main/app_main.c†L148-L165】【F:firmware/apps/recovery_factory/main/ota_portal.c†L492-L501】

### Rollback strategy
- Keep the refactor **internal-only** (same public endpoints, button behavior, and OTA steps).【F:firmware/README.md†L54-L70】【F:firmware/apps/main_app/main/app_main.c†L118-L203】
- Make each step reversible: split refactor into small PRs, retaining old paths until new helpers are proven.
- Validate on hardware by staging/activating OTA and verifying recovery back to factory via the BOOT long-press and portal `/reboot_back` flows.【F:firmware/apps/main_app/main/app_main.c†L118-L203】【F:firmware/apps/recovery_factory/main/ota_portal.c†L412-L474】

## Incremental milestones (conservative, 1–3 PRs each)
### Milestone 1: Shared boot/NVS helpers (1–2 PRs)
1. **PR 1**: Introduce `bootctl` helper module (NVS return label + partition lookup). Keep all call sites identical, just redirect to helpers.
2. **PR 2 (optional)**: Add a minimal unit-test or compile-time check (if desired) to ensure helpers are linked into both apps. No behavior changes.

### Milestone 2: Recovery portal utilities (1–2 PRs)
1. **PR 1**: Extract `send_text`, `bytes_to_hex`, and `token_ok` into `http_utils`/`portal_utils` within recovery_factory. Keep endpoints unchanged.
2. **PR 2**: Isolate `INDEX_HTML` into a dedicated module or header to reduce main file churn, without altering the HTML or endpoints.

### Milestone 3: OTA staging helpers (1 PR)
1. **PR 1**: Wrap staging state (`g_stage`) into a small struct API (`stage_reset`, `stage_commit`, `stage_status_text`) with no change in semantics or data flow.

---

## Notes on alignment with current OTA behavior
- **Factory partition safety** must remain unchanged: staging only targets OTA slots and never overwrites factory/recovery.【F:firmware/apps/recovery_factory/main/ota_portal.c†L238-L244】
- **Activation** continues to be explicit (`/activate`), and recovery retains control until the user activates the staged slot.【F:firmware/README.md†L54-L64】【F:firmware/apps/recovery_factory/main/ota_portal.c†L376-L405】
- **Main app validation** (`esp_ota_mark_app_valid_cancel_rollback`) remains in main_app boot flow, not moved into recovery portal.【F:firmware/apps/main_app/main/app_main.c†L142-L176】
