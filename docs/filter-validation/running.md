# Running the Filter-Validation Harness

**Version:** 5.09
**Date:** 2026-06-30

---

## Two ways to run

| Path | When | Command |
|---|---|---|
| Always-on gate | Every build | `./build/tests/k2000_tests` |
| Opt-in heavy runner | On demand (never CI) | `./build/tests/k2000_device_characterization ...` |

---

## 1. Always-on gate (`CharacterizationGate` inside `k2000_tests`)

This runs automatically as part of the standard test binary. It exercises a small fixed grid (Moog + Huggett, LP24 at fc 1000 Hz, OS factor 1) and asserts two classes of properties:

- **Spec gates** — slope is rolling off correctly; in-band method-agreement is < 1 dB between the stepped-sine and ESS methods.
- **Self-golden baselines** — the measured fingerprint matches a committed CSV snapshot in `tests/golden/<model>/baseline.csv`.

### Build and run

```
cmake --build build --target k2000_tests -j4
./build/tests/k2000_tests
```

Expected overhead: approximately 5 seconds on top of the rest of the test suite (the gate runs a tiny os1 grid — about three ESS sweeps total across the two models, at a coarse 40-point probe grid). Measured: the full suite is ~76 sec vs ~71 sec without the gate.

### Refreshing the self-golden baseline

When a deliberate DSP change shifts the filter fingerprint, the gate will fail CI. Regenerate with:

```
BERNIE_UPDATE_GOLDEN=1 ./build/tests/k2000_tests
```

This overwrites `tests/golden/<model>/baseline.csv`. Review the diff carefully — confirm that every changed column is explained by your intended change — then commit the new file alongside your code change.

Without the env var, the gate asserts against the committed baseline. An unexplained shift is a regression and should be investigated, not blindly regenerated.

---

## 2. Opt-in heavy runner (`k2000_device_characterization`)

This binary is for deep characterization work. It is **not run in CI** and should be launched deliberately.

### Syntax

```
./build/tests/k2000_device_characterization --model <moog|huggett|all> [--quick]
```

### Grid sizes

| Flag | Grid | Host rates | OS modes | Approximate runtime |
|---|---|---|---|---|
| `--quick` | Bounded coarse grid | 96 kHz only | OS 1, 2, 4, 8 — Live mode | 10–25 min |
| _(no flag)_ | Full dense grid | 5 host rates | Both OS modes, dense params | Many hours |

Launch the dense full grid deliberately. The `--quick` path covers the most common operating point and is the routine opt-in check; the full grid (~36 000 operating points) is for comprehensive characterization before a release or a filter-model change.

### Output

Ephemeral CSVs are written to `build/characterization/<model>/`:

| File | Contents |
|---|---|
| `response.csv` | Magnitude + phase per probe frequency per method |
| `resonance.csv` | Self-oscillation pitch tracking |
| `distortion.csv` | THD per operating point |
| `summary.csv` | One-row-per-operating-point digest |

The runner also prints a terminal digest showing the worst method-agreement delta, worst aliasing improvement (OS 1 vs OS 8), worst THD, and worst self-oscillation pitch error.

### Exit code

The runner exits non-zero if the worst in-band `method_delta_db` is >= 1.0 dB (the spec gate threshold). Zero means all operating points passed.

---

## 3. Windows CI (MSVC compile + smoke gate)

Per project convention, MSVC is the real compilation gate. Local GCC builds can pass while MSVC-only issues hide.

Push to a feature branch and trigger CI manually:

```
gh workflow run build.yml --ref <branch>
```

The push trigger only fires on `main`; feature branches need the manual dispatch above.

Watch the run:

```
gh run watch <run-id>
```

The CI job builds the full plugin and runs `k2000_tests` (which includes the `CharacterizationGate`). It does not run the heavy opt-in characterization binary.

---

## Summary

- **Routine** — build and run `k2000_tests`; the gate takes ~6 sec and gates CI.
- **Deliberate deep dive** — run the characterization binary with `--quick` (10–25 min) or without (hours).
- **After a deliberate DSP change** — update and commit the golden baseline before pushing.
- **Trusted compile gate** — trigger MSVC CI manually on the feature branch before merging.

## Live progress

The heavy runner shows a single overwriting status line on **stderr** while it
works — `[model] done/total (pct%)  elapsed  eta  current-point` — so long
grids are never a black box (engagement item 6, 2026-07-03). stdout stays
machine-readable. Disable with `BERNIE_NO_PROGRESS=1` when redirecting stderr
to a log. Programmatic consumers pass a `CharacterizationRunner::Progress`
sink (4th argument of `run`); tests omit it and stay silent.
