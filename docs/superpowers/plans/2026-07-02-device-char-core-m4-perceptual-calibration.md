# Device-Char Core — Milestone 4: Perceptual Lenses + Calibration + Generator Path + Multi-Level — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close SP-A: add the two perceptual lenses (A-weighting, audibility-split aliasing), the physical-reference/loopback calibration math, a calibrated-tone Generator reference driven by a minimal Trigger excitation driver, multi-level excitation extractors (gain-vs-level knee, headroom-to-clip), idle-noise-floor summary keys + gate goldens, and the written §5.3 hardware-correlation acceptance criterion.

**Architecture:** All signal math lands as pure L0 helpers in `tests/testdsp/` (headers, no device knowledge), each proven test-first against a synthetic known answer (spec §7: "the synthetic references *are* the tests"). The Generator path is the one L2 change: `CharacterizationRunner::run` dispatches on `dut.excitation()` — `Trigger` devices route to a new `runGeneratorCapture` battery; the `InputSweep` path is byte-for-byte untouched. Noise-floor keys are additive summary keys + additive golden rows, same pattern as M3's level keys.

**Tech Stack:** C++17, JUCE 8 (`juce::UnitTest`, `juce::dsp::FFT` via `testdsp::Spectrum`), CMake. Namespaces: `testdsp` (L0), `chz` (L1/L2).

## Global Constraints

- **C++17**, JUCE 8, CMake. L0 helpers in `testdsp`, devices/runner in `chz`.
- **Bounded build parallelism: `-j4`** — never bare `-j` (OOM → 0-byte object → confusing link failure).
- **No shipping DSP changes** — `src/` is untouched; voicing is held for hardware (SP-D).
- **Existing metrics must not change**: the InputSweep runner path, existing summary keys, and existing golden rows stay byte-for-byte identical. Golden diffs must be **additive rows only** (verify with `git diff tests/golden/` at the marked steps).
- **Sentinel convention:** `-300.0` = no-data (matches `testdsp::Level`).
- **Suite bar:** `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests` → `0 failed`. Current baseline: **242 tests, 0 failed** (block count grows with new `beginTest` blocks; the bar is 0 failed).
- Golden regeneration: `BERNIE_UPDATE_GOLDEN=1 ./build/tests/k2000_tests`, then inspect the diff.
- Commit messages end with: `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`. If a message trips bash quoting, write it to a file and `git commit -F`.

## Milestone sequence (context)

Milestone 4 (final) of SP-A (spec: `docs/superpowers/specs/2026-07-01-device-characterization-core-design.md`, §4.2 perceptual lenses, §4.1 large-signal, §5.2 physical reference, §5.3 acceptance criterion, §8 Trigger driver). Scope confirmed with the user 2026-07-02: Trigger driver **in** (proven by the calibrated-tone reference), multi-level excitation **in** (proven by `CubicNonlinearity` closed forms).

**Deliberately deferred:** device-typed schema columns + hardware-reference CSV columns → SP-C/SP-D (M3 YAGNI precedent — the tone reference's `emission.csv` is the only new artifact); applying multi-level/perceptual batteries to the real Huggett/Moog → SP-B; the analytic-oscillator reference → SP-C; MidiCapture driver → SP-D (enum value exists, no driver); harness live-progress instrumentation + fullGrid economy review → the post-SP-A engagement (user-queued, out of M4).

---

## File Structure

- **Create** `tests/testdsp/AWeighting.h` — IEC 61672 A-curve + A-weighted RMS (dBFS(A)). One responsibility: the A-weighting lens.
- **Modify** `tests/testdsp/Metrics.h` — add `AliasSplit aliasSplit(mag, fundamentalBin)` (below/above-fundamental inharmonic split) next to `inharmonicDb`.
- **Create** `tests/testdsp/LevelResponse.h` — multi-level excitation sweep + `kneeInDbfs` / `headroomToClipInDbfs` extractors.
- **Create** `tests/testdsp/CaptureCal.h` — loopback chain calibration + response compensation + calibration-tone level offset.
- **Modify** `tests/characterization/ReferenceDevices.h` — add `EngineeredAliaser` (plain adapter, known inharmonic tone) and `CalibratedToneRef` (a `DeviceUnderTest` Generator).
- **Modify** `tests/characterization/CharacterizationRunner.h/.cpp` — Trigger dispatch in `run()`, private `runGeneratorCapture`, idle-noise-floor summary keys.
- **Modify** `tests/CharacterizationGateTests.cpp` — golden the two noise-floor keys per model.
- **Modify** `tests/golden/{moog,huggett}/baseline.csv` — regenerated (additive rows only).
- **Create** tests: `tests/AWeightingTests.cpp`, `tests/AliasSplitTests.cpp`, `tests/LevelResponseTests.cpp`, `tests/GeneratorPathTests.cpp`, `tests/CaptureCalTests.cpp`, `tests/RunnerNoiseFloorTests.cpp`.
- **Modify** `tests/CMakeLists.txt` — add the six test sources.
- **Create** `docs/filter-validation/acceptance-criterion.md` (+ index link in `docs/filter-validation/README.md`) — the written §5.3 criterion.

---

## Task 1: `testdsp::AWeighting` — the A-weighting lens

**Files:**
- Create: `tests/testdsp/AWeighting.h`
- Create: `tests/AWeightingTests.cpp`
- Modify: `tests/CMakeLists.txt` (add the test source after `LevelDisparityTests.cpp`)

**Interfaces:**
- Consumes: `testdsp::Spectrum::magnitude` (power-of-two real FFT, N/2 unnormalized bins), `testdsp::SignalGen::{binAlignedSine,whiteNoise}`, `testdsp::Level::rmsDbfs`.
- Produces: `testdsp::AWeighting::aWeightDb(double f) -> double` (curve gain in dB; `-300.0` for `f <= 0`) and `testdsp::AWeighting::aWeightedRmsDbfs(const std::vector<float>& buf, double sr) -> double` (A-weighted RMS in dBFS(A); `-300.0` for empty input). Task 4 and Task 6 call `aWeightedRmsDbfs`.

- [ ] **Step 1: Write the failing test**

Create `tests/AWeightingTests.cpp`:

```cpp
#include <juce_core/juce_core.h>
#include "testdsp/AWeighting.h"
#include "testdsp/SignalGen.h"
#include "testdsp/Level.h"
#include <cmath>
#include <vector>

// The A-weighting lens must match the published IEC 61672 curve (table anchors)
// and, applied to a single bin-aligned tone, must shift the flat RMS by exactly
// the curve value at that frequency (single-bin Parseval identity).

struct AWeightingTests : public juce::UnitTest {
    AWeightingTests() : juce::UnitTest("AWeighting") {}

    void runTest() override {
        using testdsp::AWeighting;

        beginTest("curve matches IEC 61672 table anchors");
        expectWithinAbsoluteError(AWeighting::aWeightDb(1000.0),     0.0, 0.1);
        expectWithinAbsoluteError(AWeighting::aWeightDb(100.0),    -19.1, 0.3);
        expectWithinAbsoluteError(AWeighting::aWeightDb(10000.0),   -2.5, 0.3);

        beginTest("single bin-aligned tone: weighted RMS = flat RMS + A(f)");
        const double sr = 48000.0;
        const int    N  = 1 << 14;
        // ~1 kHz (bin 341) and ~100 Hz (bin 34) — both bin-aligned, leak-free.
        for (int bin : { 341, 34 }) {
            const double f = (double) bin * sr / N;
            auto tone = testdsp::SignalGen::binAlignedSine(0.25f, bin, N);
            const double flat = testdsp::Level::rmsDbfs(tone);
            const double wtd  = AWeighting::aWeightedRmsDbfs(tone, sr);
            expectWithinAbsoluteError(wtd, flat + AWeighting::aWeightDb(f), 0.1);
        }

        beginTest("white noise: A-weighted RMS is below flat RMS");
        auto noise = testdsp::SignalGen::whiteNoise(0.1f, 1 << 14, 4242u);
        expect(AWeighting::aWeightedRmsDbfs(noise, 48000.0)
                 < testdsp::Level::rmsDbfs(noise),
               "A-curve must attenuate broadband noise overall");

        beginTest("empty input / f<=0 return the -300 sentinel");
        std::vector<float> empty;
        expectWithinAbsoluteError(AWeighting::aWeightedRmsDbfs(empty, 48000.0), -300.0, 1.0e-9);
        expectWithinAbsoluteError(AWeighting::aWeightDb(0.0),                   -300.0, 1.0e-9);
    }
};

static AWeightingTests aWeightingTestsInstance;
```

In `tests/CMakeLists.txt`, add `AWeightingTests.cpp` on its own line immediately after `LevelDisparityTests.cpp` in the `k2000_tests` source list.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --target k2000_tests -j4`
Expected: **build FAILS** — `fatal error: testdsp/AWeighting.h: No such file or directory`.

- [ ] **Step 3: Create the header**

Create `tests/testdsp/AWeighting.h`:

```cpp
#pragma once
#include "Spectrum.h"
#include <vector>
#include <cmath>

// testdsp::AWeighting -- the IEC 61672 A-weighting curve as a labeled perceptual
// lens. Reported BESIDE flat dBFS numbers, never instead of them (spec rule:
// perceptual lenses are additional; the physics stays primary).
//
// aWeightedRmsDbfs is frequency-domain (Parseval): intended for bin-aligned tones
// and broadband noise. No analysis window is applied, so a non-bin-aligned tone
// leaks; callers that need exact single-tone numbers must bin-align (as the
// Generator capture driver does).
namespace testdsp {

struct AWeighting {
    // A-curve gain at f in dB (0 dB at 1 kHz). -300 sentinel for f <= 0.
    static double aWeightDb(double f) {
        if (f <= 0.0) return -300.0;
        const double f2 = f * f;
        const double r = (12194.0 * 12194.0 * f2 * f2)
                       / ((f2 + 20.6 * 20.6)
                          * std::sqrt((f2 + 107.7 * 107.7) * (f2 + 737.9 * 737.9))
                          * (f2 + 12194.0 * 12194.0));
        return 20.0 * std::log10(std::max(r, 1.0e-30)) + 2.0;
    }

    // A-weighted RMS of a time buffer, dBFS(A). Truncates to the largest
    // power-of-two prefix (Spectrum::magnitude requirement). -300 for empty.
    // Parseval with JUCE's unnormalized real FFT: sum x^2 = (1/N) * sum |X|^2,
    // and magnitude() returns bins 0..N/2-1, so rms = sqrt(2 * sum_{b>=1} |Xb*wb|^2) / N.
    // Bin 0 (DC) is skipped: A(0) = -inf; idle DC offset is a FLAT-metric concern.
    static double aWeightedRmsDbfs(const std::vector<float>& buf, double sr) {
        if (buf.empty()) return -300.0;
        int n = 1;
        while (n * 2 <= (int) buf.size()) n *= 2;
        std::vector<float> x(buf.begin(), buf.begin() + n);
        auto mag = Spectrum::magnitude(x);
        double acc = 0.0;
        for (int b = 1; b < (int) mag.size(); ++b) {
            const double f = (double) b * sr / n;
            const double w = std::pow(10.0, aWeightDb(f) / 20.0);
            const double m = (double) mag[(size_t) b] * w;
            acc += 2.0 * m * m;
        }
        const double rms = std::sqrt(acc) / (double) n;
        return 20.0 * std::log10(std::max(rms, 1.0e-9));
    }
};

} // namespace testdsp
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>/dev/null | grep -E "AWeighting|Summary:"`
Expected: the `AWeighting` group passes; `Summary: ... 0 failed`.

- [ ] **Step 5: Commit**

```bash
git add tests/testdsp/AWeighting.h tests/AWeightingTests.cpp tests/CMakeLists.txt
git commit -m "feat(chz): testdsp::AWeighting — IEC 61672 lens, table + Parseval validated" -m "Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Task 2: `Metrics::aliasSplit` + the engineered-aliasing reference

**Files:**
- Modify: `tests/testdsp/Metrics.h` (add `AliasSplit` + `aliasSplit` after `inharmonicDb`, i.e. after line 20)
- Modify: `tests/characterization/ReferenceDevices.h` (add `EngineeredAliaser` after `CubicNonlinearity`)
- Create: `tests/AliasSplitTests.cpp`
- Modify: `tests/CMakeLists.txt` (add the test source after `AWeightingTests.cpp`)

**Interfaces:**
- Consumes: `testdsp::Spectrum::magnitude`, `testdsp::SignalGen::binAlignedSine`.
- Produces: `testdsp::Metrics::AliasSplit { double belowDb, aboveDb; }` and `static AliasSplit aliasSplit(const std::vector<float>& mag, int fundamentalBin)` — inharmonic energy below vs above the fundamental, each in dB relative to the fundamental (below = exposed/dissonant, the perceptually weighted half). `chz::EngineeredAliaser` — plain `reset()/process()` adapter that ADDS a known inharmonic tone (`aliasHz`, `aliasAmp`, `sr` public fields) to the signal.

- [ ] **Step 1: Write the failing test**

Create `tests/AliasSplitTests.cpp`:

```cpp
#include <juce_core/juce_core.h>
#include "testdsp/Metrics.h"
#include "testdsp/Spectrum.h"
#include "testdsp/SignalGen.h"
#include "characterization/ReferenceDevices.h"
#include <cmath>
#include <vector>

// Audibility-split aliasing: inharmonic energy below the fundamental is the
// exposed/dissonant half. Proven two ways: (1) a hand-built spectrum with exact
// energies; (2) the EngineeredAliaser reference device, whose injected tone's
// frequency and level are known by construction.

struct AliasSplitTests : public juce::UnitTest {
    AliasSplitTests() : juce::UnitTest("AliasSplit") {}

    void runTest() override {
        using testdsp::Metrics;

        beginTest("hand-built spectrum: exact below/above split");
        std::vector<float> mag(64, 0.0f);
        mag[10] = 1.0f;     // fundamental (bin 10)
        mag[20] = 0.5f;     // harmonic (2x) — must be EXCLUDED from both halves
        mag[5]  = 0.1f;     // inharmonic below
        mag[25] = 0.05f;    // inharmonic above (not a multiple of 10)
        const auto sp = Metrics::aliasSplit(mag, 10);
        expectWithinAbsoluteError(sp.belowDb, 10.0 * std::log10(0.01),   1.0e-6);
        expectWithinAbsoluteError(sp.aboveDb, 10.0 * std::log10(0.0025), 1.0e-6);

        beginTest("EngineeredAliaser below the fundamental: split recovers 20log10(a/A)");
        const double sr = 48000.0;
        const int    N  = 1 << 14;
        chz::EngineeredAliaser dev;
        dev.sr       = sr;
        dev.aliasHz  = 64.0 * sr / N;      // bin 64 — below the bin-256 fundamental
        dev.aliasAmp = 0.01f;
        dev.reset();
        auto sig = testdsp::SignalGen::binAlignedSine(0.5f, 256, N);
        dev.process(sig.data(), N);
        auto m = testdsp::Spectrum::magnitude(sig);
        const auto s1 = Metrics::aliasSplit(m, 256);
        expectWithinAbsoluteError(s1.belowDb, 20.0 * std::log10(0.01 / 0.5), 0.1);
        expect(s1.aboveDb < -60.0, "no engineered energy above the fundamental");

        beginTest("EngineeredAliaser above the fundamental: split recovers it above");
        dev.aliasHz = 700.0 * sr / N;      // bin 700 — above, not a multiple of 256
        dev.reset();
        auto sig2 = testdsp::SignalGen::binAlignedSine(0.5f, 256, N);
        dev.process(sig2.data(), N);
        auto m2 = testdsp::Spectrum::magnitude(sig2);
        const auto s2 = Metrics::aliasSplit(m2, 256);
        expectWithinAbsoluteError(s2.aboveDb, 20.0 * std::log10(0.01 / 0.5), 0.1);
        expect(s2.belowDb < -60.0, "no engineered energy below the fundamental");
    }
};

static AliasSplitTests aliasSplitTestsInstance;
```

Add `AliasSplitTests.cpp` to `tests/CMakeLists.txt` immediately after `AWeightingTests.cpp`.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --target k2000_tests -j4`
Expected: **build FAILS** — `Metrics` has no member `aliasSplit`; `chz` has no `EngineeredAliaser`.

- [ ] **Step 3: Implement the split + the reference device**

In `tests/testdsp/Metrics.h`, insert after the `inharmonicDb` method (after its closing brace, line 20):

```cpp
    // M4 perceptual lens: inharmonic energy SPLIT at the fundamental, each half in
    // dB relative to the fundamental. Below-fundamental aliasing is exposed and
    // dissonant (no harmonic masking under it) — the perceptually weighted half.
    // Harmonic bins (multiples of fundamentalBin) are excluded, like inharmonicDb.
    struct AliasSplit { double belowDb; double aboveDb; };
    static AliasSplit aliasSplit(const std::vector<float>& mag, int fundamentalBin) {
        double fund = 0.0, below = 0.0, above = 0.0;
        for (int b = 2; b < (int) mag.size(); ++b) {
            const double e = double(mag[(size_t) b]) * mag[(size_t) b];
            if (b % fundamentalBin == 0) { if (b == fundamentalBin) fund = e; continue; }
            if (b < fundamentalBin) below += e; else above += e;
        }
        if (fund <= 0.0) return { 0.0, 0.0 };
        return { 10.0 * std::log10(std::max(below, 1.0e-30) / fund),
                 10.0 * std::log10(std::max(above, 1.0e-30) / fund) };
    }
```

In `tests/characterization/ReferenceDevices.h`, insert before the closing `} // namespace chz`:

```cpp
// Engineered aliasing case: ADDS a fixed inharmonic tone (aliasHz, aliasAmp) to
// whatever passes through — a synthetic stand-in for aliasing foldback whose
// frequency and level are known by construction. Plain reset()/process() adapter.
class EngineeredAliaser {
public:
    double sr       = 48000.0;
    double aliasHz  = 300.0;
    float  aliasAmp = 0.01f;

    void reset() { phase_ = 0.0; }
    void process(float* mono, int n) {
        const double inc = 2.0 * juce::MathConstants<double>::pi * aliasHz / sr;
        for (int i = 0; i < n; ++i) {
            mono[i] += aliasAmp * (float) std::sin(phase_);
            phase_ += inc;
            if (phase_ > 2.0 * juce::MathConstants<double>::pi)
                phase_ -= 2.0 * juce::MathConstants<double>::pi;
        }
    }

private:
    double phase_ = 0.0;
};
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>/dev/null | grep -E "AliasSplit|Summary:"`
Expected: `AliasSplit` group passes; `Summary: ... 0 failed`.

- [ ] **Step 5: Commit**

```bash
git add tests/testdsp/Metrics.h tests/characterization/ReferenceDevices.h tests/AliasSplitTests.cpp tests/CMakeLists.txt
git commit -m "feat(chz): audibility-split aliasing (Metrics::aliasSplit) + EngineeredAliaser reference" -m "Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Task 3: `testdsp::LevelResponse` — multi-level excitation + knee/headroom extractors

**Files:**
- Create: `tests/testdsp/LevelResponse.h`
- Create: `tests/LevelResponseTests.cpp`
- Modify: `tests/CMakeLists.txt` (add the test source after `AliasSplitTests.cpp`)

**Interfaces:**
- Consumes: `testdsp::SignalGen::{sine,binAlignedSine}`, `testdsp::Spectrum::magnitude`, `testdsp::Metrics::thdPlusNDb`, `testdsp::Level::{peakDbfs,rmsDbfs}`.
- Produces: `testdsp::LevelResponse::Point { double inDbfs, outPeakDbfs, outRmsDbfs, gainDb, thdDb; }`; `template <typename Adapter> static std::vector<Point> measure(Adapter&, double f0, double sr, const std::vector<double>& ampsDbfs)`; `static double kneeInDbfs(const std::vector<Point>&, double dropDb)`; `static double headroomToClipInDbfs(const std::vector<Point>&, double ceilingDbfs)`. SP-B consumes these on real filters.

- [ ] **Step 1: Write the failing test**

Create `tests/LevelResponseTests.cpp`:

```cpp
#include <juce_core/juce_core.h>
#include "testdsp/LevelResponse.h"
#include "characterization/ReferenceDevices.h"
#include <cmath>
#include <vector>

// Multi-level excitation proven against CubicNonlinearity closed forms.
// c3 = -0.3 (compressive): fundamental gain(A) = 20log10(1 - 0.225 A^2).
//   - small signal (-40 dBFS): gain ~ 0 dB
//   - A = 0.5 (-6.02 dBFS): gain = 20log10(0.94375) = -0.503 dB
//   - 1 dB knee: 1 - 0.225 A^2 = 10^(-1/20) -> A = 0.6952 -> -3.16 dBFS input
// c3 = +0.5 (expansive): output peak A + 0.5 A^3 = 1.0 at A = 0.7715 -> clip
//   headroom = 20log10(0.7715) = -2.25 dBFS input.

struct LevelResponseTests : public juce::UnitTest {
    LevelResponseTests() : juce::UnitTest("LevelResponse") {}

    void runTest() override {
        using testdsp::LevelResponse;

        std::vector<double> amps;
        for (int d = -40; d <= -1; ++d) amps.push_back((double) d);

        beginTest("gain-vs-level matches the cubic's closed form");
        chz::CubicNonlinearity comp;
        comp.c3 = -0.3f;
        auto pts = LevelResponse::measure(comp, 1000.0, 48000.0, amps);
        expectEquals((int) pts.size(), (int) amps.size());
        expect(std::abs(pts.front().gainDb) < 0.05, "small-signal gain ~ 0 dB");
        // Point at -6 dBFS (index 34: -40 + 34 = -6).
        expectWithinAbsoluteError(pts[(size_t) 34].gainDb, -0.503, 0.2);

        beginTest("THD-vs-level matches trueThdDb at the driven point");
        expectWithinAbsoluteError(pts[(size_t) 34].thdDb,
                                  comp.trueThdDb((float) std::pow(10.0, -6.0 / 20.0)), 0.5);

        beginTest("1 dB compression knee at the closed-form input level");
        expectWithinAbsoluteError(LevelResponse::kneeInDbfs(pts, 1.0), -3.16, 0.5);

        beginTest("knee sentinel when the device never compresses");
        chz::CubicNonlinearity linear;   // c3 = 0 -> pure unity
        auto lpts = LevelResponse::measure(linear, 1000.0, 48000.0, amps);
        expectWithinAbsoluteError(LevelResponse::kneeInDbfs(lpts, 1.0), -300.0, 1.0e-9);

        beginTest("headroom-to-clip at the closed-form input level");
        chz::CubicNonlinearity exp5;
        exp5.c3 = 0.5f;
        std::vector<double> hamps;
        for (int d = -20; d <= 0; ++d) hamps.push_back((double) d);
        auto hpts = LevelResponse::measure(exp5, 1000.0, 48000.0, hamps);
        expectWithinAbsoluteError(LevelResponse::headroomToClipInDbfs(hpts, 0.0), -2.25, 0.5);

        beginTest("headroom sentinel when the ceiling is never reached");
        expectWithinAbsoluteError(LevelResponse::headroomToClipInDbfs(lpts, 0.0), -300.0, 1.0e-9);
    }
};

static LevelResponseTests levelResponseTestsInstance;
```

Add `LevelResponseTests.cpp` to `tests/CMakeLists.txt` immediately after `AliasSplitTests.cpp`.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --target k2000_tests -j4`
Expected: **build FAILS** — `testdsp/LevelResponse.h: No such file or directory`.

- [ ] **Step 3: Create the header**

Create `tests/testdsp/LevelResponse.h`:

```cpp
#pragma once
#include "SignalGen.h"
#include "Spectrum.h"
#include "Metrics.h"
#include "Level.h"
#include <vector>
#include <cmath>

// testdsp::LevelResponse -- multi-level excitation (spec §4.1 "large-signal").
// Sweeps a bin-aligned tone across input levels and reads, per level: output
// peak/RMS (dBFS), fundamental gain (dB, FFT-bin amplitude out/in), THD+N (dB).
// Extractors reduce the curve to the two headline numbers: the compression knee
// (input dBFS where fundamental gain has dropped dropDb below small-signal gain)
// and headroom-to-clip (input dBFS where output peak reaches ceilingDbfs).
// -300 sentinel = the sweep never reached the condition.
namespace testdsp {

struct LevelResponse {
    struct Point {
        double inDbfs      = -300.0;
        double outPeakDbfs = -300.0;
        double outRmsDbfs  = -300.0;
        double gainDb      = -300.0;
        double thdDb       = -300.0;
    };

    template <typename Adapter>
    static std::vector<Point> measure(Adapter& a, double f0, double sr,
                                      const std::vector<double>& ampsDbfs) {
        const int N   = 1 << 14;
        const int bin = std::max(2, (int) std::lround(f0 * N / sr));
        std::vector<Point> pts;
        pts.reserve(ampsDbfs.size());
        for (double aDb : ampsDbfs) {
            const float amp = (float) std::pow(10.0, aDb / 20.0);
            a.reset();
            // Warm-up so stateful devices reach steady state (harmless for memoryless).
            auto warm = SignalGen::sine(amp, (double) bin * sr / N, sr, 8192);
            a.process(warm.data(), (int) warm.size());
            auto cap = SignalGen::binAlignedSine(amp, bin, N);
            a.process(cap.data(), N);
            auto mag = Spectrum::magnitude(cap);

            Point p;
            p.inDbfs      = aDb;
            p.outPeakDbfs = Level::peakDbfs(cap);
            p.outRmsDbfs  = Level::rmsDbfs(cap);
            // Unnormalized real FFT: a bin-aligned sine of amplitude A reads |X| = A*N/2.
            const double outAmp = 2.0 * (double) mag[(size_t) bin] / (double) N;
            p.gainDb = 20.0 * std::log10(std::max(outAmp / (double) amp, 1.0e-15));
            p.thdDb  = Metrics::thdPlusNDb(mag, bin);
            pts.push_back(p);
        }
        return pts;
    }

    static double kneeInDbfs(const std::vector<Point>& pts, double dropDb) {
        if (pts.size() < 2) return -300.0;
        const double g0 = pts.front().gainDb;
        for (size_t i = 1; i < pts.size(); ++i) {
            const double d0 = g0 - pts[i - 1].gainDb;
            const double d1 = g0 - pts[i].gainDb;
            if (d1 >= dropDb) {
                if (d1 <= d0) return pts[i].inDbfs;   // non-monotonic guard
                const double t = (dropDb - d0) / (d1 - d0);
                return pts[i - 1].inDbfs + t * (pts[i].inDbfs - pts[i - 1].inDbfs);
            }
        }
        return -300.0;
    }

    static double headroomToClipInDbfs(const std::vector<Point>& pts, double ceilingDbfs) {
        for (size_t i = 0; i < pts.size(); ++i) {
            if (pts[i].outPeakDbfs >= ceilingDbfs) {
                if (i == 0) return pts[0].inDbfs;
                const double p0 = pts[i - 1].outPeakDbfs, p1 = pts[i].outPeakDbfs;
                if (p1 <= p0) return pts[i].inDbfs;   // non-monotonic guard
                const double t = (ceilingDbfs - p0) / (p1 - p0);
                return pts[i - 1].inDbfs + t * (pts[i].inDbfs - pts[i - 1].inDbfs);
            }
        }
        return -300.0;
    }
};

} // namespace testdsp
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>/dev/null | grep -E "LevelResponse|Summary:"`
Expected: `LevelResponse` group passes; `Summary: ... 0 failed`.

- [ ] **Step 5: Commit**

```bash
git add tests/testdsp/LevelResponse.h tests/LevelResponseTests.cpp tests/CMakeLists.txt
git commit -m "feat(chz): multi-level excitation — gain/THD-vs-level, knee, headroom (closed-form validated)" -m "Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Task 4: `CalibratedToneRef` (Generator) + Trigger capture driver

**Files:**
- Modify: `tests/characterization/ReferenceDevices.h` (add `CalibratedToneRef`)
- Modify: `tests/characterization/CharacterizationRunner.h` (declare `runGeneratorCapture`)
- Modify: `tests/characterization/CharacterizationRunner.cpp` (dispatch + implementation; include `AWeighting.h`)
- Create: `tests/GeneratorPathTests.cpp`
- Modify: `tests/CMakeLists.txt` (add the test source after `LevelResponseTests.cpp`)

**Interfaces:**
- Consumes: `chz::DeviceUnderTest` (M1), `testdsp::Level`, `testdsp::AWeighting` (Task 1), `testdsp::SignalGen::silence`.
- Produces: `chz::CalibratedToneRef : DeviceUnderTest` (kind `Generator`, excitation `Trigger`; `setToneDbfs(double)`; tone frequency comes from `OperatingPoint::cutoffHz`, sample rate from `hostSampleRate` — the M4 minimal Generator convention, generalized in SP-C). Runner: `run()` routes `Excitation::Trigger` devices to `runGeneratorCapture`, which emits summary keys `"<name>/gen/peak_dbfs"`, `"<name>/gen/rms_dbfs"`, `"<name>/gen/rms_dbfsA"` and writes `emission.csv`.

- [ ] **Step 1: Write the failing test**

Create `tests/GeneratorPathTests.cpp`:

```cpp
#include <juce_core/juce_core.h>
#include "characterization/ReferenceDevices.h"
#include "characterization/CharacterizationRunner.h"
#include "testdsp/AWeighting.h"
#include <cmath>

// The Generator/Trigger path end-to-end: a calibrated tone of KNOWN absolute
// level, measured through the abstract DeviceUnderTest base by the runner's
// Trigger driver. Peak reads the set dBFS; RMS sits 3.01 dB under it; the
// A-weighted figure at ~1 kHz matches flat (A(1 kHz) = 0 dB).

struct GeneratorPathTests : public juce::UnitTest {
    GeneratorPathTests() : juce::UnitTest("GeneratorPath") {}

    void runTest() override {
        using namespace chz;

        beginTest("CalibratedToneRef declares Generator / Trigger");
        CalibratedToneRef tone;
        DeviceUnderTest& dut = tone;
        expect(dut.kind() == DeviceKind::Generator);
        expect(dut.excitation() == Excitation::Trigger);
        expectEquals(dut.name(), juce::String("ref_tone"));

        beginTest("runner Trigger driver recovers the calibrated level");
        tone.setToneDbfs(-18.0);
        Grid g;
        g.cutoffs   = { 1000.0 };       // tone frequency (Generator convention)
        g.hostRates = { 48000.0 };
        auto outDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("chz_gen_path_test");
        outDir.deleteRecursively();
        outDir.createDirectory();
        auto s = CharacterizationRunner::run(dut, g, outDir);

        expect(s.count("ref_tone/gen/peak_dbfs") == 1, "peak key present");
        expect(s.count("ref_tone/gen/rms_dbfs")  == 1, "rms key present");
        expect(s.count("ref_tone/gen/rms_dbfsA") == 1, "rms(A) key present");
        expectWithinAbsoluteError(s.at("ref_tone/gen/peak_dbfs"), -18.0,   0.05);
        expectWithinAbsoluteError(s.at("ref_tone/gen/rms_dbfs"),  -21.01,  0.05);
        // ~1 kHz: A(f) ~ 0, so the weighted figure matches flat within the lens tol.
        expectWithinAbsoluteError(s.at("ref_tone/gen/rms_dbfsA"),
                                  s.at("ref_tone/gen/rms_dbfs"), 0.3);

        expect(outDir.getChildFile("emission.csv").existsAsFile(), "emission.csv written");
        outDir.deleteRecursively();
    }
};

static GeneratorPathTests generatorPathTestsInstance;
```

Add `GeneratorPathTests.cpp` to `tests/CMakeLists.txt` immediately after `LevelResponseTests.cpp`.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --target k2000_tests -j4`
Expected: **build FAILS** — `chz` has no `CalibratedToneRef`.

- [ ] **Step 3: Implement the device and the driver**

In `tests/characterization/ReferenceDevices.h`, insert before the closing `} // namespace chz` (after `EngineeredAliaser`):

```cpp
// Calibrated-tone Generator: emits a sine of exactly known amplitude — the
// absolute-level trust anchor (spec §5.1 "calibrated tone") and the first
// Generator/Trigger device through the DeviceUnderTest contract. M4 Generator
// convention: OperatingPoint::cutoffHz is the tone frequency, hostSampleRate the
// rate (generalized OperatingPoint lands in SP-C).
class CalibratedToneRef : public DeviceUnderTest {
public:
    void setToneDbfs(double dbfs) { amp_ = (float) std::pow(10.0, dbfs / 20.0); }

    void reset() override { phase_ = 0.0; }

    // Generator: input contents ignored, buffer OVERWRITTEN with the emission.
    void process(float* mono, int n) override {
        const double inc = 2.0 * juce::MathConstants<double>::pi * freqHz_ / sr_;
        for (int i = 0; i < n; ++i) {
            mono[i] = amp_ * (float) std::sin(phase_);
            phase_ += inc;
            if (phase_ > 2.0 * juce::MathConstants<double>::pi)
                phase_ -= 2.0 * juce::MathConstants<double>::pi;
        }
    }

    juce::String name()       const override { return "ref_tone"; }
    DeviceKind   kind()       const override { return DeviceKind::Generator; }
    Excitation   excitation() const override { return Excitation::Trigger; }
    bool         supports(Mode) override { return true; }

    void setOperatingPoint(const OperatingPoint& op) override {
        freqHz_ = op.cutoffHz;
        sr_     = op.hostSampleRate;
        reset();
    }

private:
    double sr_ = 48000.0, freqHz_ = 1000.0, phase_ = 0.0;
    float  amp_ = 0.12589254f;   // -18 dBFS default (the documented calibration tone)
};
```

In `tests/characterization/CharacterizationRunner.h`, add after the `runB3OnePoint` declaration (before `interpMag`):

```cpp
    // M4 Trigger driver: capture a Generator device's emission at the base host
    // rate (tone frequency = first grid cutoff, snapped to an FFT bin for
    // leak-free level reading) and record absolute + A-weighted level.
    static Summary runGeneratorCapture(DeviceUnderTest& dut, const Grid& g,
                                       const juce::File& outDir);
```

In `tests/characterization/CharacterizationRunner.cpp`:

Add the include next to the other testdsp includes:

```cpp
#include "../testdsp/AWeighting.h"
```

At the very top of `run()`'s body (immediately after `outDir.createDirectory();`, line ~467), add the dispatch:

```cpp
    // M4: Generator devices are excited by the Trigger driver, not the input
    // sweep — B1/B2/B3 are transfer-function batteries and do not apply.
    if (fut.excitation() == Excitation::Trigger)
        return runGeneratorCapture(fut, g, outDir);
```

Add the implementation immediately before `run()`'s definition:

```cpp
Summary CharacterizationRunner::runGeneratorCapture(DeviceUnderTest& dut, const Grid& g,
                                                     const juce::File& outDir) {
    Summary summary;
    const double baseHost = [&]() {
        auto it = std::find_if(g.hostRates.begin(), g.hostRates.end(),
                               [](double r) { return std::abs(r - 96000.0) < 0.5; });
        return (it != g.hostRates.end()) ? 96000.0
                                         : (g.hostRates.empty() ? 96000.0 : g.hostRates[0]);
    }();
    const double toneHz = g.cutoffs.empty() ? 1000.0 : g.cutoffs[0];

    constexpr int N = 1 << 14;
    // Snap to an FFT bin so the A-weighted (frequency-domain) reading is leak-free.
    const double snappedHz = std::round(toneHz * N / baseHost) * baseHost / N;

    OperatingPoint op;
    op.cutoffHz       = snappedHz;
    op.hostSampleRate = baseHost;
    dut.setOperatingPoint(op);
    dut.reset();

    auto buf = testdsp::SignalGen::silence(N);
    dut.process(buf.data(), N);   // Generator: overwrites with its emission

    const double pk   = testdsp::Level::peakDbfs(buf);
    const double rms  = testdsp::Level::rmsDbfs(buf);
    const double rmsA = testdsp::AWeighting::aWeightedRmsDbfs(buf, baseHost);

    const juce::String keyBase = dut.name() + "/gen";
    summary[keyBase + "/peak_dbfs"] = pk;
    summary[keyBase + "/rms_dbfs"]  = rms;
    summary[keyBase + "/rms_dbfsA"] = rmsA;

    juce::String csv;
    csv += "model,hostSR,toneHz,peak_dbfs,rms_dbfs,rms_dbfsA\n";
    csv += dut.name() + "," + juce::String(baseHost, 1) + "," + juce::String(snappedHz, 3)
         + "," + juce::String(pk, 4) + "," + juce::String(rms, 4)
         + "," + juce::String(rmsA, 4) + "\n";
    outDir.getChildFile("emission.csv").replaceWithText(csv);
    return summary;
}
```

(`Level.h` is already included by the M3 work; if not, add `#include "../testdsp/Level.h"` beside the new `AWeighting.h` include. `SignalGen.h` likewise.)

- [ ] **Step 4: Run to verify it passes (full suite — non-regression gate)**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>/dev/null | grep -E "GeneratorPath|FAIL|Summary:"`
Expected: `GeneratorPath` passes; **every pre-existing group still passes** (`Summary: ... 0 failed`) — the InputSweep path is untouched (the dispatch is a guarded early return).

- [ ] **Step 5: Verify the heavy runner still builds**

Run: `cmake --build build --target k2000_device_characterization -j4`
Expected: builds cleanly.

- [ ] **Step 6: Commit**

```bash
git add tests/characterization/ReferenceDevices.h tests/characterization/CharacterizationRunner.h tests/characterization/CharacterizationRunner.cpp tests/GeneratorPathTests.cpp tests/CMakeLists.txt
git commit -m "feat(chz): Trigger driver + CalibratedToneRef — Generator path measured end-to-end" -m "Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Task 5: `testdsp::CaptureCal` — loopback calibration + compensation math

**Files:**
- Create: `tests/testdsp/CaptureCal.h`
- Create: `tests/CaptureCalTests.cpp`
- Modify: `tests/CMakeLists.txt` (add the test source after `GeneratorPathTests.cpp`)

**Interfaces:**
- Consumes: `testdsp::SteppedSine::transfer(adapter, probes, sr, amp)` (returns `.magDb` per probe), `testdsp::Level::rmsDbfs`, `chz::AnalyticBiquad::trueMagDb`.
- Produces: `testdsp::CaptureCal::Calibration { std::vector<double> freqs, chainMagDb; }`; `template <typename Chain> static Calibration calibrateChain(Chain&, const std::vector<double>& probes, double sr, float amp)`; `static std::vector<double> compensate(const Calibration&, const std::vector<double>& measuredMagDb)` (same probe grid, pointwise subtract); `static double levelOffsetFromTone(const std::vector<float>& capturedTone, double nominalPeakDbfs)`. SP-D consumes these on the real capture chain.

- [ ] **Step 1: Write the failing test**

Create `tests/CaptureCalTests.cpp`:

```cpp
#include <juce_core/juce_core.h>
#include "testdsp/CaptureCal.h"
#include "testdsp/SteppedSine.h"
#include "testdsp/SignalGen.h"
#include "characterization/ReferenceDevices.h"
#include "characterization/CharacterizationRunner.h"   // logFreqs
#include <cmath>
#include <vector>

// Loopback-calibration math proven on a synthetic capture chain (spec §5.2):
// chain = gentle biquad tilt + fixed -6.02 dB pad. A device measured THROUGH
// that chain, then compensated with the chain's loopback calibration, must
// recover the device's exact analytic response. The -18 dBFS calibration tone
// through the pad must recover the pad as the level offset.

namespace {

// The synthetic capture chain: known coloration (biquad) + known pad (x0.5).
struct SyntheticChain {
    chz::AnalyticBiquad bq;
    float pad = 0.5f;
    void reset() { bq.reset(); }
    void process(float* m, int n) {
        bq.process(m, n);
        for (int i = 0; i < n; ++i) m[i] *= pad;
    }
};

// A device observed through the chain (device first, chain second — as a mic
// or interface would color a hardware filter's output).
struct DeviceThroughChain {
    chz::AnalyticBiquad dev;
    SyntheticChain*     chain = nullptr;
    void reset() { dev.reset(); chain->reset(); }
    void process(float* m, int n) { dev.process(m, n); chain->process(m, n); }
};

} // namespace

struct CaptureCalTests : public juce::UnitTest {
    CaptureCalTests() : juce::UnitTest("CaptureCal") {}

    void runTest() override {
        using testdsp::CaptureCal;
        const double sr = 48000.0;
        auto probes = chz::CharacterizationRunner::logFreqs(50.0, 20000.0, 100);

        // Chain coloration: mild LP tilt (fc 8 kHz, Q ~ 0.7) + -6.02 dB pad.
        SyntheticChain chain;
        chz::OperatingPoint chainOp;
        chainOp.cutoffHz       = 8000.0;
        chainOp.resonance      = 0.021;    // Q ~ 0.7 (0.5 + 0.021*9.5)
        chainOp.hostSampleRate = sr;
        chain.bq.setOperatingPoint(chainOp);

        beginTest("loopback calibration captures the chain response");
        auto cal = CaptureCal::calibrateChain(chain, probes, sr, 0.25f);
        expectEquals((int) cal.freqs.size(), (int) probes.size());
        // Spot-check: at 100 Hz the chain is just the pad (biquad ~ flat there).
        expectWithinAbsoluteError(cal.chainMagDb.front(), -6.02, 0.15);

        beginTest("compensation recovers the device's analytic response through the chain");
        DeviceThroughChain thru;
        thru.chain = &chain;
        chz::OperatingPoint devOp;
        devOp.cutoffHz       = 1000.0;
        devOp.resonance      = 0.4737;     // Q ~ 5
        devOp.hostSampleRate = sr;
        thru.dev.setOperatingPoint(devOp);

        auto measured = testdsp::SteppedSine::transfer(thru, probes, sr, 0.25f);
        auto corrected = CaptureCal::compensate(cal, measured.magDb);
        double worst = 0.0;
        for (size_t i = 0; i < probes.size(); ++i)
            worst = std::max(worst, std::abs(corrected[i] - thru.dev.trueMagDb(probes[i])));
        expect(worst < 0.2, "compensated response within 0.2 dB of analytic truth");

        beginTest("calibration tone recovers the chain's level offset");
        auto tone = testdsp::SignalGen::sine(0.12589254f, 1000.0, sr, 1 << 14); // -18 dBFS
        for (auto& v : tone) v *= 0.5f;                                          // the pad
        expectWithinAbsoluteError(CaptureCal::levelOffsetFromTone(tone, -18.0), -6.02, 0.05);
    }
};

static CaptureCalTests captureCalTestsInstance;
```

Add `CaptureCalTests.cpp` to `tests/CMakeLists.txt` immediately after `GeneratorPathTests.cpp`.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --target k2000_tests -j4`
Expected: **build FAILS** — `testdsp/CaptureCal.h: No such file or directory`.

- [ ] **Step 3: Create the header**

Create `tests/testdsp/CaptureCal.h`:

```cpp
#pragma once
#include "SteppedSine.h"
#include "Level.h"
#include <vector>
#include <cmath>

// testdsp::CaptureCal -- capture-chain calibration math (spec §5.2, the
// "calibration weight"). Loopback: measure the chain with NO device in it;
// compensate: subtract the chain's coloration from a device-through-chain
// measurement; levelOffsetFromTone: absolute-level anchor from the documented
// calibration tone (nominal -18 dBFS). Validated synthetically now; SP-D points
// it at the real interface. compensate() requires the SAME probe grid used for
// calibration (no resampling — YAGNI until SP-D shows a need).
namespace testdsp {

struct CaptureCal {
    struct Calibration {
        std::vector<double> freqs;        // probe grid the chain was measured on
        std::vector<double> chainMagDb;   // chain-only (loopback) response, dB
    };

    template <typename Chain>
    static Calibration calibrateChain(Chain& chain, const std::vector<double>& probes,
                                      double sr, float amp) {
        auto r = SteppedSine::transfer(chain, probes, sr, amp);
        Calibration c;
        c.freqs      = probes;
        c.chainMagDb = r.magDb;
        return c;
    }

    // measuredMagDb must be sampled on cal.freqs (same grid, same order).
    static std::vector<double> compensate(const Calibration& cal,
                                          const std::vector<double>& measuredMagDb) {
        std::vector<double> out(measuredMagDb.size(), -300.0);
        const size_t n = std::min(cal.chainMagDb.size(), measuredMagDb.size());
        for (size_t i = 0; i < n; ++i)
            out[i] = measuredMagDb[i] - cal.chainMagDb[i];
        return out;
    }

    // Captured calibration tone -> chain level offset in dB (0 = transparent).
    // nominalPeakDbfs is the tone's PEAK level as emitted (sine RMS = peak - 3.01).
    static double levelOffsetFromTone(const std::vector<float>& capturedTone,
                                      double nominalPeakDbfs) {
        return Level::rmsDbfs(capturedTone) - (nominalPeakDbfs - 3.0103);
    }
};

} // namespace testdsp
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>/dev/null | grep -E "CaptureCal|Summary:"`
Expected: `CaptureCal` group passes; `Summary: ... 0 failed`.

- [ ] **Step 5: Commit**

```bash
git add tests/testdsp/CaptureCal.h tests/CaptureCalTests.cpp tests/CMakeLists.txt
git commit -m "feat(chz): CaptureCal — loopback calibration + compensation, synthetically proven" -m "Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Task 6: Idle-noise-floor summary keys + gate goldens

**Files:**
- Modify: `tests/characterization/CharacterizationRunner.cpp` (noise capture per (mode, cutoff) at the base point, inside `run()`)
- Create: `tests/RunnerNoiseFloorTests.cpp`
- Modify: `tests/CharacterizationGateTests.cpp` (golden the two new keys)
- Modify: `tests/golden/moog/baseline.csv`, `tests/golden/huggett/baseline.csv` (regenerated, additive only)
- Modify: `tests/CMakeLists.txt` (add the test source after `CaptureCalTests.cpp`)

**Interfaces:**
- Consumes: `testdsp::Level::rmsDbfs`, `testdsp::AWeighting::aWeightedRmsDbfs` (Task 1), `testdsp::SignalGen::silence`, `testdsp::GoldenSet`.
- Produces: summary keys `"<model>/<mode>/fc<cutoff>/noise_floor_dbfs"` and `".../noise_floor_dbfsA"` (idle output level, silence in, base operating point) + their golden rows.

- [ ] **Step 1: Write the failing test**

Create `tests/RunnerNoiseFloorTests.cpp`:

```cpp
#include <juce_core/juce_core.h>
#include "characterization/CharacterizationRunner.h"
#include "characterization/FilterUnderTest.h"
#include <cmath>

// §4.1 "idle noise floor": silence in at the base operating point, absolute
// output level out — flat dBFS beside the A-weighted lens. A clean digital
// filter idles far below -60 dBFS; a change that makes a filter hum at idle
// must trip this.

struct RunnerNoiseFloorTests : public juce::UnitTest {
    RunnerNoiseFloorTests() : juce::UnitTest("RunnerNoiseFloor") {}

    void runTest() override {
        using namespace chz;
        beginTest("summary records noise_floor_dbfs + noise_floor_dbfsA (Moog LP24)");
        auto moog = makeMoogFut();
        Grid g;
        g.modes       = { Mode::LP24 };
        g.cutoffs     = { 1000.0 };
        g.resonances  = { 0.0 };
        g.drives      = { 0.0 };
        g.osFactors   = { 1 };
        g.osModes     = { OsMode::Live };
        g.hostRates   = { 96000.0 };
        g.probeFreqs  = CharacterizationRunner::logFreqs(50.0, 20000.0, 20);

        auto outDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("chz_noise_floor_test");
        outDir.deleteRecursively();
        outDir.createDirectory();
        auto s = CharacterizationRunner::run(*moog, g, outDir);

        expect(s.count("moog/LP24/fc1000/noise_floor_dbfs")  == 1, "flat key present");
        expect(s.count("moog/LP24/fc1000/noise_floor_dbfsA") == 1, "A-weighted key present");
        const double flat = s.at("moog/LP24/fc1000/noise_floor_dbfs");
        const double wtd  = s.at("moog/LP24/fc1000/noise_floor_dbfsA");
        expect(flat > -300.5 && wtd > -300.5, "values are data, not missing");
        expect(flat < -60.0, "a clean digital filter idles below -60 dBFS");
        outDir.deleteRecursively();
    }
};

static RunnerNoiseFloorTests runnerNoiseFloorTestsInstance;
```

Add `RunnerNoiseFloorTests.cpp` to `tests/CMakeLists.txt` immediately after `CaptureCalTests.cpp`.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>/dev/null | grep -E "RunnerNoiseFloor|FAIL|Summary:"`
Expected: builds, but `RunnerNoiseFloor` **FAILS** on "flat key present" (the runner does not emit noise keys yet); `Summary: ... N failed` with N > 0.

- [ ] **Step 3: Emit the keys from the runner**

In `tests/characterization/CharacterizationRunner.cpp`, inside `run()`'s `for (double cutoff : g.cutoffs)` loop, immediately after the B2 block (the `{ ... }` that computes `selfosc_cents_err`, ends near line 546), add:

```cpp
            // --- M4: idle noise floor at the base point (silence in -> output level) ---
            // Flat dBFS is primary; the A-weighted figure is the labeled lens beside it.
            {
                OperatingPoint opN;
                opN.mode           = mode;
                opN.cutoffHz       = cutoff;
                opN.resonance      = baseRes;
                opN.drive          = baseDrive;
                opN.osFactor       = 1;
                opN.osMode         = OsMode::Live;
                opN.hostSampleRate = baseHost;
                fut.setOperatingPoint(opN);
                fut.reset();
                auto ns = testdsp::SignalGen::silence(1 << 14);
                fut.process(ns.data(), (int) ns.size());
                summary[keyBase + "/noise_floor_dbfs"]  = testdsp::Level::rmsDbfs(ns);
                summary[keyBase + "/noise_floor_dbfsA"] =
                    testdsp::AWeighting::aWeightedRmsDbfs(ns, baseHost);
            }
```

(`SignalGen.h`, `Level.h`, `AWeighting.h` includes: `AWeighting.h` was added in Task 4; add the others beside it if not already present.)

- [ ] **Step 4: Run to verify it passes + no golden churn**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>/dev/null | grep -E "RunnerNoiseFloor|FAIL|Summary:"`
Expected: `RunnerNoiseFloor` passes; `Summary: ... 0 failed`.
Then run: `git diff --stat tests/golden/`
Expected: **no changes** (this step only added summary keys; goldens come next).

- [ ] **Step 5: Golden the keys in the gate**

In `tests/CharacterizationGateTests.cpp`, after the M3 level-golden block (the `gs.check(... passband_gain_db ...)` call, line ~97) and before `gs.flush();`, add:

```cpp
        // Idle-noise-floor golden (M4): silence in at the base point. Catches a
        // filter that starts humming/leaking at idle. Flat + A-weighted lens.
        gs.check(*this, "LP24/fc1000/noise_floor_dbfs",
                 s.at(modelName + "/LP24/fc1000/noise_floor_dbfs"),  3.0);
        gs.check(*this, "LP24/fc1000/noise_floor_dbfsA",
                 s.at(modelName + "/LP24/fc1000/noise_floor_dbfsA"), 3.0);
```

- [ ] **Step 6: Verify missing-golden failure, regenerate, inspect the diff**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>/dev/null | grep -E "CharacterizationGate|Summary:"`
Expected: `CharacterizationGate` **FAILS** (no committed baseline rows for the noise keys).

Run: `BERNIE_UPDATE_GOLDEN=1 ./build/tests/k2000_tests 2>/dev/null | grep -E "Summary:"`
Then: `git diff tests/golden/`
Expected: **ONLY additive rows** — `LP24/fc1000/noise_floor_dbfs` and `LP24/fc1000/noise_floor_dbfsA` in each model's `baseline.csv`; every pre-existing row unchanged. If any existing row changed, STOP and investigate before committing.

- [ ] **Step 7: Run to verify it passes**

Run: `./build/tests/k2000_tests 2>/dev/null | grep -E "CharacterizationGate|FAIL|Summary:"`
Expected: `CharacterizationGate` passes; `Summary: ... 0 failed`.

- [ ] **Step 8: Commit**

```bash
git add tests/characterization/CharacterizationRunner.cpp tests/RunnerNoiseFloorTests.cpp tests/CharacterizationGateTests.cpp tests/golden/moog/baseline.csv tests/golden/huggett/baseline.csv tests/CMakeLists.txt
git commit -m "feat(chz): idle-noise-floor keys (flat + A-weighted) gated per model" -m "Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Task 7: The written §5.3 hardware-correlation acceptance criterion

**Files:**
- Create: `docs/filter-validation/acceptance-criterion.md`
- Modify: `docs/filter-validation/README.md` (index link)

**Interfaces:** none (documentation; quotes spec §5.3/§5.4/§9 so the criterion is self-contained).

- [ ] **Step 1: Write the criterion document**

Create `docs/filter-validation/acceptance-criterion.md`:

```markdown
# Hardware-Correlation Acceptance Criterion

**Version:** 5.11 (artifact; distinct from plugin SemVer)
**Date:** 2026-07-02
**Status:** Adopted (SP-A M4) — the hardware half is satisfied in SP-D
**Source:** SP-A design spec §5.3/§5.4/§9
(`docs/superpowers/specs/2026-07-01-device-characterization-core-design.md`)

## The criterion

The characterization framework is **trusted for authenticity judgments** when
BOTH hold, through the calibrated capture chain (loopback + calibration tone,
`testdsp::CaptureCal`):

1. **Ruler proof.** The physical reference standard (a passive filter whose
   response is computable from its component values) is recovered within
   tolerance (≤ ~1 dB; finalized on first capture).
2. **Model correlation.** In-box devices correlate with their hardware
   counterparts (Summit ↔ Huggett, Arturia ↔ Moog) within a stated bound —
   OR a disagreement is attributable to the model, because (1) already
   vouches for the ruler.

**Prerequisites:** the absolute-level axis (shipped: SP-A M2–M4) and a solved
Summit excitation method (the top SP-D risk — no public schematic; how to
drive a known signal through only the Summit's filter).

## Trust ladder (spec §9 — no claim outruns its evidence)

| State | Evidence | Status |
|---|---|---|
| Math-proven | synthetic known-answer net green in `k2000_tests` | **Achieved** (M2–M4) |
| Capture-chain validated | loopback + physical reference recovered | SP-D |
| Hardware-vouched | criterion (1) + (2) satisfied | SP-D |

## Tolerances (spec §5.4)

| Check | Tolerance | Basis |
|---|---|---|
| Dual-method agreement (in-band) | ≤ 0.6 dB | achieved on real models |
| Synthetic corner recovery | ≤ 2 % | log probe grid resolution |
| Synthetic peak-gain / level recovery | ≤ 0.5 dB / ≤ 0.1 dB | lock-in accuracy |
| Synthetic THD recovery | ≤ 1–2 dB | FFT/window bound |
| Self-osc pitch | ± 3 % ≤ 4 kHz, report above | v5 standard |
| Physical-reference recovery | ≤ ~1 dB (finalize on first capture) | real-world chain |

Accuracy (does the ruler measure right?) and authenticity (is the filter true
to hardware?) stay distinct: this criterion makes the ruler trustworthy;
authenticity judgments and any DSP-voicing changes remain SP-D-guided.
```

- [ ] **Step 2: Link it from the index**

In `docs/filter-validation/README.md`, add to the page index (alongside the existing page links, matching the list's style):

```markdown
- [acceptance-criterion.md](acceptance-criterion.md) — when the framework is trusted for authenticity judgments (the §5.3 criterion, trust ladder, tolerances)
```

- [ ] **Step 3: Verify the suite is unaffected and nothing references a wrong path**

Run: `grep -rn "acceptance-criterion" docs/ && ./build/tests/k2000_tests 2>/dev/null | grep -E "Summary:"`
Expected: the README link (and the file itself) show up; `Summary: ... 0 failed` (docs-only change).

- [ ] **Step 4: Commit**

```bash
git add docs/filter-validation/acceptance-criterion.md docs/filter-validation/README.md
git commit -m "docs(chz): written hardware-correlation acceptance criterion (spec 5.3) — SP-A M4 complete" -m "Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Self-Review

**1. Spec coverage (M4 = remaining SP-A).** §4.2 A-weighted noise floor → Task 1 (lens) + Task 6 (live beside flat). §4.2 audibility-weighted aliasing (below/above split, "start simple/defensible" per §10) → Task 2. §4.1 large-signal (gain-vs-level knee, THD-vs-level, headroom-to-clip) → Task 3 (user-confirmed in-scope 2026-07-02). §5.1 remaining references: engineered aliasing → Task 2; calibrated tone → Task 4. §8 Trigger driver → Task 4 (MidiCapture stays a stub for SP-D; analytic oscillator stays SP-C per the M2 deferral). §5.2 physical-reference capability (loopback + math, synthetically validated) → Task 5 (component values deferred to SP-D per §10). §4.1 idle noise floor + §6.2 level-regression gates → Task 6. §5.3 written criterion + §9 trust ladder → Task 7. §6.1 device-typed schema/hardware columns → explicitly deferred to SP-C/SP-D (M3 precedent, stated in Milestone sequence).

**2. Placeholder scan.** No TBD/TODO; every code step shows complete content; every run step has an exact command + expected result. Closed-form anchors are computed in-plan (knee −3.16 dBFS, headroom −2.25 dBFS, alias split −33.98 dB, pad −6.02 dB, A(100) −19.1).

**3. Type consistency.** `AWeighting::{aWeightDb,aWeightedRmsDbfs}` (Task 1) used identically in Tasks 4/6. `Metrics::AliasSplit{belowDb,aboveDb}` matches its test. `LevelResponse::Point{inDbfs,outPeakDbfs,outRmsDbfs,gainDb,thdDb}` consistent between `measure` and the extractors. `CalibratedToneRef` overrides match `DeviceUnderTest`'s pure virtuals (M1). `runGeneratorCapture(DeviceUnderTest&, const Grid&, const juce::File&) -> Summary` consistent between header and definition; summary keys `"<name>/gen/{peak_dbfs,rms_dbfs,rms_dbfsA}"` identical in driver and test. Noise keys `"<keyBase>/noise_floor_dbfs{,A}"` identical in runner, test, and gate.

---

## Execution Handoff

Two execution options:
1. **Subagent-Driven** — fresh subagent per task, two-stage review between tasks.
2. **Inline Execution** — execute in this session with checkpoints (the mode chosen for M4: subagent spend limits; Fable executes inline).
