# Running the Filter-Validation Harness

**Version:** 5.25
**Date:** 2026-07-07

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
./build/tests/k2000_device_characterization --model <moog|huggett|all> --grid <name>
```

`--grid` accepts seven names (`--quick` still works as a back-compat alias for
`--grid quick`). Each purpose grid is dense only along the axes its purpose
needs — see `docs/superpowers/specs/2026-07-07-purpose-grids-design.md` §3 for
the full axis-by-axis design and empirical cost model:

| Grid | Purpose | Points | Est. duration |
|---|---|---|---|
| `quick` | CI/smoke gate — bounded coarse grid at the most common operating point | ~72 | ~4 min |
| `spd` | SP-D hardware-comparison map (dense cutoff×res at capture-matched rate+OS) | 450 | ~75 min |
| `osalias` | OS/aliasing verification (full OS axis, aliasing-stress points) | 192 | ~10 min |
| `rates` | Host-rate portability spot-check | 120 | ~8 min |
| `largesig` | Large-signal/drive law (res×drive operating-point lattice) | 180 | ~10 min |
| `deep` | All four purpose grids above, in sequence, per model | 942 | ~1.7–2.0 h |
| `full` | Legacy exhaustive dense grid (36,000 raw crossings/model) — never a routine default | ~29,088 | ~40 h |

"Points" = grid crossings (the axis product). The dashboard's live counter differs
two ways: it counts *measurement units* — per supported (mode, cutoff) pair, the
B2+noise batteries and one B3 per OS factor are added on top of the B1 crossings —
and it first drops modes the model doesn't support. So `rates` shows `/144` for
both models, while `spd` shows `/540` for Moog (no Notch → 4 of 5 modes) and
`/675` for Huggett. Same work, finer-grained accounting; the ETA always follows
the live total.

Launch `deep` (or an individual purpose grid) for routine characterization —
it replaces routine `full` use. The legacy `full` grid is retained for
comprehensive characterization before a release or a filter-model change, but
is never the default path of any workflow.

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
- **Deliberate deep dive** — run the characterization binary with `--grid <name>` (`quick` ~4 min up through `deep` ~2 h and legacy `full` ~40 h — see the grid table above).
- **After a deliberate DSP change** — update and commit the golden baseline before pushing.
- **Trusted compile gate** — trigger MSVC CI manually on the feature branch before merging.

## Live progress

The heavy runner shows a single overwriting status line on **stderr** while it
works — `[model] done/total (pct%)  elapsed  eta  current-point` — so long
grids are never a black box (engagement item 6, 2026-07-03). stdout stays
machine-readable. Disable with `BERNIE_NO_PROGRESS=1` when redirecting stderr
to a log. Programmatic consumers pass a `CharacterizationRunner::Progress`
sink (4th argument of `run`); tests omit it and stay silent.
