# Documentation Staleness Inventory

**Version:** 5.12 · **Date:** 2026-07-02 · Part of the [codebase health audit](README.md).
Feeds the doc-sweep sub-project (engagement item 2) — this is the inventory, not the fix.

## Stale / misplaced (act on these)

| Artifact | Problem | Suggested action |
|---|---|---|
| `HANDOFF.md` (repo root, untracked) | 2026-06-20 snapshot: pre-Moog, pre-oversampling, "138 tests", references since-removed plans. Actively misleading for a fresh session. | Delete (memory + `.superpowers/sdd/progress.md` + the dashboard now serve this role) or archive under `docs/handoffs/`. |
| `REVIEW-PR7-filter-validation.md`, `ROADMAP-PR7-filter-validation-L3.md` (root; L3 tracked, L2 untracked) | Review artifacts living at repo root; PR #7 is now closed (superseded by #8). The L3 doc is referenced by the SP-A spec. | Move both to `docs/reviews/` and fix the spec's relative link. |
| `build_output.txt` (root, untracked) | Build log in the tree. | Delete + add to `.gitignore`. |
| `docs/filter-validation/README.md` | Test count corrected to 261 during M4; **other pages in the manual still cite old counts/grid sizes** — not re-verified page-by-page. | Page-by-page accuracy pass in the doc sweep. |
| CMake `VERSION 5.4.0` vs docs "5.9.0" | Version truth unclear ([02](02-static-analysis.md)). | Reconcile; then automate the check. |
| `docs/roadmap/phases.md` | Declared vision-only (the live roadmap is the dashboard) but nothing in the file says so. | Add a banner: "vision document — live plan is `tools/roadmap-dashboard`". |
| `docs/roadmap/v2-known-concerns.md`, `v4-known-concerns.md` | Point-in-time concern lists from v2/v4; several items since resolved. | Sweep: mark resolved items, or fold survivors into the engine-questions register. |

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
