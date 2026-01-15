# Firmware Maintenance Workflow (Cleanup + Refactor)

This workflow turns the audit + refactor plans into repeatable, low-risk steps.
Follow each phase in order and pause at the stated checkpoints for build/test and
commit gating.

## Scope

- **Applies to**: firmware cleanup, refactors, notation/linting, and documentation
  updates for both `main_app` and `recovery_factory`.
- **Constraints**: do not change boot behavior, partition table, or OTA flow unless
  explicitly approved. Preserve BOOT long-press recovery escape behavior.
- **Logging**: record all actions in `firmware/docs/AGENT_LOG.md`.

## Milestone mapping (from refactor plan)

Use these milestones to choose which phase work to execute next.

- **Milestone 0**: documentation updates, agent work log entries, minimal CI build
  workflow, and any approved safety fixes that do not change behavior.
- **Milestone 1**: introduce `bootctl` helper component and shared logging conventions.
- **Milestone 2**: extract portal UI/text responses; optional non-breaking status
  endpoint.
- **Milestone 3**: consolidate OTA partition safety helpers and optional size checks.

## Phase 0 — Preflight (context + guardrails)

**Goals**
- Review the audit + refactor plans and validate any constraints.
- Confirm the working branch and verify the state of local changes.

**Tasks**
- Read `firmware/docs/FIRMWARE_AUDIT_PLAN.md` and
  `firmware/docs/REFRACTOR_PLAN.md`.
- Capture a quick inventory of files/modules in scope for this iteration.
- Identify the minimal behavior-preserving change set.

**Checkpoint (pause)**
- Share the planned change list for approval before modifying code.

## Phase 1 — Documentation updates (inventory + guidance)

**Goals**
- Keep documentation in sync with current behavior and findings.

**Tasks**
- Update `firmware/README.md` with any new build or bring-up notes.
- Update `firmware/docs/REFRACTOR_PLAN.md` with newly discovered redundancy or
  helpers that can be shared safely.
- Add notes to `firmware/docs/AGENT_LOG.md` for every command and verification.

**Checkpoint (pause)**
- Run firmware build(s) if possible and commit doc changes before moving on.

## Phase 2 — Notation & linting consistency (no behavior change)

**Goals**
- Align naming, logging, and formatting without altering logic.

**Tasks**
- Normalize log tags and error messages where they do not affect behavior.
- Apply formatting/lint fixes (whitespace, comment clarity, ordering).
- Prefer localized changes that are easy to review and revert.

**Checkpoint (pause)**
- Run `./firmware/tools/idf <app> build` (or CI equivalent) and commit.

## Phase 3 — Code cleanup (safe helpers, remove redundancy)

**Goals**
- Reduce duplication and improve readability without changing behavior.

**Tasks**
- Extract small helpers (e.g., bootctl NVS access) when refactor plan approves.
- Remove obviously unused code paths only when proven redundant.
- Keep APIs stable; avoid signature changes unless necessary.

**Checkpoint (pause)**
- Run firmware builds and commit before any further refactor work.

## Phase 4 — Refactor (incremental componentization)

**Goals**
- Introduce shared components (bootctl, portal_ui, ota_helpers) in conservative,
  reviewable steps.

**Tasks**
- Implement the next approved milestone from the refactor plan.
- Add/update tests or safety checks only if they do not alter behavior.

**Checkpoint (pause)**
- Run firmware builds and commit once the refactor milestone passes validation.

## Rollback strategy

- Every phase should be independently revertible.
- If behavior changes are detected, revert to the previous commit and record the
  findings in `firmware/docs/AGENT_LOG.md`.
