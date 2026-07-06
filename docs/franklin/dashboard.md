# Franklin — Runs Dashboard Manual

**Version:** 5.22 (artifact; distinct from plugin SemVer — see `CLAUDE.md` process rails)
**Date:** 2026-07-03

How to run and read the Franklin tab: the live view of Franklin's runs (device
characterization + the test suite), its archive, and its run control. Design
rationale lives in the spec; this doc is the operator's manual. See
`docs/franklin/charter.md` for what Franklin is and why it exists.

## How to run

```bash
cd tools/roadmap-dashboard && npm run dashboard
```

Open `http://localhost:4173`, then click the **Franklin** tab (beside **Roadmap**).

Runs record to disk the moment a Franklin producer (chz or the test suite) starts —
whether or not the dashboard is running. Starting the dashboard only gives you a
window onto runs that are already happening or already recorded; it never gates
whether a run is captured.

## Event-file contract

Every run writes one NDJSON file to `.franklin/runs/<stamp>-<kind>-<pid>.ndjson`
(`kind` is `chz` or `suite` today — a future kind such as `capture` flows through
generically; the pid suffix disambiguates same-second starts), one
JSON object per line, append-only. The full event
schema (`start` / `progress` / `test` / `end`, exact fields, and the write-throttle
rule) is specified in
`docs/superpowers/specs/2026-07-03-franklin-dashboard-design.md` §4 — that section
is the source of truth; this doc only summarizes operator-visible behavior.

The directory is gitignored: it survives `rm -rf build/` but is never committed.

## Environment variables

- `BERNIE_NO_RUNLOG=1` — disables runlog writing entirely for that invocation. Use
  this if you want a Franklin producer to run with zero disk side effects (e.g.
  scripted CI runs that already have their own logging).
- `BERNIE_RUNLOG_DIR` — overrides the runlog directory (default: `./.franklin/runs`
  under the current working directory, which is the repo root by convention).

- `BERNIE_RUNNER` — overrides the auto-detected run provenance (see below) with a
  verbatim string. Rarely needed; the detection chain is right for normal use.

A runlog write failure never affects the measurement itself — the writer disables
itself silently on the first error and the run proceeds normally.

## Run provenance (who ran it)

Every run records **who started it** in its `start` event, shown as a banner on the
run header and on each test's info card. Detection priority, highest first:

1. `BERNIE_RUNNER` env set → its verbatim value.
2. `GITHUB_ACTIONS` set → **CI**.
3. `CLAUDECODE` set (Claude Code's shell) → **Claude**.
4. otherwise → **you (terminal)**.

Runs you start from the dashboard's **Start** button are stamped **you (dashboard)** —
the dashboard injects `BERNIE_RUNNER=dashboard` into the child, so a run you launched
by clicking is attributed to you, not to Claude, even when the dashboard was itself
started from a Claude Code shell. Runs recorded before provenance existed show
**unknown** (archives are never rewritten).

## Test info cards

Each test in a run's detail view expands to a six-field card: **What** it does ·
its **Purpose** · what it **Compares** (the actual reference — a golden file, an
analytic form, a fixed threshold, or cross-method agreement) · what **success**
means in product terms · what **failure** means · and **who ran** it. The fields
come from `docs/franklin/test-catalog.json` (v2); the `franklin-catalog` drift rule
WARNs (in the `session` and `ci` tiers) on any test whose card is missing a field
or any card with no matching test — so a half-carded test is flagged loudly, though
the warning does not hard-block a commit.

## Catalog browser

The Franklin tab has a collapsible **Catalog** section: a search box over all test
cards (matches keys, files, and every prose field). Each card here shows **Last
result** (the newest archived suite run's pass/fail for that test) in place of the
per-run "Run by".

## What "stalled" means

A run with no `end` event whose file hasn't been touched in over 120 seconds is
shown as **stalled** rather than **running**. This is a heuristic for "probably
crashed or hung, but we can't be certain" — it doesn't stop anything or change the
recorded outcome; a stalled run can still complete normally and later append its
`end` event, at which point it becomes a normal finished run.

## Compaction (never deletes)

Franklin never deletes an archived run. When a run finishes (or is first
discovered finished by the server), its file is rewritten to keep the `start`
event, the `end` event (which carries every gate/golden `checks[]` verdict), and
**every** `test` event verbatim — only the `progress` stream is downsampled to at
most 500 evenly-spaced points. A 40-hour chz run compacts from tens of megabytes to
roughly 100 KB with no loss of verdicts, deviations, or metrics — only the
intermediate progress-bar resolution is thinned. Compaction is atomic (write a
temp file, then rename); if it fails, the original file is left intact.

## Start / stop / re-run

- **Start** — the new-run form only offers **whitelisted templates**: the test
  suite (plain, or with `BERNIE_RUN_DISPARITY=1` / `BERNIE_RUN_VOICEPERF=1`), or
  chz characterization (model ∈ `moog` / `huggett` / `all`, grid ∈ `quick` /
  `full`). Arbitrary commands are never accepted.
- **Stop** — the server verifies the recorded PID's `/proc/<pid>/cmdline` still
  contains the expected binary name before signaling anything (never signal a
  reused PID). It sends `SIGTERM`, waits 5 seconds, then escalates to `SIGKILL` if
  the process hasn't exited. The archived run's outcome is marked `stopped`.
- **Re-run** — replays the recorded template from an archived run. Suite re-runs
  are always **plain** — a suite run's env flags (e.g. `BERNIE_RUN_VOICEPERF=1`)
  aren't recoverable from the recorded argv, so re-run cannot reproduce them; if you
  need a flagged suite run again, start it fresh from the form.

## Where chz CSVs land

Device-characterization CSV output (the actual measurement data, separate from the
runlog's progress/status events) lands under `build/characterization/<model>/`, as
it always has — Franklin's runlog is a record of *that a run happened and how it
went*, not a replacement for the CSV output.

## Test catalog + drift rule

`docs/franklin/test-catalog.json` holds one entry per test-suite test (what it
does, why it exists, what a deviation would mean, and related links), rendered in
the run-detail view next to each test's pass/fail. The `franklin-catalog`
drift-check rule (`tools/drift-check`) cross-references the catalog against the
**latest suite runlog** in `.franklin/runs/` (picked by file mtime — the runlog
carries the full `name / subcategory` pair that the plain suite log does not), in
both directions — a test with no catalog entry, or a catalog entry with no
matching test, is a WARN. Chz points are not cataloged;
their explanations are generated client-side from battery templates + the
operating-point fields already present in the events.

## See also

- `docs/franklin/charter.md` — what Franklin is and its remit map.
- `docs/superpowers/specs/2026-07-03-franklin-dashboard-design.md` — full design
  spec (architecture, server APIs, error handling, testing).
- `tools/roadmap-dashboard/README.md` — the dashboard tool's own README, including
  the Franklin tab section.
