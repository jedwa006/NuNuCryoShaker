# Firmware Audit Plan (3-Step)

This plan organizes the firmware cleanup effort into three explicit phases so we can
trace decisions and preserve the working recovery + OTA behavior while preparing for
production firmware implementation.

## Step 1 — Analysis (inventory + constraints)

**Goal:** Build a shared understanding of the current firmware state, invariants, and
single sources of truth.

Deliverables:
- Inventory of firmware modules, tasks, and key flows (recovery portal, OTA staging,
  BOOT long-press escape, rollback marking).
- Explicit list of invariants to preserve (partition table, BOOT long-press behavior,
  OTA slot sizing, NVS return label semantics).
- Map of configuration sources (sdkconfig defaults, partition table, CMake overrides).

Artifacts:
- Update `firmware/docs/REFRACTOR_PLAN.md` as we learn more about redundancy and shared
  helpers needed.
- Update `firmware/README.md` when new build or bring-up knowledge is discovered.

## Step 2 — Review (risk + quality assessment)

**Goal:** Identify safety and maintainability risks without changing behavior.

Deliverables:
- Risk register: boot selection errors, OTA activation mistakes, NVS corruption paths,
  HTTP error handling that could interrupt responses.
- Logging/observability review: log tags, log levels, and error handling patterns.
- Security posture review for the recovery portal (token/AP credentials), with
  documented hardening options.

Artifacts:
- Track findings and mitigation options in `firmware/docs/REFRACTOR_PLAN.md`.
- Add targeted TODO notes only when action is explicitly approved.

## Step 3 — Refactor (safe, incremental cleanup)

**Goal:** Implement minimal, reviewable changes that reduce risk without altering the
recovery + OTA workflow.

Deliverables:
- Small helper extraction (e.g., `bootctl`, `ota_helpers`) as described in the
  refactor plan.
- Deterministic configuration guarantees (no sdkconfig drift, pinned partitions).
- Minimal CI checks to keep builds and size gates stable.

Constraints:
- Preserve BOOT long-press as the recovery escape mechanism.
- Preserve partition layout, OTA behavior, and factory recovery image.
- Every change must include a build/test step using `./firmware/tools/idf`.

## Status tracking

Use the Agent Work Log (`firmware/docs/AGENT_LOG.md`) to record each action, command,
result, and behavioral verification note for traceability.
