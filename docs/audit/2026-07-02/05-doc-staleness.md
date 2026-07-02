# Documentation Staleness Inventory

**Version:** 5.12 · **Date:** 2026-07-02 · Part of the [codebase health audit](README.md).
Feeds the doc-sweep sub-project (engagement item 2) — this is the inventory, not the fix.

## Stale / misplaced (act on these)

| Artifact | Problem | Suggested action |
|---|---|---|
| `HANDOFF.md` (repo root, untracked) | 2026-06-20 snapshot, actively misleading. | **SWEPT**: archived to `docs/handoffs/2026-06-20-session-handoff.md`. |
| `REVIEW-PR7-filter-validation.md`, `ROADMAP-PR7-filter-validation-L3.md` (root) | Review artifacts at repo root; PR #7 closed. | **SWEPT**: moved to `docs/reviews/`; SP-A spec link fixed. |
| `build_output.txt` (root, untracked) | Build log in the tree. | **SWEPT**: deleted; `build_output.txt` + `build-asan/` gitignored. |
| `docs/filter-validation/README.md` | ~~Other pages may cite old counts~~ **SWEPT**: grep found no stale counts or old exe names anywhere in the manual. | Done. |
| CMake `VERSION 5.4.0` vs docs "5.9.0" | **RESOLVED**: 5.4.0 is truth; spec's "5.9.0" misread artifact stream 5.09. Spec corrected. | Automate the two-stream check in the anti-drift harness. |
| `docs/roadmap/phases.md` | ~~Missing vision-only banner~~ **Inventory error**: the banner already exists (points to the dashboard). | None. |
| `docs/roadmap/v2-known-concerns.md`, `v4-known-concerns.md` | Point-in-time concern lists from v2/v4. | **SWEPT**: point-in-time banners added pointing to the engine-questions register; item-by-item grooming deferred to register grooming. |

## Healthy (spot-checked, no action)

- `docs/decisions/` ADRs — immutable records; correct to leave.
- `docs/superpowers/specs+plans/` — dated artifacts, self-labeling.
- `docs/filter-validation/acceptance-criterion.md` — new (M4), current.
- `docs/architecture/engine-questions.md` — the living register; groomed recently.

## Anti-drift hooks this inventory suggests

Checkable invariants for the anti-drift harness (engagement item 4):
1. Version claims in docs == CMake `project(VERSION)`.
2. "Expected: Summary: N tests" strings in docs == actual suite count.
3. No `*.md` at repo root except `README.md` (+ optionally `HANDOFF.md` with a max-age).
4. Spec cross-links resolve (no dangling relative paths).
