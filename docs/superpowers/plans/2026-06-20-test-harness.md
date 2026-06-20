# Bernie DSP Test Harness — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn the print-only `OverdriveDiagnosticTests` seed into a reusable, framework-agnostic DSP test library (`tests/testdsp/`) with per-component `juce::UnitTest` fixtures and pass/fail gates that fail CI on DSP regression.

**Architecture:** A header-only support library `tests/testdsp/` (signal generators, spectrum/metrics, an oversampled-reference aliasing comparator, process adapters, a result gate) is validated against analytically-known signals *before* it is trusted to validate the spine DSP. Per-component fixtures (`tests/fixtures/*.cpp`) build an adapter → feed a signal → measure → gate. The existing `k2000_tests` CTest target already exits non-zero on failure, so adding fixtures to it is the CI gate.

**Tech Stack:** C++17, JUCE 8.0.4 (`juce_core`, `juce_dsp::FFT`), CMake, `juce::UnitTest` runner ([tests/TestMain.cpp](../../../tests/TestMain.cpp)).

**Spec:** [docs/specs/2026-06-20-test-harness-design.md](../../specs/2026-06-20-test-harness-design.md) (read it; this plan implements §9's build sequence with the §"Decisions (resolved 2026-06-20)" applied).

## Global Constraints

- **Build with bounded parallelism: `cmake --build build --target k2000_tests -j4`** — bare `-j` OOMs the JUCE compile (project memory). Configure once: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`.
- **`-Wfloat-equal` is on for the plugin and treated as a finding.** Never use float `==`/`!=`; use `std::abs(a-b) > 0.0f` in code and `expectWithinAbsoluteError` in tests.
- **New code must be `-Wsign-conversion`-clean** (cast indices with `(size_t)`/`(int)` deliberately, as the seed does).
- **Non-ASCII text** in any user-facing string must go through `util::u8()` at the JUCE boundary (not relevant to test-only code, but no raw non-ASCII in source).
- **Determinism:** fixed sample rate, fixed FFT size, fixed seeds for noise. No wall-clock, no threading in metrics.
- **`testdsp/` is header-only** (templates over buffer types + `static`/`inline` free functions); CMake gains only new fixture `.cpp` files + an include path, never new link libs.
- **Decisions applied (spec §"Decisions"):** CSV goldens (not WAV); **stereo metrics from the start**; **self-osc pitch tolerance ±0.5 % (~8.5 cents)**; keep `OverdriveDiagnosticTests.cpp` as the verbose human-inspection entry (do not delete/rename); thresholds captured both from literature AND measured baseline, finalized after Task 5 produces real numbers.
- **Branch:** all work on `feat/test-harness`. Commit after every green step.

---

### Task 1: `testdsp` skeleton — SignalGen + Spectrum + self-tests

**Files:**
- Create: `tests/testdsp/SignalGen.h`
- Create: `tests/testdsp/Spectrum.h`
- Create: `tests/testdsp/TestDspSelfTests.cpp` (a `juce::UnitTest` that validates the library itself)
- Modify: `tests/CMakeLists.txt` (add `fixtures`/`testdsp` include path + the new self-test `.cpp`)
- Reference (extract from): `tests/OverdriveDiagnosticTests.cpp` (`magSpectrum`, `sine`, `maxAbs`, `allFinite`)

**Interfaces:**
- Produces — `testdsp::SignalGen`:
  - `static std::vector<float> sine(float amp, double freqHz, double sr, int n)` — `amp·sin(2π·f·i/sr)`.
  - `static std::vector<float> binAlignedSine(float amp, int bin, int n)` — `amp·sin(2π·bin·i/n)` (bin-aligned for leakage-free FFT; freq = `bin·sr/n`).
  - `static std::vector<float> impulse(float amp, int n)` — `v[0]=amp`, rest 0.
  - `static std::vector<float> dc(float v, int n)`; `static std::vector<float> silence(int n)`.
  - `static std::vector<float> whiteNoise(float amp, int n, uint32_t seed)` — deterministic `juce::Random(seed)`.
- Produces — `testdsp::Spectrum`:
  - `static std::vector<float> magnitude(const std::vector<float>& x)` — real-only FFT magnitude, size `N/2`, `N` = `x.size()` (must be power of two). Extracted from the seed's `magSpectrum`.
  - `static float rms(const float* x, int n)`; `static float rms(const std::vector<float>& x)`.
  - `static float maxAbs(const std::vector<float>& x)`; `static bool allFinite(const std::vector<float>& x)`.

- [ ] **Step 1: Create `SignalGen.h` with the generators above.**

```cpp
#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include <cmath>
#include <cstdint>

namespace testdsp {
struct SignalGen {
    static constexpr double pi() { return juce::MathConstants<double>::pi; }

    static std::vector<float> sine(float amp, double freqHz, double sr, int n) {
        std::vector<float> v((size_t) n);
        for (int i = 0; i < n; ++i)
            v[(size_t) i] = amp * (float) std::sin(2.0 * pi() * freqHz * i / sr);
        return v;
    }
    static std::vector<float> binAlignedSine(float amp, int bin, int n) {
        std::vector<float> v((size_t) n);
        for (int i = 0; i < n; ++i)
            v[(size_t) i] = amp * (float) std::sin(2.0 * pi() * bin * i / n);
        return v;
    }
    static std::vector<float> impulse(float amp, int n) {
        std::vector<float> v((size_t) n, 0.0f); if (n > 0) v[0] = amp; return v;
    }
    static std::vector<float> dc(float val, int n) { return std::vector<float>((size_t) n, val); }
    static std::vector<float> silence(int n)       { return std::vector<float>((size_t) n, 0.0f); }
    static std::vector<float> whiteNoise(float amp, int n, uint32_t seed) {
        juce::Random rng((juce::int64) seed);
        std::vector<float> v((size_t) n);
        for (int i = 0; i < n; ++i) v[(size_t) i] = amp * (float) (rng.nextDouble() * 2.0 - 1.0);
        return v;
    }
};
} // namespace testdsp
```

- [ ] **Step 2: Create `Spectrum.h`** (extract `magSpectrum` from the seed; add `rms`/`maxAbs`/`allFinite`).

```cpp
#pragma once
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <cmath>

namespace testdsp {
struct Spectrum {
    // Real-only FFT magnitude. x.size() must be a power of two. Returns N/2 bins.
    static std::vector<float> magnitude(const std::vector<float>& x) {
        const int n = (int) x.size();
        const int order = (int) std::log2((double) n);
        jassert((1 << order) == n);            // power-of-two required
        juce::dsp::FFT fft(order);
        std::vector<float> buf((size_t) (2 * n), 0.0f);
        for (int i = 0; i < n; ++i) buf[(size_t) i] = x[(size_t) i];
        fft.performRealOnlyForwardTransform(buf.data());
        std::vector<float> mag((size_t) (n / 2));
        for (int b = 0; b < n / 2; ++b) {
            const float re = buf[(size_t) (2 * b)], im = buf[(size_t) (2 * b + 1)];
            mag[(size_t) b] = std::sqrt(re * re + im * im);
        }
        return mag;
    }
    static float rms(const float* x, int n) {
        double s = 0.0; for (int i = 0; i < n; ++i) s += double(x[i]) * x[i];
        return n > 0 ? (float) std::sqrt(s / n) : 0.0f;
    }
    static float rms(const std::vector<float>& x) { return rms(x.data(), (int) x.size()); }
    static float maxAbs(const std::vector<float>& x) {
        float m = 0.0f; for (float v : x) m = std::max(m, std::abs(v)); return m;
    }
    static bool allFinite(const std::vector<float>& x) {
        for (float v : x) if (! std::isfinite(v)) return false; return true;
    }
};
} // namespace testdsp
```

- [ ] **Step 3: Create `TestDspSelfTests.cpp` with the first failing self-test** (a pure bin-aligned tone has all energy in one bin).

```cpp
#include <juce_core/juce_core.h>
#include "testdsp/SignalGen.h"
#include "testdsp/Spectrum.h"

class TestDspSelfTests : public juce::UnitTest {
public:
    TestDspSelfTests() : juce::UnitTest("TestDspSelf") {}
    void runTest() override {
        beginTest("bin-aligned tone is a single FFT bin");
        const int N = 1 << 14, bin = 75;
        auto x = testdsp::SignalGen::binAlignedSine(1.0f, bin, N);
        auto mag = testdsp::Spectrum::magnitude(x);
        // fundamental bin dominates; neighbours are ~0 (numerical floor).
        const float fund = mag[(size_t) bin];
        float other = 0.0f;
        for (int b = 2; b < (int) mag.size(); ++b) if (b != bin) other = std::max(other, mag[(size_t) b]);
        expect(fund > 1.0f, "fundamental present: " + juce::String(fund));
        expect(other < fund * 1.0e-4f, "no leakage: other=" + juce::String(other));

        beginTest("rms of unit sine is ~0.707");
        expectWithinAbsoluteError(testdsp::Spectrum::rms(x), 0.70710677f, 1.0e-3f);

        beginTest("allFinite catches NaN");
        std::vector<float> bad { 0.0f, std::nanf(""), 1.0f };
        expect(! testdsp::Spectrum::allFinite(bad));
    }
};
static TestDspSelfTests testDspSelfTestsInstance;
```

- [ ] **Step 4: Register in `tests/CMakeLists.txt`.** Add `TestDspSelfTests.cpp` to the `add_executable(k2000_tests ...)` source list (near `OverdriveDiagnosticTests.cpp`), and add an include directory so `#include "testdsp/SignalGen.h"` resolves:

```cmake
target_include_directories(k2000_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
```
(Place after the `add_executable(...)` block. `${CMAKE_CURRENT_SOURCE_DIR}` is `tests/`, so `testdsp/...` and `fixtures/...` includes resolve.)

- [ ] **Step 5: Build and run.**

Run: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | grep TestDspSelf`
Expected: `[PASS] TestDspSelf: ...` lines, 0 failures; overall `Summary: N tests, 0 failed`.

- [ ] **Step 6: Commit.**

```bash
git add tests/testdsp/SignalGen.h tests/testdsp/Spectrum.h tests/TestDspSelfTests.cpp tests/CMakeLists.txt
git commit -m "test(harness): add testdsp SignalGen + Spectrum with self-tests"
```

---

### Task 2: `Metrics.h` — the catalog (M1–M3, M6, M10) + self-tests

**Files:**
- Create: `tests/testdsp/Metrics.h`
- Modify: `tests/TestDspSelfTests.cpp` (add metric self-tests with analytic expected values)
- Reference: `tests/OverdriveDiagnosticTests.cpp` (`inharmonicDb`)

**Interfaces:**
- Consumes — `testdsp::Spectrum`, `testdsp::SignalGen` (Task 1).
- Produces — `testdsp::Metrics` (free functions over `const std::vector<float>&`):
  - `static bool finite(const std::vector<float>& y)` (M1) → `Spectrum::allFinite`.
  - `static float maxAbs(const std::vector<float>& y)` (M2) → `Spectrum::maxAbs`.
  - `static double inharmonicDb(const std::vector<float>& y, int fundamentalBin)` (M3) — extract from seed: `10·log10(Σ inharmonic-bin energy / fundamental-bin energy)`, skipping bins 0,1; harmonic bins are integer multiples of `fundamentalBin`.
  - `static double thdPlusNDb(const std::vector<float>& y, int fundamentalBin)` (M6) — `10·log10((total energy − fundamental energy) / fundamental energy)` over bins ≥ 2.
  - `static float maxSampleDelta(const std::vector<float>& y)` (M7 part) — `max |y[i]-y[i-1]|`.
  - `static float maxDiff(const std::vector<float>& a, const std::vector<float>& b)` (M8/M13) — `max |a[i]-b[i]|` (equal sizes).
  - `static double dcOffset(const std::vector<float>& y, int fromIndex)` (M14) — mean of tail from `fromIndex`.
  - `static float stereoCorrelation(const std::vector<float>& l, const std::vector<float>& r)` (stereo) — normalized cross-correlation in [−1, 1] (1.0 for identical channels).

- [ ] **Step 1: Write the failing self-tests** in `TestDspSelfTests.cpp` (append a `beginTest` group). Use analytically-known values:

```cpp
beginTest("M3 inharmonicDb ~ floor for a pure tone");
{
    auto x = testdsp::SignalGen::binAlignedSine(0.7f, 75, 1 << 14);
    auto mag = testdsp::Spectrum::magnitude(x);
    expect(testdsp::Metrics::inharmonicDb(mag, 75) < -100.0, "pure tone is clean");
}
beginTest("M6 thdPlusNDb ~ floor for a pure tone");
{
    auto x = testdsp::SignalGen::binAlignedSine(0.7f, 75, 1 << 14);
    auto mag = testdsp::Spectrum::magnitude(x);
    expect(testdsp::Metrics::thdPlusNDb(mag, 75) < -100.0, "pure tone has ~no harmonics");
}
beginTest("M8 maxDiff is zero for identical buffers, exact for a known offset");
{
    auto a = testdsp::SignalGen::binAlignedSine(1.0f, 10, 1024);
    auto b = a; expectWithinAbsoluteError(testdsp::Metrics::maxDiff(a, b), 0.0f, 0.0f);
    for (auto& v : b) v += 0.25f;
    expectWithinAbsoluteError(testdsp::Metrics::maxDiff(a, b), 0.25f, 1.0e-6f);
}
beginTest("stereoCorrelation = 1 identical, ~ -1 inverted");
{
    auto l = testdsp::SignalGen::binAlignedSine(1.0f, 10, 4096); auto r = l;
    expectWithinAbsoluteError(testdsp::Metrics::stereoCorrelation(l, r), 1.0f, 1.0e-4f);
    for (auto& v : r) v = -v;
    expectWithinAbsoluteError(testdsp::Metrics::stereoCorrelation(l, r), -1.0f, 1.0e-4f);
}
```

- [ ] **Step 2: Run to verify it fails** (Metrics undefined).
Run: `cmake --build build --target k2000_tests -j4`
Expected: compile error `'Metrics' is not a member of 'testdsp'` (or include error). That is the expected "failing test" for header-only code.

- [ ] **Step 3: Implement `Metrics.h`.** (Spectrum-domain functions take a magnitude vector; time-domain take the signal.)

```cpp
#pragma once
#include "Spectrum.h"
#include <vector>
#include <cmath>

namespace testdsp {
struct Metrics {
    static bool  finite(const std::vector<float>& y) { return Spectrum::allFinite(y); }
    static float maxAbs(const std::vector<float>& y) { return Spectrum::maxAbs(y); }

    // M3: inharmonic energy below the fundamental, dB. `mag` = Spectrum::magnitude(signal).
    static double inharmonicDb(const std::vector<float>& mag, int fundamentalBin) {
        double fund = 0.0, inh = 0.0;
        for (int b = 2; b < (int) mag.size(); ++b) {
            const double e = double(mag[(size_t) b]) * mag[(size_t) b];
            if (b % fundamentalBin == 0) { if (b == fundamentalBin) fund = e; }
            else inh += e;
        }
        return fund > 0.0 ? 10.0 * std::log10(inh / fund) : 0.0;
    }
    // M6: (everything except fundamental) / fundamental, dB.
    static double thdPlusNDb(const std::vector<float>& mag, int fundamentalBin) {
        double fund = 0.0, rest = 0.0;
        for (int b = 2; b < (int) mag.size(); ++b) {
            const double e = double(mag[(size_t) b]) * mag[(size_t) b];
            if (b == fundamentalBin) fund = e; else rest += e;
        }
        return fund > 0.0 ? 10.0 * std::log10(rest / fund) : 0.0;
    }
    static float maxSampleDelta(const std::vector<float>& y) {
        float m = 0.0f; for (size_t i = 1; i < y.size(); ++i) m = std::max(m, std::abs(y[i] - y[i-1])); return m;
    }
    static float maxDiff(const std::vector<float>& a, const std::vector<float>& b) {
        float m = 0.0f; const size_t n = std::min(a.size(), b.size());
        for (size_t i = 0; i < n; ++i) m = std::max(m, std::abs(a[i] - b[i])); return m;
    }
    static double dcOffset(const std::vector<float>& y, int fromIndex) {
        double s = 0.0; int c = 0;
        for (int i = fromIndex; i < (int) y.size(); ++i) { s += y[(size_t) i]; ++c; }
        return c > 0 ? s / c : 0.0;
    }
    static float stereoCorrelation(const std::vector<float>& l, const std::vector<float>& r) {
        double sll = 0, srr = 0, slr = 0; const size_t n = std::min(l.size(), r.size());
        for (size_t i = 0; i < n; ++i) { sll += double(l[i])*l[i]; srr += double(r[i])*r[i]; slr += double(l[i])*r[i]; }
        const double d = std::sqrt(sll * srr);
        return d > 0.0 ? (float) (slr / d) : 0.0f;
    }
};
} // namespace testdsp
```
Add `#include "testdsp/Metrics.h"` to `TestDspSelfTests.cpp`.

- [ ] **Step 4: Run to verify pass.**
Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | grep -E "TestDspSelf|Summary"`
Expected: all `TestDspSelf` groups PASS; `0 failed`.

- [ ] **Step 5: Commit.**
```bash
git add tests/testdsp/Metrics.h tests/TestDspSelfTests.cpp
git commit -m "test(harness): add Metrics catalog (M1-M3,M6,M8,M14,stereo) with analytic self-tests"
```

---

### Task 3: `Reference.h` — oversampled-reference NSR comparator (M4/M5) + self-tests

**Files:**
- Create: `tests/testdsp/Reference.h`
- Modify: `tests/TestDspSelfTests.cpp`

**Interfaces:**
- Consumes — `testdsp::Spectrum`, `testdsp::SignalGen`.
- Produces — `testdsp::Reference`:
  - `static std::vector<float> decimate(const std::vector<float>& hi, int M)` — steep linear-phase FIR low-pass at `0.5/M` normalized then drop every `M`th sample. Returns `hi.size()/M` samples. Use a Kaiser-windowed sinc (order ≥ 256, β ≈ 8) computed inline (no juce::dsp::FIR dependency needed, but `juce::dsp::FIR` is acceptable).
  - `template <class ProcessFn> static std::vector<float> truth(ProcessFn&& makeAndRun, float amp, int bin, int n, int M)` — generate the bin-aligned sine **at M·fs** (i.e. `binAlignedSine(amp, bin*M-ish…)` — see note), run the identical process at high rate via `makeAndRun(hiBuf)`, then `decimate(.., M)` to `n`. (Process is supplied as a callable that prepares at `M·fsBase` and processes in place.)
  - `static double noiseToSignalDb(const std::vector<float>& dut, const std::vector<float>& truth, int fundamentalBin)` (M4) — bin-wise: `noise = Σ_bins |DUT − TRUTH|²` over non-fundamental bins (complex diff via magnitude proxy is acceptable v1: compare magnitude spectra), `signal = fundamental energy of truth`; return `10·log10(noise/signal)`.
  - `static double noiseToSignalDbA(...)` (M5) — same, A-weighted bin weights (provide the A-weighting curve in `Spectrum.h` or inline).

**Note on the high-rate tone:** to keep the *same* musical frequency at `M·fs`, generate `SignalGen::sine(amp, fBaseHz, M*fsBase, n*M)` where `fBaseHz = bin*fsBase/n`. The decimated result is then bin-aligned at `bin` in the base-rate FFT.

- [ ] **Step 1: Write the failing decimator self-test** (reconstruction error < −120 dB for a band-limited tone through up→identity→down — here we test down-only on an already-high-rate clean tone, asserting the passband tone survives and rms is preserved).

```cpp
beginTest("decimator preserves an in-band tone (reconstruction < -100 dB error)");
{
    const int n = 1 << 13, M = 16, bin = 60;
    // clean tone well inside base-rate Nyquist, generated at high rate:
    const double fsBase = 48000.0, f = bin * fsBase / n;
    auto hi = testdsp::SignalGen::sine(0.5f, f, fsBase * M, n * M);
    auto lo = testdsp::Reference::decimate(hi, M);
    auto ref = testdsp::SignalGen::sine(0.5f, f, fsBase, n);     // ideal base-rate tone
    expectEquals((int) lo.size(), n);
    // compare steady-state tails (skip FIR group delay transient)
    std::vector<float> a(lo.begin() + 1024, lo.end()), b(ref.begin() + 1024, ref.end());
    // allow a fixed phase/group-delay shift: compare RMS and spectra, not sample-wise
    expectWithinAbsoluteError(testdsp::Spectrum::rms(a), testdsp::Spectrum::rms(b), 5.0e-3f);
}
```

- [ ] **Step 2: Run to verify fail** (`Reference` undefined). Expected: compile error.

- [ ] **Step 3: Implement `Reference.h`** (Kaiser-sinc FIR decimator + NSR). Provide the FIR designer inline:

```cpp
#pragma once
#include "Spectrum.h"
#include "SignalGen.h"
#include <vector>
#include <cmath>

namespace testdsp {
struct Reference {
    // Kaiser-windowed sinc low-pass, cutoff = fc (normalized 0..0.5), order taps.
    static std::vector<double> firLowpass(double fc, int taps, double beta) {
        std::vector<double> h((size_t) taps);
        const int M = taps - 1;
        const double i0b = besselI0(beta);
        double sum = 0.0;
        for (int i = 0; i < taps; ++i) {
            const double m = i - M / 2.0;
            const double sinc = (std::abs(m) < 1e-9) ? 2.0 * fc
                              : std::sin(2.0 * juce::MathConstants<double>::pi * fc * m) / (juce::MathConstants<double>::pi * m);
            const double r = 2.0 * i / M - 1.0;
            const double w = besselI0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0b;
            h[(size_t) i] = sinc * w; sum += h[(size_t) i];
        }
        for (auto& v : h) v /= sum;   // unity DC gain
        return h;
    }
    static double besselI0(double x) {
        double sum = 1.0, term = 1.0; for (int k = 1; k < 25; ++k) { term *= (x*x) / (4.0*k*k); sum += term; } return sum;
    }
    static std::vector<float> decimate(const std::vector<float>& hi, int M) {
        static const std::vector<double> h = firLowpass(0.5 / 16.0 * (16.0 / 16.0) , 257, 8.0); // see below
        // Build a per-M cutoff filter (cutoff just under base Nyquist / M):
        const std::vector<double> fir = firLowpass(0.5 / M * 0.9, 257, 8.0);
        const int taps = (int) fir.size();
        const int outN = (int) hi.size() / M;
        std::vector<float> out((size_t) outN, 0.0f);
        for (int o = 0; o < outN; ++o) {
            const int center = o * M;
            double acc = 0.0;
            for (int t = 0; t < taps; ++t) {
                const int idx = center + (t - taps / 2);
                if (idx >= 0 && idx < (int) hi.size()) acc += fir[(size_t) t] * hi[(size_t) idx];
            }
            out[(size_t) o] = (float) acc;
        }
        (void) h;
        return out;
    }
    // M4: noise-to-signal in dB. dut/truth are base-rate time signals (equal length, power of two).
    static double noiseToSignalDb(const std::vector<float>& dut, const std::vector<float>& truth, int fundamentalBin) {
        auto md = Spectrum::magnitude(dut), mt = Spectrum::magnitude(truth);
        double noise = 0.0, sig = 0.0;
        for (int b = 2; b < (int) md.size(); ++b) {
            const double diff = double(md[(size_t) b]) - mt[(size_t) b];
            if (b == fundamentalBin) sig = double(mt[(size_t) b]) * mt[(size_t) b];
            else noise += diff * diff;
        }
        return sig > 0.0 ? 10.0 * std::log10(noise / sig) : 0.0;
    }
};
} // namespace testdsp
```
*(Implementer: simplify `decimate` — the first `static h` line is illustrative; keep only the per-call `fir`. Ensure `-Wsign-conversion` clean.)*

- [ ] **Step 4: Add the authoritative NSR ordering self-test** (DAFx-16 ordering: a hard clipper aliases worse than soft tanh). Write + run:

```cpp
beginTest("M4 NSR: hard clip aliases worse than soft tanh (DAFx-16 ordering)");
{
    const int n = 1 << 13, M = 16, bin = 1500;            // high bin so harmonics fold
    const double fsBase = 48000.0, f = bin * fsBase / n;
    auto run = [&](float (*shape)(float), double sr, int len) {
        auto x = testdsp::SignalGen::sine(0.9f, f, sr, len);
        for (auto& v : x) v = shape(v); return x;
    };
    auto hard = [](float v){ return std::max(-0.5f, std::min(0.5f, v)); };
    auto soft = [](float v){ return std::tanh(v); };
    auto truthHard = testdsp::Reference::decimate(run(+[](float v){return std::max(-0.5f,std::min(0.5f,v));}, fsBase*M, n*M), M);
    auto dutHard   = run(+[](float v){return std::max(-0.5f,std::min(0.5f,v));}, fsBase, n);
    auto truthSoft = testdsp::Reference::decimate(run(+[](float v){return std::tanh(v);}, fsBase*M, n*M), M);
    auto dutSoft   = run(+[](float v){return std::tanh(v);}, fsBase, n);
    const double nsrHard = testdsp::Reference::noiseToSignalDb(dutHard, truthHard, bin);
    const double nsrSoft = testdsp::Reference::noiseToSignalDb(dutSoft, truthSoft, bin);
    expect(nsrHard > nsrSoft + 6.0, "hard clip NSR " + juce::String(nsrHard) + " worse than soft " + juce::String(nsrSoft));
    (void) hard; (void) soft;
}
```

- [ ] **Step 5: Run + verify pass.** Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | grep -E "TestDspSelf|Summary"` → all PASS. If the ordering test is flaky, increase `M` to 32 or `bin` so more harmonics fold.

- [ ] **Step 6: Commit.**
```bash
git add tests/testdsp/Reference.h tests/TestDspSelfTests.cpp
git commit -m "test(harness): add oversampled-reference NSR comparator + decimator self-tests"
```

---

### Task 4: `ProcessAdapter.h` + `Gate.h` + runner message upgrade

**Files:**
- Create: `tests/testdsp/ProcessAdapter.h`
- Create: `tests/testdsp/Gate.h`
- Modify: `tests/TestMain.cpp` (surface metric values — already works via `expect` messages; add a `BERNIE_TEST_VERBOSE` note only)

**Interfaces:**
- Produces — `testdsp::Gate`:
  - `enum class Dir { Max, Min };`
  - `template <class UT> static void check(UT& t, double measured, double threshold, Dir dir, const juce::String& label)` — builds a message `"<label>: <measured> <op> gate <threshold>"` and calls `t.expect(pass, message)`. `Max` → pass if `measured <= threshold`; `Min` → pass if `measured >= threshold`.
- Produces — `testdsp::ProcessAdapter` concept (documented; not a class hierarchy): any type with `void prepare(double sr)`, `void reset()`, `void process(float* buf, int n)`. Provide concrete adapters:
  - `struct ShaperAdapter` — wraps a memoryless `float(float)` callable; `process` maps in place. (For `AsymSaturator`: configure via constructor.)
  - `struct CellAdapter` — wraps `NlSvfCell` + a tap; `process` runs mono (feed `l=r`, take `l`).
  - `struct ModelAdapter` — wraps a `FilterModel*` + its `State*`; `process` calls `processStereo(state, buf, buf, n)` (mono via same ptr is wrong for stereo — use a second scratch; document: mono adapter duplicates buf into r-scratch and discards r).

- [ ] **Step 1: Write `Gate.h`** with a self-test (a passing and a failing-message check):

```cpp
#pragma once
#include <juce_core/juce_core.h>
namespace testdsp {
struct Gate {
    enum class Dir { Max, Min };
    template <class UT>
    static void check(UT& t, double measured, double threshold, Dir dir, const juce::String& label) {
        const bool pass = (dir == Dir::Max) ? (measured <= threshold) : (measured >= threshold);
        const juce::String op = (dir == Dir::Max) ? " <= " : " >= ";
        t.expect(pass, label + ": " + juce::String(measured, 3) + op + "gate " + juce::String(threshold, 3));
    }
};
} // namespace testdsp
```
Self-test in `TestDspSelfTests.cpp`:
```cpp
beginTest("Gate passes within bound");
testdsp::Gate::check(*this, -70.0, -60.0, testdsp::Gate::Dir::Max, "M4 demo");  // -70 <= -60 -> pass
```

- [ ] **Step 2: Write `ProcessAdapter.h`** with the three adapters. (Show full code; `ModelAdapter` allocates an `r`-scratch in `prepare`.)

```cpp
#pragma once
#include <vector>
#include <functional>
#include "../../src/dsp/spine/NlSvfCell.h"
#include "../../src/dsp/spine/FilterModel.h"

namespace testdsp {
struct ShaperAdapter {                       // memoryless float->float
    std::function<float(float)> fn;
    void prepare(double) {} void reset() {}
    void process(float* b, int n) { for (int i = 0; i < n; ++i) b[i] = fn(b[i]); }
};
struct CellAdapter {
    NlSvfCell cell; int tap = NlSvfCell::LP; double sr = 48000.0;
    float cutoff = 1000.0f, res = 0.0f, resSat = 0.0f;
    void prepare(double s) { sr = s; cell.prepare(s); cell.setCutoff(cutoff); cell.setResonance(res); cell.setResSat(resSat); }
    void reset() { cell.reset(); }
    void process(float* b, int n) { for (int i = 0; i < n; ++i) { float l = b[i], r = b[i]; cell.process(l, r, tap); b[i] = l; } }
};
struct ModelAdapter {
    FilterModel* model = nullptr; std::unique_ptr<FilterModel::State> state; std::vector<float> rscratch;
    void prepare(double s) { model->prepare(s); state.reset(model->makeState()); model->reset(*state); }
    void reset() { model->reset(*state); rscratch.clear(); }
    void process(float* b, int n) {
        if ((int) rscratch.size() < n) rscratch.resize((size_t) n);
        std::copy(b, b + n, rscratch.begin());
        model->processStereo(*state, b, rscratch.data(), n);   // L = b, R = scratch (discarded)
    }
};
} // namespace testdsp
```

- [ ] **Step 3: Build + run** the adapter/gate self-tests. Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | grep -E "TestDspSelf|Summary"` → PASS.

- [ ] **Step 4: Add `BERNIE_TEST_VERBOSE` note to `TestMain.cpp`** (a comment + optional `std::getenv` guard around any future verbose table; no behavior change required now). Keep the non-zero-exit gate intact.

- [ ] **Step 5: Commit.**
```bash
git add tests/testdsp/ProcessAdapter.h tests/testdsp/Gate.h tests/TestDspSelfTests.cpp tests/TestMain.cpp
git commit -m "test(harness): add ProcessAdapter + Gate; verbose-flag note in runner"
```

---

### Task 5: Port the seed into gated fixtures (shaper + Huggett spine) + v5.0 regression gate + stereo

**Files:**
- Create: `tests/fixtures/SpineShaperHarnessTests.cpp`
- Create: `tests/fixtures/SpineHuggettHarnessTests.cpp`
- Modify: `tests/CMakeLists.txt` (add the two fixtures)
- Reference: `tests/OverdriveDiagnosticTests.cpp` (the four blocks become gated fixtures; the seed file STAYS as the verbose entry per the decisions)

**Interfaces:**
- Consumes — all of `testdsp/` (Tasks 1–4), `AsymSaturator`, `HuggettFilter`.
- Produces — gated fixtures (no exported symbols; registered `juce::UnitTest` statics).

- [ ] **Step 1: `SpineShaperHarnessTests.cpp`** — gate the AsymSaturator. Steps: build a `ShaperAdapter` for `comp·tanh(gain·x+bias)` at several drives, measure M1/M2/M4(NSR)/M13, and add the **v5.0 regression gate**: plain-tanh NSR must be ≤ a committed baseline constant. Show full fixture:

```cpp
#include <juce_core/juce_core.h>
#include "testdsp/SignalGen.h"
#include "testdsp/Spectrum.h"
#include "testdsp/Metrics.h"
#include "testdsp/Reference.h"
#include "../../src/dsp/spine/AsymSaturator.h"

class SpineShaperHarnessTests : public juce::UnitTest {
public:
    SpineShaperHarnessTests() : juce::UnitTest("SpineShaperHarness") {}
    void runTest() override {
        const int n = 1 << 13, M = 32, bin = 1500;
        const double fsBase = 48000.0, f = bin * fsBase / n;
        auto shape = [](float x, float drive){
            const float gain = std::pow(10.0f, (drive*24.0f)/20.0f);
            const float full = (gain > 1.0f) ? (1.0f/std::tanh(gain)) : 1.0f;
            const float comp = 1.0f + 0.75f*(full-1.0f);
            return comp * std::tanh(gain * x + 0.15f);
        };
        for (float drive : { 0.0f, 0.5f, 1.0f }) {
            beginTest("AsymSaturator NSR gate @drive=" + juce::String(drive));
            auto dut   = testdsp::SignalGen::sine(0.9f, f, fsBase, n);
            for (auto& v : dut) v = shape(v, drive);
            auto hi    = testdsp::SignalGen::sine(0.9f, f, fsBase*M, n*M);
            for (auto& v : hi) v = shape(v, drive);
            auto truth = testdsp::Reference::decimate(hi, M);
            const double nsr = testdsp::Reference::noiseToSignalDb(dut, truth, bin);
            expect(testdsp::Metrics::finite(dut), "finite");
            // Starting gate -45 dB; tighten in Task 5 follow-up once baseline CSV exists.
            testdsp::Gate::check(*this, nsr, -45.0, testdsp::Gate::Dir::Max, "M4 NSR drive=" + juce::String(drive));
        }
    }
};
static SpineShaperHarnessTests spineShaperHarnessTestsInstance;
```
*(Implementer: include `testdsp/Gate.h`. The −45 dB starting number is a placeholder gate to be re-anchored to the measured baseline in Step 4.)*

- [ ] **Step 2: `SpineHuggettHarnessTests.cpp`** — gate the whole spine path: M1, M2 (≤ 4.0), M8 (block 64/128/256 vs per-sample ≤ 1e-5), M13 (zero-drive == bare cell). Port from the seed's `filterPostDrive`/`HuggettNonlinearTests` block-vs-sample logic. Full fixture with the M8 gate (the click regression that the droop removal fixed):

```cpp
// build HuggettFilter (LP, db24, res 0, post-drive 1.0); process amp=2.0 sine in
// blocks of 128 and per-sample; assert maxDiff <= 1e-5 (no block-rate artifact).
```
*(Implementer: mirror `tests/OverdriveDiagnosticTests.cpp::filterPostDrive`; assert `Gate::check(*this, maxDiff, 1e-5, Max, "M8 block-vs-sample")`. Add the M13 zero-drive==bare-cell check from `HuggettNonlinearTests`.)*

- [ ] **Step 3: Add stereo gate** (decisions: stereo from the start). In `SpineHuggettHarnessTests`, process a mono sine through the spine as stereo and assert `stereoCorrelation(L,R) ≥ 0.999` (spine is mono-coefficient → channels must match). `Gate::check(*this, corr, 0.999, Min, "stereo L==R")`.

- [ ] **Step 4: Register fixtures, build, run, then RE-ANCHOR gates.** Add both `.cpp` to `tests/CMakeLists.txt`. Run the suite; read the printed NSR numbers; set each `M4` gate constant to `measuredBaseline + 3 dB` headroom (capture the literature target −60 dB as a comment). Re-run green.
Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | grep -E "SpineShaperHarness|SpineHuggettHarness|Summary"`

- [ ] **Step 5: Commit.**
```bash
git add tests/fixtures/SpineShaperHarnessTests.cpp tests/fixtures/SpineHuggettHarnessTests.cpp tests/CMakeLists.txt
git commit -m "test(harness): gated shaper + Huggett-spine fixtures (M1/M2/M4/M8/M13 + stereo + v5.0 regression gate)"
```

---

### Task 6: Stateful fixtures — NlSvf (M10/M11/M12), DcBlocker (M14), HpStage (M10/M11) + M9 zipper + M15 denormal-flush

**Files:**
- Create: `tests/fixtures/SpineNlSvfHarnessTests.cpp`
- Create: `tests/fixtures/SpineDcBlockerHarnessTests.cpp`
- Create: `tests/fixtures/SpineHpStageHarnessTests.cpp`
- Modify: `tests/CMakeLists.txt`
- Add to `testdsp/Metrics.h`: `magResponseDb(adapter, freqHz, sr, amp)` helper (steady-state |out|/|in| in dB via RMS, reusing the seed's sweep approach) and `selfOscPitchHz(adapter, sr)` (impulse-kick → FFT peak bin → Hz).

**Interfaces:**
- Consumes — `CellAdapter`, `ModelAdapter`, `Metrics`, `SignalGen`, `Gate`.
- Produces — `testdsp::Response`: `static double magDb(Adapter&, double f, double sr, float amp)`; `static double peakFreqHz(Adapter&, double sr)` (FFT-peak of a self-oscillating run).

- [ ] **Step 1: Add `Response` helpers to `testdsp`** (header `Response.h`), with a self-test against the linear TPT analytic |H(f)| (M10): a `CellAdapter` LP at cutoff 1000 Hz, res 0, must match the closed-form `|H| = 1/sqrt(1+(f/fc)^4)`-ish within 0.5 dB at a few points. (Use the exact Cytomic LP magnitude; the implementer derives it from `tpt-svf-core.md`.)

- [ ] **Step 2: `SpineNlSvfHarnessTests.cpp`** — M10 (LP/HP/BP response vs analytic ≤ 0.5 dB passband), M11 (resonant peak freq ±2 %, height ±1.5 dB at res), **M12 self-osc pitch within ±0.5 % (~8.5 cents) of cutoff** across cutoffs {200, 1000, 5000 Hz} and sample rates {44100, 48000, 96000}, M2 bounded self-osc (≤ 4.0), M13 low-level linear, M15 denormal-flush (run 1 s silence after a kick, assert tail energy ≤ −300 dB). Gate each via `Gate::check`.

- [ ] **Step 3: `SpineDcBlockerHarnessTests.cpp`** — M14: feed `0.5 + sine`, assert tail `|DC| ≤ 0.02` and audio RMS preserved (≥ 0.5×); L/R independence (port from `HuggettNonlinearTests`).

- [ ] **Step 4: `SpineHpStageHarnessTests.cpp`** — M10/M11: HP corner + 12 vs 24 dB slope (24 steeper), resonant peak; M2 bounded self-osc at max res. Port thresholds from `HpPreFilterTests`.

- [ ] **Step 5: Add M9 zipper** to `SpineNlSvfHarnessTests`: hold a tone while ramping cutoff per block; NSR of the output must stay ≤ the M4 floor (no extra sidebands from coefficient steps).

- [ ] **Step 6: Register, build, run, anchor any new thresholds, commit.**
Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | grep -E "Harness|Summary"`
```bash
git add tests/fixtures/SpineNlSvfHarnessTests.cpp tests/fixtures/SpineDcBlockerHarnessTests.cpp tests/fixtures/SpineHpStageHarnessTests.cpp tests/testdsp/Response.h tests/CMakeLists.txt
git commit -m "test(harness): stateful fixtures (NlSvf M10/M11/M12, DcBlocker M14, HpStage) + zipper + denormal-flush"
```

---

### Task 7: Golden anchoring (CSV) + CI confirmation

**Files:**
- Create: `tests/testdsp/GoldenIO.h`
- Create: `tests/golden/` (CSV baselines for M3/M6/M10/M11)
- Modify: the fixtures (compare against golden where the decisions chose CSV)

**Interfaces:**
- Produces — `testdsp::GoldenIO`: `static std::map<juce::String,double> load(const juce::File&)`; `static void save(const juce::File&, const std::map<juce::String,double>&)`; CSV `key,value` per line. A fixture compares `measured` vs `golden[key]` within a per-metric tolerance and, when `BERNIE_UPDATE_GOLDEN=1`, rewrites the file (intentional voicing-change workflow).

- [ ] **Step 1: Implement `GoldenIO.h`** (tiny CSV reader/writer, deterministic key order). Self-test: round-trip a map.

- [ ] **Step 2: Wire golden comparison** into the shaper + NlSvf fixtures for M6 (THD+N within ±1 dB of golden) and M10/M11 (response/peak within tol). Generate the initial `tests/golden/*.csv` via `BERNIE_UPDATE_GOLDEN=1` run; commit the CSVs.

- [ ] **Step 3: CI confirmation.** Nudge a spine coefficient (e.g. temporarily change `NlSvfCell` Q constant), build, run — assert a gate FAILS (capture the failing line). Revert the nudge; re-run green. Record the demonstrated failure in the spec's as-built note.
Run (fail demo, then revert): `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests; echo "exit=$?"` → non-zero on the nudged build, zero after revert.

- [ ] **Step 4: Commit.**
```bash
git add tests/testdsp/GoldenIO.h tests/golden/ tests/fixtures/
git commit -m "test(harness): CSV golden anchoring + CI regression-fail confirmation"
```

---

## Self-review notes (author)

- **Spec coverage:** §2 architecture → Tasks 1–4 (testdsp lib) + 5–6 (fixtures); §3 oversampled reference → Task 3; §4 metrics M1–M15 → M1–M3/M6/M8/M13/M14/stereo in Tasks 2/5, M4/M5 Task 3, M10/M11/M12/M9/M15 Task 6; §6 runner upgrade → Task 4 (message via Gate, verbose flag note); §7 CI → Task 7; §8 layout → Tasks 1–7 file paths; §"Decisions" → Global Constraints + Task 5 step 3 (stereo), Task 6 step 2 (±0.5 % pitch), Task 7 (CSV goldens), seed kept.
- **Deferred within plan:** M5 A-weighted NSR is specified in Task 3 interfaces but its gate is optional in Task 5 (enable once M4 is anchored) — not a placeholder, an explicit phasing.
- **Type consistency:** `Spectrum::magnitude`, `Metrics::inharmonicDb/thdPlusNDb/maxDiff/dcOffset/stereoCorrelation`, `Reference::decimate/noiseToSignalDb`, `Gate::check/Dir`, adapters `prepare/reset/process` — names are used identically across tasks.
- **Known rough edge for implementer:** `Reference::decimate` code in Task 3 has an illustrative leftover `static h` line — delete it; keep only the per-call `fir`. The NSR via magnitude-difference is a v1 proxy (phase-insensitive); if the hard-vs-soft ordering self-test is marginal, raise `M` to 32 and `bin` so more harmonics fold.
