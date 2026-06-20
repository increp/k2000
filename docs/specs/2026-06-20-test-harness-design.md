# Bernie DSP Test Harness — design spec

> One-line purpose: a robust, reusable, per-component **automated** DSP test harness for Bernie's
> spine/source DSP (and later Ricky's FX) with objective, ear-independent metrics and per-component
> **pass/fail gates that fail CI on regression.**
>
> **Version:** 5.01 · **Status:** design spec, written 2026-06-20 · **Owner:** spine/DSP
> **Plugin SemVer at authoring:** 5.1.0 · **Supersedes:** the ad-hoc, print-only diagnostic style of
> [`OverdriveDiagnosticTests`](../../tests/OverdriveDiagnosticTests.cpp) (which is the *seed* — it
> stays, but its metrics become gated).

## 0. Why this exists (and the evidence it works)

The v5.0 remediation is the proof of value: ADAA was implemented per
[antialiasing-adaa.md](../architecture/antialiasing-adaa.md), then **measured worse** than plain
`tanh` across Bernie's drive range — it added inharmonic energy rather than removing it. The
`g_eff` "darken when loud" droop and the HP drive were likewise removed. None of these were caught by
ear; they were caught by the inharmonic-energy FFT metric in `OverdriveDiagnosticTests`. The current
shapers are plain asymmetric `tanh`; oversampling is deferred to v5.1.

The lesson: **objective DSP measurement is a first-class deliverable**, not test scaffolding. This
spec turns the one-off diagnostic into a reusable platform with gates, so that a future change which
re-introduces aliasing, a click, a Q error, a NaN, or a zipper **fails CI** instead of shipping.

Related: [phases.md](../roadmap/phases.md) "Testing harness — next deliverable (cross-cutting)";
component docs [nonlinear-filter-modeling.md](../architecture/nonlinear-filter-modeling.md),
[huggett-filter.md](../architecture/huggett-filter.md) §"Validation, test vectors, and audio QA",
[tpt-svf-core.md](../architecture/tpt-svf-core.md).

## 1. Framework decision: extend JUCE `UnitTest` + `juce_dsp::FFT` (recommended)

**Recommendation: keep JUCE `UnitTest` as the test/runner layer; build the metric + signal +
oversampled-reference machinery as a *standalone, JUCE-light* support library that the fixtures call
into.** Do **not** adopt a third-party framework (Catch2 / GoogleTest / doctest).

Rationale:

- **The DSP under test already links `juce_dsp`** (see [tests/CMakeLists.txt](../../tests/CMakeLists.txt));
  `juce::dsp::FFT` is the existing aliasing-metric workhorse and is already validated in-repo. A new
  framework adds a dependency and a second runner for zero metric benefit.
- **The seed and 20+ existing test files are `juce::UnitTest`** (Smoke, Voice, Algorithm, Spine,
  Huggett, Hp…). A framework swap is a large, risk-only refactor with no payoff for the actual hard
  part — the *metrics*, which are framework-agnostic C++/FFT.
- The custom runner ([TestMain.cpp](../../tests/TestMain.cpp)) already returns non-zero on failure
  (CI gate works today) and prints a per-test pass/fail summary. The only real weakness — `logMessage`
  is not surfaced, diagnostics go through `std::printf` — is **fixed by this spec** (see §6), and is
  orthogonal to framework choice.
- The cost of a framework (Catch2's macro magic, GoogleTest's build weight) buys us assertion sugar we
  don't need: DSP gates are `expect(metric <= threshold, descriptive-message)`, which `UnitTest`
  already does cleanly.

**The decisive design move is therefore *not* the framework — it is extracting the metrics and signal
generators out of the test bodies into a reusable, independently-testable support library** (`testdsp/`).
That library has no JUCE-UnitTest dependency, so if we ever *do* outgrow the runner, the valuable code
(metrics, references, generators) ports unchanged.

## 2. Architecture

Four layers, lowest to highest:

```
  testdsp/   (reusable, framework-agnostic support library — the real deliverable)
    SignalGen.h        bin-aligned sines, swept sines, impulses, steps, white/pink noise,
                       saw/square (bandlimited refs), parameter ramps, two-tone (IMD)
    Spectrum.h         FFT magnitude (Hann + bin-aligned variants), windowing, RMS,
                       A-weighting curve, peak/crest
    Metrics.h          the metric catalog (§4) as free functions over float buffers
    Reference.h        OversampledReference<Process> — runs a process at M× via a
                       hand-rolled ideal-ish decimator and returns the bandlimited "truth"
    ProcessAdapter.h   thin callable wrappers so any component (memoryless shaper, stateful
                       filter, whole spine) presents a uniform `process(buffer, n)` face
    Gate.h             ResultGate: metric + threshold + direction + label -> expect()-able
    GoldenIO.h         (phase 2) load/store reference WAV/CSV "golden" vectors for regression

  fixtures/  (per-component juce::UnitTest classes; one file per component)
    SpineShaperHarnessTests.cpp        AsymSaturator
    SpineNlSvfHarnessTests.cpp         NlSvfCell
    SpineDcBlockerHarnessTests.cpp     DcBlocker
    SpineHuggettHarnessTests.cpp       HuggettFilter (whole spine path)
    SpineHpStageHarnessTests.cpp       HuggettHpStage
    (later) SourceOscHarnessTests, RickyFxHarnessTests, ...

  TestMain.cpp  (existing runner; minor upgrade in §6 to surface metric values)
  CMakeLists.txt (add testdsp sources + new fixtures)
```

Key principles:

- **Metrics are pure functions of buffers**, not of components — so the same `noiseToSignalDb()`
  works on a shaper, a filter, an oscillator, or Ricky's chorus.
- **Components are adapted, not special-cased.** A `ProcessAdapter` exposes `prepare(sr)`,
  `reset()`, `process(float* buf, int n)` (mono; stereo runs L/R as two adapters or a stereo
  variant). Memoryless shapers, stateful filters, and the full spine all wear this face, so a fixture
  is "make adapter → feed signal → measure → gate."
- **The oversampled reference is the heart of the aliasing gate** (§3). It is *test-only* code; it
  has nothing to do with the shipped (non-oversampled, v5.0) DSP. It exists purely to manufacture a
  ground-truth "what should this nonlinearity sound like with no aliasing" so we can measure the gap.
- **Two test species, per [huggett-filter.md](../architecture/huggett-filter.md) §QA:**
  *linear-reference* tests (assert the TPT/analytic math is exact) and *musical-behavior* tests
  (assert drive/resonance/separation behave and stay bounded/clean). Both are gated.

## 3. The oversampled-reference comparator (bandlimited-reference method)

This is the SOTA way to measure nonlinear aliasing objectively (Parker/Zavalishin/Le Bivic,
DAFx-16 — [pdf](../incoming_research/dafx2016-adaa-parker-zavalishin-lebivic.pdf); Holters DAFx-19 —
[pdf](../incoming_research/dafx2019-adaa-stateful-systems.pdf); Köper/Holters/Esqueda/Parker DAFx-22
Wasp VCF — [pdf](../incoming_research/dafx2022-wasp-vcf-koper-holters-esqueda-parker.pdf)). Method:

1. Generate a bin-aligned test sine at base rate `fs`.
2. **Truth:** upsample (zero-stuff + steep linear-phase low-pass, or generate the input directly at
   `M·fs`), run the *identical process* at `M·fs` (M = 16 or 32), then **ideal-decimate** (steep
   anti-alias low-pass then drop samples) back to `fs`. At high M the process's own harmonics that
   fold are pushed far above the audio band and removed by the decimator → the result is the
   *bandlimited* output: harmonics present, alias products absent.
3. **Device under test:** run the shipped process at base `fs`.
4. **Metric:** spectrally compare DUT vs truth. The energy the DUT has at frequencies the truth does
   *not* (the folded/inharmonic bins) is the **aliasing noise**; report **NSR** (noise-to-signal,
   dB) and **A-weighted NSR** (perceptual). This is strictly more rigorous than the seed's
   "inharmonic bin energy" because it accounts for the legitimate harmonics the nonlinearity *should*
   produce, instead of assuming all non-fundamental energy is bad.

Notes:

- The seed's bin-aligned `inharmonicDb` stays as a fast, cheap **smoke metric** (no oversampling, one
  FFT). The oversampled NSR is the **authoritative aliasing gate**; run it on the shaper and the full
  drive path.
- The reference decimator is a one-time, test-only, high-order FIR (or `juce::dsp::FIR` with a
  Kaiser-windowed design). Correctness of the decimator itself is gated by a linear control: pass a
  pure tone through "up → identity → down" and assert reconstruction error is below −120 dB.
- For **stateful** components (filters with feedback), follow DAFx-19: the reference still runs the
  whole stateful process at `M·fs` (state included). We do *not* need fused-ADAA; we only need a
  faithful high-rate run as truth.

## 4. Metric catalog (with target thresholds)

Thresholds are **starting gates**, chosen from the literature and the seed's observed numbers; each
is a named constant in `Metrics.h`/the fixture so calibration tightens them over time. Direction:
"≤" = upper bound (fail if exceeded), "≥" = lower bound.

| # | Metric | What it catches | Function | Starting gate |
|---|---|---|---|---|
| M1 | **Finiteness / NaN-Inf guard** | denormal blow-ups, divide-by-zero, unstable feedback | `allFinite()` | **must** be finite, always (hard fail) |
| M2 | **Output boundedness** | self-osc divergence, runaway feedback | `maxAbs()` | ≤ a per-component ceiling (e.g. spine ≤ 4.0) |
| M3 | **Inharmonic energy (cheap)** | quick aliasing/click smoke (seed metric) | `inharmonicDb()` | ≤ component-specific dB (regression-anchored) |
| M4 | **NSR vs oversampled ref** | true aliasing of a nonlinearity | `noiseToSignalDb()` | shaper ≤ −60 dB at unity, degrade-curve gate vs drive |
| M5 | **A-weighted NSR** | *perceived* aliasing | `noiseToSignalDbA()` | ≤ M4 gate − headroom (A-weighting only relaxes) |
| M6 | **THD+N** | gross distortion sanity / regression anchor | `thdPlusNDb()` | within ±1 dB of golden per (drive, amp) |
| M7 | **Click/discontinuity** | per-block coefficient steps, zipper clicks on param change | `maxSampleDelta()` + `peakDerivativeRatio()` | no inter-block sample jump > N× local slope |
| M8 | **Block-vs-per-sample equivalence** | block-rate artifacts (seed already does this) | `maxDiff(blkN, blk1)` | ≤ 1e-5 unless processing is intentionally block-rate |
| M9 | **Zipper on modulation** | smoothing failures under audio-rate cutoff/res ramps | NSR of a held tone while a param ramps | ≤ M4 gate (no extra sidebands above floor) |
| M10 | **Frequency-response error vs analytic** | TPT coeff bugs, wrong corner/slope | `magResponse()` vs closed-form `\|H(f)\|` | ≤ 0.5 dB in passband, slope within ±1 dB/oct |
| M11 | **Q / resonant-peak accuracy** | wrong resonance taper, peak height/freq | peak-pick `magResponse()` | peak freq within ±2%, height within ±1.5 dB |
| M12 | **Self-oscillation pitch tracking** | osc pitch ≠ cutoff across notes/SR | impulse-kick → FFT peak / zero-cross pitch | ≤ ±1% (≈ ±17 cents) cutoff vs measured f0 |
| M13 | **Low-level linear equivalence** | nonlinear path not bit-exact-linear when undriven | `maxDiff(nonlinear@small, linear ref)` | ≤ 1e-5 (already a Huggett invariant) |
| M14 | **DC offset / DC-blocker efficacy** | asymmetric-shaper DC, blocker corner drift | mean of tail | \|DC\| ≤ 0.02 after blocker; audio RMS preserved |
| M15 | **Denormal-flush on silence** | CPU spikes / drift from denormals in decaying state | run silence, assert exact-zero or flushed | tail energy ≤ −300 dB after reset+silence |

Grounding notes:
- M4/M5 thresholds reference DAFx-16 Table 1 framing (alias floor vs oversampling/ADAA factor). The
  v5.0 finding (plain tanh ≤ ADAA) is *itself* expressible as a gate: a regression test that fails if
  any future shaper measures *worse* NSR than the committed plain-tanh baseline at matched drive.
- M10/M11 analytic targets come from the Cytomic `SvfLinearTrapOptimised` closed form
  ([tpt-svf-core.md](../architecture/tpt-svf-core.md)) — the linear core has an exact `\|H(f)\|`.
- M12 grounded in Zavalishin (self-osc as a limit cycle) and the NlSvfCell self-limit design.

## 5. How fixtures use it (sketch, not code to ship in this spec)

A fixture is uniform: build adapter → choose signal(s) → measure → `gate.check(this, ...)`.

- **AsymSaturator** (memoryless): M1, M2, M4/M5 (NSR vs oversampled ref across drive 0…1 and amp
  0.5…2), M6 golden, M13 (low-level ≈ linear), M14 (asymmetry DC present pre-blocker). Also keep the
  existing "== plain tanh inline reference" exact check as a regression anchor.
- **NlSvfCell** (stateful): M1, M2 (bounded self-osc), M10/M11 (LP/HP/BP/notch response + Q peak vs
  analytic at res), M12 (self-osc pitch vs cutoff across notes and 44.1/48/96 kHz), M13, M15.
- **DcBlocker**: M14 (DC removed, audio kept), L/R independence (existing), M10 (8 Hz corner ±tol).
- **HuggettFilter** (whole spine path): M1, M2, M8 (block 32/64/128/256/512 vs per-sample — also a
  *host-block-size* gate per huggett-filter.md QA), M4 on the post-drive path, M9 (cutoff ramp zipper),
  M10/M11 (LP response vs separation/slope, regression-anchored), M13 (zero-drive == bare cell).
- **HuggettHpStage**: M2, M10/M11 (HP corner + 12 vs 24 dB slope + resonant peak), M2 self-osc bound.

## 6. Runner upgrade (small, framework-preserving)

[TestMain.cpp](../../tests/TestMain.cpp) is kept. Two minimal improvements so failures are
diagnosable in CI logs:

1. **Surface metric values on failure.** `Gate.h` builds the `expect()` message to always include the
   measured value, the threshold, and the direction (e.g. `"M4 NSR=-52.1 dB exceeds gate -60 dB
   @drive=1.0 amp=1.5"`). This needs no runner change — `UnitTest::expect(bool, String)` already
   carries the message; we just stop relying on `printf`.
2. **Optionally print a metric table per fixture** behind an env flag (`BERNIE_TEST_VERBOSE=1`) using
   the seed's `printf` style for human inspection, while the gate `expect()`s drive pass/fail. This
   preserves the seed's "see where the artifact comes from" value without making CI noisy.

No change to the non-zero-exit-on-failure behavior — that is the CI gate and it already works.

## 7. CI integration (fails the build on regression)

- The test target `k2000_tests` already runs under CTest and returns non-zero on any failure
  ([CMakeLists.txt](../../tests/CMakeLists.txt), [TestMain.cpp](../../tests/TestMain.cpp)). Adding the
  harness fixtures to that target means **a blown gate fails the existing CI job** with no workflow
  change required.
- The Windows/Ableton build ([.github/workflows/build.yml](../../.github/workflows/build.yml)) is the
  trusted smoke target; the harness runs there (and on Linux dev). Use **bounded build parallelism
  (`-j4`)** when compiling tests — bare `-j` OOMs JUCE compiles (project memory).
- **Golden/regression anchoring (phase 2):** M3/M6/M10/M11 baselines are committed as small CSVs in
  `tests/golden/`. A change that shifts a baseline must *intentionally* update the golden file in the
  same commit — so an accidental regression fails, and a deliberate voicing change is a reviewable
  diff. (Keep goldens tiny: spectra summaries / a few dB values, not WAVs, to keep the repo lean.)
- **Determinism:** fix sample rate, FFT size, seeds for noise generators; no wall-clock or threading in
  metrics. CI must be bit-reproducible run-to-run.

## 8. File / directory layout

```
tests/
  testdsp/                         # NEW reusable support lib (framework-agnostic)
    SignalGen.h
    Spectrum.h
    Metrics.h
    Reference.h                    # OversampledReference + decimator
    ProcessAdapter.h
    Gate.h
    GoldenIO.h                     # phase 2
  fixtures/                        # NEW per-component harness fixtures
    SpineShaperHarnessTests.cpp
    SpineNlSvfHarnessTests.cpp
    SpineDcBlockerHarnessTests.cpp
    SpineHuggettHarnessTests.cpp
    SpineHpStageHarnessTests.cpp
  golden/                          # phase 2 regression baselines (tiny CSVs)
  OverdriveDiagnosticTests.cpp     # KEPT (seed; metrics migrate to Metrics.h, gates added)
  TestMain.cpp                     # KEPT (+ §6 message upgrade)
  CMakeLists.txt                   # + testdsp headers are header-only; add fixture .cpp files
```

`testdsp/` is header-only where practical (templates over buffer types), so CMake only gains the new
fixture `.cpp` files in the `add_executable` list and an include path.

## 9. Build sequence (tasks)

Each task is independently testable; the harness validates itself before it validates the DSP.

1. **`testdsp` skeleton + self-tests.** `SignalGen`, `Spectrum`, `Metrics` (M1–M3, M6, M10) extracted
   from the seed. Self-test: feed known signals (pure tone, known-THD waveshaper) and assert the
   metrics return analytically-known values. *Gate the metrics before trusting them.*
2. **`OversampledReference` + decimator + self-test (M4/M5).** Validate the decimator (up→identity→down
   reconstruction < −120 dB), then validate NSR on a known case (a hard clipper at high gain must
   measure a *worse* NSR than soft tanh — reproduces the DAFx-16 ordering).
3. **`ProcessAdapter` + `Gate` + runner message upgrade (§6).** Wrap AsymSaturator/NlSvfCell/Huggett.
4. **Port the seed into gated fixtures.** Re-express the four existing `OverdriveDiagnosticTests`
   blocks as `SpineShaperHarnessTests` + `SpineHuggettHarnessTests` with M3/M4/M8/M10 gates. Land the
   v5.0-finding regression gate: plain tanh NSR ≤ committed baseline (fails if a future shaper is dirtier).
5. **Stateful fixtures.** `SpineNlSvfHarnessTests` (M10/M11/M12), `SpineDcBlockerHarnessTests` (M14),
   `SpineHpStageHarnessTests` (M10/M11). Add M9 zipper + M15 denormal-flush.
6. **Golden anchoring (phase 2).** `GoldenIO`, `tests/golden/`, commit baselines for M3/M6/M10/M11.
7. **CI confirmation.** Verify a deliberately-injected regression (e.g. nudge a coefficient) fails the
   `k2000_tests` job; revert. Document the harness in this spec's "as-built" note.

Later (no work now, but the architecture already supports it): **source blocks** (osc anti-aliasing
NSR, pitch accuracy) and **Ricky FX** (delay/chorus zipper M9, EQ response M10, saturator M4) reuse
the same `testdsp` library + adapter pattern — only new fixtures are added.

## 10. Non-goals / out of scope

- Not a perceptual/listening-test framework; metrics are objective proxies, calibration against the
  user's Summit hardware stays a manual, separate activity (the `// CALIB` constants).
- Not a benchmark/perf harness — CPU-budget profiling (the Q11 perf gate) is a separate cross-cutting
  deliverable. (The two could share `ProcessAdapter`; not specified here.)
- Does not reintroduce shipped oversampling — the `OversampledReference` is **test-only** truth, not a
  product DSP path. (Product oversampling is v5.1.)

## 11. Open questions

1. **M4 absolute thresholds.** −60 dB NSR at unity is a defensible starting gate, but the *right*
   numbers should be anchored to the committed plain-tanh baseline once measured at M=32. Tighten
   after task 4 produces real numbers?
2. **Golden storage format & churn.** CSV summaries vs storing a few WAV reference vectors — how much
   regression fidelity vs repo cleanliness do you want? (I lean tiny CSVs.)
3. **Stereo metrics.** Run L/R as two mono adapters (simplest) or add a stereo correlation/imaging
   metric now? The spine is mono-coefficient stereo, so per-channel is likely sufficient until Ricky.
4. **Self-osc pitch gate (M12) tolerance.** ±1% (≈17 cents) — acceptable, or do you want tighter near
   the calibration notes?
5. **Should the seed file be renamed** (it is the "diagnostic," now partly a gate) or left as the
   verbose human-inspection entry point alongside the gated fixtures?

## Decisions (resolved 2026-06-20, user review)

1. **Pass/fail thresholds** — capture BOTH the literature targets AND the measured plain-tanh baseline (task 4); finalise the gate numbers from real data afterward, not up front.
2. **Goldens** — tiny CSV metric summaries (git-diffable), not WAV vectors.
3. **Stereo** — include stereo metrics (correlation/imaging) from the start, not mono-only.
4. **Self-osc pitch tolerance (M12)** — **±0.5 % (~8.5 cents)** (tight).
5. **Seed file** — keep `OverdriveDiagnosticTests` as the verbose human-inspection entry alongside the gated fixtures (do not rename/replace).
