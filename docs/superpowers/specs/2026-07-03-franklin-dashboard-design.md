# Franklin Dashboard — Live Runs, Archive, Run Control — Design

**Version:** 5.15 (artifact) · **Date:** 2026-07-03 · **Status:** Approved in brainstorm; awaiting written-spec review
**Register:** unblocks **Q28** (grid-economy questions deferred by the user until this ships) · names ruled by the user 2026-07-03
**Related:** SP-A core (`2026-07-01-device-characterization-core-design.md`), engagement item 6 (Progress sink, PR #12), anti-drift harness (`2026-07-02-anti-drift-harness-design.md`)

---

## 0. Naming ruling (user, 2026-07-03)

The measurement/validation product is **Franklin** — *"Franklin is what keeps us honest and our synths musical."* Bernie makes the sound, Ricky colors it, Franklin measures both. Franklin retroactively names: the SP-A device-characterization core (shipped), the SP-B/C profiles, the SP-D hardware bridge, the external-VST capture harness (roadmap), the suite gates, and this dashboard.

Deliverables of this ruling inside this build: register product-constants amendment, CLAUDE.md constants line, `docs/franklin/charter.md` (vision-only, one page: Franklin's remit mapped to existing roadmap items — **no capture rig is built here**).

## 1. Problem

- The deep grid (`chz::fullGrid()`) was priced 2026-07-03 via the item-6 ETA line at **≈39–41 h per model** (29,088 supported points). Nobody can babysit that blind; the user declines to answer the Q28 grid-economy questions until they can *watch* runs live.
- Runs today narrate to stderr only, vanish when the terminal closes, and leave no record. Suite runs say pass/fail with no explanation of *what* a test guards or *how close* to the edge a pass sailed.
- User requirements (verbatim intent): live view of **all** tests; every test **explains what it does and why**; **all deviations from expected output highlighted**; browser **start/stop**; **full archive** of every run, with **log rotation**.

## 2. Scope

**In:** runlog event files + C++ producers (chz + suite), dashboard server APIs (runs / control / CI), Franklin tab UI (live cards, CI strip, archive, run detail, deviations panel, new-run form), test catalog with full ~290-entry backfill + drift-check rule, finish-time compaction, Franklin naming deliverables.

**Out (non-goals):** building any capture rig (VST or hardware), remote access / auth, CI-internal test detail (GitHub exposes job status only), deleting any archived run, websockets/SSE.

## 3. Architecture

```
k2000_device_characterization ──┐  append NDJSON            ┌─ GET /api/runs, /api/runs/<id>
k2000_tests (TestMain tap)      ├─► .franklin/runs/*.ndjson ─┤  GET /api/ci
(any future Franklin producer) ─┘        ▲                   ├─ POST /api/control/start|stop
                                         │ spawn/signal      │
        dashboard server (plain node) ───┴───────────────────┴─► Franklin tab (poll 2 s)
                        └─ gh CLI poll (60 s) ─► CI strip payload
```

File transport is the load-bearing decision (approved): runs record with the dashboard **down**; no network code in the instrument; crash-safe append-only; stalls detectable by mtime.

## 4. Runlog event schema (v2)

One file per run: `.franklin/runs/<ISO8601>-<kind>.ndjson` (kind: `chz` | `suite`). Gitignored; survives `rm -rf build/`. One JSON object per line:

| ev | fields | notes |
|---|---|---|
| `start` | ts, kind, argv[], env{BERNIE_* relevant}, pid, gitSha, buildType, model?, grid?, total? | total from the item-6 pre-pass when known |
| `progress` | ts, done, total, label, metrics{cornerHz?, methodDeltaDb?, peakDb?} | throttle: ≥1 s between writes; **≥10 s after the first hour**; final point always written |
| `test` | ts, name, ok, failures[] | suite only; failures = JUCE expect messages (expected-vs-actual text), empty when ok |
| `end` | ts, outcome (`pass`\|`fail`\|`error`\|`stopped`), durationS, summary{}, checks[] | checks = every gate/golden comparison: {name, measured, expected, delta, verdict} |

Producer semantics: **best-effort** — first write error disables logging for the rest of the run silently; a runlog failure must never affect a measurement. Env: `BERNIE_NO_RUNLOG=1` disables; `BERNIE_RUNLOG_DIR` overrides the directory (default `./.franklin/runs` under the CWD, which is the repo root by convention).

## 5. C++ producers

- **`runlog::Writer`** — small append-only helper (single header + cpp beside the other test-side instruments; exact path at plan time): open, `event(json-ish key/values)`, flush per line, throttle helper. No third-party JSON lib — fields are flat, hand-serialized with proper escaping (mirrors the CSV writers' spirit).
- **Characterization**: `characterize_main.cpp` wraps its existing stderr Progress lambda with a second sink that also emits `progress` events. **`CharacterizationRunner` itself is untouched.** `end.checks[]` is filled from the same values the runner already prints/gates.
- **Suite**: `TestMain.cpp` tap emitting one `test` event per completed test with failure messages pulled from the JUCE test runner results. Granularity = whatever name the runner reports per counted test (the "290" unit); the catalog keys on exactly those emitted names (verified at plan time).

## 6. Test catalog (full backfill)

`docs/franklin/test-catalog.json` — array of entries:

```json
{ "name": "<emitted test name>", "file": "tests/HuggettBoundedResonanceTests.cpp",
  "what": "…", "why": "…", "deviationMeans": "…", "links": ["docs/…", "Q27"] }
```

- **Backfill:** an entry for every existing suite test (~290), written by reading each test file. One-time grind, explicitly accepted by the user.
- **Drift rule:** new drift-check check `franklin-catalog`: parse test names from `build/last-test-run.log`, cross-reference the catalog **both directions** (test without entry / entry without test) → WARN. Same tiering as existing checks (`--session`/`--commit`/`--ci`).
- **Chz points are NOT cataloged** — their explanations are **generated**: battery templates (B1 magnitude dual-method · B2 resonance/self-osc · B3 THD/aliasing · B4 phase/GD descriptive-only per Q20) × operating-point fields → prose, linking `docs/filter-validation/acceptance-criterion.md`. Rendered client-side from the label + grid data already in the events.

## 7. Dashboard server

- **`server/runs.ts`** — scan/parse `.franklin/runs/`; status = `running` (no `end`, mtime fresh) | `stalled` (no `end`, mtime > 120 s) | finished outcome; list summaries (`/api/runs`), detail with event tail (`/api/runs/<id>`); triggers compaction (§9) on discovering finished uncompacted files.
- **`server/control.ts`** — **whitelisted templates only** (never arbitrary commands): suite (plain / `BERNIE_RUN_DISPARITY=1` / `BERNIE_RUN_VOICEPERF=1`), chz (model ∈ {moog, huggett, all} × grid ∈ {quick, full}). `POST /api/control/start {template, params}` → spawn detached child, cwd = repo root, stdout/stderr to a sidecar `.log`; `POST /api/control/stop {id}` → verify `/proc/<pid>/cmdline` contains the recorded binary name **before** SIGTERM, escalate SIGKILL after 5 s, mark `outcome: stopped`. Works for terminal-started runs too (pid comes from the `start` event). Stale-binary check: binary mtime vs newest `src/`+`tests/` source mtime → warning payload for the form.
- **`server/ci.ts`** — every 60 s shell `gh pr list`/`gh pr checks`/`gh run list` (main); merge into `/api/ci`; if `gh` missing or errors → `{available:false}` and the UI shows "CI unavailable". Live-only; GitHub is the CI archive.
- Existing roadmap endpoints untouched. Frontend polls `/api/runs` + `/api/ci` every 2 s / 10 s respectively.

## 8. Franklin tab (frontend)

New route/tab "Franklin" beside the roadmap view (new `src/franklin*.ts`; the existing `src/progress.ts` — roadmap-item percent — is unrelated and untouched):

1. **Active cards** — per running run: kind badge, progress bar, done/total/%, ETA, current label + its generated explanation, live headline metrics, stalled warning, **Stop** button.
2. **CI strip** — per branch/PR: check name, status, conclusion, link to the GitHub job.
3. **New-run form** — template picker (whitelist mirror), stale-binary warning chip, start.
4. **Archive table** — newest first: when, kind, duration, outcome, headline (chz: model/grid/points; suite: N/failed), disk footprint of `.franklin/runs/` in the header; filters (kind/outcome); **Re-run** action replaying the recorded template.
5. **Run detail** — opens from any row/card: **Deviations panel first** — every failure and gate/golden miss ranked by severity (fail > gate miss > margin outlier), red-highlighted, expected vs measured with delta; passing cross-checks still show margins (method-delta dB, self-osc cents error, noise floor). Then the per-test list (suite: catalog prose per test — what/why/deviation-means; chz: generated battery prose), then run metadata (argv, git SHA, build type, CSV output path for chz).

## 9. Rotation = finish-time compaction (never deletion)

Honors the user's full-archive ruling: **no run is ever deleted**. On finish (or first scan of a finished file): rewrite keeping `start`, `end` (which carries all `checks[]`), and **every** `test` event verbatim — only the `progress` stream is downsampled, to ≤500 evenly-spaced events. A 40 h run: ~20 MB → ~100 KB with zero loss of verdicts/deviations/metrics. Compaction is atomic (write `.tmp`, rename). Active-file growth additionally bounded by the 1 s → 10 s throttle (§4).

## 10. Error handling

- Producer write failure → silent disable for the run (measurement never harmed).
- Malformed/truncated NDJSON lines skipped (crash mid-line is expected).
- Spawn failure / template mismatch / pid-verify mismatch → 4xx with message, surfaced in the UI; **refuse to signal** on any cmdline mismatch.
- `gh` absent → CI strip degrades; runs dir absent → empty list; compaction failure leaves the original file intact.
- Server binds **localhost only**; no auth; whitelist-only spawn — accepted single-user boundary, stated here deliberately.

## 11. Testing

- **Node (`node --test`, existing pattern):** runs.ts parsing/status/stale/compaction (fixture NDJSON incl. truncated lines); control.ts template mapping, spawn/stop against a fake long-running script, pid-verify refusal on cmdline mismatch; ci.ts parsing from fixture gh output (no real gh calls); API surface via the server.test.ts pattern.
- **C++:** `RunLogTests.cpp` — event shape (escaping), throttle behavior, `BERNIE_NO_RUNLOG`, best-effort disable on write error. Suite count grows → README expected-count + drift-check stay aligned.
- **Drift:** `franklin-catalog` rule gets a `--self-test` case like the other checks.
- Suite tap and chz sink are exercised by every real run; the drift rule catches catalog rot from day one.

## 12. Deliverables checklist

1. `.gitignore` + `.franklin/runs/` layout · runlog Writer + tests
2. chz second sink · TestMain tap
3. `docs/franklin/test-catalog.json` (full ~290 backfill) · drift-check `franklin-catalog` rule
4. `server/runs.ts` · `server/control.ts` · `server/ci.ts` + tests
5. Franklin tab UI (cards / CI strip / form / archive / detail+deviations)
6. Compaction
7. Naming: register amendment + CLAUDE.md + `docs/franklin/charter.md`
8. Docs: `docs/franklin/dashboard.md` (how to run/read it); README pointer

## 13. Open questions

- Exact per-test name granularity emitted by the JUCE runner (drives catalog keys) — resolve in plan task 1 by inspecting `TestMain.cpp`'s counting unit; the catalog keys on whatever is emitted.
- None else — Q28 itself stays deferred by design until this ships.
