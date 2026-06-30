# Interpreting Filter Characterization Results

**Version:** 5.09
**Date:** 2026-06-30

This page explains every column and key produced by `k2000_filter_characterization`.
All CSVs land in `build/characterization/<model>/` after a run.

---

## response.csv — B1 magnitude + B4 phase/group delay

One row per probe frequency per measurement method. Two rows per probe frequency
(one `stepped`, one `ess`) for every operating point in the grid.

| Column | Type | Meaning |
|---|---|---|
| `model` | string | Filter model name (e.g. `moog`, `huggett`) |
| `mode` | string | Filter mode: `LP12`, `LP24`, `BP`, `HP`, `Notch` |
| `osFactor` | int | Oversampling factor: 1, 2, 4, or 8 |
| `osMode` | string | Oversampling mode: `live` or `render` |
| `hostSR` | int | Host sample rate in Hz (e.g. 96000) |
| `cutoffHz` | float | Nominal cutoff frequency in Hz (the parameter value sent to the filter, NOT the measured physical corner) |
| `resonance` | float | Resonance parameter [0..1] |
| `drive` | float | Drive parameter [0..1] |
| `probeHz` | float | Probe tone frequency in Hz |
| `method` | string | `stepped` (stepped-sine, reference) or `ess` (calibrated exponential sine sweep, Farina method) |
| `magDb` | float | Transfer function magnitude at `probeHz` in dB relative to passband |
| `phaseRad` | float | Unwrapped phase in radians. For `stepped` rows, this is the measured steady-state phase shift. For `ess` rows, phase is extracted from the complex impulse response |
| `groupDelaySec` | float | Group delay in seconds. **Present only for `ess` rows** — the column is empty (blank field) for `stepped` rows, because the stepped-sine measurement does not produce a group delay estimate |

> **B4 phase / group delay are descriptive-only today (known limitation).** The `phaseRad`
> and `groupDelaySec` values from the `ess` method are NOT yet time-aligned: the deconvolved
> impulse response sits at the linear-convolution centre (~N samples), whose bulk latency
> wraps the phase far faster than the probe grid can unwrap (a synthetic 30-sample delay reads
> a group delay ~9× off). Absolute phase / group-delay values are therefore unreliable, and
> **no gate depends on them** — the magnitude path (B1), the method-agreement gate, and every
> spec / self-golden gate are magnitude-only and unaffected (the magnitude calibration is
> provably delay-invariant; see the `EssResponse magnitude is delay-invariant` test).
> Time-aligning the IR before the transfer function is a tracked follow-up (engine-questions Q20).

### Method-agreement delta

The key metric extracted from response.csv and stored in summary.csv is
`method_delta_db`: the maximum absolute difference in `magDb` between the
`stepped` and `ess` rows at the same operating point, taken over all probe
frequencies. A delta below 1.0 dB is the pass criterion.

A large delta signals a sweep artefact (pre-ringing in the ESS deconvolution,
or a settling issue in the stepped-sine). Values above 0.5 dB are worth
investigating; above 1.0 dB the run exits non-zero.

---

## resonance.csv — B2 self-oscillation pitch

One row per `(mode, cutoff)` pair at max resonance, `osFactor=1`, `osMode=live`,
base host rate. Moog LP24 self-oscillates at high resonance; other modes/models
may not.

| Column | Type | Meaning |
|---|---|---|
| `model` | string | Filter model name |
| `mode` | string | Filter mode |
| `osFactor` | int | Always 1 for the B2 measurement (base condition) |
| `osMode` | string | Always `live` for the B2 measurement |
| `hostSR` | int | Host sample rate in Hz |
| `cutoffHz` | float | Nominal cutoff in Hz |
| `resonance` | float | Resonance at which self-osc was measured (max resonance in grid) |
| `selfoscHz` | float | Measured self-oscillation pitch in Hz (FFT peak of 4096-sample ring-down). `-1` when no ring energy was detected (the guard threshold is 1e-4 amplitude) |
| `selfosc_cents_err` | float | `1200 * log2(selfoscHz / cutoffHz)`. Zero would mean the filter self-oscillates exactly at the nominal cutoff. The Moog ladder's warping means it self-oscillates below the nominal cutoff. `-1` when self-osc was not detected |

### Reading self-osc in cents

Negative cents: filter self-oscillates below the nominal cutoff (expected for
the Moog ladder due to pre-warping). At `cutoffHz > 4 kHz` the measurement is
reported with a log note but still valid. The ±3% (~52 cents) target from the
filter-validation standard applies up to ~4 kHz; above that, report and inspect.

---

## distortion.csv — B3 THD and aliasing

One row per `(mode, cutoff, osFactor)` at base resonance and base drive,
`osMode=live`, base host rate. The row has two distinct measurement conditions
in the same row, separated by column groups.

**Important**: the `alias_*` columns use a **different operating point** from
the THD columns. This is intentional — the aliasing isolation probe uses a
fixed high-cutoff / high-drive / high-resonance condition independent of the
grid's `cutoffHz`/`drive`/`resonance`. The alias probe is chosen to maximise
foldback contrast between OS tiers, not to characterise the same condition as
the THD measurement.

| Column | Type | Meaning |
|---|---|---|
| `model` | string | Filter model name |
| `mode` | string | Filter mode |
| `osFactor` | int | Oversampling factor for this row |
| `osMode` | string | `live` (B3 always uses the Live OS pipeline) |
| `hostSR` | int | Host sample rate |
| `cutoffHz` | float | Grid cutoff for the THD measurement |
| `resonance` | float | Grid resonance for the THD measurement (base resonance) |
| `drive` | float | Grid drive for the THD measurement (base drive) |
| `thd_probeHz` | float | Probe tone used for THD (closest probe frequency in the grid to 1 kHz) |
| `thd_db` | float | THD in dB (ratio of total harmonic power to fundamental). More negative = less distortion |
| `alias_cutoffHz` | float | Cutoff used for the alias isolation probe: `0.4 * hostSR`. **Differs from `cutoffHz`** |
| `alias_resonance` | float | Resonance for the alias probe: 0.9 (fixed) |
| `alias_drive` | float | Drive for the alias probe: 1.0 (fixed) |
| `alias_toneHz` | float | Probe tone for aliasing: `0.35 * hostSR`. At `osFactor=1` its second harmonic (`0.70 * hostSR`) lies above base-rate Nyquist and folds back; at `osFactor=2` the second harmonic is below the internal Nyquist and does not fold |
| `alias_db` | float | Inharmonic energy relative to fundamental in dB (from FFT). Less negative = more alias energy. At `osFactor=1` you expect a relatively high value; at `osFactor=8` it should be much lower |

---

## summary.csv — headline metrics

Key-value CSV (`key,value`) covering one scalar per `(model, mode, cutoff)`
combination. Keys are formed as `<model>/<mode>/fc<cutoff_rounded>/<metric>`.
The operating point is always the base case: `osFactor=1`, `osMode=live`,
lowest resonance in grid, host rate 96000 Hz (or grid's first if 96000 absent).

| Key suffix | Meaning |
|---|---|
| `corner_hz` | Measured physical -3 dB point in Hz. For LP modes, anchored to the DC-adjacent passband level. **For the Moog LP24 at res=0 this is approximately 0.44 × nominal cutoff**, NOT the nominal cutoff, because the Moog ladder's bilinear pre-warping shifts the physical pole. For HP it anchors to the high-frequency passband. For BP/Notch it is the first upward crossing relative to the peak |
| `slope_db_oct` | Magnitude difference one octave above `corner_hz` in dB. This is a transition-band reading, not the asymptotic stopband rate. A four-pole LP shows approximately -20 dB/oct at one octave above the corner |
| `method_delta_db` | Maximum magnitude difference (dB) between `stepped` and `ess` at this `(mode, cutoff)` base condition. **Values >= 1.0 dB cause a non-zero exit** |
| `selfosc_cents_err` | Self-oscillation pitch error in cents at max grid resonance (B2 result) |
| `thd_db` | THD in dB at `osFactor=1`, base drive, base resonance (B3 result) |
| `alias_db@os1` | Inharmonic alias energy at `osFactor=1` (B3 isolation probe) |
| `alias_db@os2` | Same at `osFactor=2` |
| `alias_db@os4` | Same at `osFactor=4` |
| `alias_db@os8` | Same at `osFactor=8` |

### Exit code

`k2000_filter_characterization` returns 0 (PASS) when the worst `method_delta_db`
across all summary keys is below 1.0 dB. It returns 1 (FAIL) otherwise.
