---
name: characterize-filter
description: Run the filter-characterization harness for a model and present a readable report. Invoke as /characterize-filter <moog|huggett|all> [quick].
argument-hint: "<moog|huggett|all> [quick]"
---

# characterize-filter

Interactive front door over the self-sufficient `k2000_filter_characterization`
binary. This skill adds interpretation, the baseline-update offer, and
follow-up Q&A only. If this skill vanished, the binary, CI, and golden tests
would all continue working without it.

## Boundary (state once, never revisit)

The binary at `./build/tests/k2000_filter_characterization` is fully
self-sufficient. This skill does NOT reimplement DSP, alter sweep logic, or
write CSV files. Its only value is reading the binary output and making it
human-readable.

---

## Step 1 — Parse arguments

Parse `$ARGUMENTS` (the text after `/characterize-filter`):

- First token: model name. Accept `moog`, `huggett`, or `all`. Default to
  `all` if omitted or unrecognised.
- If the token `quick` appears anywhere in `$ARGUMENTS`, set `QUICK=true`.

Before running, state the chosen model and grid. Recommend `--quick` for
interactive use: the full grid is MANY HOURS (dense production sweep);
`--quick` (coarseGrid, 96 kHz, OS {1,2,4,8}, live-only) takes ~10-25 minutes.
If the user did not pass `quick` and the model is `all`, warn them and ask if
they want to add `--quick` before proceeding.

---

## Step 2 — Build the target

Run from the repo root:

```bash
cmake --build build --target k2000_filter_characterization -j4
```

Use `-j4`, not bare `-j` (bare `-j` OOMs a JUCE compile and produces 0-byte
objects that cause confusing link failures).

Stop and report the build error if the command exits non-zero.

---

## Step 3 — Run the binary

```bash
./build/tests/k2000_filter_characterization --model <model> [--quick]
```

Where `<model>` is the parsed model name and `[--quick]` is appended only if
`QUICK=true`.

Capture stdout (the printed digest) and the exit code. Exit 0 = PASS (all
method-agreement deltas < 1.0 dB). Exit 1 = FAIL (at least one delta >= 1.0
dB). The binary also writes four CSV artifacts per model:

```
build/characterization/<model>/response.csv
build/characterization/<model>/resonance.csv
build/characterization/<model>/distortion.csv
build/characterization/<model>/summary.csv   <- key headline metrics
```

---

## Step 4 — Read the summary

Read `build/characterization/<model>/summary.csv` (key,value format) for each
model that was run. The binary already printed the worst-value digest to
stdout; use the CSV to fill in any per-mode breakdowns you want to surface.

For an `all` run, read both `build/characterization/moog/summary.csv` and
`build/characterization/huggett/summary.csv`.

---

## Step 5 — Present a concise, readable report

For each model, emit a short report using these headline metrics:

### Method-agreement verdict (cross-validation gate)

`method_delta_db` — worst across all modes/fc combinations. Gate = 1.0 dB.
- PASS if worst delta < 1.0 dB
- FAIL if worst delta >= 1.0 dB (flag clearly; report the key name from stdout)

### -3 dB corner

`corner_hz` from LP24 at the tested fc values. Note that an authentic 4-pole
Moog or Huggett at res=0 places the -3 dB point at ~0.44 * fc (not 1.0 * fc).
Report whether the measured corners are consistent with this expectation. Also
report `slope_db_oct` (expected ~-24 dB/oct for LP24).

### Self-oscillation pitch accuracy

`selfosc_cents_err` — worst absolute value across all conditions. Typical spec:
accurate to within 3% (~51 cents) for fc <= ~4 kHz; report any values above
that threshold.

### THD

`thd_db` — worst (highest / least negative) value across all conditions. More
negative is cleaner. Report the worst value and flag if it is unexpectedly
high (above ~-20 dB is noteworthy for a filter model without intentional
drive).

### Aliasing vs oversampling digest

`alias_db@os1` worst (highest) value — 0 dB means full aliasing at 1x; very
negative is clean. `alias_db@os8` best (lowest) value — shows how much
oversampling buys. Present as: `alias_db@os1 worst = X dB -> @os8 best = Y
dB`. Intermediate tiers (os2, os4) may be summarised.

### Overall verdict line

End with a single bold line: `PASS` or `FAIL`, plus the binary exit code.

---

## Step 6 — Self-golden drift check

After presenting the report, run `./build/tests/k2000_tests` (do NOT pass
`BERNIE_UPDATE_GOLDEN=1`). If it fails with a golden-baseline mismatch for a
filter model, surface WHAT changed:

- Which CSV keys drifted and by how much.
- Whether the change is consistent with any DSP edits on the current branch.

Then OFFER (do not auto-apply) to refresh the baseline:

> "The golden baseline for `<model>` is stale. To update it, I can run:
> `BERNIE_UPDATE_GOLDEN=1 ./build/tests/k2000_tests`
> This will overwrite `tests/golden/<model>/baseline.csv`. You should then
> review the diff with `git diff tests/golden/<model>/baseline.csv` and
> commit the updated file intentionally. Do you want to proceed?"

NEVER run `BERNIE_UPDATE_GOLDEN=1` without explicit user confirmation.

After a user-confirmed update, remind them to:
1. `git diff tests/golden/<model>/baseline.csv` — review the actual numbers.
2. `git add tests/golden/<model>/baseline.csv && git commit -m "chore: refresh <model> golden baseline"`.

---

## Step 7 — Follow-up Q&A

Remain available to answer interpretation questions about the metrics, compare
against a prior run, or explain any spec deviation. Do not re-run the binary
unless the user asks.
