# Operating Points and Grid Structure

**Version:** 5.09
**Date:** 2026-06-30

This page describes the parameter axes that define an operating point, the two
grid presets (`coarseGrid` / `fullGrid`), and what `--quick` selects.

---

## The operating-point axis model

Every measurement in the characterization suite is anchored to one
`OperatingPoint`. All six axes are independent and their cross-product is the
grid.

| Axis | Type | Range / values | Notes |
|---|---|---|---|
| `mode` | enum | `LP12`, `LP24`, `BP`, `HP`, `Notch` | Filter topology. A model may not support all modes; unsupported modes are skipped |
| `cutoffHz` | double | 20 Hz .. host Nyquist | Nominal cutoff parameter sent to the model. The **measured physical -3 dB corner** (`corner_hz` in summary.csv) differs from this value (see interpreting-results.md) |
| `resonance` | double | [0.0 .. 1.0] | 0 = no resonance; 1.0 = self-oscillation threshold (model-dependent) |
| `drive` | double | [0.0 .. 1.0] | Nonlinear drive input. 0 = linear path; 1.0 = saturated path |
| `osFactor` | int | 1, 2, 4, 8 | Oversampling factor. The filter runs at `hostSampleRate * osFactor` internally; the VoiceOversampler handles up/down conversion |
| `osMode` | enum | `Live`, `Render` | Selects the oversampler kernel. Live uses the minimum-latency halfband chain; Render uses the higher-quality Render-tier path |
| `hostSampleRate` | double | 44100, 48000, 88200, 96000, 192000 | DAW host sample rate |

---

## coarseGrid — fast/CI mode (`--quick`)

The coarse grid is designed to run fast enough for interactive smoke-testing and
can be triggered on CI via `workflow_dispatch`.

```
hostRates  = [96000]
osFactors  = [1, 2, 4, 8]
osModes    = [Live]
modes      = [LP24, BP, HP]          # 3 modes (LP12 and Notch skipped)
cutoffs    = [250, 1000, 4000]       # 3 coarse cutoff points
resonances = [0.0, 0.9]             # 2 resonance levels
drives     = [0.0]                   # linear only
probeFreqs = 200 log-spaced from 20 Hz to 24000 Hz
```

Operating-point count estimate for B1 (response.csv):
- 3 modes x 3 cutoffs x 2 resonances x 1 drive x 4 osFactor x 1 osMode x 1 hostRate = **72 B1 points**
- B2 (resonance.csv): 3 modes x 3 cutoffs = 9 points
- B3 (distortion.csv): 3 modes x 3 cutoffs x 4 osFactor = 36 points

Each B1 point involves two dual-method sweeps (stepped-sine + ESS) over 200
probe frequencies. Typical wall time: seconds to low minutes on a modern
desktop.

---

## fullGrid — dense/production mode (no `--quick`)

The full grid is intended for a deliberate production characterization run,
not for CI. It may take a very long time to complete (minutes to hours depending
on hardware) and that is expected.

```
hostRates  = [44100, 48000, 88200, 96000, 192000]   # 5 host rates
osFactors  = [1, 2, 4, 8]
osModes    = [Live, Render]                           # both OS modes
modes      = [LP12, LP24, BP, HP, Notch]             # all 5 modes
cutoffs    = 12 log-spaced from 50 Hz to 16 kHz
resonances = [0.0, 0.3, 0.6, 0.9, 1.0]              # 5 levels
drives     = [0.0, 0.5, 1.0]                         # 3 drive levels
probeFreqs = 700 log-spaced from 10 Hz to 25000 Hz
```

Operating-point count estimate for B1 (response.csv):
- 5 modes x 12 cutoffs x 5 res x 3 drive x 4 osFactor x 2 osMode x 5 hostRate
- = 5 x 12 x 5 x 3 x 4 x 2 x 5 = **36,000 B1 operating points**
- B2: 5 modes x 12 cutoffs = 60 points
- B3: 5 modes x 12 cutoffs x 4 osFactor = 240 points

**Do not attempt to run fullGrid to completion during development.** Use
`--quick` for verification and save fullGrid for scheduled production runs.

---

## `--quick` flag

```
k2000_filter_characterization [--model moog|huggett|all] [--quick]
```

| Flag | Grid | Use |
|---|---|---|
| `--quick` | `coarseGrid` | CI smoke / interactive development. Completes in a reasonable time |
| _(absent)_ | `fullGrid` | Production characterization. May be very slow; acceptable for a deliberate run |

The `--model` flag selects which filter under test to run:
- `moog` — Moog ladder filter (`makeMoogFut()`)
- `huggett` — Huggett+HP nonlinear SVF (`makeHuggettFut()`)
- `all` _(default)_ — both models, sequentially

---

## OS-tier decision instrument: alias_db@os\<N\>

The `alias_db@os<N>` family of summary keys is the primary instrument for
validating and comparing oversampling tiers. Understanding how it is measured
and what the values mean is essential before interpreting results or adjusting
OS configuration.

### What is measured

Battery B3 produces an `alias_db` value for each `(model, mode, cutoff, osFactor)`
combination. Crucially, the **aliasing measurement is independent of the grid
operating point** (i.e., it does not depend on `cutoffHz`, `resonance`, or
`drive` from the grid row). B3 uses a fixed internal isolation probe:

- Cutoff at `0.4 * hostSampleRate` — wide open so the probe tone passes the
  filter unattenuated.
- Resonance = 0.9, drive = 1.0 — engages the nonlinear `tanh` path so harmonics
  are generated at the operating sample rate.
- Tone at `0.35 * hostSampleRate` — the second harmonic (H2) lands at
  `0.70 * hostSampleRate`, which is **above the base-rate Nyquist** at `os1`
  (folds back as an alias) but **below the internal Nyquist** at `os2` (does
  not fold).

The output spectrum's inharmonic energy (`Metrics::inharmonicDb`) is the
reported `aliasDb`. A more negative value means lower aliasing.

### Interpreting the curve

| OS factor | Tier label | Example Moog LP24 fc4000 measurement |
|-----------|------------|--------------------------------------|
| 1x        | None       | 0.0 dB (worst — folded harmonics at full power) |
| 2x        | Live       | -29.0 dB |
| 4x        | Live HQ    | -80.5 dB |
| 8x        | Render     | -117.6 dB (best) |

The Moog curve shows the expected monotonic improvement: each doubling of
`osFactor` roughly halves (in dB/octave terms) the folded harmonic energy,
with the largest single step at `os1 -> os2` (~29 dB) because that is where
H2 first clears the base-rate Nyquist ceiling.

An improvement of ~100 dB from os1 to os8 is the expected pattern for a
well-implemented oversampling path. The regression assertion requires at minimum
`alias_db@os8 < alias_db@os1 - 3.0 dB` — a deliberately conservative threshold
(the real margin is ~100 dB) so a genuine regression cannot hide behind noise.

### Live vs Render

`Live` and `Render` are labels describing the OS mode (halfband filter chain
quality), not separate audio engines. The harness measures the `osFactor`
directly; `osMode` is metadata. In the B3 isolation probe, `osMode` is always
set to `Live` (the base-case condition) so `alias_db@os<N>` values from different
grids are directly comparable. Render-mode aliasing is not separately tracked
in summary keys — the Live-mode curve is the OS-tier decision instrument.

---

## Summary key base-case selection

When the grid has many operating points per `(model, mode, cutoff)` triple,
the `summary.csv` stores only ONE value per metric per triple, anchored to the
**base operating point**:

- `osFactor = 1`
- `osMode = Live`
- `resonance` = lowest resonance in the grid
- `hostSampleRate` = 96000 Hz (or the grid's first host rate if 96000 is absent)

This keeps summary keys deterministic regardless of grid density, so
`summary.csv` from coarseGrid and fullGrid have the same key schema (fullGrid
produces more keys because it covers more modes and cutoffs).
