# Troubleshooting the Filter-Validation Harness

**Version:** 5.01
**Date:** 2026-06-30

---

## Known noise in the test output

### `JUCE Assertion failure in juce_String.cpp:327`

**Pre-existing, not from the harness.** This is a known `juce::String(const char*)` UTF-8 issue that fires in other tests (Voice, AlgorithmName, MultiLayer, MoogPipeline — approximately 7 occurrences). The characterization harness does not use the affected code path. Ignore these lines; they do not affect harness results.

---

## Metrics look wrong

### A huge method-agreement delta — tens of dB

**Almost always the deep stopband.** The dual-method gate is scoped to within 40 dB of the passband peak (`maxMagDeltaDbInBand`). Below that threshold, both the stepped-sine method and the ESS method approach their respective noise floors and the measurements scatter. For example, a Moog HP filter scatters approximately 5 dB at -60 dB but agrees to within 0.7 dB down to -40 dB.

The `method_delta_db` column in `summary.csv` already uses the in-band metric. If you compute an unscoped delta across the full sweep, the result will look alarming and is not a real disagreement. Use the in-band figure.

### A metric reads `-1.0`

**Sentinel value — not a measurement.** `-1.0` signals a degenerate or failed measurement: self-oscillation never started, or a NaN/inf guard fired. A `WARNING` is logged via `juce::Logger` at the time of the failure. Find the corresponding log line to identify which operating point was affected. Do not treat `-1.0` as a numeric result.

### `corner_hz` is much lower than the nominal cutoff

**Expected behaviour.** The authentic 4-pole Moog and Huggett ladder topologies place their -3 dB corner at approximately 0.44 × fc at resonance = 0 (four cascaded 1-pole stages, each contributing a quarter-octave of rolloff). The `slope_db_oct` value is a transition-band reading, not the asymptotic stopband rate. This is not a bug.

### `corner_hz` for Notch mode is a huge or degenerate number

**Known limitation.** The notch topology has a split passband — signal passes on both sides of the rejection band — so a single -3 dB crossing scan is not a meaningful descriptor. The value is a placeholder that confirms the mode ran and completed without error. Tight Notch metrics are deferred to future work.

---

## Gate failures

### Self-golden baseline fails after an intentional DSP change

Regenerate:

```
BERNIE_UPDATE_GOLDEN=1 ./build/tests/k2000_tests
```

Then inspect `tests/golden/<model>/baseline.csv` in `git diff`. Every changed row should be explained by your DSP change. If the diff looks right, commit the new baseline alongside the code change.

If you did NOT intend any filter behaviour change and the baseline still differs, it is a regression. Investigate before committing.

### Windows CI shows errors that local GCC did not

Two usual culprits:

- **`M_PI` is not defined.** MSVC does not define `M_PI` by default. Use `juce::MathConstants<double>::pi` instead.
- **Stack overflow from large local objects.** MSVC enforces a ~1 MB stack limit. Large arrays or objects declared locally (especially inside DSP loops) must be heap-allocated. GCC's larger default stack often hides these silently.

See `running.md` for how to trigger the Windows CI job manually on a feature branch.
