# Bernie (repo k2000) — Session Handoff

**Version:** 5.27 (artifact) · **Date:** 2026-07-08
Written for a fresh session with zero memory. Concrete paths and symbols throughout.
**Supersedes** `2026-07-03-session-handoff.md` (archived here; pre-Franklin, do not trust for current state).
**FIRST ACTION of any session:** run `tools/drift-check --session`, read every WARN, then read `CLAUDE.md`.

---

## 1. What happened (2026-07-03 → 07-08): Franklin — v1, v1.1, robustness, and Q28

The gold-standard engagement's item 6 (chz live progress) merged 2026-07-03 as PR #12,
which is exactly where the previous handoff left off. Since then: the Bernie rename
shipped (#13), then the whole Franklin dashboard arc — v1 (#14), v1.1 (#15), two
drift-robustness fixes (#16, #17) — then **engagement item 7 (register Q28, grid
economy) resolved and shipped** as purpose-driven characterization grids (#18), plus
a documentation-discoverability fix (#19). This handoff also bundles a roadmap/README
sync (this same branch) as the session's final housekeeping pass.

**Franklin is the repo's measurement/validation product** (register L8, ruled
2026-07-03): SP-A/B/C/D, the suite gates + drift rules, and the runs dashboard are
all Franklin. Bernie = synth, Ricky = FX section, Franklin = the instrument that
keeps both honest. Franklin is explicitly held to an **instrument-grade bar**
(`docs/franklin/charter.md:20-26`, now pointed to from `CLAUDE.md`) — reviewed as
instrument code, not UI code.

## 2. Repo state RIGHT NOW

- **main @ `0699bfe`** (PR #19). Suite: **292 tests, 0 failed**. `tools/drift-check --ci`
  fully clean. Zero open PRs as of the last merge.
- **This session's close-out is on branch `docs/session-close-2026-07-08`**
  (not yet a PR at the time this doc was written — open one if it isn't already):
  roadmap.json sync (`franklin-dashboard` + `cmajor-spike-windows-ci` → shipped),
  README.md + docs/README.md refresh, and this handoff.
- **Parked branch: `feat/bernie-gui`** — created off main, **zero commits**, brainstorm
  **not started**. The user explicitly paused the GUI cycle (engagement item 5) after
  the #17 merge on 2026-07-07: *"actually let's take a pause after merge. don't open
  the GUI cycle quite yet."* Do not resume without the user's go-ahead.
- Other stale branches present but not this session's concern: `feat/filter-characterization-harness`,
  `feat/filter-validation-internal`, `feat/moog-spec2` (all pre-date this arc; check
  before assuming live).
- **SDD ledger:** `.superpowers/sdd/progress.md` has the full blow-by-blow of every
  task/review/fix cycle for the Franklin and purpose-grids builds (subagent-driven
  development, per-task review gates + final whole-branch reviews). Read it if you
  need the "why" behind a specific line of code.

## 3. Franklin v1 (PR #14) — the dashboard itself

- **Producers (C++):** `runlog::Writer` (`tests/testdsp/RunLog.{h,cpp}`) emits NDJSON
  events (`start`/`progress`/`test`/`end`) to `.franklin/runs/<stamp>-<kind>-<pid>.ndjson`
  (gitignored). Suite tap in `tests/TestMain.cpp`; chz sink in
  `tests/characterization/characterize_main.cpp` — **`CharacterizationRunner::run()`
  itself was never touched**, by design, across the whole arc.
- **Server** (`tools/roadmap-dashboard/server/`): `runs.ts` (scan/parse/status/
  compaction), `control.ts` (whitelisted-template spawn, pid-verified stop —
  group-leader-gated group-kill, SIGTERM→5s→SIGKILL), `ci.ts` (gh-polled, 60s/10s
  TTL split). Compaction is finish-time, **never deletes** — keeps every test/check
  verbatim, downsamples only the progress stream to ≤500 points.
- **291-test catalog** (`docs/franklin/test-catalog.json` v1, now v2 — see §4) +
  the `franklin-catalog` drift-check rule enforcing every test has an entry.
- **Live-found and fixed mid-arc:** a control.ts bug where `startRun`/`stopRun`
  resolved binaries against the dashboard's own directory instead of the repo root
  (binaries always "missing" → always stale) — caught by the T12 live API smoke,
  not by any unit test. Lesson banked: **live smoke through the real API is not
  optional for this instrument** — per-task unit tests cannot see cross-directory
  wiring bugs.

## 4. Franklin v1.1 (PR #15) — from the user's first live session

The user opened the dashboard and reported two things directly; both are now fixed
and both fixes were themselves live-verified (dashboard-started runs, not just unit tests):

1. **Dropdowns died in ~2 seconds.** Root cause: the whole Franklin tab re-rendered
   every 2s poll tick, closing any open `<select>`. **Structural fix**, not a timer
   tweak — `src/franklin.ts` now builds five persistent `<section data-fr="...">`
   containers ONCE at mount; each poller writes only its own section; the form
   section repaints only when its data actually changes; no section repaints while
   `document.activeElement` is inside it (pending HTML stashed, flushed on focusout).
2. **Test info cards.** Six fields per test in the catalog now (v2): What · Purpose ·
   **Compares** (the actual reference — golden file / analytic form / threshold) ·
   On success · On failure · **Run by**. Catalog backfilled to v2 for all 291 entries
   (reviewer-audited: 5-entry spot-check, 0 factual errors both times it was checked).
3. **Run provenance**, added on the user's own follow-up ruling: every run records
   who started it. Detection chain in `tests/testdsp/RunLog.cpp` (`detectRunner()`):
   `BERNIE_RUNNER` env override → `GITHUB_ACTIONS` set → `"ci"` → `CLAUDECODE` set →
   `"claude"` → else `"terminal"`. **Dashboard Start-button runs are stamped
   `BERNIE_RUNNER=dashboard`** (`control.ts`) — a run YOU start by clicking is
   attributed to you, not to Claude, even inside a Claude Code shell.
4. **Searchable catalog browser** — a collapsible section over all 291 cards.

**Two more bugs the user found by actually using it (fixed same day, both on this
v1.1 branch before it merged):**
- **Layout**: `#app` is the roadmap tool's own 2-column CSS grid (`1fr 240px`);
  Franklin's 2-child skeleton auto-placed the whole tab into the 240px sidebar
  column (~75% of the screen blank). Fix: `switchView` toggles a `.franklin-view`
  class on `#app`; that class drops the roadmap grid and Franklin lays out its own
  dashboard grid (`src/styles.css`) — wide main column (active runs + archive)
  beside a controls sidebar (CI + new-run form), catalog full-width beneath.
  **No test or review catches CSS layout bugs — this is a browser-only smoke,
  permanently.**
- **Suite runs stuck at 0%**: chz streams progress per measured point; the test
  suite wrote all its per-test events only AFTER `runAllTests()` returned, so a
  running suite had nothing to show until it teleported to done (and long opt-in
  suites like `suite-disparity`/`suite-voiceperf` would falsely trip the 120s
  stall detector while healthy). Fix: `ProgressRunner : juce::UnitTestRunner` in
  `tests/TestMain.cpp` emits a progress event from `resultsUpdated()` per result
  (throttled ~1/s); total is estimated from the PREVIOUS completed suite run's
  count via `runlog::lastSuiteTestCount()` (read before the current run's own
  file exists, so it never self-references). First run on a fresh clone shows a
  live count with no percentage; every run after gets the full bar + ETA.

## 5. Drift-check robustness (#16, #17) — both found by the user LIVE-using the dashboard

- **#16**: `VoicePerf`/`LargeSignal` change their `beginTest` subcategory name by
  their `BERNIE_RUN_*` env flag ("skipped (set BERNIE_RUN_X=1…)" off vs the real
  measurement name on). The `franklin-catalog` rule's exact bidirectional key-match
  spuriously warned when a heavy opt-in run's runlog was newest. Fix: those two
  families match by test-name only; the other 289 tests stay exact.
- **#17**: the rule picked "newest suite runlog by mtime" unconditionally — but a
  suite's per-test events only land AFTER it finishes (§4), so running `drift-check`
  while a suite was mid-flight (e.g. watching it live) compared against an empty
  test set and spuriously orphaned all 291/292 catalog entries. Fix: walk
  newest-first, use the first runlog that actually HAS test events.
- Both are `tools/drift-check`, function `chk_franklin_catalog` — read it before
  touching catalog/runlog matching logic again; the self-test fixture
  (`fx_franklin_catalog`) proves both the missing- and orphaned-direction warnings
  fire correctly, and now also survives a running (test-event-less) newest file.

## 6. Engagement item 7 — register Q28, grid economy — RESOLVED (#18)

Full text: `docs/architecture/engine-questions.md` row **Q28** (🟢 resolved,
resolve-at `v5.23`). Short version: `chz::fullGrid()` priced at **≈39–41 h/model**
via the item-6 ETA line — dead weight as a routine instrument. The user's
precondition ("I first need to see the tests going in real time") was met by
Franklin v1/v1.1, and the user then ruled: **(a) all four candidate purposes,
(b) ~2 hour budget.**

Design: `docs/superpowers/specs/2026-07-07-purpose-grids-design.md` (v5.23), built
from an **empirical per-point cost model** derived from real Franklin runlog
timestamps (not guessed). Four new `Grid` factories in
`tests/characterization/CharacterizationRunner.{h,cpp}` (axis-exact vs the spec,
independently re-verified by two separate reviewers): `spdGrid()` (450 pts, ~75 min
— the SP-D hardware-comparison map, drive=0, 96k/os8/Live as a **working capture-
reference assumption**, CALIB-commented for SP-D to re-pin), `osAliasGrid()` (192
pts, ~10 min), `hostRateGrid()` (120 pts, ~8 min), `largeSignalGrid()` (180 pts,
~10 min — the Q27/SP-B drive-law lattice). `--grid quick|full|spd|osalias|rates|
largesig|deep` on `characterize_main.cpp`; `deep` runs all four in sequence per
model under ONE runlog Writer, each sub-grid nested at
`build/characterization/<model>/<gridname>/` (never clobbers siblings or the
legacy bare `<model>/` layout). `--quick` still works as an alias.

**Live-smoked to completion**, not just unit-tested: started `rates` on Moog via
the real dashboard API, ran **447 s** (matched the ~8 min estimate), `outcome:
pass`, 66 checks recorded, output correctly nested.

**Caution for future doc edits near this feature:** the dashboard's live progress
counter and the spec's "Points" column are DIFFERENT NUMBERS by design (the live
counter counts measurement units — B1 crossings plus per-(mode,cutoff) B2/B3
batteries — and drops modes a model doesn't support; e.g. `spd` shows `/540` for
Moog, no Notch, but `/675` for Huggett, which does have Notch). A footnote
explaining this in `docs/filter-validation/running.md` was **wrong twice** before
a reviewer got it right on the third pass (first version stated an unqualified
`/540` that's false for Huggett; the "fix" for that also contradicted its own
example). If you ever touch that footnote: recompute BOTH models' numbers from
`CharacterizationRunner.cpp`'s progress-total formula and `FilterUnderTest.cpp`'s
per-model mode-support table before asserting anything — don't trust the previous
prose, verify from source.

## 7. This session's process notes (useful if you resume subagent-driven work)

- **SDD artifact naming convention**: namespace scratch briefs/reports per feature
  (`franklin11-task-N-*.md`, `grids-task-N-*.md`) — a bare `task-N-report.md` will
  get silently overwritten by the next milestone's Task N (this happened once,
  early in the Franklin arc; the fix was adopted mid-session and held since).
- **Docs-only tasks still need `npm test`, not just drift-check**, when they touch
  a server-consumed artifact. Task 3 of the purpose-grids build (catalog-version-only
  change) broke a `server.test.ts` assertion that the review didn't catch because
  it only ran drift + the C++ suite — the npm regression was found by the NEXT
  task's implementer, not by review.
- **The final whole-branch review earns its keep on this codebase.** It caught a
  real server-crashing Critical (`startRun` with a missing binary → unhandled
  spawn `'error'` event → the WHOLE dashboard process dies) that six task-level
  reviews upstream of it all missed, because none of them tried the missing-binary
  path specifically.

## 8. Next steps, in order

1. **Merge `docs/session-close-2026-07-08`** (this handoff + the roadmap/README
   sync) if not already merged.
2. **GUI cycle (engagement item 5) — PAUSED, awaiting the user's go-ahead.**
   Branch `feat/bernie-gui` is parked empty at main. When resumed: start with
   `superpowers:brainstorming` (per `CLAUDE.md`'s process rails — this is a
   creative/design task, brainstorm before any implementation skill). First
   question to the user: locate the GUI sketches the roadmap item references
   ("First move towards a proper GUI based on sketches") — they haven't been
   provided or found yet. Constraints already on record: register **L5** (GUI
   grows with the engine — each phase ships its feature UI); memory
   `feedback_no_menu_diving` (Summit surface for live params, tiered immediacy;
   menus OK only for the VAST long tail).
3. **Item 2 remnant** (stale-doc sweep) — the gold-standard engagement's item 2
   was mostly absorbed into other work; confirm whether anything's still
   outstanding before declaring the engagement closed.
4. **Close the gold-standard engagement** once items 2 and 5 are done — items
   1, 3, 4, 6, 7 are all shipped.
5. **SP-B proper** (filter large-signal profile on the SP-A core) — the next
   substantive DSP-measurement work after the engagement closes. SP-B Phase 0
   (the large-signal read that found the Q27 defect) is already banked; SP-B
   proper is the full battery.
6. **SP-D excitation-method risk** (register Q25) stays the top blocker for any
   real-hardware fingerprinting — Arturia Mini V first (software, zero rig risk),
   Summit second. All DSP voicing stays HELD until then (authenticity-purist
   stance, unchanged).

## 9. Verify / build

```bash
tools/drift-check --session                       # ALWAYS FIRST
cmake --build build --target k2000_tests k2000_device_characterization -j4
./build/tests/k2000_tests | tee build/last-test-run.log | tail -1   # Summary: 292 tests, 0 failed
cd tools/roadmap-dashboard && npm test && npm run build             # 185/185 (may have grown since)
```

Live dashboard: `cd tools/roadmap-dashboard && npm run dashboard` → `localhost:4173`
→ **Franklin** tab. Start a `rates` grid (~8 min) for a quick, real end-to-end feel
for the instrument before touching it.
