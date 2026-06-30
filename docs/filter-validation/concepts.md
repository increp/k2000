# Filter-Validation Harness — Concepts

**Version:** 5.09
**Audience:** Audio engineer operating or extending the harness. DSP math is not required.

---

## What the harness proves

The harness measures each filter model (Moog, Huggett, and any future models) and checks that its behaviour matches the textbook ideal for its filter type — correct slope, correct resonance tracking, correct phase behaviour. All measurements are numerical: the harness drives the filter's C++ API directly, records the output, and asserts against expected values. It does not render audio and cannot replace listening. Subjective UAT (does it sound right?) is a separate, later stage.

---

## The layered definition of "correct"

"Correct" has two independent layers:

**(a) Internal correctness** — the model matches the textbook ideal for its own filter type (e.g. a 4-pole Moog ladder gives a 24 dB/octave slope in the stopband, self-oscillation tracks pitch within tolerance). This is what the harness currently checks. It is sub-project 1.

**(b) External correctness** — the model matches the real analog device it emulates: Huggett versus the hardware Summit, Moog versus an Arturia Mini V measured in the same way. This is sub-project 2 and is **not built yet**. The harness is designed so that external reference data can be dropped in later without restructuring the existing tests.

---

## The dual-method ruler

Every frequency-response measurement (magnitude and phase versus frequency) is taken by **two independent methods running in parallel**:

**Stepped-sine method** — Play a steady sine tone at one frequency, wait for the filter to settle, then measure the output level. Repeat for each frequency point in the sweep. Slow, but trivially correct: there is no signal-processing math that can hide a bug. This is the trusted reference.

**Farina exponential-sine-sweep (ESS) method** — Play a single chirp that rises from the lowest to the highest frequency in one pass, then use a deconvolution calculation to recover the complete frequency response in one shot. This is the state-of-the-art single-pass approach and is fast enough to sweep thousands of points in milliseconds.

**Why both?** The ESS is a sophisticated signal-processing technique — a bug in the deconvolution math or in the sweep parameters could silently distort every number it produces without any individual measurement looking obviously wrong. Running both methods and requiring them to agree means that class of error gets caught. On a synthetic reference filter (the L0 self-test) the two methods agree to within 0.090 dB, and on the real models they agree to roughly 0.08–0.6 dB across the meaningful band. The gate threshold is **1.0 dB**, measured **in-band only** (see "The three gates").

---

## Calibration of the ESS

The ESS measures magnitude relative to the test signal itself, so its raw dB values depend on the sweep's energy distribution. To put ESS readings in the same frame as the stepped-sine method (0 dB = unity gain), the harness runs a second "straight wire" reference sweep — the sweep passed through with no filtering — and divides the filter's sweep response by it. After this division the two methods share the same 0-dB reference and their readings are directly comparable.

---

## The three gates

A measurement passes only if it clears all three gates:

**(a) Spec gates** — Assert against the textbook ideal for this filter type. Examples: the magnitude is rolling off above the corner (the gate checks `slope_db_oct <= -3` — an LP24's asymptotic 24 dB/octave is only reached deep in the stopband, and note the authentic Moog/Huggett ladder places its −3 dB corner near **0.44 × the nominal cutoff** at resonance 0, not at the cutoff); self-oscillation pitch tracking within 3% of the expected note up to approximately 4 kHz (above that, tolerance is reported but not a hard fail). These thresholds come from the filter specification, not from a previous measurement.

**(b) Method-agreement gate** — The stepped-sine and ESS results must agree within **1.0 dB**, measured over the **in-band region only** (within 40 dB of the passband peak). The deep stopband is excluded: there both methods approach their numerical noise floor and disagree meaninglessly (an HP filter's near-DC stopband can scatter tens of dB while the passband agrees to a fraction of a dB). If the in-band methods diverge by more than 1.0 dB, the ESS result is suspect and the run fails.

**(c) Self-golden gate** — After a measurement session the harness can commit a fingerprint of the results to the repo. Future CI runs assert that the new measurement has not drifted from that fingerprint beyond a small delta. This catches accidental regressions — a code change that silently shifts the filter's response will fail this gate even if it still passes the spec gates.

---

## The four measurement batteries

**B1 — Frequency response (magnitude)** — Sweeps the filter's magnitude response across a log-spaced probe grid (up to ~700 points in the full grid, 200 in the coarse grid, 40 in the fast gate). Checks that the passband is flat, locates the −3 dB corner, and confirms the slope rolls off. This is the core measurement that most spec gates reference.

**B2 — Resonance and self-oscillation** — Drives resonance to the self-oscillation point and measures the pitch of the resulting tone across several cutoff settings. Checks that the pitch tracks the cutoff frequency within the allowed tolerance, confirming that the feedback loop in the filter model is correctly tuned.

**B3 — Distortion and aliasing** — Applies a sine tone and looks for harmonic content and out-of-band energy in the output. Checks that the filter does not add unexpected harmonics in its normal operating range and that any aliasing products stay below a floor consistent with the oversampling tier in use.

**B4 — Phase and group delay** — Measures the phase shift at each frequency and derives group delay (the rate of change of phase with frequency). Checks that the phase behaviour matches the filter's theoretical all-pole response and that group delay is smooth, without unexpected peaks that would smear transients.

---

## See also

- [operating-points.md](operating-points.md) — how to choose cutoff, resonance, and level settings for each measurement
- [interpreting-results.md](interpreting-results.md) — reading the pass/fail output and what each gate failure means
- [running.md](running.md) — how to run the harness locally and in CI
- [extending.md](extending.md) — adding a new filter model to the harness
- [troubleshooting.md](troubleshooting.md) — common failure modes and fixes
- [README.md](README.md) — overview and quick-start
