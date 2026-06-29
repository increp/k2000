# Filter-Validation Framework — Internal SOTA Layer (Sub-Project #1) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a model-agnostic, dual-method filter-characterization harness that proves each filter matches its textbook ideal (stepped-sine reference cross-validated against a Farina ESS), across four batteries and the live oversampling tiers, with a fast CI gate subset and an opt-in heavy runner.

**Architecture:** Five layers. **L0** = pure-DSP measurement library in `tests/testdsp/` (two transfer-function engines + an agreement check), validated against synthetic filters. **L1** = `chz::FilterUnderTest`, one uniform socket that excites any `FilterModel` and applies the OS path internally. **L2** = `chz::CharacterizationRunner`, which sweeps an operating-point grid and emits a fingerprint. **L3a** = a fast gate subset compiled into the existing `k2000_tests` (CI). **L3b** = a new opt-in `k2000_filter_characterization` executable (full grid, not in CI).

**Tech Stack:** C++17, JUCE 8 (`juce::UnitTest`, `juce::dsp::FFT`), CMake. Header-only `testdsp` library. Existing helpers reused: `Spectrum`, `SignalGen`, `Metrics`, `Reference`, `Response`, `Gate`, `GoldenIO`/`GoldenSet`, `ProcessAdapter::ModelAdapter`.

## Global Constraints

- **Language/std:** C++17 (`set_target_properties(... CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)`), match existing test targets.
- **Build parallelism:** always `cmake --build build --target <t> -j4` — never bare `-j` (OOMs a JUCE compile → confusing link failure).
- **MSVC portability (Windows CI is the compile gate):** never use `M_PI` — use `juce::MathConstants<double>::pi`. Keep large buffers in `std::vector` (heap), never large stack arrays (MSVC 1 MB stack). Before declaring the branch done, run `gh workflow run build.yml --ref feat/filter-validation-internal`.
- **Test framework:** every test is a `class XTests : public juce::UnitTest` with a `runTest()` override and a single file-scope `static XTests instance;`. The runner prints `Summary: N tests, M failed` and returns non-zero on failure.
- **Fast suite bound:** anything added to `k2000_tests` must stay bounded — host rate pinned to **96000.0**, OS factors **{1,2,4,8}** in **live** mode, a **coarse** cutoff/resonance grid only. The full sweep (all 5 host rates, both OS modes, dense grid) lives only in `k2000_filter_characterization`, which is NOT registered with `add_test` and NOT in CI.
- **Self-golden store:** reuse `testdsp::GoldenSet` (CSV under compile-time `BERNIE_GOLDEN_DIR` = `tests/golden`, update-or-assert via env `BERNIE_UPDATE_GOLDEN`). Committed baselines live at `tests/golden/<model>/baseline.csv`.
- **Branch:** all work on `feat/filter-validation-internal` (spec already committed there at `2d540b6`).
- **Commits:** in the sandbox, no apostrophes in `-m` messages; end every commit message with a trailer line `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`.
- **No preset/patch backcompat concerns** — not applicable to this harness.

---

## File Structure

**L0 — new header-only measurement primitives (`tests/testdsp/`):**
- `SteppedSine.h` — lock-in (single-bin DFT) magnitude + phase across a frequency grid. The trusted reference engine.
- `Sweep.h` — Farina exponential-sine-sweep generation, inverse filter, FFT deconvolution → impulse response.
- `TransferFunction.h` — impulse response → complex `H(f)` (magnitude dB, phase, group delay) at requested frequencies.
- `Harmonics.h` — stepped THD-vs-frequency helper (wraps a driven tone + `Metrics::thdPlusNDb`).
- `MethodAgreement.h` — max magnitude delta (dB) between two responses sampled at the same frequencies.

**L1/L2 — new characterization layer (`tests/characterization/`):**
- `OperatingPoint.h` — `chz::Mode`, `chz::OsMode`, `chz::OperatingPoint` struct (the shared schema).
- `FilterUnderTest.h` + `.cpp` — `chz::FilterUnderTest` socket + `makeMoogFut()` / `makeHuggettFut()` factories (per-model mode/slope knowledge isolated here). Applies the OS path via `VoiceOversampler`.
- `CharacterizationRunner.h` + `.cpp` — `chz::Grid`, `chz::Fingerprint`, `chz::CharacterizationRunner` (runs the four batteries over a grid → fingerprint + CSV writer).
- `characterize_main.cpp` — the opt-in heavy executable `main()` (parses `--model`, runs the full grid, writes artifacts, prints summary, returns exit code).

**L3 — wiring & gates:**
- `tests/CharacterizationGateTests.cpp` — the fast spec-gate + method-agreement + coarse self-golden subset, added to `k2000_tests` sources.
- `tests/CMakeLists.txt` — modify: add new test file to `k2000_tests`; add the new `k2000_filter_characterization` executable target.

**Committed artifacts:**
- `tests/golden/moog/baseline.csv`, `tests/golden/huggett/baseline.csv` — coarse self-golden baselines.

**Documentation (`docs/filter-validation/`, one page per phase):**
- `README.md`, `concepts.md`, `running.md`, `interpreting-results.md`, `operating-points.md`, `extending.md`, `troubleshooting.md`.

**Interface summary (consistent names used across tasks):**

```cpp
// L0
namespace testdsp {
  struct ComplexResponse { std::vector<double> freqHz, magDb, phaseRad; };
  struct SteppedSine { template <class A>
    static ComplexResponse transfer(A& a, const std::vector<double>& freqs, double sr, float amp); };
  struct Sweep {
    static std::vector<float> ess(double f0, double f1, double durSec, double sr, float amp);
    static std::vector<float> inverseFilter(double f0, double f1, double durSec, double sr);
    static std::vector<float> impulseResponse(const std::vector<float>& output,
                                              const std::vector<float>& invFilter); };
  struct TransferFunction {
    struct Result { std::vector<double> freqHz, magDb, phaseRad, groupDelaySec; };
    static Result fromImpulse(const std::vector<float>& ir, double sr,
                              const std::vector<double>& freqs); };
  struct Harmonics { template <class A>
    static double thdDb(A& a, double f0, double sr, float amp); };
  struct MethodAgreement {
    static double maxMagDeltaDb(const std::vector<double>& magA,
                                const std::vector<double>& magB); };
}
// L1/L2
namespace chz {
  enum class Mode { LP12, LP24, BP, HP, Notch };
  enum class OsMode { Live, Render };
  struct OperatingPoint { Mode mode = Mode::LP24; double cutoffHz = 1000.0, resonance = 0.0,
    drive = 0.0; int osFactor = 1; OsMode osMode = OsMode::Live; double hostSampleRate = 96000.0; };
  class FilterUnderTest { /* setOperatingPoint, reset, process, supports, name */ };
  std::unique_ptr<FilterUnderTest> makeMoogFut();
  std::unique_ptr<FilterUnderTest> makeHuggettFut();
}
```

---

# Phase 1 — L0 dual-method ruler + self-tests

Produces the trusted measurement library. No model code yet; everything is validated against synthetic closed-form filters.

### Task 1: SteppedSine — lock-in magnitude + phase (reference engine)

**Files:**
- Create: `tests/testdsp/SteppedSine.h`
- Test: add a `class SteppedSineTests` to a new file `tests/SteppedSineTests.cpp`; register it in `tests/CMakeLists.txt`.

**Interfaces:**
- Consumes: nothing (only `<vector>`, `<cmath>`, JUCE math constants).
- Produces: `testdsp::ComplexResponse { std::vector<double> freqHz, magDb, phaseRad; }` and `testdsp::SteppedSine::transfer<Adapter>(Adapter& a, const std::vector<double>& freqs, double sr, float amp) -> ComplexResponse`. Adapter contract: `void reset(); void process(float* buf, int n);` (same as `Response`, no `prepare`).

- [ ] **Step 1: Write the failing test**

Add `tests/SteppedSineTests.cpp`:

```cpp
#include <juce_core/juce_core.h>
#include "testdsp/SteppedSine.h"
#include "testdsp/ProcessAdapter.h"
#include "../src/dsp/spine/NlSvfCell.h"
#include <cmath>

struct SteppedSineTests : public juce::UnitTest {
    SteppedSineTests() : juce::UnitTest("SteppedSine") {}
    void runTest() override {
        const double sr = 96000.0;

        beginTest("identity passthrough: 0 dB, ~0 phase across the grid");
        {
            struct Passthrough { void reset() {} void process(float*, int) {} };
            Passthrough p;
            std::vector<double> freqs { 100.0, 1000.0, 10000.0 };
            auto r = testdsp::SteppedSine::transfer(p, freqs, sr, 0.1f);
            expect(r.magDb.size() == freqs.size(), "one result per frequency");
            for (double m : r.magDb) expectWithinAbsoluteError(m, 0.0, 0.05);
        }

        beginTest("matches Cytomic LP analytic at fc=1000 (CellAdapter, res=0)");
        {
            const double fc = 1000.0, k = 2.0;   // res=0 -> Q=0.5 -> k=2.0 (see TestDspSelfTests)
            testdsp::CellAdapter ca; ca.cutoff = (float) fc; ca.res = 0.0f; ca.tap = NlSvfCell::LP;
            ca.prepare(sr);
            auto analyticDb = [&](double f) { const double u = f / fc, u2 = u * u;
                return -10.0 * std::log10((1.0 - u2) * (1.0 - u2) + k * k * u2); };
            std::vector<double> freqs { 100.0, 300.0, 700.0, 1000.0 };
            auto r = testdsp::SteppedSine::transfer(ca, freqs, sr, 0.05f);
            for (size_t i = 0; i < freqs.size(); ++i)
                expectWithinAbsoluteError(r.magDb[i], analyticDb(freqs[i]), 0.5);
        }
    }
};
static SteppedSineTests steppedSineTestsInstance;
```

Register it: in `tests/CMakeLists.txt`, add `SteppedSineTests.cpp` to the `k2000_tests` source list (after `TestDspSelfTests.cpp`).

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target k2000_tests -j4`
Expected: FAIL to compile — `testdsp/SteppedSine.h` not found.

- [ ] **Step 3: Write minimal implementation**

Create `tests/testdsp/SteppedSine.h`:

```cpp
#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include <cmath>

// testdsp::SteppedSine — the trusted REFERENCE transfer-function engine.
// For each frequency f, drives a pure sine through the adapter, discards a warm-up
// window, then lock-in detects the steady-state response: correlate the output with
// sin(wt) and cos(wt) over the measurement window to recover complex amplitude.
//   magDb  = 20*log10(|H|),  phaseRad = arg(H) relative to the input sine.
// Adapter contract: void reset(); void process(float* buf, int n);  (no prepare — sr is a param)
namespace testdsp {

struct ComplexResponse {
    std::vector<double> freqHz, magDb, phaseRad;
};

struct SteppedSine {
    static constexpr int kWarmSamples = 8192;
    static constexpr int kMeasSamples = 16384;

    template <typename Adapter>
    static ComplexResponse transfer(Adapter& a, const std::vector<double>& freqs,
                                    double sr, float amp) {
        ComplexResponse out;
        out.freqHz = freqs;
        out.magDb.reserve(freqs.size());
        out.phaseRad.reserve(freqs.size());
        const double twoPi = 2.0 * juce::MathConstants<double>::pi;
        for (double f : freqs) {
            a.reset();
            const int total = kWarmSamples + kMeasSamples;
            double I = 0.0, Q = 0.0;            // in-phase / quadrature accumulators
            float buf[1];
            for (int i = 0; i < total; ++i) {
                const double ph = twoPi * f * i / sr;
                buf[0] = amp * (float) std::sin(ph);
                a.process(buf, 1);
                if (i >= kWarmSamples) {
                    I += (double) buf[0] * std::sin(ph);
                    Q += (double) buf[0] * std::cos(ph);
                }
            }
            // Lock-in: output sine component amplitude is 2/N * (I + jQ); input amplitude is amp.
            const double scale = 2.0 / (double) kMeasSamples;
            const double re = I * scale, im = Q * scale;
            const double magLin = std::sqrt(re * re + im * im) / (double) amp;
            out.magDb.push_back(magLin > 0.0 ? 20.0 * std::log10(magLin) : -300.0);
            out.phaseRad.push_back(std::atan2(im, re));
        }
        return out;
    }
};

} // namespace testdsp
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests`
Expected: `[PASS] SteppedSine` in the output; `Summary` failed count unchanged from before this task.

- [ ] **Step 5: Commit**

```bash
git add tests/testdsp/SteppedSine.h tests/SteppedSineTests.cpp tests/CMakeLists.txt
git commit -m "test(chz): SteppedSine lock-in transfer-function engine (L0 reference)" -m "Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: Sweep — Farina ESS, inverse filter, deconvolution

**Files:**
- Create: `tests/testdsp/Sweep.h`
- Test: new file `tests/SweepTests.cpp`; register in `tests/CMakeLists.txt`.

**Interfaces:**
- Consumes: `testdsp::Spectrum` (FFT), JUCE FFT.
- Produces: `Sweep::ess(f0,f1,durSec,sr,amp)`, `Sweep::inverseFilter(f0,f1,durSec,sr)`, `Sweep::impulseResponse(output, invFilter)` — all `std::vector<float>`.

- [ ] **Step 1: Write the failing test**

Add `tests/SweepTests.cpp`:

```cpp
#include <juce_core/juce_core.h>
#include "testdsp/Sweep.h"
#include "testdsp/Spectrum.h"
#include <cmath>

struct SweepTests : public juce::UnitTest {
    SweepTests() : juce::UnitTest("Sweep") {}
    void runTest() override {
        const double sr = 96000.0, f0 = 20.0, f1 = 24000.0, dur = 1.0;

        beginTest("ESS deconvolves to a unit impulse (identity system)");
        {
            auto sweep = testdsp::Sweep::ess(f0, f1, dur, sr, 0.5f);
            auto inv   = testdsp::Sweep::inverseFilter(f0, f1, dur, sr);
            // Identity system: output == sweep. IR should be a single dominant peak.
            auto ir = testdsp::Sweep::impulseResponse(sweep, inv);
            int peak = 0; float pmax = 0.0f;
            for (int i = 0; i < (int) ir.size(); ++i)
                if (std::abs(ir[(size_t) i]) > pmax) { pmax = std::abs(ir[(size_t) i]); peak = i; }
            // Energy outside a +-8 sample window around the peak must be tiny vs the peak.
            double outside = 0.0;
            for (int i = 0; i < (int) ir.size(); ++i)
                if (std::abs(i - peak) > 8) outside += (double) ir[(size_t) i] * ir[(size_t) i];
            expect(pmax > 0.0f, "peak present");
            expect(std::sqrt(outside) < (double) pmax * 0.05,
                   "out-of-peak energy " + juce::String(std::sqrt(outside))
                   + " must be < 5% of peak " + juce::String(pmax));
        }
    }
};
static SweepTests sweepTestsInstance;
```

Register `SweepTests.cpp` in `tests/CMakeLists.txt`.

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target k2000_tests -j4`
Expected: FAIL to compile — `testdsp/Sweep.h` not found.

- [ ] **Step 3: Write minimal implementation**

Create `tests/testdsp/Sweep.h`:

```cpp
#pragma once
#include "Spectrum.h"
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <cmath>

// testdsp::Sweep — Farina exponential-sine-sweep (ESS) measurement core.
// Generate a log sweep f0->f1 over durSec; the inverse filter is the time-reversed
// sweep with an amplitude envelope that flattens its spectrum, so deconvolution of
// the system output with it yields the system impulse response. FFT-based linear
// convolution. See Farina, "Simultaneous measurement of impulse response and
// distortion with a swept-sine technique" (AES 2000).
namespace testdsp {

struct Sweep {
    // Exponential sine sweep, length round(durSec*sr) samples.
    static std::vector<float> ess(double f0, double f1, double durSec, double sr, float amp) {
        const int n = (int) std::lround(durSec * sr);
        const double w0 = 2.0 * juce::MathConstants<double>::pi * f0;
        const double w1 = 2.0 * juce::MathConstants<double>::pi * f1;
        const double K  = w0 * durSec / std::log(w1 / w0);
        const double Lr = std::log(w1 / w0) / durSec;     // 1/L
        std::vector<float> x((size_t) n);
        for (int i = 0; i < n; ++i) {
            const double t = (double) i / sr;
            x[(size_t) i] = amp * (float) std::sin(K * (std::exp(t * Lr) - 1.0));
        }
        return x;
    }

    // Inverse filter: time-reversed sweep, amplitude-modulated by e^{-t/L} (rises with
    // frequency) so that conv(sweep, inverse) ~ delta. Normalised so the delta peak ~1.
    static std::vector<float> inverseFilter(double f0, double f1, double durSec, double sr) {
        auto sweep = ess(f0, f1, durSec, sr, 1.0f);
        const int n = (int) sweep.size();
        const double Lr = std::log((2.0 * juce::MathConstants<double>::pi * f1)
                                   / (2.0 * juce::MathConstants<double>::pi * f0)) / durSec;
        std::vector<float> inv((size_t) n);
        for (int i = 0; i < n; ++i) {
            const double t = (double) i / sr;
            const double env = std::exp(-t * Lr);
            inv[(size_t) (n - 1 - i)] = sweep[(size_t) i] * (float) env;
        }
        // Normalise so the autoconvolution peak is ~1 (keeps IR magnitudes interpretable).
        auto probe = impulseResponseRaw(sweep, inv);
        float pk = 0.0f; for (float v : probe) pk = std::max(pk, std::abs(v));
        if (pk > 0.0f) for (auto& v : inv) v /= pk;
        return inv;
    }

    // System output (already captured) deconvolved with the inverse filter -> IR.
    static std::vector<float> impulseResponse(const std::vector<float>& output,
                                              const std::vector<float>& invFilter) {
        return impulseResponseRaw(output, invFilter);
    }

private:
    // FFT linear convolution of a and b (full length a+b-1), real part returned.
    static std::vector<float> impulseResponseRaw(const std::vector<float>& a,
                                                 const std::vector<float>& b) {
        const int full = (int) a.size() + (int) b.size() - 1;
        int order = 0; while ((1 << order) < full) ++order;
        const int N = 1 << order;
        juce::dsp::FFT fft(order);
        std::vector<float> fa((size_t) (2 * N), 0.0f), fb((size_t) (2 * N), 0.0f);
        for (size_t i = 0; i < a.size(); ++i) fa[i] = a[i];
        for (size_t i = 0; i < b.size(); ++i) fb[i] = b[i];
        fft.performRealOnlyForwardTransform(fa.data());
        fft.performRealOnlyForwardTransform(fb.data());
        std::vector<float> prod((size_t) (2 * N), 0.0f);
        for (int k = 0; k <= N / 2; ++k) {
            const float ar = fa[(size_t) (2 * k)], ai = fa[(size_t) (2 * k + 1)];
            const float br = fb[(size_t) (2 * k)], bi = fb[(size_t) (2 * k + 1)];
            prod[(size_t) (2 * k)]     = ar * br - ai * bi;
            prod[(size_t) (2 * k + 1)] = ar * bi + ai * br;
        }
        fft.performRealOnlyInverseTransform(prod.data());
        std::vector<float> out((size_t) full);
        for (int i = 0; i < full; ++i) out[(size_t) i] = prod[(size_t) i];
        return out;
    }
};

} // namespace testdsp
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests`
Expected: `[PASS] Sweep`.

- [ ] **Step 5: Commit**

```bash
git add tests/testdsp/Sweep.h tests/SweepTests.cpp tests/CMakeLists.txt
git commit -m "test(chz): Farina ESS sweep + inverse + deconvolution (L0)" -m "Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 3: TransferFunction — IR to magnitude / phase / group delay

**Files:**
- Create: `tests/testdsp/TransferFunction.h`
- Test: new file `tests/TransferFunctionTests.cpp`; register in `tests/CMakeLists.txt`.

**Interfaces:**
- Consumes: nothing beyond std/JUCE.
- Produces: `TransferFunction::Result { freqHz, magDb, phaseRad, groupDelaySec }` and `fromImpulse(ir, sr, freqs) -> Result`.

- [ ] **Step 1: Write the failing test**

Add `tests/TransferFunctionTests.cpp`:

```cpp
#include <juce_core/juce_core.h>
#include "testdsp/TransferFunction.h"
#include "testdsp/SignalGen.h"
#include <cmath>

struct TransferFunctionTests : public juce::UnitTest {
    TransferFunctionTests() : juce::UnitTest("TransferFunction") {}
    void runTest() override {
        const double sr = 96000.0;

        beginTest("unit impulse -> 0 dB flat, ~0 phase");
        {
            auto ir = testdsp::SignalGen::impulse(1.0f, 4096);
            std::vector<double> freqs { 100.0, 1000.0, 10000.0 };
            auto r = testdsp::TransferFunction::fromImpulse(ir, sr, freqs);
            for (double m : r.magDb) expectWithinAbsoluteError(m, 0.0, 1.0e-3);
        }

        beginTest("pure delay -> group delay equals the delay");
        {
            const int D = 24;                          // 24-sample delay
            std::vector<float> ir(4096, 0.0f); ir[(size_t) D] = 1.0f;
            std::vector<double> freqs { 500.0, 1000.0, 2000.0, 4000.0 };
            auto r = testdsp::TransferFunction::fromImpulse(ir, sr, freqs);
            const double expected = (double) D / sr;   // seconds
            for (double g : r.groupDelaySec) expectWithinAbsoluteError(g, expected, 1.0e-6);
        }
    }
};
static TransferFunctionTests transferFunctionTestsInstance;
```

Register `TransferFunctionTests.cpp` in `tests/CMakeLists.txt`.

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target k2000_tests -j4`
Expected: FAIL to compile — `testdsp/TransferFunction.h` not found.

- [ ] **Step 3: Write minimal implementation**

Create `tests/testdsp/TransferFunction.h`:

```cpp
#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include <cmath>

// testdsp::TransferFunction — evaluate H(f) from an impulse response.
// Single-bin DFT at each requested frequency (Goertzel-style direct sum), so the
// frequency grid is arbitrary/log-spaced and independent of FFT bin spacing.
//   magDb = 20*log10(|H|), phaseRad = unwrapped arg(H),
//   groupDelaySec = -d(phase)/d(omega) via central finite difference on the grid.
namespace testdsp {

struct TransferFunction {
    struct Result {
        std::vector<double> freqHz, magDb, phaseRad, groupDelaySec;
    };

    static Result fromImpulse(const std::vector<float>& ir, double sr,
                              const std::vector<double>& freqs) {
        Result r; r.freqHz = freqs;
        const int N = (int) ir.size();
        const double twoPi = 2.0 * juce::MathConstants<double>::pi;
        std::vector<double> rawPhase;
        rawPhase.reserve(freqs.size());
        for (double f : freqs) {
            double re = 0.0, im = 0.0;
            const double w = twoPi * f / sr;
            for (int n = 0; n < N; ++n) {
                re += (double) ir[(size_t) n] * std::cos(w * n);
                im -= (double) ir[(size_t) n] * std::sin(w * n);
            }
            const double mag = std::sqrt(re * re + im * im);
            r.magDb.push_back(mag > 0.0 ? 20.0 * std::log10(mag) : -300.0);
            rawPhase.push_back(std::atan2(im, re));
        }
        // Unwrap phase along the grid.
        r.phaseRad = rawPhase;
        for (size_t i = 1; i < r.phaseRad.size(); ++i) {
            double d = r.phaseRad[i] - r.phaseRad[i - 1];
            while (d >  juce::MathConstants<double>::pi) { r.phaseRad[i] -= twoPi; d -= twoPi; }
            while (d < -juce::MathConstants<double>::pi) { r.phaseRad[i] += twoPi; d += twoPi; }
        }
        // Group delay = -dphase/domega, central difference (one-sided at the ends).
        r.groupDelaySec.assign(freqs.size(), 0.0);
        for (size_t i = 0; i < freqs.size(); ++i) {
            const size_t lo = (i == 0) ? 0 : i - 1;
            const size_t hi = (i + 1 < freqs.size()) ? i + 1 : i;
            const double dW = twoPi * (freqs[hi] - freqs[lo]) / sr * sr; // = twoPi*(fhi-flo)
            const double dPhi = r.phaseRad[hi] - r.phaseRad[lo];
            r.groupDelaySec[i] = (dW != 0.0) ? -dPhi / dW : 0.0;
        }
        return r;
    }
};

} // namespace testdsp
```

> Note: `dW` simplifies to `twoPi*(fhi-flo)` (the `/sr*sr` cancels) — kept explicit to mirror the omega definition; the engineer may simplify.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests`
Expected: `[PASS] TransferFunction`.

- [ ] **Step 5: Commit**

```bash
git add tests/testdsp/TransferFunction.h tests/TransferFunctionTests.cpp tests/CMakeLists.txt
git commit -m "test(chz): TransferFunction IR-to-H(f) with group delay (L0)" -m "Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 4: MethodAgreement + Harmonics, and the dual-method agreement self-test

**Files:**
- Create: `tests/testdsp/MethodAgreement.h`, `tests/testdsp/Harmonics.h`
- Test: new file `tests/MethodAgreementTests.cpp`; register in `tests/CMakeLists.txt`.

**Interfaces:**
- Consumes: `Spectrum`, `Metrics`, `SteppedSine`, `Sweep`, `TransferFunction`, `ProcessAdapter::CellAdapter`.
- Produces: `MethodAgreement::maxMagDeltaDb(magA, magB) -> double`; `Harmonics::thdDb<Adapter>(a, f0, sr, amp) -> double`.

- [ ] **Step 1: Write the failing test**

Add `tests/MethodAgreementTests.cpp` (this is the cornerstone test — the two engines must agree on a real synthetic filter):

```cpp
#include <juce_core/juce_core.h>
#include "testdsp/MethodAgreement.h"
#include "testdsp/Harmonics.h"
#include "testdsp/SteppedSine.h"
#include "testdsp/Sweep.h"
#include "testdsp/TransferFunction.h"
#include "testdsp/ProcessAdapter.h"
#include "../src/dsp/spine/NlSvfCell.h"
#include <cmath>
#include <vector>

struct MethodAgreementTests : public juce::UnitTest {
    MethodAgreementTests() : juce::UnitTest("MethodAgreement") {}

    static std::vector<double> logFreqs(double f0, double f1, int n) {
        std::vector<double> v((size_t) n);
        for (int i = 0; i < n; ++i)
            v[(size_t) i] = f0 * std::pow(f1 / f0, (double) i / (double) (n - 1));
        return v;
    }

    void runTest() override {
        const double sr = 96000.0;

        beginTest("maxMagDeltaDb is 0 for identical curves, exact for a known offset");
        {
            std::vector<double> a { -1.0, -6.0, -12.0 }, b = a;
            expectWithinAbsoluteError(testdsp::MethodAgreement::maxMagDeltaDb(a, b), 0.0, 0.0);
            for (auto& v : b) v -= 0.5;
            expectWithinAbsoluteError(testdsp::MethodAgreement::maxMagDeltaDb(a, b), 0.5, 1.0e-9);
        }

        beginTest("stepped-sine and ESS agree on a synthetic LP (the dual-method gate)");
        {
            auto freqs = logFreqs(50.0, 20000.0, 60);

            // Stepped-sine reference.
            testdsp::CellAdapter ca; ca.cutoff = 1000.0f; ca.res = 0.0f; ca.tap = NlSvfCell::LP;
            ca.prepare(sr);
            auto stepped = testdsp::SteppedSine::transfer(ca, freqs, sr, 0.05f);

            // ESS: drive the same filter with the sweep, deconvolve, evaluate H(f).
            testdsp::CellAdapter cb; cb.cutoff = 1000.0f; cb.res = 0.0f; cb.tap = NlSvfCell::LP;
            cb.prepare(sr);
            auto sweep = testdsp::Sweep::ess(20.0, 24000.0, 1.0, sr, 0.05f);
            auto out = sweep;
            cb.process(out.data(), (int) out.size());
            auto inv = testdsp::Sweep::inverseFilter(20.0, 24000.0, 1.0, sr);
            auto ir  = testdsp::Sweep::impulseResponse(out, inv);
            auto ess = testdsp::TransferFunction::fromImpulse(ir, sr, freqs);

            const double delta = testdsp::MethodAgreement::maxMagDeltaDb(stepped.magDb, ess.magDb);
            logMessage("dual-method max |dMag| = " + juce::String(delta, 3) + " dB");
            expect(delta < 1.0, "stepped vs ESS disagree by " + juce::String(delta, 3) + " dB (> 1 dB)");
        }

        beginTest("Harmonics::thdDb ~ floor for a clean passthrough");
        {
            struct Passthrough { void reset() {} void process(float*, int) {} };
            Passthrough p;
            expect(testdsp::Harmonics::thdDb(p, 1000.0, sr, 0.5f) < -60.0, "clean signal low THD");
        }
    }
};
static MethodAgreementTests methodAgreementTestsInstance;
```

Register `MethodAgreementTests.cpp` in `tests/CMakeLists.txt`.

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target k2000_tests -j4`
Expected: FAIL to compile — `testdsp/MethodAgreement.h` / `Harmonics.h` not found.

- [ ] **Step 3: Write minimal implementation**

Create `tests/testdsp/MethodAgreement.h`:

```cpp
#pragma once
#include <vector>
#include <cmath>
#include <algorithm>

// testdsp::MethodAgreement — compares two magnitude curves sampled at the SAME
// frequency grid. The dual-method gate: the Farina ESS magnitude must match the
// trusted stepped-sine magnitude within tolerance, proving the deconvolution.
namespace testdsp {

struct MethodAgreement {
    // Max |magA[i] - magB[i]| in dB over the shared grid. Both vectors must be the
    // same length (sampled at identical frequencies).
    static double maxMagDeltaDb(const std::vector<double>& magA,
                                const std::vector<double>& magB) {
        const size_t n = std::min(magA.size(), magB.size());
        double worst = 0.0;
        for (size_t i = 0; i < n; ++i)
            worst = std::max(worst, std::abs(magA[i] - magB[i]));
        return worst;
    }
};

} // namespace testdsp
```

Create `tests/testdsp/Harmonics.h`:

```cpp
#pragma once
#include "Spectrum.h"
#include "Metrics.h"
#include "SignalGen.h"
#include <vector>
#include <cmath>

// testdsp::Harmonics — stepped THD at a single tone (B3 distortion is single-method).
// Drives a bin-aligned tone through the adapter, discards a warm-up window, then
// measures THD+N (dB) from the FFT magnitude via Metrics::thdPlusNDb.
namespace testdsp {

struct Harmonics {
    template <typename Adapter>
    static double thdDb(Adapter& a, double f0, double sr, float amp) {
        const int N = 1 << 14;
        const int bin = std::max(2, (int) std::lround(f0 * N / sr));
        a.reset();
        // Warm up so the filter reaches steady state, then capture N bin-aligned samples.
        std::vector<float> warm = SignalGen::sine(amp, (double) bin * sr / N, sr, 8192);
        a.process(warm.data(), (int) warm.size());
        std::vector<float> cap = SignalGen::binAlignedSine(amp, bin, N);
        a.process(cap.data(), N);
        auto mag = Spectrum::magnitude(cap);
        return Metrics::thdPlusNDb(mag, bin);
    }
};

} // namespace testdsp
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests`
Expected: `[PASS] MethodAgreement`, including the `dual-method max |dMag|` log line under ~1 dB.

- [ ] **Step 5: Commit**

```bash
git add tests/testdsp/MethodAgreement.h tests/testdsp/Harmonics.h tests/MethodAgreementTests.cpp tests/CMakeLists.txt
git commit -m "test(chz): method-agreement gate + stepped THD, dual-method proven on synthetic LP (L0)" -m "Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 5: Phase-1 documentation — `concepts.md`

**Files:**
- Create: `docs/filter-validation/concepts.md`

- [ ] **Step 1: Write the page**

Create `docs/filter-validation/concepts.md` covering, in plain English (one short section each): what the harness proves; the layered definition of "correct" (textbook-ideal now, real-device later in sub-project #2); the **dual-method ruler** (stepped-sine = trusted reference; Farina ESS = SOTA single-pass; why two — the ESS is trusted only because it agrees with the simple method); the **three gates** (spec / method-agreement / self-golden); and the **four batteries** (B1 magnitude, B2 resonance/self-osc, B3 distortion/aliasing, B4 phase/group-delay). Link to `operating-points.md` and `interpreting-results.md` (created in Phase 2).

- [ ] **Step 2: Commit**

```bash
git add docs/filter-validation/concepts.md
git commit -m "docs(chz): concepts page — dual-method ruler, gates, batteries" -m "Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

# Phase 2 — L1 socket + L2 runner + artifacts + opt-in target (Moog, base rate)

### Task 6: OperatingPoint schema + FilterUnderTest socket (base rate, no OS yet)

**Files:**
- Create: `tests/characterization/OperatingPoint.h`, `tests/characterization/FilterUnderTest.h`, `tests/characterization/FilterUnderTest.cpp`
- Test: new file `tests/FilterUnderTestTests.cpp`; register in `tests/CMakeLists.txt` and add `characterization/FilterUnderTest.cpp` + the Moog/Huggett model sources to `k2000_tests`.

**Interfaces:**
- Consumes: `FilterModel`, `MoogLadder`, `HuggettFilter`, `testdsp::SteppedSine`.
- Produces: `chz::Mode`, `chz::OsMode`, `chz::OperatingPoint`; `chz::FilterUnderTest` with `void setOperatingPoint(const OperatingPoint&)`, `void reset()`, `void process(float*, int)`, `bool supports(Mode) const`, `juce::String name() const`; factories `std::unique_ptr<FilterUnderTest> makeMoogFut()`, `makeHuggettFut()`.

- [ ] **Step 1: Write the failing test**

Add `tests/FilterUnderTestTests.cpp`:

```cpp
#include <juce_core/juce_core.h>
#include "characterization/FilterUnderTest.h"
#include "testdsp/SteppedSine.h"
#include <memory>

struct FilterUnderTestTests : public juce::UnitTest {
    FilterUnderTestTests() : juce::UnitTest("FilterUnderTest") {}
    void runTest() override {
        const double sr = 96000.0;

        beginTest("Moog FUT: supports LP/BP/HP, rejects Notch");
        {
            auto fut = chz::makeMoogFut();
            expect(fut->supports(chz::Mode::LP24));
            expect(fut->supports(chz::Mode::BP));
            expect(! fut->supports(chz::Mode::Notch), "Moog has no Notch");
            expect(fut->name() == "moog", "name is moog");
        }

        beginTest("Huggett FUT: supports Notch");
        {
            auto fut = chz::makeHuggettFut();
            expect(fut->supports(chz::Mode::Notch), "Huggett supports Notch");
            expect(fut->name() == "huggett");
        }

        beginTest("Moog LP24 passes bass, rejects treble (socket drives a real measurement)");
        {
            auto fut = chz::makeMoogFut();
            chz::OperatingPoint op; op.mode = chz::Mode::LP24; op.cutoffHz = 1000.0;
            op.resonance = 0.0; op.drive = 0.0; op.hostSampleRate = sr; op.osFactor = 1;
            fut->setOperatingPoint(op);
            auto r = testdsp::SteppedSine::transfer(*fut, { 100.0, 8000.0 }, sr, 0.05f);
            expect(r.magDb[0] > -3.0, "100 Hz near passband: " + juce::String(r.magDb[0], 1));
            expect(r.magDb[1] < -24.0, "8 kHz strongly attenuated: " + juce::String(r.magDb[1], 1));
        }
    }
};
static FilterUnderTestTests filterUnderTestTestsInstance;
```

Register `FilterUnderTestTests.cpp` and `characterization/FilterUnderTest.cpp` in `tests/CMakeLists.txt`. (The Moog/Huggett `.cpp` sources are already in `k2000_tests`.)

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target k2000_tests -j4`
Expected: FAIL to compile — `characterization/FilterUnderTest.h` not found.

- [ ] **Step 3: Write the schema header**

Create `tests/characterization/OperatingPoint.h`:

```cpp
#pragma once
#include <juce_core/juce_core.h>

// chz — filter CHaracteriZation layer. One OperatingPoint describes a single
// measurement configuration; it is the shared schema for L1, L2, and the CSV
// column header, so a fingerprint row and (later, sub-project #2) an external
// capture row are column-aligned.
namespace chz {

enum class Mode   { LP12, LP24, BP, HP, Notch };
enum class OsMode { Live, Render };

struct OperatingPoint {
    Mode   mode           = Mode::LP24;
    double cutoffHz       = 1000.0;
    double resonance      = 0.0;     // 0 .. 1
    double drive          = 0.0;     // 0 .. 1
    int    osFactor       = 1;       // 1, 2, 4, 8
    OsMode osMode         = OsMode::Live;
    double hostSampleRate = 96000.0;
};

inline juce::String modeName(Mode m) {
    switch (m) { case Mode::LP12: return "LP12"; case Mode::LP24: return "LP24";
                 case Mode::BP: return "BP"; case Mode::HP: return "HP";
                 case Mode::Notch: return "Notch"; }
    return "?";
}
inline juce::String osModeName(OsMode m) { return m == OsMode::Live ? "live" : "render"; }

} // namespace chz
```

- [ ] **Step 4: Write the socket header**

Create `tests/characterization/FilterUnderTest.h`:

```cpp
#pragma once
#include "OperatingPoint.h"
#include "../../src/dsp/spine/FilterModel.h"
#include "../../src/dsp/VoiceOversampler.h"
#include <juce_core/juce_core.h>
#include <functional>
#include <memory>
#include <vector>

namespace chz {

// One uniform socket to excite ANY FilterModel. Generic params (cutoff/res/drive)
// go through FilterModel::setCommon; model-specific mode/slope go through a
// Configurator bound at construction (this is where per-model knowledge lives).
// The OS axis is applied internally via VoiceOversampler: when osFactor > 1 the
// model runs at hostSampleRate*osFactor and the socket up/downsamples around it.
// Adapter contract for testdsp engines: reset(); process(float*, int).
class FilterUnderTest {
public:
    // Returns false if the model does not support the requested Mode.
    using Configurator = std::function<bool(FilterModel&, Mode)>;

    FilterUnderTest(juce::String name, std::unique_ptr<FilterModel> model, Configurator cfg);

    juce::String name() const { return name_; }
    bool supports(Mode m) const;

    void setOperatingPoint(const OperatingPoint& op);
    void reset();
    void process(float* mono, int n);   // base-rate in/out; OS applied internally

private:
    juce::String name_;
    std::unique_ptr<FilterModel> model_;
    Configurator cfg_;
    std::unique_ptr<FilterModel::State> state_;
    OperatingPoint op_;
    VoiceOversampler os_;
    std::vector<float> upL_, upR_, dnL_, dnR_;   // OS scratch (heap; MSVC stack-safe)
    static constexpr int kBlock = 1024;          // base-rate processing block
};

std::unique_ptr<FilterUnderTest> makeMoogFut();
std::unique_ptr<FilterUnderTest> makeHuggettFut();

} // namespace chz
```

- [ ] **Step 5: Write the socket implementation**

Create `tests/characterization/FilterUnderTest.cpp`:

```cpp
#include "FilterUnderTest.h"
#include "../../src/dsp/spine/MoogLadder.h"
#include "../../src/dsp/spine/HuggettFilter.h"

namespace chz {

FilterUnderTest::FilterUnderTest(juce::String name, std::unique_ptr<FilterModel> model,
                                 Configurator cfg)
    : name_(std::move(name)), model_(std::move(model)), cfg_(std::move(cfg)) {}

bool FilterUnderTest::supports(Mode m) const {
    // Probe the configurator without disturbing measurement state.
    return cfg_(*model_, m);
}

void FilterUnderTest::setOperatingPoint(const OperatingPoint& op) {
    op_ = op;
    const double effSr = op.hostSampleRate * (double) op.osFactor;
    model_->prepare(effSr);
    cfg_(*model_, op.mode);
    model_->setCommon((float) op.cutoffHz, (float) op.resonance, (float) op.drive);
    state_.reset(model_->makeState());
    model_->reset(*state_);

    os_.prepare(kBlock);
    os_.setFactor(op.osFactor);
    const size_t cap = (size_t) kBlock * (size_t) VoiceOversampler::kMaxFactor;
    upL_.assign(cap, 0.0f); upR_.assign(cap, 0.0f);
    dnL_.assign((size_t) kBlock, 0.0f); dnR_.assign((size_t) kBlock, 0.0f);
}

void FilterUnderTest::reset() {
    if (state_) model_->reset(*state_);
    os_.setFactor(op_.osFactor);   // also clears the halfband state
}

void FilterUnderTest::process(float* mono, int n) {
    int done = 0;
    while (done < n) {
        const int blk = std::min(kBlock, n - done);
        if (op_.osFactor == 1) {
            // Base rate: duplicate L into R-scratch, discard R (mono measurement).
            std::copy(mono + done, mono + done + blk, dnR_.begin());
            model_->processStereo(*state_, mono + done, dnR_.data(), blk);
        } else {
            os_.processMonoUp(mono + done, blk, upL_.data());
            std::copy(upL_.begin(), upL_.begin() + os_.osBlock(blk), upR_.begin());
            model_->processStereo(*state_, upL_.data(), upR_.data(), os_.osBlock(blk));
            os_.processStereoDown(upL_.data(), upR_.data(), blk, dnL_.data(), dnR_.data());
            std::copy(dnL_.begin(), dnL_.begin() + blk, mono + done);
        }
        done += blk;
    }
}

std::unique_ptr<FilterUnderTest> makeMoogFut() {
    auto cfg = [](FilterModel& fm, Mode m) -> bool {
        auto& moog = static_cast<MoogLadder&>(fm);
        switch (m) {
            case Mode::LP12: moog.setMode(MoogLadder::Mode::LP); moog.setSlope(MoogLadder::Slope::db12); return true;
            case Mode::LP24: moog.setMode(MoogLadder::Mode::LP); moog.setSlope(MoogLadder::Slope::db24); return true;
            case Mode::BP:   moog.setMode(MoogLadder::Mode::BP); return true;
            case Mode::HP:   moog.setMode(MoogLadder::Mode::HP); return true;
            case Mode::Notch: return false;   // Moog ladder has no notch
        }
        return false;
    };
    return std::make_unique<FilterUnderTest>("moog", std::make_unique<MoogLadder>(), cfg);
}

std::unique_ptr<FilterUnderTest> makeHuggettFut() {
    auto cfg = [](FilterModel& fm, Mode m) -> bool {
        auto& hug = static_cast<HuggettFilter&>(fm);
        switch (m) {
            case Mode::LP12: hug.setRouting(HuggettFilter::Routing::LP); hug.setSlope(HuggettFilter::Slope::db12); return true;
            case Mode::LP24: hug.setRouting(HuggettFilter::Routing::LP); hug.setSlope(HuggettFilter::Slope::db24); return true;
            case Mode::BP:   hug.setRouting(HuggettFilter::Routing::BP); return true;
            case Mode::HP:   hug.setRouting(HuggettFilter::Routing::HP); return true;
            case Mode::Notch: hug.setRouting(HuggettFilter::Routing::Notch); return true;
        }
        return false;
    };
    return std::make_unique<FilterUnderTest>("huggett", std::make_unique<HuggettFilter>(), cfg);
}

} // namespace chz
```

> Note on `supports()`: it calls the configurator, which mutates the model's mode. That is harmless because `setOperatingPoint()` always re-applies mode before measuring. Keep `supports()` calls out of the middle of a measurement.

- [ ] **Step 6: Run tests to verify they pass**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests`
Expected: `[PASS] FilterUnderTest`.

- [ ] **Step 7: Commit**

```bash
git add tests/characterization/OperatingPoint.h tests/characterization/FilterUnderTest.h tests/characterization/FilterUnderTest.cpp tests/FilterUnderTestTests.cpp tests/CMakeLists.txt
git commit -m "feat(chz): model-agnostic FilterUnderTest socket + Moog/Huggett factories" -m "Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 7: OS round-trip fidelity through the socket

Proves the OS path (factor 2/4/8) is transparent in-band — a prerequisite for trusting OS-tier measurements in Phase 4.

**Files:**
- Modify: `tests/FilterUnderTestTests.cpp` (add a test)

**Interfaces:**
- Consumes: `chz::FilterUnderTest`, `testdsp::SteppedSine`.

- [ ] **Step 1: Write the failing test**

Add to `FilterUnderTestTests::runTest()`:

```cpp
beginTest("OS factors are in-band transparent (LP, probe well below cutoff)");
{
    auto fut = chz::makeMoogFut();
    const double sr = 96000.0;
    auto measAt = [&](int factor) {
        chz::OperatingPoint op; op.mode = chz::Mode::LP24; op.cutoffHz = 2000.0;
        op.hostSampleRate = sr; op.osFactor = factor;
        fut->setOperatingPoint(op);
        return testdsp::SteppedSine::transfer(*fut, { 200.0 }, sr, 0.05f).magDb[0];
    };
    const double base = measAt(1);
    for (int f : { 2, 4, 8 })
        expectWithinAbsoluteError(measAt(f), base, 0.5);   // within 0.5 dB of base rate in-band
}
```

- [ ] **Step 2: Run test to verify it fails or passes**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests`
Expected: PASS if the OS path is correct. If it FAILS, the socket's up/down wiring is wrong — debug `process()` before proceeding (this test is the gate for Phase 4).

- [ ] **Step 3: Commit**

```bash
git add tests/FilterUnderTestTests.cpp
git commit -m "test(chz): OS round-trip in-band transparency through the socket" -m "Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 8: CharacterizationRunner — batteries + fingerprint + CSV writer

**Files:**
- Create: `tests/characterization/CharacterizationRunner.h`, `tests/characterization/CharacterizationRunner.cpp`
- Test: new file `tests/CharacterizationRunnerTests.cpp`; register both in `tests/CMakeLists.txt` for `k2000_tests`.

**Interfaces:**
- Consumes: `chz::FilterUnderTest`, all L0 engines.
- Produces:
  - `chz::Grid { std::vector<Mode> modes; std::vector<double> cutoffs, resonances, drives, hostRates, probeFreqs; std::vector<int> osFactors; std::vector<OsMode> osModes; }`
  - `chz::Summary` = `std::map<juce::String, double>` (headline metrics keyed e.g. `moog/LP24/fc1000/corner_hz`, `.../slope_db_oct`, `.../selfosc_cents_err`, `.../alias_db@os1`, `.../method_delta_db`).
  - `chz::CharacterizationRunner::run(FilterUnderTest&, const Grid&, const juce::File& outDir) -> Summary` — runs all four batteries, writes dense CSVs to `outDir`, returns the headline summary.
  - Free helpers: `chz::Grid coarseGrid()` (fast/CI grid) and `chz::Grid fullGrid()` (heavy grid).

- [ ] **Step 1: Write the failing test**

Add `tests/CharacterizationRunnerTests.cpp`:

```cpp
#include <juce_core/juce_core.h>
#include "characterization/CharacterizationRunner.h"
#include "characterization/FilterUnderTest.h"

struct CharacterizationRunnerTests : public juce::UnitTest {
    CharacterizationRunnerTests() : juce::UnitTest("CharacterizationRunner") {}
    void runTest() override {
        beginTest("coarseGrid is bounded: 96k only, OS {1,2,4,8}, live only");
        {
            auto g = chz::coarseGrid();
            expect(g.hostRates.size() == 1 && g.hostRates[0] == 96000.0, "single host rate 96k");
            expect(g.osFactors == (std::vector<int>{1,2,4,8}), "all four OS factors");
            expect(g.osModes.size() == 1 && g.osModes[0] == chz::OsMode::Live, "live only");
        }

        beginTest("runner produces LP24 headline metrics for Moog and writes a CSV");
        {
            auto fut = chz::makeMoogFut();
            chz::Grid g;                                   // tiny grid for a fast unit test
            g.modes = { chz::Mode::LP24 };
            g.cutoffs = { 1000.0 }; g.resonances = { 0.0 }; g.drives = { 0.0 };
            g.osFactors = { 1 }; g.osModes = { chz::OsMode::Live }; g.hostRates = { 96000.0 };
            g.probeFreqs = chz::CharacterizationRunner::logFreqs(50.0, 20000.0, 40);

            auto outDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                              .getChildFile("chz_runner_test");
            outDir.deleteRecursively(); outDir.createDirectory();

            auto summary = chz::CharacterizationRunner::run(*fut, g, outDir);

            // -3 dB corner near 1000 Hz (within +-1 octave is a loose sanity bound).
            const double corner = summary.at("moog/LP24/fc1000/corner_hz");
            expect(corner > 500.0 && corner < 2000.0, "corner near fc: " + juce::String(corner));
            // 24 dB/oct LP: one octave above the corner is steeply down.
            expect(summary.at("moog/LP24/fc1000/slope_db_oct") < -18.0, "steep LP slope");
            expect(outDir.getChildFile("response.csv").existsAsFile(), "response.csv written");
            outDir.deleteRecursively();
        }
    }
};
static CharacterizationRunnerTests characterizationRunnerTestsInstance;
```

Register `CharacterizationRunnerTests.cpp` + `characterization/CharacterizationRunner.cpp` in `tests/CMakeLists.txt`.

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target k2000_tests -j4`
Expected: FAIL to compile — `CharacterizationRunner.h` not found.

- [ ] **Step 3: Write the header**

Create `tests/characterization/CharacterizationRunner.h`:

```cpp
#pragma once
#include "OperatingPoint.h"
#include "FilterUnderTest.h"
#include <juce_core/juce_core.h>
#include <map>
#include <vector>

namespace chz {

struct Grid {
    std::vector<Mode>   modes;
    std::vector<double> cutoffs, resonances, drives, hostRates, probeFreqs;
    std::vector<int>    osFactors;
    std::vector<OsMode> osModes;
};

Grid coarseGrid();   // fast/CI: 96k, OS {1,2,4,8}, live, coarse cutoff/res
Grid fullGrid();     // heavy: all 5 host rates, OS {1,2,4,8} x {live,render}, dense

using Summary = std::map<juce::String, double>;

struct CharacterizationRunner {
    static std::vector<double> logFreqs(double f0, double f1, int n);

    // Runs B1..B4 over the grid for one filter. Writes dense CSVs into outDir
    // (response.csv, resonance.csv, distortion.csv) and returns headline metrics.
    static Summary run(FilterUnderTest& fut, const Grid& g, const juce::File& outDir);
};

} // namespace chz
```

- [ ] **Step 4: Write the implementation**

Create `tests/characterization/CharacterizationRunner.cpp`. Implement, in order:
- `logFreqs(f0,f1,n)` — log-spaced grid.
- `coarseGrid()` — `hostRates={96000}`, `osFactors={1,2,4,8}`, `osModes={Live}`, `modes={LP24,BP,HP}`, `cutoffs={250,1000,4000}`, `resonances={0.0,0.9}`, `drives={0.0}`, `probeFreqs=logFreqs(20,24000,200)`.
- `fullGrid()` — `hostRates={44100,48000,88200,96000,192000}`, `osFactors={1,2,4,8}`, `osModes={Live,Render}`, `modes={LP12,LP24,BP,HP,Notch}`, dense `cutoffs` (e.g. 12 log points 50..16000), `resonances={0,0.3,0.6,0.9,1.0}`, `drives={0,0.5,1.0}`, `probeFreqs=logFreqs(10,25000,700)`.
- `run(...)` — for each operating point the model `supports()`:
  - **B1 (magnitude, both engines):** `SteppedSine::transfer` (reference) + ESS path (`Sweep::ess`→`process`→`Sweep::impulseResponse`→`TransferFunction::fromImpulse`). Compute `-3 dB corner_hz` (first freq below set cutoff region where magnitude drops 3 dB from passband), `slope_db_oct` (magnitude delta one octave above corner), and `method_delta_db = MethodAgreement::maxMagDeltaDb(stepped.magDb, ess.magDb)`. Write a `response.csv` row per probe freq with columns `model,mode,osFactor,osMode,hostSR,cutoffHz,resonance,drive,probeHz,method,magDb,phaseRad,groupDelaySec`.
  - **B2 (resonance/self-osc):** at `resonance=max`, kick an impulse, capture, `Response::peakFreqHz` → `selfosc_cents_err` vs set cutoff (report-only above 4 kHz). Peak gain/Q from B1 at high resonance. Write `resonance.csv`.
  - **B3 (distortion/aliasing):** `Harmonics::thdDb` at a mid tone for THD; aliasing via the existing oversampled-truth pattern — run the model at the operating point, and compare against a high-`M` decimated truth using `Reference::decimate` + `Reference::noiseToSignalDb` to get `alias_db@os<factor>`. Write `distortion.csv`.
  - **B4 (phase/group delay):** taken from the ESS `TransferFunction::Result` already computed in B1 (no extra excitation).
  - Headline summary keys per `(model,mode,cutoff)`: `corner_hz`, `slope_db_oct`, `selfosc_cents_err`, `method_delta_db`, `alias_db@os<factor>`, `thd_db`.
- Robustness: guard NaN/inf (`std::isfinite`) on every metric before storing; if self-osc never started, store a sentinel (e.g. `-1.0`) and log a warning via `juce::Logger::writeToLog` — never silently skip.

Keep all large buffers in `std::vector` (heap). Use `juce::String` + `juce::File::appendText`/a `juce::FileOutputStream` for CSV writing; write a header row first.

- [ ] **Step 5: Run test to verify it passes**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests`
Expected: `[PASS] CharacterizationRunner`.

- [ ] **Step 6: Commit**

```bash
git add tests/characterization/CharacterizationRunner.h tests/characterization/CharacterizationRunner.cpp tests/CharacterizationRunnerTests.cpp tests/CMakeLists.txt
git commit -m "feat(chz): CharacterizationRunner — four batteries, fingerprint, CSV artifacts" -m "Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 9: Opt-in heavy executable `k2000_filter_characterization` + Moog fingerprint + docs

**Files:**
- Create: `tests/characterization/characterize_main.cpp`
- Modify: `tests/CMakeLists.txt` (add the new executable target; NOT registered via `add_test`)
- Create: `docs/filter-validation/interpreting-results.md`, `docs/filter-validation/operating-points.md`

**Interfaces:**
- Consumes: `chz::CharacterizationRunner`, `chz::makeMoogFut`, `chz::makeHuggettFut`.
- Produces: a CLI binary `./build/tests/k2000_filter_characterization [--model moog|huggett|all]` that writes `build/characterization/<model>/{response,resonance,distortion}.csv` + `summary.csv`, prints a human summary, returns 0 on all-gates-pass else 1.

- [ ] **Step 1: Write the executable**

Create `tests/characterization/characterize_main.cpp`:

```cpp
#include <juce_core/juce_core.h>
#include "CharacterizationRunner.h"
#include "FilterUnderTest.h"
#include "../testdsp/GoldenIO.h"
#include <cstdio>
#include <vector>

// Self-sufficient heavy runner. CI and a plain developer use this directly; the
// /characterize-filter skill is only an interactive layer on top.
static int runOne(const juce::String& model) {
    auto fut = (model == "moog") ? chz::makeMoogFut() : chz::makeHuggettFut();
    auto outDir = juce::File::getCurrentWorkingDirectory()
                      .getChildFile("build/characterization").getChildFile(model);
    outDir.deleteRecursively(); outDir.createDirectory();
    auto summary = chz::CharacterizationRunner::run(*fut, chz::fullGrid(), outDir);

    // Persist the summary and print a readable digest; gate on method-agreement.
    std::map<juce::String, double> asMap(summary.begin(), summary.end());
    testdsp::GoldenIO::save(outDir.getChildFile("summary.csv"), asMap);

    double worstDelta = 0.0;
    for (const auto& kv : summary)
        if (kv.first.endsWith("method_delta_db")) worstDelta = std::max(worstDelta, kv.second);
    std::printf("[%s] worst method-agreement delta = %.3f dB\n",
                model.toRawUTF8(), worstDelta);
    std::printf("[%s] summary written to %s\n",
                model.toRawUTF8(), outDir.getFullPathName().toRawUTF8());
    return worstDelta < 1.0 ? 0 : 1;     // method-agreement is the headline gate
}

int main(int argc, char** argv) {
    juce::String model = "all";
    for (int i = 1; i < argc; ++i)
        if (juce::String(argv[i]) == "--model" && i + 1 < argc) model = argv[++i];

    int rc = 0;
    if (model == "all") { rc |= runOne("moog"); rc |= runOne("huggett"); }
    else                  rc = runOne(model);
    std::printf("\n%s\n", rc == 0 ? "PASS" : "FAIL");
    return rc;
}
```

- [ ] **Step 2: Add the CMake target**

In `tests/CMakeLists.txt`, after the `k2000_tests` block, add a second executable that compiles `characterize_main.cpp`, `characterization/FilterUnderTest.cpp`, `characterization/CharacterizationRunner.cpp`, and the same model/DSP `.cpp` sources `k2000_tests` links (MoogLadder, HuggettFilter, the cmajor adapters, SpineFilterSlot, HuggettHpStage, FilterModelLibrary). Mirror the `target_include_directories`, `target_link_libraries`, `target_compile_definitions` (including `BERNIE_GOLDEN_DIR`), and `CXX_STANDARD 17`. **Do NOT call `add_test` on it** — it must stay out of CI.

```cmake
add_executable(k2000_filter_characterization
    characterization/characterize_main.cpp
    characterization/FilterUnderTest.cpp
    characterization/CharacterizationRunner.cpp
    ../src/dsp/spine/MoogLadder.cpp
    ../src/dsp/spine/HuggettFilter.cpp
    ../src/dsp/spine/HuggettHpStage.cpp
    ../src/dsp/spine/SpineFilterSlot.cpp
    ../src/dsp/spine/FilterModelLibrary.cpp
    ../src/dsp/spine/cmajor/MoogLadderAdapter.cpp
    ../src/dsp/spine/cmajor/NlSvfAdapter.cpp
    ../src/dsp/spine/cmajor/AsymDriveAdapter.cpp
    ../src/dsp/spine/cmajor/NlSvfLeanAdapter.cpp
    ../src/dsp/spine/cmajor/NlSvfDriveLeanAdapter.cpp
    ../src/dsp/spine/cmajor/SvfLinearAdapter.cpp
    ../src/dsp/spine/cmajor/CmajorSvfFilter.cpp)
target_include_directories(k2000_filter_characterization PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(k2000_filter_characterization PRIVATE
    juce::juce_core juce::juce_audio_basics juce::juce_dsp
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
target_compile_definitions(k2000_filter_characterization PRIVATE
    JUCE_STANDALONE_APPLICATION=1 JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 K2000_TESTING=1
    BERNIE_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")
set_target_properties(k2000_filter_characterization PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
```

> The exact cmajor-adapter source list may need trimming to what actually links — if the build reports an undefined symbol, add the named `.cpp`; if it reports an unused source, the linker will not complain. Verify against the `k2000_tests` source list (lines 65-72 of the original CMake) as the source of truth.

- [ ] **Step 3: Build and run the heavy target for Moog**

Run: `cmake --build build --target k2000_filter_characterization -j4 && ./build/tests/k2000_filter_characterization --model moog`
Expected: prints `[moog] worst method-agreement delta = <small> dB`, writes `build/characterization/moog/response.csv` etc., final line `PASS`.

- [ ] **Step 4: Write the docs pages**

Create `docs/filter-validation/interpreting-results.md` (column-by-column meaning of `response.csv`/`resonance.csv`/`distortion.csv`/`summary.csv`; the method-agreement delta; self-osc in cents; how to read a slope/corner) and `docs/filter-validation/operating-points.md` (the axis model + the fast vs heavy grids).

- [ ] **Step 5: Commit**

```bash
git add tests/characterization/characterize_main.cpp tests/CMakeLists.txt docs/filter-validation/interpreting-results.md docs/filter-validation/operating-points.md
git commit -m "feat(chz): opt-in k2000_filter_characterization heavy runner + Moog fingerprint + docs" -m "Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

# Phase 3 — Huggett back-test (model-agnosticism gate)

### Task 10: Run the full battery through Huggett and prove the socket is model-agnostic

**Files:**
- Modify: `tests/CharacterizationRunnerTests.cpp` (add a Huggett smoke test)
- Create: `docs/filter-validation/extending.md`

**Interfaces:**
- Consumes: `chz::makeHuggettFut`, `chz::CharacterizationRunner`.

- [ ] **Step 1: Write the failing test**

Add to `CharacterizationRunnerTests::runTest()`:

```cpp
beginTest("Huggett runs the same battery (model-agnostic socket) incl. Notch");
{
    auto fut = chz::makeHuggettFut();
    chz::Grid g;
    g.modes = { chz::Mode::LP24, chz::Mode::Notch };
    g.cutoffs = { 1000.0 }; g.resonances = { 0.0 }; g.drives = { 0.0 };
    g.osFactors = { 1 }; g.osModes = { chz::OsMode::Live }; g.hostRates = { 96000.0 };
    g.probeFreqs = chz::CharacterizationRunner::logFreqs(50.0, 20000.0, 40);

    auto outDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                      .getChildFile("chz_huggett_test");
    outDir.deleteRecursively(); outDir.createDirectory();
    auto summary = chz::CharacterizationRunner::run(*fut, g, outDir);

    expect(summary.count("huggett/LP24/fc1000/corner_hz") == 1, "Huggett LP24 corner present");
    expect(summary.count("huggett/Notch/fc1000/corner_hz") == 1, "Huggett Notch ran");
    outDir.deleteRecursively();
}
```

- [ ] **Step 2: Run test to verify it passes (or surfaces a Moog-shaped assumption)**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests`
Expected: PASS. If it FAILS, the runner/socket has a Moog-specific assumption — fix it in `CharacterizationRunner.cpp` / `FilterUnderTest.cpp` (this is the whole point of running the back-test early).

- [ ] **Step 3: Write `extending.md`** — how to add a new filter model: write a `makeXFut()` factory with the model-specific `Configurator`, list which `Mode`s it supports, and how to generate + commit its baseline (Phase 5). Document the `Configurator` contract (return false for unsupported modes).

- [ ] **Step 4: Commit**

```bash
git add tests/CharacterizationRunnerTests.cpp docs/filter-validation/extending.md
git commit -m "test(chz): Huggett back-test proves model-agnostic socket (incl. Notch)" -m "Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

# Phase 4 — OS tiers go live

### Task 11: Aliasing-vs-osFactor deliverable

The OS path already runs (Tasks 7-8). This task makes the **aliasing-vs-osFactor curve** a first-class output and asserts the expected monotonic improvement.

**Files:**
- Modify: `tests/CharacterizationRunnerTests.cpp` (assert aliasing improves with OS factor)
- Modify: `docs/filter-validation/operating-points.md` (add the OS-tier section: what the curve means, how live vs render differ as labels over the same engine)

**Interfaces:**
- Consumes: `summary["<model>/<mode>/fc<cut>/alias_db@os<factor>"]` produced by Task 8's B3.

- [ ] **Step 1: Write the failing test**

Add to `CharacterizationRunnerTests::runTest()` (drive the model hard so aliasing is measurable, sweep OS factors, assert higher factor = less aliasing):

```cpp
beginTest("aliasing decreases as OS factor rises (Moog, driven LP)");
{
    auto fut = chz::makeMoogFut();
    chz::Grid g;
    g.modes = { chz::Mode::LP24 };
    g.cutoffs = { 4000.0 }; g.resonances = { 0.0 }; g.drives = { 1.0 };   // hard drive -> harmonics fold
    g.osFactors = { 1, 2, 4, 8 }; g.osModes = { chz::OsMode::Live };
    g.hostRates = { 96000.0 };
    g.probeFreqs = chz::CharacterizationRunner::logFreqs(50.0, 20000.0, 40);

    auto outDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                      .getChildFile("chz_alias_test");
    outDir.deleteRecursively(); outDir.createDirectory();
    auto s = chz::CharacterizationRunner::run(*fut, g, outDir);

    const double a1 = s.at("moog/LP24/fc4000/alias_db@os1");
    const double a8 = s.at("moog/LP24/fc4000/alias_db@os8");
    logMessage("alias os1=" + juce::String(a1, 1) + " dB, os8=" + juce::String(a8, 1) + " dB");
    expect(a8 < a1 - 3.0, "8x must reduce aliasing by >3 dB vs 1x");
    outDir.deleteRecursively();
}
```

- [ ] **Step 2: Run test**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests`
Expected: PASS, with the `alias os1=… os8=…` log line showing the improvement. If aliasing does NOT improve, the B3 aliasing metric or the OS path is mis-wired — debug before proceeding (this is the OS-validation payoff).

- [ ] **Step 3: Commit**

```bash
git add tests/CharacterizationRunnerTests.cpp docs/filter-validation/operating-points.md
git commit -m "test(chz): aliasing-vs-osFactor curve — OS tiers validated live" -m "Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

# Phase 5 — Spec-gate subset + self-golden baselines into CI

### Task 12: Fast gate subset in `k2000_tests` (spec gates + method-agreement + coarse self-golden)

**Files:**
- Create: `tests/CharacterizationGateTests.cpp`; register in `tests/CMakeLists.txt` for `k2000_tests`.

**Interfaces:**
- Consumes: `chz::coarseGrid`, `chz::CharacterizationRunner`, `chz::makeMoogFut`, `chz::makeHuggettFut`, `testdsp::GoldenSet`.
- Produces: the CI gate. Uses `GoldenSet("moog/baseline")` / `GoldenSet("huggett/baseline")`.

- [ ] **Step 1: Write the failing test**

Create `tests/CharacterizationGateTests.cpp`:

```cpp
#include <juce_core/juce_core.h>
#include "characterization/CharacterizationRunner.h"
#include "characterization/FilterUnderTest.h"
#include "testdsp/GoldenIO.h"

struct CharacterizationGateTests : public juce::UnitTest {
    CharacterizationGateTests() : juce::UnitTest("CharacterizationGate") {}

    void gateModel(const juce::String& name) {
        auto fut = (name == "moog") ? chz::makeMoogFut() : chz::makeHuggettFut();
        auto outDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("chz_gate_" + name);
        outDir.deleteRecursively(); outDir.createDirectory();
        auto s = chz::CharacterizationRunner::run(*fut, chz::coarseGrid(), outDir);

        // Spec gate: LP24 at fc=1000 is steep, and the two methods agree everywhere.
        beginTest(name + ": LP24 fc1000 slope is >= 24 dB/oct-ish");
        testdsp::Gate::check(*this, s.at(name + "/LP24/fc1000/slope_db_oct"), -18.0,
                             testdsp::Gate::Dir::Max, name + " LP24 slope");

        beginTest(name + ": method-agreement < 1 dB across the coarse grid");
        double worst = 0.0;
        for (const auto& kv : s)
            if (kv.first.endsWith("method_delta_db")) worst = std::max(worst, kv.second);
        testdsp::Gate::check(*this, worst, 1.0, testdsp::Gate::Dir::Max,
                             name + " worst method delta");

        beginTest(name + ": self-golden baseline (coarse headline metrics)");
        testdsp::GoldenSet gs(name + "/baseline");
        gs.check(*this, "LP24/fc1000/corner_hz", s.at(name + "/LP24/fc1000/corner_hz"), 50.0);
        gs.check(*this, "LP24/fc1000/slope_db_oct", s.at(name + "/LP24/fc1000/slope_db_oct"), 2.0);
        gs.flush();

        outDir.deleteRecursively();
    }

    void runTest() override { gateModel("moog"); gateModel("huggett"); }
};
static CharacterizationGateTests characterizationGateTestsInstance;
```

Register `CharacterizationGateTests.cpp` in `tests/CMakeLists.txt`.

- [ ] **Step 2: Run test to verify it fails (golden missing)**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests`
Expected: FAIL — `golden missing key 'LP24/fc1000/corner_hz' ... (run with BERNIE_UPDATE_GOLDEN=1 to create)`.

- [ ] **Step 3: Generate the committed baselines**

Run: `BERNIE_UPDATE_GOLDEN=1 ./build/tests/k2000_tests`
This writes `tests/golden/moog/baseline.csv` and `tests/golden/huggett/baseline.csv`.

- [ ] **Step 4: Run test to verify it passes**

Run: `./build/tests/k2000_tests`
Expected: `[PASS] CharacterizationGate`.

- [ ] **Step 5: Commit (test + both baselines)**

```bash
git add tests/CharacterizationGateTests.cpp tests/CMakeLists.txt tests/golden/moog/baseline.csv tests/golden/huggett/baseline.csv
git commit -m "test(chz): CI gate subset — spec gates + method-agreement + self-golden baselines" -m "Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 13: Verify the full suite + Windows CI compile gate; write `running.md` + `troubleshooting.md`

**Files:**
- Create: `docs/filter-validation/running.md`, `docs/filter-validation/troubleshooting.md`

- [ ] **Step 1: Full local suite**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests`
Expected: `Summary: N tests, 0 failed` (N = previous 203 + the new test classes).

- [ ] **Step 2: Trigger the Windows CI compile gate (MSVC)**

Run: `gh workflow run build.yml --ref feat/filter-validation-internal`
Then watch: `gh run watch $(gh run list --branch feat/filter-validation-internal --limit 1 --json databaseId --jq '.[0].databaseId')`
Expected: green. If MSVC fails on `M_PI` or a stack-size issue, fix per Global Constraints and re-run. Do not proceed until CI is green.

- [ ] **Step 3: Write the docs**

Create `docs/filter-validation/running.md` (CLI flags, the `coarseGrid` CI subset vs the `fullGrid` heavy run, the `BERNIE_UPDATE_GOLDEN` baseline-refresh workflow, the manual `workflow_dispatch` path) and `docs/filter-validation/troubleshooting.md` (golden-missing message, NaN/degenerate guards, "self-osc never started" sentinel, baseline drift vs regression).

- [ ] **Step 4: Commit**

```bash
git add docs/filter-validation/running.md docs/filter-validation/troubleshooting.md
git commit -m "docs(chz): running + troubleshooting pages; CI compile gate verified" -m "Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

# Phase 6 — The `/characterize-filter` skill

### Task 14: Interactive front-door skill over the self-sufficient binary

**Files:**
- Create: `.claude/skills/characterize-filter/SKILL.md`

- [ ] **Step 1: Write the skill**

Create `.claude/skills/characterize-filter/SKILL.md` with frontmatter (`name: characterize-filter`, a `description` matching the invocation `/characterize-filter <model|all>`), and a body instructing: build `k2000_filter_characterization` (`-j4`); run for the requested model(s); read `build/characterization/<model>/summary.csv`; present a concise report (per-battery headline metrics, method-agreement verdict, spec-gate pass/fail, aliasing-vs-osFactor digest); on self-golden drift, surface what changed and OFFER to refresh the baseline via `BERNIE_UPDATE_GOLDEN=1 ./build/tests/k2000_tests` — never auto-update. State the boundary explicitly: the binary stands alone; the skill only adds interpretation.

- [ ] **Step 2: Verify the skill is discoverable and the binary path is correct**

Run: `cmake --build build --target k2000_filter_characterization -j4 && ./build/tests/k2000_filter_characterization --model moog`
Expected: the binary runs and writes `build/characterization/moog/summary.csv` (the artifact the skill reads). Confirm the SKILL.md instructions reference this exact path.

- [ ] **Step 3: Commit**

```bash
git add .claude/skills/characterize-filter/SKILL.md
git commit -m "feat(chz): /characterize-filter skill — interactive front door over the heavy binary" -m "Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

# Phase 7 — Manual consolidation

### Task 15: `README.md` quickstart + cross-links + accuracy pass

**Files:**
- Create: `docs/filter-validation/README.md`
- Modify: all `docs/filter-validation/*.md` (add cross-links; verify each matches shipped behavior)

- [ ] **Step 1: Write the front-door README**

Create `docs/filter-validation/README.md`: what the harness is; the layered definition of "correct"; a 60-second quickstart (configure build, `cmake --build build --target k2000_filter_characterization -j4`, `./build/tests/k2000_filter_characterization --model moog`, where artifacts land); and a table linking every page (`concepts`, `running`, `interpreting-results`, `operating-points`, `extending`, `troubleshooting`).

- [ ] **Step 2: Accuracy pass**

Re-read each page against the shipped code: confirm every command, file path, CSV column name, env var (`BERNIE_UPDATE_GOLDEN`), and grid value matches the implementation. Fix drift inline.

- [ ] **Step 3: Final full verification**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests`
Expected: `Summary: N tests, 0 failed`.
Run: `cmake --build build --target k2000_filter_characterization -j4 && ./build/tests/k2000_filter_characterization --model all`
Expected: final line `PASS`.

- [ ] **Step 4: Commit**

```bash
git add docs/filter-validation/
git commit -m "docs(chz): README quickstart + cross-links + accuracy pass (manual consolidated)" -m "Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Self-Review (filled in by the plan author)

**1. Spec coverage:**
- L0 dual-method ruler + self-tests → Tasks 1-4. ✓
- L1 model-agnostic socket (retires `mag()`/`magR()` — see note below) → Task 6. ✓
- OS path in the socket → Tasks 6-7. ✓
- L2 runner + four batteries + artifacts → Task 8. ✓
- Opt-in `k2000_filter_characterization` (self-sufficient) → Task 9. ✓
- Huggett back-test → Task 10. ✓
- OS tiers live + aliasing-vs-osFactor → Task 11. ✓
- Spec-gate + method-agreement + coarse self-golden in CI (96k, OS{1,2,4,8}, live) → Task 12. ✓
- Committed baselines (`tests/golden/<model>/baseline.csv`) → Task 12. ✓
- Windows CI compile gate → Task 13. ✓
- `/characterize-filter` skill → Task 14. ✓
- Multi-file operator manual (one page per phase + consolidation) → Tasks 5, 9, 10, 13, 15. ✓
- Four batteries incl. phase/group delay → Task 8 (B4 from the ESS result). ✓

**2. Placeholder scan:** Task 8's `run()` and several docs steps are described as ordered specifications rather than full code, because they are integration/prose rather than novel algorithms — each names exact inputs, outputs, CSV columns, and metric keys. Acceptable per "fold setup/scaffolding into the task," but flag for the executor: Task 8 is the largest single task and may warrant splitting per-battery during execution.

**3. Type consistency:** `chz::Mode`/`OsMode`/`OperatingPoint`/`Grid`/`Summary` names, the `FilterUnderTest` method set (`setOperatingPoint`/`reset`/`process`/`supports`/`name`), the L0 signatures (`SteppedSine::transfer`, `Sweep::ess/inverseFilter/impulseResponse`, `TransferFunction::fromImpulse`, `MethodAgreement::maxMagDeltaDb`, `Harmonics::thdDb`), and the summary key convention (`<model>/<mode>/fc<cut>/<metric>`) are used identically across Tasks 6-14. ✓

**Two reconciliations to confirm with the user (deviations from the spec's letter, kept in spirit):**
- **Self-golden store** is the existing `GoldenSet` CSV (`tests/golden/<model>/baseline.csv`) rather than a new `baseline.json` — it already provides the exact update-or-assert workflow the spec asks for.
- **`mag()`/`magR()` retirement**: this plan builds the socket that replaces them, but does NOT delete the helpers from `MoogLadderTests.cpp` / `Halfband2xTests.cpp` / `SpineNlSvfHarnessTests.cpp` (to keep each task green and focused). A follow-up cleanup task can migrate those call sites onto `FilterUnderTest` once the socket is trusted; deleting them mid-plan would churn unrelated tests. Flag for the user: do this as a Phase 8 cleanup, or leave the duplication?
