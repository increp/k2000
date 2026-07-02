# Device-Char Core — Milestone 2: Level Axis + Synthetic Trust References — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Teach the L0 ruler to measure **absolute level** (peak/passband gain, dBFS, crest) and **prove it reads true** against synthetic references whose answers are known by construction — an analytic biquad (validated against its exact z-domain response) and a cubic nonlinearity (validated against its closed-form THD).

**Architecture:** Add pure level-extractor functions in `testdsp/Level.h` (no device knowledge). Add two synthetic reference devices in `tests/characterization/ReferenceDevices.h`: `AnalyticBiquad` (a `chz::DeviceUnderTest`, doubling as the first non-Huggett/Moog device through the contract — the extensibility proof) which can report its own ground-truth response; and `CubicNonlinearity` (a plain adapter) which reports its own closed-form THD. Trust gates in a new always-on test assert the ruler + extractors recover those known answers within tolerance.

**Tech Stack:** C++17, JUCE 8 (`juce::UnitTest`, `juce::dsp::FFT`), CMake. Namespace `chz` (devices) / `testdsp` (ruler).

## Global Constraints

- **C++17**, JUCE 8, CMake. Level helpers in namespace `testdsp`; reference devices in namespace `chz`.
- **Bounded build parallelism: `-j4`** — never bare `-j`.
- **Adapter contract** the ruler needs: `void reset(); void process(float* mono, int n)`. `AnalyticBiquad` additionally implements the full `chz::DeviceUnderTest` (M1).
- **Physics-first:** every extractor returns an honest physical number (dB / dBFS). No perceptual weighting in M2 (that is M4).
- **Scope boundary:** M2 delivers the L0 *capability* + *synthetic proof* only. Wiring level into the runner's summary/CSV/gates is **M3**; reading the real Huggett/Moog level is **SP-B**. Do not modify `CharacterizationRunner` in M2.
- **Non-regression:** the full `k2000_tests` suite (currently 232 tests, 0 failed) must stay green; new tests add to it.
- Dev loop: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests`.
- Commit messages end with: `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`.

## Milestone sequence (context)

Milestone 2 of SP-A (spec: `docs/superpowers/specs/2026-07-01-device-characterization-core-design.md`, §4 level axis, §5.1 synthetic references, §5.4 tolerances). Builds on M1's `DeviceUnderTest` contract. Deferred: the analytic-oscillator reference (SP-C, when oscillator batteries need it); schema/summary/gate wiring (M3); perceptual + physical-reference (M4).

---

## File Structure

- **Create** `tests/testdsp/Level.h` — `testdsp::Level` pure extractors: `peakGainDb`, `passbandGainDb`, `peakDbfs`, `rmsDbfs`, `crestFactorDb`. One responsibility: reduce a response/buffer to a level number.
- **Create** `tests/characterization/ReferenceDevices.h` — `chz::AnalyticBiquad` (`DeviceUnderTest`, RBJ low-pass with a `trueMagDb()` ground truth) and `chz::CubicNonlinearity` (plain adapter with a `trueThdDb()` ground truth).
- **Create** `tests/SyntheticReferenceTests.cpp` — `LevelExtractorTests` (Task 1) + `SyntheticReferenceTests` (Tasks 2–3): the always-on trust gates.
- **Modify** `tests/CMakeLists.txt` — add `SyntheticReferenceTests.cpp` to `k2000_tests`.

---

## Task 1: `testdsp::Level` extractors

**Files:**
- Create: `tests/testdsp/Level.h`
- Create: `tests/SyntheticReferenceTests.cpp`
- Modify: `tests/CMakeLists.txt:78-83` (add the test source)

**Interfaces:**
- Produces: `testdsp::Level::peakGainDb(const std::vector<double>&) -> double`; `testdsp::Level::Passband { Low, High }`; `passbandGainDb(const std::vector<double>&, Passband) -> double`; `peakDbfs(const std::vector<float>&) -> double`; `rmsDbfs(const std::vector<float>&) -> double`; `crestFactorDb(const std::vector<float>&) -> double`.

- [ ] **Step 1: Write the failing test**

Create `tests/SyntheticReferenceTests.cpp`:

```cpp
#include <juce_core/juce_core.h>
#include "testdsp/Level.h"
#include "testdsp/SignalGen.h"
#include <vector>
#include <cmath>

// M2 trust gates: the ruler + level extractors must recover known answers.

struct LevelExtractorTests : public juce::UnitTest {
    LevelExtractorTests() : juce::UnitTest("LevelExtractors") {}

    void runTest() override {
        using testdsp::Level;

        beginTest("peak / passband gain reductions over a response");
        std::vector<double> magDb { -3.0, 0.0, 6.0, 0.0, -12.0 };
        expectWithinAbsoluteError(Level::peakGainDb(magDb), 6.0, 1.0e-9);
        expectWithinAbsoluteError(Level::passbandGainDb(magDb, Level::Passband::Low),  -3.0,  1.0e-9);
        expectWithinAbsoluteError(Level::passbandGainDb(magDb, Level::Passband::High), -12.0, 1.0e-9);

        beginTest("dBFS on a DC signal is exact (0.5 -> -6.0206 dBFS, crest 0)");
        auto dcBuf = testdsp::SignalGen::dc(0.5f, 1024);
        expectWithinAbsoluteError(Level::peakDbfs(dcBuf), -6.0206, 0.01);
        expectWithinAbsoluteError(Level::rmsDbfs(dcBuf),  -6.0206, 0.01);
        expectWithinAbsoluteError(Level::crestFactorDb(dcBuf), 0.0, 0.01);

        beginTest("crest factor of a sine is ~3.01 dB (sqrt(2))");
        auto sineBuf = testdsp::SignalGen::sine(0.5f, 1000.0, 48000.0, 4096);
        expectWithinAbsoluteError(Level::crestFactorDb(sineBuf), 3.0103, 0.2);

        beginTest("noise-floor RMS recovered (uniform amp 0.1 -> amp/sqrt(3))");
        auto noise = testdsp::SignalGen::whiteNoise(0.1f, 16384, 1234u);
        const double expectedDbfs = 20.0 * std::log10(0.1 / std::sqrt(3.0));  // ~ -24.77
        expectWithinAbsoluteError(testdsp::Level::rmsDbfs(noise), expectedDbfs, 0.5);
    }
};

static LevelExtractorTests levelExtractorTestsInstance;
```

Add the source to the `k2000_tests` target. In `tests/CMakeLists.txt`, change:

```cmake
    CharacterizationGateTests.cpp
    DeviceUnderTestTests.cpp
    characterization/FilterUnderTest.cpp
    characterization/CharacterizationRunner.cpp)
```

to:

```cmake
    CharacterizationGateTests.cpp
    DeviceUnderTestTests.cpp
    SyntheticReferenceTests.cpp
    characterization/FilterUnderTest.cpp
    characterization/CharacterizationRunner.cpp)
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --target k2000_tests -j4`
Expected: **build FAILS** — `fatal error: testdsp/Level.h: No such file or directory`.

- [ ] **Step 3: Create the extractors**

Create `tests/testdsp/Level.h`:

```cpp
#pragma once
#include <vector>
#include <cmath>
#include <algorithm>

// testdsp::Level -- absolute-level / gain reductions. Pure functions over a
// magnitude response (dB, absolute gain from SteppedSine) or a time buffer (dBFS,
// 0 dBFS = +-1.0 full scale). No device knowledge.
namespace testdsp {

struct Level {
    // Highest gain anywhere in the response (e.g. the resonant peak), dB.
    static double peakGainDb(const std::vector<double>& magDb) {
        double m = -300.0;
        for (double v : magDb) m = std::max(m, v);
        return m;
    }

    enum class Passband { Low, High };

    // Gain at the passband anchor: lowest probe (LP / DC-side) or highest probe (HP), dB.
    static double passbandGainDb(const std::vector<double>& magDb, Passband p) {
        if (magDb.empty()) return -300.0;
        return p == Passband::Low ? magDb.front() : magDb.back();
    }

    // Peak sample level, dBFS (0 dBFS = +-1.0).
    static double peakDbfs(const std::vector<float>& buf) {
        float pk = 0.0f;
        for (float v : buf) pk = std::max(pk, std::abs(v));
        return 20.0 * std::log10(std::max((double) pk, 1.0e-9));
    }

    // RMS level, dBFS.
    static double rmsDbfs(const std::vector<float>& buf) {
        double s = 0.0;
        for (float v : buf) s += (double) v * v;
        const double r = buf.empty() ? 0.0 : std::sqrt(s / (double) buf.size());
        return 20.0 * std::log10(std::max(r, 1.0e-9));
    }

    // Crest factor = peak - rms, dB (0 for DC, ~3.01 for a sine).
    static double crestFactorDb(const std::vector<float>& buf) {
        return peakDbfs(buf) - rmsDbfs(buf);
    }
};

} // namespace testdsp
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests`
Expected: builds; `[PASS] LevelExtractors` lines with 0 failures; overall `Summary: 233 tests, 0 failed` (232 + 1 new group).

- [ ] **Step 5: Commit**

```bash
git add tests/testdsp/Level.h tests/SyntheticReferenceTests.cpp tests/CMakeLists.txt
git commit -m "feat(chz): testdsp::Level extractors (peak/passband gain, dBFS, crest)" -m "Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 2: `AnalyticBiquad` reference — ruler validated against an exact transfer function

**Files:**
- Create: `tests/characterization/ReferenceDevices.h`
- Modify: `tests/SyntheticReferenceTests.cpp` (add the `SyntheticReferenceTests` struct)

**Interfaces:**
- Consumes: `chz::DeviceUnderTest` (M1), `chz::OperatingPoint`, `testdsp::SteppedSine::transfer`, `testdsp::Level`.
- Produces: `chz::AnalyticBiquad : public DeviceUnderTest` with `setOperatingPoint(op)` (RBJ low-pass from `op.cutoffHz`, `Q = 0.5 + op.resonance*9.5`, `op.hostSampleRate`) and `double trueMagDb(double f) const` (exact z-domain magnitude at `f`).

- [ ] **Step 1: Write the failing test**

Extend `tests/SyntheticReferenceTests.cpp` — add includes after the existing ones:

```cpp
#include "characterization/ReferenceDevices.h"
#include "characterization/CharacterizationRunner.h"   // logFreqs
#include "testdsp/SteppedSine.h"
```

…and add this struct **above** the final `static LevelExtractorTests ...` line (registration lines go at file end):

```cpp
struct SyntheticReferenceTests : public juce::UnitTest {
    SyntheticReferenceTests() : juce::UnitTest("SyntheticReference") {}

    void runTest() override {
        using namespace chz;

        beginTest("ruler recovers an analytic biquad's exact response (<= 0.1 dB in-band)");
        AnalyticBiquad bq;
        OperatingPoint op;
        op.cutoffHz       = 1000.0;
        op.resonance      = 0.4737;      // -> Q ~= 5.0 (0.5 + 0.4737*9.5)
        op.hostSampleRate = 48000.0;
        op.osFactor       = 1;
        bq.setOperatingPoint(op);

        auto probes = CharacterizationRunner::logFreqs(50.0, 20000.0, 100);
        auto r = testdsp::SteppedSine::transfer(bq, probes, 48000.0, 0.5f);

        double worst = 0.0, truePeak = -300.0;
        for (size_t i = 0; i < probes.size(); ++i) {
            const double truth = bq.trueMagDb(probes[i]);
            worst    = std::max(worst, std::abs(r.magDb[i] - truth));
            truePeak = std::max(truePeak, truth);
        }
        expect(worst < 0.1, "measured vs analytic |H| within 0.1 dB at every probe");

        beginTest("peak-gain extractor matches the analytic peak (<= 0.2 dB)");
        const double measPeak = testdsp::Level::peakGainDb(r.magDb);
        expectWithinAbsoluteError(measPeak, truePeak, 0.2);
    }
};
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --target k2000_tests -j4`
Expected: **build FAILS** — `fatal error: characterization/ReferenceDevices.h: No such file or directory`.

- [ ] **Step 3: Create the reference device**

Create `tests/characterization/ReferenceDevices.h`:

```cpp
#pragma once
#include "DeviceUnderTest.h"
#include <cmath>

// chz -- synthetic reference devices with answers known by construction. Used to
// prove the ruler + level/THD extractors read true before trusting them on real DSP.
namespace chz {

// Analytic RBJ low-pass biquad. Doubles as the first non-Huggett/Moog device through
// the DeviceUnderTest contract (extensibility proof). trueMagDb(f) is the EXACT
// z-domain magnitude, the ground truth the ruler must recover.
class AnalyticBiquad : public DeviceUnderTest {
public:
    void reset() override { z1_ = z2_ = 0.0f; }

    void process(float* mono, int n) override {
        for (int i = 0; i < n; ++i) {
            const float x = mono[i];
            const float y = b0_ * x + z1_;
            z1_ = b1_ * x - a1_ * y + z2_;
            z2_ = b2_ * x - a2_ * y;
            mono[i] = y;
        }
    }

    juce::String name()       const override { return "ref_biquad"; }
    DeviceKind   kind()       const override { return DeviceKind::TransferFunction; }
    Excitation   excitation() const override { return Excitation::InputSweep; }
    bool         supports(Mode) override { return true; }

    void setOperatingPoint(const OperatingPoint& op) override {
        sr_ = op.hostSampleRate;
        const double Q  = 0.5 + op.resonance * 9.5;     // res in [0,1] -> Q in [0.5, 10]
        const double w0 = 2.0 * juce::MathConstants<double>::pi * op.cutoffHz / sr_;
        const double cw = std::cos(w0), sw = std::sin(w0);
        const double alpha = sw / (2.0 * Q);
        const double b0 = (1.0 - cw) * 0.5, b1 = 1.0 - cw, b2 = (1.0 - cw) * 0.5;
        const double a0 = 1.0 + alpha,      a1 = -2.0 * cw, a2 = 1.0 - alpha;
        b0_ = float(b0 / a0); b1_ = float(b1 / a0); b2_ = float(b2 / a0);
        a1_ = float(a1 / a0); a2_ = float(a2 / a0);
        reset();
    }

    // Exact |H(e^{jw})| in dB, w = 2*pi*f/sr. H = (b0+b1 z^-1+b2 z^-2)/(1+a1 z^-1+a2 z^-2).
    double trueMagDb(double f) const {
        const double w = 2.0 * juce::MathConstants<double>::pi * f / sr_;
        const double c1 = std::cos(w),  s1 = std::sin(w);
        const double c2 = std::cos(2*w), s2 = std::sin(2*w);
        const double nre = b0_ + b1_ * c1 + b2_ * c2;   // e^{-jw}: real=cos, imag=-sin
        const double nim = -(b1_ * s1 + b2_ * s2);
        const double dre = 1.0 + a1_ * c1 + a2_ * c2;
        const double dim = -(a1_ * s1 + a2_ * s2);
        const double num = std::sqrt(nre*nre + nim*nim);
        const double den = std::sqrt(dre*dre + dim*dim);
        return 20.0 * std::log10(std::max(num / std::max(den, 1e-30), 1e-30));
    }

private:
    double sr_ = 48000.0;
    float  b0_ = 1.0f, b1_ = 0.0f, b2_ = 0.0f, a1_ = 0.0f, a2_ = 0.0f;
    float  z1_ = 0.0f, z2_ = 0.0f;
};

} // namespace chz
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests`
Expected: builds; `[PASS] SyntheticReference` (2 blocks so far) with 0 failures; overall `Summary: 235 tests, 0 failed`.

- [ ] **Step 5: Commit**

```bash
git add tests/characterization/ReferenceDevices.h tests/SyntheticReferenceTests.cpp
git commit -m "feat(chz): AnalyticBiquad reference — ruler validated vs exact z-domain response" -m "Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 3: `CubicNonlinearity` reference — THD ruler validated against a closed form

**Files:**
- Modify: `tests/characterization/ReferenceDevices.h` (add `CubicNonlinearity`)
- Modify: `tests/SyntheticReferenceTests.cpp` (add the THD assertions + `Harmonics.h` include)

**Interfaces:**
- Consumes: `testdsp::Harmonics::thdDb` (templated on `reset()/process()`).
- Produces: `chz::CubicNonlinearity` (plain adapter) with public `float c3` and `double trueThdDb(float amp) const` (closed-form 3rd-harmonic THD for `y = x + c3*x^3`).

- [ ] **Step 1: Write the failing test**

In `tests/SyntheticReferenceTests.cpp`, add the include with the others:

```cpp
#include "testdsp/Harmonics.h"
```

…and add these blocks at the **end of** `SyntheticReferenceTests::runTest()` (after the peak-gain block):

```cpp
        beginTest("THD ruler recovers a cubic's closed-form THD (<= 0.5 dB)");
        CubicNonlinearity cubic;
        cubic.c3 = 0.5f;                 // y = x + 0.5*x^3 -> pure 3rd harmonic
        const double measThd = testdsp::Harmonics::thdDb(cubic, 1000.0, 48000.0, 0.5f);
        const double trueThd = cubic.trueThdDb(0.5f);   // ~ -30.88 dB
        expectWithinAbsoluteError(measThd, trueThd, 0.5);
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --target k2000_tests -j4`
Expected: **build FAILS** — `'CubicNonlinearity' was not declared in this scope`.

- [ ] **Step 3: Add the cubic reference**

In `tests/characterization/ReferenceDevices.h`, add this class inside `namespace chz` (before the closing `} // namespace chz`):

```cpp
// Memoryless cubic: y = x + c3*x^3. For a sine A*sin(wt), x^3 = A^3*(3/4 sin - 1/4 sin3),
// so the output is a fundamental of amplitude (A + 3/4 c3 A^3) plus a single 3rd harmonic
// of amplitude (1/4 c3 A^3) -- a THD known in closed form. Plain reset()/process() adapter.
class CubicNonlinearity {
public:
    float c3 = 0.0f;

    void reset() {}
    void process(float* mono, int n) {
        for (int i = 0; i < n; ++i) {
            const float x = mono[i];
            mono[i] = x + c3 * x * x * x;
        }
    }

    // Closed-form THD (3rd / fundamental amplitude), dB.
    double trueThdDb(float amp) const {
        const double A = amp;
        const double fund = A + 0.75 * (double) c3 * A * A * A;
        const double h3   = 0.25 * (double) c3 * A * A * A;
        return 20.0 * std::log10(std::abs(h3) / std::abs(fund));
    }
};
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests`
Expected: builds; `[PASS] SyntheticReference` (now includes the THD block) with 0 failures; overall `Summary: 236 tests, 0 failed`.

- [ ] **Step 5: Commit**

```bash
git add tests/characterization/ReferenceDevices.h tests/SyntheticReferenceTests.cpp
git commit -m "feat(chz): CubicNonlinearity reference — THD ruler validated vs closed form" -m "Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Self-Review

**1. Spec coverage (M2 scope).** §4.1 absolute-level metrics (peak/passband gain, dBFS, crest, noise floor) → Task 1 `Level.h` + tests. §5.1 synthetic references: analytic biquad → Task 2; known static nonlinearity (THD) → Task 3; calibrated tone/noise level → Task 1 (DC + noise blocks). §5.4 tolerances applied (0.1 dB magnitude, 0.2 dB peak, 0.5 dB THD, ~0.5 dB noise). The analytic-oscillator reference is correctly deferred to SP-C (noted in Milestone sequence). Schema/summary wiring is correctly excluded (M3).

**2. Placeholder scan.** No TBD/TODO. Every code step is complete; every run step has an exact command + expected result (including the exact test counts 233 → 235 → 236). The `AnalyticBiquad` `supports()` returns `true` unconditionally by design (a reference device measured directly, not routed through the runner) — intended, not a stub.

**3. Type consistency.** `testdsp::Level::{peakGainDb,passbandGainDb,peakDbfs,rmsDbfs,crestFactorDb}` and `Passband{Low,High}` are defined in Task 1 and used identically in Task 2. `chz::AnalyticBiquad` overrides exactly the M1 `DeviceUnderTest` pure virtuals (`reset`, `process`, `name`, `kind`, `excitation`, `supports`, `setOperatingPoint`) — signatures match. `chz::CubicNonlinearity` exposes `float c3` and `trueThdDb(float)` used verbatim in Task 3. `Harmonics::thdDb` and `SteppedSine::transfer` are consumed with their real signatures (verified against the L0 headers).

---

## Execution Handoff

Two execution options:
1. **Subagent-Driven (recommended for M2)** — a fresh subagent per task, two-stage review; M2's three tasks are more independent (extractors / biquad / cubic) than M1's, so this parallelizes cleanly.
2. **Inline Execution** — execute the tasks in this session with checkpoints (as M1 was done).
