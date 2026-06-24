# SOTA Filter-Characterization Harness — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a reusable, model-agnostic filter-characterization framework (exponential-sine-sweep core) that measures every filter model across four batteries — magnitude, phase/group-delay, resonance/self-oscillation, distortion/aliasing — feeding fast CI gates, a committed self-golden baseline, and a dormant Arturia-comparison layer, fronted by a `/characterize-filter` skill.

**Architecture:** Five layers (spec §3). **L0** `tests/testdsp/` pure-DSP measurement library (ESS gen/deconvolution → transfer function + harmonics), validated against synthetic closed-form filters. **L1** `FilterUnderTest` — one socket to excite any `FilterModel`. **L2** a runner that sweeps an operating-point grid and emits CSV/`summary.json` fingerprints. **L3** two consumers: a fast spec-gate + self-golden subset inside `k2000_tests`, and an opt-in `k2000_filter_characterization` executable. Oversampling axes are designed into the operating-point model but pinned to base today.

**Tech Stack:** C++17, JUCE 8.0.4 (`juce::dsp::FFT`, `juce::UnitTest`), CMake. No new third-party deps.

**Design spec:** `docs/superpowers/specs/2026-06-23-filter-characterization-harness-design.md` (v5.07).

## Global Constraints

- **Test-target only.** This harness lives entirely under `tests/`. It does **not** touch shipping `src/` code except to *consume* existing public interfaces (`FilterModel`, `MoogLadder`, `HuggettFilter`). No `src/` modification.
- **Build/test:** `cmake --build build --target <target> -j4` (ALWAYS `-j4`; bare `-j` OOMs the JUCE build). `k2000_tests` is the existing fast suite; `k2000_filter_characterization` is the new opt-in executable. A passing JUCE `UnitTest` run ends `Summary: N tests, 0 failed` — grep stdout.
- **Pristine output:** `-Wshadow` / `-Wfloat-equal` are defects (use `std::fpclassify(v)==FP_ZERO` for zero tests, rename shadowing params); the pre-existing `-Wsign-conversion` policy is deferred (don't add new instances in hand-written code).
- **The self-tests are the oracle.** L0 measurement code is validated against synthetic filters with *known analytic answers* (Task 1–3). When a measurement is wrong, fix the measurement code — never loosen an analytic-reference tolerance to force green.
- **OS axes are designed-in, base-only.** `OperatingPoint` carries `osFactor`/`osMode`/`hostSampleRate`; today every grid pins them to `{1, live, 48000}`. No oversampling is implemented here.
- **Branch:** `feat/filter-characterization-harness` (already created off `main`; spec committed). Independent of the Moog DSP PR.
- **`FilterModel` lifecycle (from v5.1, consumed here):** `prepare(double)`, `makeState()` → `std::unique_ptr<State>`, `reset(State&)`, `setCommon(cutoffHz,res,drive)`, `processStereo(State&,float* L,float* R,int n)`. `MoogLadder` adds `setSlope(Slope{db12,db24})`, `setMode(Mode{LP,BP,HP})`, `setSeparation(float)`. Read `src/dsp/spine/FilterModel.h`, `MoogLadder.h`, `HuggettFilter.h` before Task 4.

---

## Phase 1 — L0 measurement library (the ruler) + self-tests

### Task 1: Exponential sine sweep — generation, inverse filter, deconvolution

Create the ESS engine: generate a log sweep, its inverse filter, and FFT-deconvolve a recorded response into an impulse response. Validate by deconvolving known synthetic systems.

**Files:**
- Create: `tests/testdsp/Sweep.h`
- Test: `tests/TestDspSelfTests.cpp` (extend — it already exists and is registered)

**Interfaces:**
- Consumes: `testdsp::Spectrum` (FFT helpers, exists).
- Produces:
  - `struct testdsp::Sweep` with:
    - `static std::vector<float> generate(double f0, double f1, double sr, int lengthSamples)` — Farina ESS, amplitude 1.0.
    - `static std::vector<float> inverse(double f0, double f1, double sr, int lengthSamples)` — time-reversed sweep with +6 dB/oct (pink-compensating) amplitude envelope, normalised so deconvolution of the bare sweep yields unit peak.
    - `static std::vector<float> deconvolve(const std::vector<float>& recorded, const std::vector<float>& inverseFilter)` — linear convolution via FFT (zero-padded to next pow2 ≥ len(recorded)+len(inverse)−1), returns the impulse response (length = next pow2).
    - `static int linearIRIndex(int lengthSamples, int recordedLen, int fftLen)` — index in the deconvolved output where the *linear* impulse response peaks (end of the sweep region); harmonic IRs precede it (used by Task 3).

- [ ] **Step 1: Write the failing test — sweep through identity yields a single peak**

Add to `tests/TestDspSelfTests.cpp` `runTest()`:

```cpp
beginTest("ESS: deconvolving an identity system yields a unit impulse at the linear-IR index");
{
    const double sr = 48000.0; const double f0 = 20.0, f1 = 20000.0;
    const int N = 1 << 16;                                  // sweep length
    auto sweep = testdsp::Sweep::generate(f0, f1, sr, N);
    auto inv   = testdsp::Sweep::inverse  (f0, f1, sr, N);
    // identity system: recorded == sweep
    auto ir = testdsp::Sweep::deconvolve(sweep, inv);
    // find global peak
    int peak = 0; float pmax = 0.0f;
    for (int i = 0; i < (int) ir.size(); ++i)
        if (std::abs(ir[(size_t)i]) > pmax) { pmax = std::abs(ir[(size_t)i]); peak = i; }
    const int expected = testdsp::Sweep::linearIRIndex(N, N, (int) ir.size());
    expect(std::abs(peak - expected) <= 2, "linear-IR peak not at expected index: " + juce::String(peak) + " vs " + juce::String(expected));
    // impulse is sharp: energy is concentrated at the peak (peak >> neighbours a few samples away)
    expect(std::abs(ir[(size_t)(peak+50)]) < pmax * 0.05f, "deconvolved identity IR not impulse-like");
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --target k2000_tests -j4 2>&1 | grep -E "error:|Sweep" | head`
Expected: compile error — `testdsp::Sweep` / `Sweep.h` does not exist.

- [ ] **Step 3: Implement `tests/testdsp/Sweep.h`**

```cpp
#pragma once
#include "Spectrum.h"
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <cmath>

// testdsp::Sweep — Farina exponential-sine-sweep (ESS) measurement primitives.
// generate(): s(t) = sin( K * (exp(t/L * ln(w2/w1)) - 1) ),  K = w1*L/ln(w2/w1).
// inverse():  time-reversed sweep with a +6 dB/oct amplitude envelope that
//             compensates the sweep's -3 dB/oct (pink) spectrum, so that
//             deconvolution recovers a flat-band impulse.  (Farina 2000.)
// deconvolve(): linear convolution recorded * inverse via FFT (the IR + its
//             harmonic pre-echoes).
namespace testdsp {

struct Sweep {
    static std::vector<float> generate(double f0, double f1, double sr, int lengthSamples) {
        const double w1 = 2.0 * juce::MathConstants<double>::pi * f0;
        const double w2 = 2.0 * juce::MathConstants<double>::pi * f1;
        const double L  = (double) lengthSamples / sr;          // sweep duration (s)
        const double lnR = std::log(w2 / w1);
        const double K  = w1 * L / lnR;
        std::vector<float> s((size_t) lengthSamples);
        for (int i = 0; i < lengthSamples; ++i) {
            const double t = (double) i / sr;
            s[(size_t) i] = (float) std::sin(K * (std::exp(t / L * lnR) - 1.0));
        }
        return s;
    }

    static std::vector<float> inverse(double f0, double f1, double sr, int lengthSamples) {
        auto s = generate(f0, f1, sr, lengthSamples);
        const double w1 = 2.0 * juce::MathConstants<double>::pi * f0;
        const double w2 = 2.0 * juce::MathConstants<double>::pi * f1;
        const double L  = (double) lengthSamples / sr;
        const double lnR = std::log(w2 / w1);
        std::vector<float> inv((size_t) lengthSamples);
        // time-reverse and apply +6 dB/oct envelope: gain(t) ∝ exp(-t/L * lnR)
        // (reversed, so index i maps to original time (L - i/sr)).
        for (int i = 0; i < lengthSamples; ++i) {
            const double tRev = (double) (lengthSamples - 1 - i) / sr;
            const double env  = std::exp(-tRev / L * lnR);       // amplitude modulation
            inv[(size_t) i] = s[(size_t) (lengthSamples - 1 - i)] * (float) env;
        }
        // Normalise so that deconvolving the bare sweep gives unit peak.
        auto ir = deconvolve(s, inv);
        float pk = 0.0f; for (float v : ir) pk = std::max(pk, std::abs(v));
        if (pk > 0.0f) for (auto& v : inv) v /= pk;
        return inv;
    }

    static std::vector<float> deconvolve(const std::vector<float>& recorded,
                                         const std::vector<float>& inverseFilter) {
        const int need = (int) recorded.size() + (int) inverseFilter.size() - 1;
        int order = 0; while ((1 << order) < need) ++order;
        const int n = 1 << order;
        juce::dsp::FFT fft(order);
        std::vector<float> a((size_t)(2 * n), 0.0f), b((size_t)(2 * n), 0.0f);
        for (size_t i = 0; i < recorded.size();      ++i) a[i] = recorded[i];
        for (size_t i = 0; i < inverseFilter.size(); ++i) b[i] = inverseFilter[i];
        fft.performRealOnlyForwardTransform(a.data());
        fft.performRealOnlyForwardTransform(b.data());
        // complex multiply a *= b (interleaved re,im), bins 0..n/2
        for (int bin = 0; bin <= n / 2; ++bin) {
            const float ar = a[(size_t)(2*bin)], ai = a[(size_t)(2*bin+1)];
            const float br = b[(size_t)(2*bin)], bi = b[(size_t)(2*bin+1)];
            a[(size_t)(2*bin)]   = ar*br - ai*bi;
            a[(size_t)(2*bin+1)] = ar*bi + ai*br;
        }
        fft.performRealOnlyInverseTransform(a.data());
        a.resize((size_t) n);
        return a;
    }

    // Linear IR lands at the end of the sweep region; with this generate/inverse
    // pairing the peak sits at index (lengthSamples - 1).
    static int linearIRIndex(int lengthSamples, int /*recordedLen*/, int /*fftLen*/) {
        return lengthSamples - 1;
    }
};

} // namespace testdsp
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | grep -E "TestDsp|Summary"`
Expected: `[PASS] TestDsp …`, `Summary: … 0 failed`. If the peak index is off by a constant, fix `linearIRIndex` (not the test) so it matches the implementation's actual peak.

- [ ] **Step 5: Add the second oracle — recover a known 1-pole RC response**

Append to the same `beginTest` block region a new test:

```cpp
beginTest("ESS: recovers a 1-pole RC magnitude within 0.5 dB at probe frequencies");
{
    const double sr = 48000.0, f0 = 20.0, f1 = 20000.0; const int N = 1 << 16;
    const double fc = 1000.0;
    const double g = std::tan(juce::MathConstants<double>::pi * fc / sr);
    const double G = g / (1.0 + g);
    auto sweep = testdsp::Sweep::generate(f0, f1, sr, N);
    // process sweep through a TPT 1-pole LP: y = G*(x - z) + z ; z = y + G*(x - z)
    std::vector<float> rec((size_t) N); double z = 0.0;
    for (int i = 0; i < N; ++i) {
        const double x = sweep[(size_t) i];
        const double v = (x - z) * G; const double y = v + z; z = y + v;
        rec[(size_t) i] = (float) y;
    }
    auto inv = testdsp::Sweep::inverse(f0, f1, sr, N);
    auto ir  = testdsp::Sweep::deconvolve(rec, inv);
    auto mag = testdsp::Spectrum::magnitude(ir);                 // |H(bin)|
    auto dbAt = [&](double f) {
        const int bin = (int) std::round(f * (double) ir.size() / sr);
        return 20.0 * std::log10(std::max(1e-9f, mag[(size_t) bin]));
    };
    auto analytic = [&](double f) {                              // 1-pole LP magnitude
        const double wr = f / fc; return 20.0 * std::log10(1.0 / std::sqrt(1.0 + wr*wr));
    };
    for (double f : { 200.0, 1000.0, 4000.0 })
        expect(std::abs(dbAt(f) - analytic(f)) < 0.5,
               "RC mag mismatch @" + juce::String(f) + ": got " + juce::String(dbAt(f),2)
               + " want " + juce::String(analytic(f),2));
}
```

Run the suite; iterate the ESS `generate`/`inverse` math (NOT the 0.5 dB tolerance) until it passes. The normalisation in `inverse()` makes the deconvolved DC/low-band gain ≈ unity, so the magnitude curve reads directly in dB.

- [ ] **Step 6: Commit**

```bash
git add tests/testdsp/Sweep.h tests/TestDspSelfTests.cpp
git commit -m "test(harness): ESS sweep gen + inverse + deconvolution (L0) with synthetic oracles

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: Transfer function — magnitude, phase, group delay + derived metrics

Turn an impulse response into the complex transfer function and the B1/B4 metrics. Validate against RBJ biquads (analytic) and a pure delay (exact group delay).

**Files:**
- Create: `tests/testdsp/TransferFunction.h`
- Test: `tests/TestDspSelfTests.cpp` (extend)

**Interfaces:**
- Consumes: `testdsp::Spectrum`, `testdsp::Sweep` (Task 1).
- Produces:
  - `struct testdsp::TransferFunction`:
    - `static TF analyze(const std::vector<float>& ir, double sr)` where `struct TF { std::vector<float> magDb, phaseRad, groupDelaySec; double sr; int n; }` (`n` = FFT bins = ir.size()/2).
    - `double magDbAt(double f) const`, `double phaseAt(double f) const`, `double groupDelayAt(double f) const` — linear-interpolated reads at an arbitrary frequency.
    - `static double cornerHz(const TF& tf, double passbandDb)` — lowest frequency where `magDb` falls `3 dB` below `passbandDb`.
    - `static double slopeDbPerOct(const TF& tf, double fLow, double fHigh)` — `(magDbAt(fHigh)-magDbAt(fLow)) / log2(fHigh/fLow)`.
    - `static double peakDb(const TF& tf)` and `static double qFactor(const TF& tf)` — resonance peak gain and Q (peak freq / −3 dB bandwidth around the peak).

- [ ] **Step 1: Write the failing test — RBJ biquad LP analytic match**

Add to `tests/TestDspSelfTests.cpp`. Include a small inline RBJ biquad helper in the test file (test-local, not shipped):

```cpp
// test-local RBJ biquad (Direct Form I) for L0 oracles
struct RbjLp {
    double b0,b1,b2,a1,a2, x1=0,x2=0,y1=0,y2=0;
    RbjLp(double fc, double q, double sr) {
        const double w = 2.0*juce::MathConstants<double>::pi*fc/sr, c=std::cos(w), s=std::sin(w);
        const double alpha = s/(2.0*q);
        const double a0 = 1.0+alpha;
        b0=(1.0-c)/2.0/a0; b1=(1.0-c)/a0; b2=b0; a1=(-2.0*c)/a0; a2=(1.0-alpha)/a0;
    }
    float process(float xin){ double x=xin; double y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2;
        x2=x1;x1=x;y2=y1;y1=y; return (float)y; }
};

beginTest("TransferFunction: RBJ biquad LP magnitude within 0.5 dB; corner ~fc; slope ~ -12 dB/oct");
{
    const double sr=48000.0,f0=20.0,f1=20000.0; const int N=1<<16; const double fc=1000.0,Q=0.707;
    auto sweep = testdsp::Sweep::generate(f0,f1,sr,N);
    RbjLp ref(fc,Q,sr); std::vector<float> rec((size_t)N);
    for (int i=0;i<N;++i) rec[(size_t)i]=ref.process(sweep[(size_t)i]);
    auto ir = testdsp::Sweep::deconvolve(rec, testdsp::Sweep::inverse(f0,f1,sr,N));
    auto tf = testdsp::TransferFunction::analyze(ir, sr);
    // analytic biquad magnitude
    auto refDb=[&](double f){ RbjLp r(fc,Q,sr); /* eval via freq response */
        const double w=2.0*juce::MathConstants<double>::pi*f/sr;
        std::complex<double> z=std::exp(std::complex<double>(0,-w));
        std::complex<double> H=(r.b0+r.b1*z+r.b2*z*z)/(1.0+r.a1*z+r.a2*z*z);
        return 20.0*std::log10(std::abs(H)); };
    for (double f : {200.0,1000.0,4000.0})
        expect(std::abs(tf.magDbAt(f)-refDb(f))<0.5, "biquad mag @"+juce::String(f));
    expect(std::abs(testdsp::TransferFunction::cornerHz(tf, tf.magDbAt(100.0)) - fc) < fc*0.08,
           "corner not within 8% of fc");
    const double slope = testdsp::TransferFunction::slopeDbPerOct(tf, 2000.0, 4000.0);
    expect(slope < -9.0 && slope > -15.0, "LP slope not ~ -12 dB/oct: " + juce::String(slope,1));
}
```

- [ ] **Step 2: Run to verify it fails** — Run: `cmake --build build --target k2000_tests -j4 2>&1 | grep -E "error:|TransferFunction" | head`. Expected: compile error (`TransferFunction.h` missing). (Add `#include <complex>` to the test file.)

- [ ] **Step 3: Implement `tests/testdsp/TransferFunction.h`**

```cpp
#pragma once
#include "Spectrum.h"
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <cmath>

namespace testdsp {

struct TransferFunction {
    struct TF {
        std::vector<float> magDb, phaseRad, groupDelaySec;
        double sr = 48000.0; int n = 0;     // n = number of bins (= ir.size()/2)
        double binHz() const { return sr / (double)(2 * n); }
        double readLerp(const std::vector<float>& v, double f) const {
            const double x = f / binHz(); const int i = (int) x;
            if (i < 0) return v.front(); if (i >= n-1) return v.back();
            const double frac = x - i; return v[(size_t)i]*(1.0-frac) + v[(size_t)(i+1)]*frac;
        }
        double magDbAt(double f) const { return readLerp(magDb, f); }
        double phaseAt(double f) const { return readLerp(phaseRad, f); }
        double groupDelayAt(double f) const { return readLerp(groupDelaySec, f); }
    };

    static TF analyze(const std::vector<float>& ir, double sr) {
        const int n2 = (int) ir.size(); int order=0; while ((1<<order)<n2) ++order;
        juce::dsp::FFT fft(order);
        std::vector<float> buf((size_t)(2*n2), 0.0f);
        for (int i=0;i<n2;++i) buf[(size_t)i]=ir[(size_t)i];
        fft.performRealOnlyForwardTransform(buf.data());
        const int n = n2/2;
        TF tf; tf.sr=sr; tf.n=n;
        tf.magDb.resize((size_t)n); tf.phaseRad.resize((size_t)n); tf.groupDelaySec.resize((size_t)n);
        std::vector<double> ph((size_t)n);
        for (int b=0;b<n;++b){
            const double re=buf[(size_t)(2*b)], im=buf[(size_t)(2*b+1)];
            tf.magDb[(size_t)b]=(float)(20.0*std::log10(std::max(1e-12,std::sqrt(re*re+im*im))));
            ph[(size_t)b]=std::atan2(im,re);
        }
        // unwrap phase
        for (int b=1;b<n;++b){ double d=ph[(size_t)b]-ph[(size_t)(b-1)];
            while (d> juce::MathConstants<double>::pi) { ph[(size_t)b]-=2.0*juce::MathConstants<double>::pi; d=ph[(size_t)b]-ph[(size_t)(b-1)]; }
            while (d<-juce::MathConstants<double>::pi) { ph[(size_t)b]+=2.0*juce::MathConstants<double>::pi; d=ph[(size_t)b]-ph[(size_t)(b-1)]; } }
        const double dw = 2.0*juce::MathConstants<double>::pi*tf.binHz();
        for (int b=0;b<n;++b){
            tf.phaseRad[(size_t)b]=(float)ph[(size_t)b];
            const int b0=std::max(0,b-1), b1=std::min(n-1,b+1);
            tf.groupDelaySec[(size_t)b]=(float)(-(ph[(size_t)b1]-ph[(size_t)b0])/((b1-b0)*dw));
        }
        return tf;
    }

    static double cornerHz(const TF& tf, double passbandDb) {
        for (int b=1;b<tf.n;++b) if (tf.magDb[(size_t)b] <= passbandDb-3.0) return b*tf.binHz();
        return tf.sr*0.5;
    }
    static double slopeDbPerOct(const TF& tf, double fLow, double fHigh) {
        return (tf.magDbAt(fHigh)-tf.magDbAt(fLow)) / std::log2(fHigh/fLow);
    }
    static double peakDb(const TF& tf) {
        double m=-300; for (int b=1;b<tf.n;++b) m=std::max(m,(double)tf.magDb[(size_t)b]); return m;
    }
    static double qFactor(const TF& tf) {
        int pk=1; for (int b=2;b<tf.n;++b) if (tf.magDb[(size_t)b]>tf.magDb[(size_t)pk]) pk=b;
        const double pkDb=tf.magDb[(size_t)pk], target=pkDb-3.0, fpk=pk*tf.binHz();
        int lo=pk; while (lo>1 && tf.magDb[(size_t)lo]>target) --lo;
        int hi=pk; while (hi<tf.n-1 && tf.magDb[(size_t)hi]>target) ++hi;
        const double bw=(hi-lo)*tf.binHz(); return bw>0 ? fpk/bw : 0.0;
    }
};

} // namespace testdsp
```

- [ ] **Step 4: Run to verify it passes** — Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | grep -E "TransferFunction|Summary"`. Expected PASS. Iterate the analyze math (phase unwrap / group-delay sign), never the tolerances.

- [ ] **Step 5: Add the group-delay oracle (pure delay line)**

```cpp
beginTest("TransferFunction: pure D-sample delay -> group delay = D/sr across band");
{
    const double sr=48000.0,f0=20.0,f1=20000.0; const int N=1<<16; const int D=32;
    auto sweep=testdsp::Sweep::generate(f0,f1,sr,N);
    std::vector<float> rec((size_t)N,0.0f);
    for (int i=0;i<N;++i) rec[(size_t)i] = (i>=D)? sweep[(size_t)(i-D)] : 0.0f;
    auto ir=testdsp::Sweep::deconvolve(rec, testdsp::Sweep::inverse(f0,f1,sr,N));
    auto tf=testdsp::TransferFunction::analyze(ir,sr);
    for (double f : {500.0,2000.0,8000.0})
        expect(std::abs(tf.groupDelayAt(f) - (double)D/sr) < 1.5/sr,
               "group delay @"+juce::String(f)+" = "+juce::String(tf.groupDelayAt(f)*sr,2)+" samples, want "+juce::String(D));
}
```

Run the suite; the delay shows up as a shift of the linear-IR peak — group delay must read `D/sr`. Commit when green.

- [ ] **Step 6: Commit**

```bash
git add tests/testdsp/TransferFunction.h tests/TestDspSelfTests.cpp
git commit -m "test(harness): transfer function (mag/phase/group-delay) + metrics, biquad+delay oracles

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 3: Harmonics — Farina THD separation + stepped THD + aliasing metric

Extract per-harmonic responses from the deconvolved IR (Farina pre-echoes), a stepped single-tone THD, and an inharmonic-energy aliasing metric. Validate against a memoryless `tanh` shaper (known harmonics), a clean sine (≈0 aliasing), and a deliberate aliaser (fires).

**Files:**
- Create: `tests/testdsp/Harmonics.h`
- Test: `tests/TestDspSelfTests.cpp` (extend)

**Interfaces:**
- Consumes: `testdsp::Spectrum`.
- Produces:
  - `struct testdsp::Harmonics`:
    - `static double thdStepped(const std::vector<float>& out, double f0, double sr)` — THD of a steady single-tone output: `sqrt(Σ harmonics²)/fundamental`, harmonics located at `k·f0` bins.
    - `static double aliasingEnergyDb(const std::vector<float>& out, double f0, double sr)` — ratio (dB) of energy at **inharmonic** bins (not within ±1 bin of any `k·f0` ≤ Nyquist) to the fundamental's energy. Higher (less negative) = more aliasing.
    - `static double harmonicLevelDb(const std::vector<float>& out, double f0, int k, double sr)` — level of the k-th harmonic relative to the fundamental.

(Farina IR-domain harmonic separation is *optional polish*; the stepped single-tone THD above is the gating metric and is simpler/robust. The runner uses `thdStepped`; the IR-domain method can be added later without interface change.)

- [ ] **Step 1: Write the failing test — tanh shaper has the expected odd-harmonic THD**

```cpp
beginTest("Harmonics: tanh shaper THD matches analytic; clean sine ~0 aliasing; aliaser fires");
{
    const double sr=48000.0, f0=997.0; const int N=1<<15;       // ~prime-ish to avoid bin lock
    auto tone=[&](double amp){ std::vector<float> x((size_t)N);
        for (int i=0;i<N;++i) x[(size_t)i]=(float)(amp*std::sin(2.0*juce::MathConstants<double>::pi*f0*i/sr)); return x; };
    // (a) clean sine -> near-zero THD and aliasing
    {
        auto x=tone(0.5);
        expect(testdsp::Harmonics::thdStepped(x,f0,sr) < 0.01, "clean sine THD not ~0");
        expect(testdsp::Harmonics::aliasingEnergyDb(x,f0,sr) < -60.0, "clean sine shows aliasing");
    }
    // (b) tanh shaper -> measurable odd-harmonic THD (> 1%)
    {
        auto x=tone(0.9); for (auto& v:x) v=std::tanh(3.0f*v);
        const double thd=testdsp::Harmonics::thdStepped(x,f0,sr);
        expect(thd>0.05 && thd<0.6, "tanh THD out of expected band: "+juce::String(thd,3));
        // odd harmonics dominate: 3rd > 2nd
        expect(testdsp::Harmonics::harmonicLevelDb(x,f0,3,sr) > testdsp::Harmonics::harmonicLevelDb(x,f0,2,sr),
               "tanh 3rd harmonic not above 2nd");
    }
    // (c) deliberate aliaser: square a high tone so harmonics fold back
    {
        const double fh=9000.0; std::vector<float> x((size_t)N);
        for (int i=0;i<N;++i){ double s=std::sin(2.0*juce::MathConstants<double>::pi*fh*i/sr); x[(size_t)i]=(float)(s*s*s); }
        expect(testdsp::Harmonics::aliasingEnergyDb(x,fh,sr) > -40.0, "aliaser did not register fold-back");
    }
}
```

- [ ] **Step 2: Run to verify it fails** — `cmake --build build --target k2000_tests -j4 2>&1 | grep -E "error:|Harmonics" | head`. Expected: missing `Harmonics.h`.

- [ ] **Step 3: Implement `tests/testdsp/Harmonics.h`**

```cpp
#pragma once
#include "Spectrum.h"
#include <vector>
#include <cmath>

namespace testdsp {

struct Harmonics {
    // power-of-two crop of the steady region for clean bins
    static std::vector<float> crop(const std::vector<float>& x) {
        int order=0; while ((1<<(order+1)) <= (int)x.size()) ++order; const int n=1<<order;
        return std::vector<float>(x.end()-n, x.end());
    }
    static double thdStepped(const std::vector<float>& xin, double f0, double sr) {
        auto x=crop(xin); auto mag=Spectrum::magnitude(x);
        const double binHz=sr/(double)x.size();
        auto lvl=[&](double f){ int b=(int)std::round(f/binHz); if(b<1||b>=(int)mag.size()) return 0.0;
            return (double)mag[(size_t)b]; };
        const double fund=lvl(f0); if (fund<=0) return 0.0;
        double hsq=0.0; for (int k=2;k*f0<sr*0.5;++k){ const double h=lvl(k*f0); hsq+=h*h; }
        return std::sqrt(hsq)/fund;
    }
    static double harmonicLevelDb(const std::vector<float>& xin, double f0, int k, double sr) {
        auto x=crop(xin); auto mag=Spectrum::magnitude(x); const double binHz=sr/(double)x.size();
        auto lvl=[&](double f){ int b=(int)std::round(f/binHz); if(b<1||b>=(int)mag.size()) return 1e-12; return std::max(1e-12,(double)mag[(size_t)b]); };
        return 20.0*std::log10(lvl(k*f0)/lvl(f0));
    }
    static double aliasingEnergyDb(const std::vector<float>& xin, double f0, double sr) {
        auto x=crop(xin); auto mag=Spectrum::magnitude(x); const double binHz=sr/(double)x.size();
        auto isHarmonic=[&](int b){ const double f=b*binHz;
            for (int k=1;k*f0<sr*0.5;++k){ if (std::abs(f-k*f0) <= 1.5*binHz) return true; } return false; };
        const int fb=(int)std::round(f0/binHz);
        const double fund=std::max(1e-12,(double)mag[(size_t)fb]);
        double inh=0.0; for (int b=2;b<(int)mag.size();++b) if (!isHarmonic(b)) inh+=(double)mag[(size_t)b]*mag[(size_t)b];
        return 20.0*std::log10(std::sqrt(inh)/fund);
    }
};

} // namespace testdsp
```

- [ ] **Step 4: Run to verify it passes** — `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | grep -E "Harmonics|Summary"`. Expected PASS. Iterate the bin-tolerance / crop logic (not the test bands) if the clean-sine aliasing floor reads too high (spectral leakage — widen the `1.5*binHz` harmonic guard or apply a Hann window in `crop`).

- [ ] **Step 5: Commit**

```bash
git add tests/testdsp/Harmonics.h tests/TestDspSelfTests.cpp
git commit -m "test(harness): harmonics — stepped THD + aliasing-energy metric, shaper/aliaser oracles

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Phase 2 — L1 driver + L2 runner + artifacts + opt-in target (Moog)

### Task 4: `FilterUnderTest` — the model-agnostic socket + `OperatingPoint`

One uniform interface to excite any `FilterModel`, carrying the full operating point (incl. dormant OS axes). Validate by reproducing the existing Moog magnitude measurement (and deleting the duplicated `mag()`/`magR()` from `MoogLadderTests`).

**Files:**
- Create: `tests/characterization/FilterUnderTest.h`
- Test: `tests/TestDspSelfTests.cpp` (extend — adds a Moog-drive sanity check)
- Modify: `tests/CMakeLists.txt` (add `tests/characterization` to the include path — header-only so far)

**Interfaces:**
- Consumes: `FilterModel`, `MoogLadder` (read `src/dsp/spine/MoogLadder.h`); `testdsp::Response`.
- Produces:
  - `struct testdsp::OperatingPoint { int mode=0; double cutoffHz=1000, resonance=0, drive=0; int slope=1; int osFactor=1; int osMode=0 /*0=live,1=render*/; double hostSampleRate=48000; };`
  - `class testdsp::FilterUnderTest` with:
    - `explicit FilterUnderTest(FilterModel& model)`.
    - `void prepare(double sr)`, `void setOperatingPoint(const OperatingPoint&)`, `void reset()`, `void process(float* mono, int n)` (drives the model's left lane; right lane is identical and ignored).
    - Mode/slope are forwarded only if the concrete model exposes them — Task 4 wires `MoogLadder` (and Task 7 adds Huggett). Use a small set of `std::function` setters injected at construction so the socket stays model-agnostic:
      `FilterUnderTest(FilterModel& m, std::function<void(int)> setMode, std::function<void(int)> setSlope)` (either may be `nullptr`).

- [ ] **Step 1: Write the failing test — socket reproduces a known Moog magnitude**

```cpp
#include "characterization/FilterUnderTest.h"
#include "../src/dsp/spine/MoogLadder.h"

beginTest("FilterUnderTest: drives MoogLadder; LP passband ~unity, far stopband attenuated");
{
    const double sr=48000.0;
    MoogLadder m; m.prepare(sr);
    auto st=m.makeState();
    testdsp::FilterUnderTest fut(m, [&](int md){ m.setMode((MoogLadder::Mode)md); },
                                    [&](int sl){ m.setSlope((MoogLadder::Slope)sl); });
    fut.prepare(sr);
    // socket must expose a process() that drives one lane; measure magnitude via testdsp::Response
    testdsp::OperatingPoint op; op.cutoffHz=1000; op.resonance=0; op.drive=0; op.slope=1; op.mode=0;
    fut.setOperatingPoint(op);
    // FilterUnderTest satisfies testdsp::Response's adapter contract (prepare/reset/process):
    const double pass = testdsp::Response::magDb(fut, 100.0, sr, 0.25f);
    const double stop = testdsp::Response::magDb(fut, 8000.0, sr, 0.25f);
    expect(pass > -3.0 && pass < 3.0, "passband not ~unity: " + juce::String(pass,2) + " dB");
    expect(stop < -30.0, "stopband not attenuated: " + juce::String(stop,2) + " dB");
}
```

- [ ] **Step 2: Run to verify it fails** — missing `FilterUnderTest.h`.

- [ ] **Step 3: Implement `tests/characterization/FilterUnderTest.h`**

```cpp
#pragma once
#include "../../src/dsp/spine/FilterModel.h"
#include <functional>
#include <memory>
#include <vector>

namespace testdsp {

struct OperatingPoint {
    int    mode = 0;           // 0=LP 1=BP 2=HP 3=Notch (model-defined)
    double cutoffHz = 1000.0, resonance = 0.0, drive = 0.0;
    int    slope = 1;          // 0=12dB 1=24dB
    int    osFactor = 1;       // {1,2,4,8,16,32} — base only today
    int    osMode = 0;         // 0=live 1=render — live only today
    double hostSampleRate = 48000.0;
};

// Adapter satisfying testdsp::Response's contract: prepare(double)/reset()/process(float*,int).
class FilterUnderTest {
public:
    FilterUnderTest(FilterModel& model,
                    std::function<void(int)> setMode  = nullptr,
                    std::function<void(int)> setSlope = nullptr)
        : model_(model), setMode_(std::move(setMode)), setSlope_(std::move(setSlope)) {}

    void prepare(double sr) { model_.prepare(sr); state_ = model_.makeState(); }
    void setOperatingPoint(const OperatingPoint& op) {
        op_ = op;
        if (setSlope_) setSlope_(op.slope);
        if (setMode_)  setMode_(op.mode);
        model_.setCommon((float) op.cutoffHz, (float) op.resonance, (float) op.drive);
        // osFactor/osMode/hostSampleRate are recorded for the artifact schema; no OS today.
    }
    void reset() { model_.reset(*state_); }
    void process(float* mono, int n) {
        right_.assign((size_t) n, 0.0f);
        for (int i = 0; i < n; ++i) right_[(size_t) i] = mono[i];
        model_.setCommon((float) op_.cutoffHz, (float) op_.resonance, (float) op_.drive);
        if (setSlope_) setSlope_(op_.slope);
        if (setMode_)  setMode_(op_.mode);
        model_.processStereo(*state_, mono, right_.data(), n);   // left lane is the measured output
    }
    const OperatingPoint& operatingPoint() const { return op_; }
private:
    FilterModel& model_;
    std::unique_ptr<FilterModel::State> state_;
    std::function<void(int)> setMode_, setSlope_;
    OperatingPoint op_;
    std::vector<float> right_;
};

} // namespace testdsp
```

- [ ] **Step 4: Run to verify it passes** — `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | grep -E "FilterUnderTest|Summary"`. Expected PASS. (If `MoogLadder`'s mode/slope enums differ from the casts, adjust the lambdas in the test to match `MoogLadder.h`.)

- [ ] **Step 5: Delete the duplicated helpers**

In `tests/MoogLadderTests.cpp`, replace the private `mag()`/`magR()` helpers' *uses* with `testdsp::Response::magDb(...)` driven through a `FilterUnderTest`, and delete the now-dead `mag()`/`magR()` definitions. Run the full suite; all existing Moog magnitude tests must stay green (same numbers within their existing tolerances).

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | grep -E "MoogLadder|Summary"`
Expected: `Summary: … 0 failed` (no behavioral change — just the measurement path unified).

- [ ] **Step 6: Commit**

```bash
git add tests/characterization/FilterUnderTest.h tests/TestDspSelfTests.cpp tests/MoogLadderTests.cpp tests/CMakeLists.txt
git commit -m "test(harness): FilterUnderTest socket + OperatingPoint (OS-ready); unify Moog mag() path

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 5: The four batteries

Implement B1–B4 as functions over `FilterUnderTest` + L0, returning structured results. Validate each on Moog for sane values.

**Files:**
- Create: `tests/characterization/Batteries.h`
- Test: `tests/TestDspSelfTests.cpp` (extend)

**Interfaces:**
- Consumes: `FilterUnderTest`, `OperatingPoint`, `testdsp::{Sweep,TransferFunction,Harmonics,Response,Spectrum}`.
- Produces (`namespace testdsp`):
  - `struct ResponseResult { double cornerHz, slopeDbPerOct, passbandRippleDb, stopbandDb; std::vector<std::pair<double,double>> magCurve; /* (Hz,dB) */ };`
  - `struct ResonanceResult { double peakDb, q, selfOscOnset, limitCycleAmp, crest; std::vector<std::pair<double,double>> pitchTrack; /* (cutoff,measuredHz) */ };`
  - `struct DistortionResult { std::vector<std::pair<double,double>> thdVsFreq, aliasingVsTone; double thdAtUnity; };`
  - `struct PhaseResult { std::vector<std::pair<double,double>> phaseCurve, groupDelayCurve; };`
  - `struct Batteries`:
    - `static ResponseResult linearResponse(FilterUnderTest&, const OperatingPoint&, double sr, int nLogPoints);`
    - `static ResonanceResult resonance(FilterUnderTest&, OperatingPoint, double sr, const std::vector<double>& cutoffs);`
    - `static DistortionResult distortion(FilterUnderTest&, OperatingPoint, double sr, const std::vector<double>& tones);`
    - `static PhaseResult phase(FilterUnderTest&, const OperatingPoint&, double sr, int nLogPoints);`

- [ ] **Step 1: Write the failing tests — Moog batteries return sane numbers**

```cpp
#include "characterization/Batteries.h"

beginTest("Batteries: Moog linear response, resonance, distortion, phase produce sane numbers");
{
    const double sr=48000.0; MoogLadder m; m.prepare(sr);
    testdsp::FilterUnderTest fut(m,[&](int md){m.setMode((MoogLadder::Mode)md);},
                                    [&](int sl){m.setSlope((MoogLadder::Slope)sl);});
    fut.prepare(sr);
    testdsp::OperatingPoint op; op.cutoffHz=1000; op.slope=1; op.mode=0;

    auto r1=testdsp::Batteries::linearResponse(fut,op,sr,512);
    expect(std::abs(r1.cornerHz-1000.0)<200.0, "corner off: "+juce::String(r1.cornerHz));
    expect(r1.slopeDbPerOct<-18.0 && r1.slopeDbPerOct>-30.0, "24dB slope off: "+juce::String(r1.slopeDbPerOct,1));

    auto r2=testdsp::Batteries::resonance(fut,op,sr,{220.0,880.0});
    expect(r2.pitchTrack.size()==2, "pitch track size");
    for (auto& p : r2.pitchTrack)
        expect(std::abs(p.second-p.first)/p.first < 0.05, "self-osc pitch off @"+juce::String(p.first));

    op.drive=0.6; auto r3=testdsp::Batteries::distortion(fut,op,sr,{500.0,2000.0});
    expect(r3.thdVsFreq.size()==2 && r3.thdVsFreq[0].second>0.0, "no THD measured");

    op.drive=0.0; auto r4=testdsp::Batteries::phase(fut,op,sr,256);
    expect(r4.groupDelayCurve.size()==256, "phase curve size");
}
```

- [ ] **Step 2: Run to verify it fails** — missing `Batteries.h`.

- [ ] **Step 3: Implement `tests/characterization/Batteries.h`** (reference implementation; iterate against the Step-1 oracle)

```cpp
#pragma once
#include "FilterUnderTest.h"
#include "../testdsp/Sweep.h"
#include "../testdsp/TransferFunction.h"
#include "../testdsp/Harmonics.h"
#include "../testdsp/Response.h"
#include <vector>
#include <utility>
#include <cmath>

namespace testdsp {

struct ResponseResult { double cornerHz=0,slopeDbPerOct=0,passbandRippleDb=0,stopbandDb=0;
    std::vector<std::pair<double,double>> magCurve; };
struct ResonanceResult { double peakDb=0,q=0,selfOscOnset=0,limitCycleAmp=0,crest=0;
    std::vector<std::pair<double,double>> pitchTrack; };
struct DistortionResult { std::vector<std::pair<double,double>> thdVsFreq, aliasingVsTone; double thdAtUnity=0; };
struct PhaseResult { std::vector<std::pair<double,double>> phaseCurve, groupDelayCurve; };

struct Batteries {
    static TransferFunction::TF measureTF(FilterUnderTest& f, const OperatingPoint& op, double sr) {
        const int N=1<<16; const double f0=20.0,f1=std::min(20000.0,sr*0.49);
        auto sweep=Sweep::generate(f0,f1,sr,N);
        f.setOperatingPoint(op); f.reset();
        std::vector<float> rec(sweep.begin(),sweep.end());
        for (auto& v:rec) v*=0.2f;                 // small-signal
        f.process(rec.data(),N);
        auto ir=Sweep::deconvolve(rec, Sweep::inverse(f0,f1,sr,N));
        return TransferFunction::analyze(ir,sr);
    }
    static ResponseResult linearResponse(FilterUnderTest& f, const OperatingPoint& op, double sr, int nLogPoints) {
        OperatingPoint p=op; p.resonance=0; p.drive=0;
        auto tf=measureTF(f,p,sr); ResponseResult r;
        const double pb=tf.magDbAt(std::max(20.0,op.cutoffHz*0.1));
        r.cornerHz=TransferFunction::cornerHz(tf,pb);
        r.slopeDbPerOct=TransferFunction::slopeDbPerOct(tf,op.cutoffHz*2.0,op.cutoffHz*4.0);
        r.stopbandDb=tf.magDbAt(std::min(sr*0.45,op.cutoffHz*8.0));
        const double f0=20.0,f1=std::min(20000.0,sr*0.49);
        for (int i=0;i<nLogPoints;++i){ const double fr=f0*std::pow(f1/f0,(double)i/(nLogPoints-1));
            r.magCurve.push_back({fr,tf.magDbAt(fr)}); }
        double mn=1e9,mx=-1e9; for (auto& m:r.magCurve) if (m.first<op.cutoffHz*0.5){ mn=std::min(mn,m.second); mx=std::max(mx,m.second);} r.passbandRippleDb=mx-mn;
        return r;
    }
    static ResonanceResult resonance(FilterUnderTest& f, OperatingPoint op, double sr, const std::vector<double>& cutoffs) {
        ResonanceResult r;
        { OperatingPoint p=op; p.resonance=0.9; auto tf=measureTF(f,p,sr);
          r.peakDb=TransferFunction::peakDb(tf); r.q=TransferFunction::qFactor(tf); }
        for (double fc:cutoffs){
            OperatingPoint p=op; p.cutoffHz=fc; p.resonance=1.0;
            f.setOperatingPoint(p); f.reset();
            std::vector<float> buf(1<<15,0.0f); buf[0]=1.0f;
            f.process(buf.data(),(int)buf.size());
            std::vector<float> tail(buf.end()-8192,buf.end());
            const double hz=Response::peakFreqHz(tail,sr);
            r.pitchTrack.push_back({fc,hz});
            double pk=0; for (float v:tail) pk=std::max(pk,std::abs(v)); r.limitCycleAmp=pk;
            r.crest = pk/std::max(1e-9f,Spectrum::rms(tail));
        }
        r.selfOscOnset=0.94;   // reported-only (provisional); finalized in the calibration pass (spec §17), never gated
        return r;
    }
    static DistortionResult distortion(FilterUnderTest& f, OperatingPoint op, double sr, const std::vector<double>& tones) {
        DistortionResult r;
        for (double ft:tones){
            OperatingPoint p=op; f.setOperatingPoint(p); f.reset();
            const int N=1<<15; std::vector<float> x((size_t)N);
            for (int i=0;i<N;++i) x[(size_t)i]=(float)(0.5*std::sin(2.0*juce::MathConstants<double>::pi*ft*i/sr));
            f.process(x.data(),N);
            r.thdVsFreq.push_back({ft,Harmonics::thdStepped(x,ft,sr)});
            r.aliasingVsTone.push_back({ft,Harmonics::aliasingEnergyDb(x,ft,sr)});
        }
        if (!r.thdVsFreq.empty()) r.thdAtUnity=r.thdVsFreq.front().second;
        return r;
    }
    static PhaseResult phase(FilterUnderTest& f, const OperatingPoint& op, double sr, int nLogPoints) {
        OperatingPoint p=op; p.resonance=0; p.drive=0; auto tf=measureTF(f,p,sr);
        PhaseResult r; const double f0=20.0,f1=std::min(20000.0,sr*0.49);
        for (int i=0;i<nLogPoints;++i){ const double fr=f0*std::pow(f1/f0,(double)i/(nLogPoints-1));
            r.phaseCurve.push_back({fr,tf.phaseAt(fr)}); r.groupDelayCurve.push_back({fr,tf.groupDelayAt(fr)}); }
        return r;
    }
};

} // namespace testdsp
```

- [ ] **Step 4: Run to verify it passes** — `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | grep -E "Batteries|Summary"`. Iterate the battery measurement code (sweep length, small-signal amplitude) against the oracle — never the test bounds.

- [ ] **Step 5: Commit**

```bash
git add tests/characterization/Batteries.h tests/TestDspSelfTests.cpp
git commit -m "test(harness): four measurement batteries (response/resonance/distortion/phase) over FilterUnderTest

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 6: Runner + artifacts + the opt-in `k2000_filter_characterization` executable

Sweep the operating-point grid, run the batteries, emit CSV + `summary.json`, print a human summary, return a meaningful exit code. New standalone target (not in `add_test`).

**Files:**
- Create: `tests/characterization/CharacterizationRunner.h`, `tests/characterization/CharacterizationRunner.cpp`
- Create: `tests/characterization/Artifacts.h`, `tests/characterization/Artifacts.cpp`
- Create: `tests/CharacterizationMain.cpp`
- Modify: `tests/CMakeLists.txt` (new executable target)

**Interfaces:**
- Consumes: `Batteries`, `FilterUnderTest`.
- Produces:
  - `struct testdsp::ModelFingerprint { std::string model; ResponseResult resp; ResonanceResult reso; DistortionResult dist; PhaseResult phase; OperatingPoint op; };`
  - `class testdsp::CharacterizationRunner { ModelFingerprint run(FilterUnderTest&, std::string modelName, double sr); int writeArtifacts(const ModelFingerprint&, const std::string& dir); std::string humanSummary(const ModelFingerprint&); };`
  - `tests/characterization/Artifacts.{h,cpp}`: `writeResponseCsv`, `writeResonanceCsv`, `writeDistortionCsv`, `writeSummaryJson` — all rows prefixed with the operating-point columns (`model,mode,osFactor,osMode,hostSR,cutoffHz,resonance,drive`).
  - `CharacterizationMain.cpp`: `int main(int argc, char** argv)` — parses `--model moog|huggett|all`, builds the `FilterUnderTest`(s), runs, writes to `build/characterization/<model>/`, prints `humanSummary`, returns `0` if all spec-gate sanity checks pass else `1`.

- [ ] **Step 1: Write the failing test — runner produces a fingerprint + writes artifacts**

Add to `tests/TestDspSelfTests.cpp`:

```cpp
#include "characterization/CharacterizationRunner.h"

beginTest("CharacterizationRunner: produces a Moog fingerprint and writes artifacts to a temp dir");
{
    const double sr=48000.0; MoogLadder m; m.prepare(sr);
    testdsp::FilterUnderTest fut(m,[&](int md){m.setMode((MoogLadder::Mode)md);},
                                    [&](int sl){m.setSlope((MoogLadder::Slope)sl);});
    fut.prepare(sr);
    testdsp::CharacterizationRunner runner;
    auto fp = runner.run(fut, "moog", sr);
    expect(!fp.resp.magCurve.empty(), "no response curve");
    auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("k2000_char_test");
    tmp.deleteRecursively(); tmp.createDirectory();
    const int rc = runner.writeArtifacts(fp, tmp.getFullPathName().toStdString());
    expect(rc==0, "writeArtifacts failed");
    expect(tmp.getChildFile("response.csv").existsAsFile(), "response.csv not written");
    expect(tmp.getChildFile("summary.json").existsAsFile(), "summary.json not written");
    expect(runner.humanSummary(fp).length() > 0, "empty human summary");
    tmp.deleteRecursively();
}
```

- [ ] **Step 2: Run to verify it fails** — missing `CharacterizationRunner.h`.

- [ ] **Step 3: Implement `Artifacts.{h,cpp}`, `CharacterizationRunner.{h,cpp}`**

`Artifacts.h`:

```cpp
#pragma once
#include "Batteries.h"
#include <string>
namespace testdsp { struct ModelFingerprint; }
namespace testdsp { struct Artifacts {
    static int writeResponseCsv  (const ModelFingerprint&, const std::string& path);
    static int writeResonanceCsv (const ModelFingerprint&, const std::string& path);
    static int writeDistortionCsv(const ModelFingerprint&, const std::string& path);
    static int writeSummaryJson  (const ModelFingerprint&, const std::string& path);
}; }
```

`CharacterizationRunner.h`:

```cpp
#pragma once
#include "Batteries.h"
#include <string>
namespace testdsp {
struct ModelFingerprint { std::string model; ResponseResult resp; ResonanceResult reso;
    DistortionResult dist; PhaseResult phase; OperatingPoint op; };
class CharacterizationRunner {
public:
    ModelFingerprint run(FilterUnderTest& f, const std::string& modelName, double sr);
    int writeArtifacts(const ModelFingerprint&, const std::string& dir);
    std::string humanSummary(const ModelFingerprint&);
};
}
```

`CharacterizationRunner.cpp` (grid is small today — LP mode, a few cutoffs; widen in calibration):

```cpp
#include "CharacterizationRunner.h"
#include "Artifacts.h"
#include <juce_core/juce_core.h>

namespace testdsp {
ModelFingerprint CharacterizationRunner::run(FilterUnderTest& f, const std::string& modelName, double sr) {
    ModelFingerprint fp; fp.model=modelName; fp.op.cutoffHz=1000; fp.op.slope=1; fp.op.mode=0;
    fp.resp  = Batteries::linearResponse(f, fp.op, sr, 512);
    fp.reso  = Batteries::resonance     (f, fp.op, sr, {110.0,220.0,440.0,880.0,1760.0,3520.0});
    OperatingPoint d=fp.op; d.drive=0.6;
    fp.dist  = Batteries::distortion    (f, d, sr, {250.0,1000.0,4000.0});
    fp.phase = Batteries::phase          (f, fp.op, sr, 256);
    return fp;
}
int CharacterizationRunner::writeArtifacts(const ModelFingerprint& fp, const std::string& dir) {
    juce::File d(juce::String(dir)); d.createDirectory();
    int rc=0;
    rc |= Artifacts::writeResponseCsv  (fp, d.getChildFile("response.csv").getFullPathName().toStdString());
    rc |= Artifacts::writeResonanceCsv (fp, d.getChildFile("resonance.csv").getFullPathName().toStdString());
    rc |= Artifacts::writeDistortionCsv(fp, d.getChildFile("distortion.csv").getFullPathName().toStdString());
    rc |= Artifacts::writeSummaryJson  (fp, d.getChildFile("summary.json").getFullPathName().toStdString());
    return rc;
}
std::string CharacterizationRunner::humanSummary(const ModelFingerprint& fp) {
    juce::String s; s << fp.model.c_str() << ": corner " << juce::String(fp.resp.cornerHz,0) << " Hz, slope "
      << juce::String(fp.resp.slopeDbPerOct,1) << " dB/oct, peak " << juce::String(fp.reso.peakDb,1)
      << " dB, self-osc pts " << (int) fp.reso.pitchTrack.size()
      << ", THD@unity " << juce::String(fp.dist.thdAtUnity*100.0,2) << "%";
    return s.toStdString();
}
}
```

`Artifacts.cpp` (CSV rows carry the operating-point prefix; `summary.json` is hand-emitted, no JSON dep):

```cpp
#include "Artifacts.h"
#include "CharacterizationRunner.h"
#include <juce_core/juce_core.h>

namespace testdsp {
static juce::String opPrefix(const ModelFingerprint& fp) {
    const char* osm = fp.op.osMode==0 ? "live":"render";
    return juce::String(fp.model) + "," + juce::String(fp.op.mode) + ","
         + juce::String(fp.op.osFactor) + "," + osm + ","
         + juce::String(fp.op.hostSampleRate,0) + ",";
}
int Artifacts::writeResponseCsv(const ModelFingerprint& fp, const std::string& path) {
    juce::String csv="model,mode,osFactor,osMode,hostSR,probeHz,magDb,phaseRad,groupDelaySec\n";
    for (size_t i=0;i<fp.resp.magCurve.size();++i){ const auto& mc=fp.resp.magCurve[i];
        double ph=0,gd=0; if (i<fp.phase.phaseCurve.size()){ ph=fp.phase.phaseCurve[i].second; gd=fp.phase.groupDelayCurve[i].second; }
        csv<<opPrefix(fp)<<juce::String(mc.first,2)<<","<<juce::String(mc.second,3)<<","<<juce::String(ph,4)<<","<<juce::String(gd,8)<<"\n"; }
    return juce::File(juce::String(path)).replaceWithText(csv) ? 0 : 1;
}
int Artifacts::writeResonanceCsv(const ModelFingerprint& fp, const std::string& path) {
    juce::String csv="model,mode,osFactor,osMode,hostSR,cutoffHz,measuredHz\n";
    for (auto& p:fp.reso.pitchTrack) csv<<opPrefix(fp)<<juce::String(p.first,2)<<","<<juce::String(p.second,2)<<"\n";
    return juce::File(juce::String(path)).replaceWithText(csv) ? 0 : 1;
}
int Artifacts::writeDistortionCsv(const ModelFingerprint& fp, const std::string& path) {
    juce::String csv="model,mode,osFactor,osMode,hostSR,toneHz,thd,aliasingDb\n";
    for (size_t i=0;i<fp.dist.thdVsFreq.size();++i){ double al=i<fp.dist.aliasingVsTone.size()?fp.dist.aliasingVsTone[i].second:0;
        csv<<opPrefix(fp)<<juce::String(fp.dist.thdVsFreq[i].first,2)<<","<<juce::String(fp.dist.thdVsFreq[i].second,5)<<","<<juce::String(al,2)<<"\n"; }
    return juce::File(juce::String(path)).replaceWithText(csv) ? 0 : 1;
}
int Artifacts::writeSummaryJson(const ModelFingerprint& fp, const std::string& path) {
    juce::String j; j<<"{\n  \"model\": \""<<fp.model.c_str()<<"\",\n"
      <<"  \"cornerHz\": "<<juce::String(fp.resp.cornerHz,1)<<",\n"
      <<"  \"slopeDbPerOct\": "<<juce::String(fp.resp.slopeDbPerOct,2)<<",\n"
      <<"  \"peakDb\": "<<juce::String(fp.reso.peakDb,2)<<",\n"
      <<"  \"q\": "<<juce::String(fp.reso.q,2)<<",\n"
      <<"  \"thdAtUnity\": "<<juce::String(fp.dist.thdAtUnity,5)<<",\n"
      <<"  \"selfOscPoints\": "<<(int)fp.reso.pitchTrack.size()<<"\n}\n";
    return juce::File(juce::String(path)).replaceWithText(j) ? 0 : 1;
}
}
```

- [ ] **Step 4: Run the unit test to verify it passes** — `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | grep -E "CharacterizationRunner|Summary"`. Expected PASS. (Add the new `.cpp` files to the `k2000_tests` source list too, so the unit test links them.)

- [ ] **Step 5: Write `CharacterizationMain.cpp` + the executable target**

`tests/CharacterizationMain.cpp`:

```cpp
#include "characterization/CharacterizationRunner.h"
#include "characterization/FilterUnderTest.h"
#include "../src/dsp/spine/MoogLadder.h"
#include <juce_core/juce_core.h>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    std::string model = "all";
    for (int i=1;i<argc;++i){ std::string a=argv[i]; if (a=="--model" && i+1<argc) model=argv[++i]; }
    const double sr=48000.0; int rc=0;
    auto outDir=[](const std::string& m){ return juce::File::getCurrentWorkingDirectory()
        .getChildFile("build/characterization").getChildFile(juce::String(m)); };

    auto runMoog=[&](){
        MoogLadder m; m.prepare(sr);
        testdsp::FilterUnderTest fut(m,[&](int md){m.setMode((MoogLadder::Mode)md);},
                                        [&](int sl){m.setSlope((MoogLadder::Slope)sl);});
        fut.prepare(sr);
        testdsp::CharacterizationRunner runner; auto fp=runner.run(fut,"moog",sr);
        auto d=outDir("moog"); d.createDirectory();
        runner.writeArtifacts(fp,d.getFullPathName().toStdString());
        std::cout << runner.humanSummary(fp) << "\n";
        // spec-gate sanity for exit code
        if (!(fp.resp.slopeDbPerOct<-18.0 && fp.resp.slopeDbPerOct>-30.0)) rc=1;
    };
    if (model=="moog"||model=="all") runMoog();
    // Huggett wired in Task 7.
    std::cout << (rc==0 ? "CHARACTERIZATION OK\n" : "CHARACTERIZATION FAIL\n");
    return rc;
}
```

In `tests/CMakeLists.txt`, after the `k2000_tests` target, add:

```cmake
add_executable(k2000_filter_characterization
    CharacterizationMain.cpp
    characterization/CharacterizationRunner.cpp
    characterization/Artifacts.cpp
    ../src/dsp/spine/HuggettFilter.cpp
    ../src/dsp/spine/cmajor/MoogLadderAdapter.cpp
    ../src/dsp/spine/MoogLadder.cpp)
target_include_directories(k2000_filter_characterization PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(k2000_filter_characterization PRIVATE
    juce::juce_core juce::juce_audio_basics juce::juce_dsp
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
target_compile_definitions(k2000_filter_characterization PRIVATE JUCE_STANDALONE_APPLICATION=1 K2000_TESTING=1)
set_target_properties(k2000_filter_characterization PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
# deliberately NOT registered via add_test — opt-in only.
```

- [ ] **Step 6: Build + run the executable**

Run: `cmake --build build --target k2000_filter_characterization -j4 && ./build/tests/k2000_filter_characterization --model moog 2>&1 | tail -3`
Expected: a one-line summary, `CHARACTERIZATION OK`, exit 0; `build/characterization/moog/{response,resonance,distortion}.csv` + `summary.json` exist.

- [ ] **Step 7: Commit**

```bash
git add tests/characterization/CharacterizationRunner.* tests/characterization/Artifacts.* tests/CharacterizationMain.cpp tests/CMakeLists.txt tests/TestDspSelfTests.cpp
git commit -m "test(harness): runner + artifacts + opt-in k2000_filter_characterization (Moog), self-sufficient

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Phase 3 — Huggett back-test (model-agnosticism gate)

### Task 7: Drive Huggett through the same harness

Point the runner at the existing `HuggettFilter` as soon as the runner works — *before* gates/golden/skill — to surface any Moog-shaped assumptions in `FilterUnderTest`/`Batteries`.

**Files:**
- Modify: `tests/CharacterizationMain.cpp` (add the Huggett branch)
- Test: `tests/TestDspSelfTests.cpp` (Huggett sanity)
- Read first: `src/dsp/spine/HuggettFilter.h` (its mode/slope/separation API may differ from Moog).

**Interfaces:**
- Consumes: `HuggettFilter` (a `FilterModel`), `FilterUnderTest`, `CharacterizationRunner`.
- Produces: a `huggett` fingerprint + artifacts; any abstraction fix lands in `FilterUnderTest.h` (kept model-agnostic).

- [ ] **Step 1: Write the failing test — Huggett runs through FilterUnderTest**

```cpp
#include "../src/dsp/spine/HuggettFilter.h"

beginTest("FilterUnderTest is model-agnostic: drives HuggettFilter, produces a sane fingerprint");
{
    const double sr=48000.0; HuggettFilter h; h.prepare(sr);
    // wire Huggett's mode/slope setters per its header (adjust casts to match HuggettFilter.h):
    testdsp::FilterUnderTest fut(h,
        /*setMode*/ [&](int md){ h.setMode((HuggettFilter::Mode) md); },
        /*setSlope*/ nullptr);
    fut.prepare(sr);
    testdsp::CharacterizationRunner runner; auto fp=runner.run(fut,"huggett",sr);
    expect(!fp.resp.magCurve.empty(), "no Huggett response");
    expect(fp.resp.cornerHz>50.0 && fp.resp.cornerHz<20000.0, "Huggett corner implausible: "+juce::String(fp.resp.cornerHz));
}
```

- [ ] **Step 2: Run to verify it fails / compiles** — adapt the lambda to Huggett's actual mode enum from `HuggettFilter.h`. If `FilterUnderTest` has any Moog-specific assumption, this is where it surfaces — fix it in `FilterUnderTest.h` (keep it generic).

- [ ] **Step 3: Add the Huggett branch to `CharacterizationMain.cpp`**

```cpp
auto runHuggett=[&](){
    HuggettFilter h; h.prepare(sr);
    testdsp::FilterUnderTest fut(h,[&](int md){h.setMode((HuggettFilter::Mode)md);}, nullptr);
    fut.prepare(sr);
    testdsp::CharacterizationRunner runner; auto fp=runner.run(fut,"huggett",sr);
    auto d=outDir("huggett"); d.createDirectory();
    runner.writeArtifacts(fp,d.getFullPathName().toStdString());
    std::cout << runner.humanSummary(fp) << "\n";
};
if (model=="huggett"||model=="all") runHuggett();
```

Add `#include "../src/dsp/spine/HuggettFilter.h"` and ensure `HuggettFilter.cpp` (+ its deps `HuggettHpStage.cpp`, any Cmajor adapters) are in the executable's source list.

- [ ] **Step 4: Build + run both models** — `cmake --build build --target k2000_filter_characterization -j4 && ./build/tests/k2000_filter_characterization --model all 2>&1 | tail -4`. Expect a Moog line, a Huggett line, `CHARACTERIZATION OK`. Run the unit suite too (`k2000_tests`) — green.

- [ ] **Step 5: Commit**

```bash
git add tests/CharacterizationMain.cpp tests/characterization/FilterUnderTest.h tests/TestDspSelfTests.cpp tests/CMakeLists.txt
git commit -m "test(harness): Huggett back-test — prove FilterUnderTest is model-agnostic

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Phase 4 — Spec-gate regression subset + self-golden baselines (CI)

### Task 8: Fast spec-gate regression tests in `k2000_tests`

A small, fast subset that runs on every build: per-model analytic assertions (corner, slope, self-osc tracking, bounded output, aliasing below threshold), using the same batteries on a tiny grid.

**Files:**
- Create: `tests/FilterCharacterizationGateTests.cpp`
- Modify: `tests/CMakeLists.txt` (add to `k2000_tests` source list)

**Interfaces:**
- Consumes: `Batteries`, `FilterUnderTest`, `MoogLadder`, `HuggettFilter`.

- [ ] **Step 1: Write the gate tests**

```cpp
#include <juce_core/juce_core.h>
#include "characterization/Batteries.h"
#include "characterization/FilterUnderTest.h"
#include "../src/dsp/spine/MoogLadder.h"

struct FilterCharacterizationGateTests : juce::UnitTest {
    FilterCharacterizationGateTests() : juce::UnitTest("FilterCharacterizationGate") {}
    void runTest() override {
        const double sr=48000.0;
        beginTest("Moog gate: corner ~set, 24 dB/oct, self-osc tracks <=3% <=4kHz");
        MoogLadder m; m.prepare(sr);
        testdsp::FilterUnderTest fut(m,[&](int md){m.setMode((MoogLadder::Mode)md);},
                                        [&](int sl){m.setSlope((MoogLadder::Slope)sl);});
        fut.prepare(sr);
        testdsp::OperatingPoint op; op.cutoffHz=1000; op.slope=1; op.mode=0;
        auto r=testdsp::Batteries::linearResponse(fut,op,sr,256);
        expect(std::abs(r.cornerHz-1000.0)<200.0, "corner: "+juce::String(r.cornerHz,0));
        expect(r.slopeDbPerOct<-18.0 && r.slopeDbPerOct>-30.0, "slope: "+juce::String(r.slopeDbPerOct,1));
        auto rr=testdsp::Batteries::resonance(fut,op,sr,{220.0,880.0});
        for (auto& p:rr.pitchTrack)
            expect(std::abs(p.second-p.first)/p.first < 0.03, "self-osc >3% @"+juce::String(p.first)+": "+juce::String(p.second,1));
    }
};
static FilterCharacterizationGateTests filterCharacterizationGateTestsInstance;
```

Add `FilterCharacterizationGateTests.cpp` + the `characterization/*.cpp` to the `k2000_tests` source list in `tests/CMakeLists.txt` (if not already there from Task 6).

- [ ] **Step 2: Run — expect PASS** — `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | grep -E "FilterCharacterizationGate|Summary"`. These reuse the proven batteries, so they pass; if a gate fails it's a real model issue — investigate, don't loosen.

- [ ] **Step 3: Commit**

```bash
git add tests/FilterCharacterizationGateTests.cpp tests/CMakeLists.txt
git commit -m "test(harness): fast per-model spec-gate regression subset in k2000_tests

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 9: Self-golden baseline — read/write compact `baseline.json` + FP-tolerant drift check

Commit a compact fingerprint snapshot per model; a coarse drift check runs in `k2000_tests` (tolerant of small floating-point variance).

**Files:**
- Create: `tests/characterization/Baseline.h`, `tests/characterization/Baseline.cpp`
- Create: `tests/golden/moog/baseline.json`, `tests/golden/huggett/baseline.json` (generated then committed)
- Modify: `tests/FilterCharacterizationGateTests.cpp` (add the drift check), `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `struct testdsp::Baseline { static juce::var toVar(const ModelFingerprint&); static bool compare(const ModelFingerprint&, const juce::var& baseline, juce::String& report, double tolDb, double tolPct); };` — compares headline metrics (cornerHz within `tolPct`, slope within `tolDb`, peakDb within `tolDb`, self-osc points within `tolPct`) with tolerance.

- [ ] **Step 1: Write the failing test — a fingerprint matches its own freshly-written baseline**

```cpp
#include "characterization/Baseline.h"
#include "characterization/CharacterizationRunner.h"

beginTest("Self-golden: a Moog fingerprint matches its own baseline within tolerance");
{
    const double sr=48000.0; MoogLadder m; m.prepare(sr);
    testdsp::FilterUnderTest fut(m,[&](int md){m.setMode((MoogLadder::Mode)md);},
                                    [&](int sl){m.setSlope((MoogLadder::Slope)sl);});
    fut.prepare(sr);
    testdsp::CharacterizationRunner runner; auto fp=runner.run(fut,"moog",sr);
    auto v = testdsp::Baseline::toVar(fp);
    juce::String report;
    expect(testdsp::Baseline::compare(fp, v, report, /*tolDb*/1.0, /*tolPct*/0.05),
           "fingerprint should match its own baseline: " + report);
}
```

- [ ] **Step 2: Run to verify it fails** — missing `Baseline.h`.

- [ ] **Step 3: Implement `Baseline.{h,cpp}`** (use `juce::JSON` / `juce::var`)

```cpp
// Baseline.h
#pragma once
#include "CharacterizationRunner.h"
#include <juce_core/juce_core.h>
namespace testdsp { struct Baseline {
    static juce::var toVar(const ModelFingerprint& fp);
    static bool compare(const ModelFingerprint& fp, const juce::var& base, juce::String& report,
                        double tolDb, double tolPct);
}; }
```

```cpp
// Baseline.cpp
#include "Baseline.h"
namespace testdsp {
juce::var Baseline::toVar(const ModelFingerprint& fp){ auto* o=new juce::DynamicObject();
    o->setProperty("model", juce::String(fp.model));
    o->setProperty("cornerHz", fp.resp.cornerHz);
    o->setProperty("slopeDbPerOct", fp.resp.slopeDbPerOct);
    o->setProperty("peakDb", fp.reso.peakDb);
    o->setProperty("selfOscPoints", (int) fp.reso.pitchTrack.size());
    o->setProperty("thdAtUnity", fp.dist.thdAtUnity);
    return juce::var(o); }
bool Baseline::compare(const ModelFingerprint& fp, const juce::var& b, juce::String& rep, double tolDb, double tolPct){
    bool ok=true;
    auto near=[&](const char* k, double got, bool pct){ double want=(double)b[k];
        double err = pct ? std::abs(got-want)/std::max(1e-9,std::abs(want)) : std::abs(got-want);
        double tol = pct ? tolPct : tolDb;
        if (err>tol){ ok=false; rep<<k<<" drift: got "<<juce::String(got,3)<<" want "<<juce::String(want,3)<<"; "; } };
    near("cornerHz", fp.resp.cornerHz, true);
    near("slopeDbPerOct", fp.resp.slopeDbPerOct, false);
    near("peakDb", fp.reso.peakDb, false);
    return ok; }
}
```

- [ ] **Step 4: Run to verify it passes** — `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | grep -E "Self-golden|Summary"`. Expected PASS.

- [ ] **Step 5: Generate + commit the baselines, wire the drift check**

Generate the committed baselines from the executable (add a `--write-baseline` flag to `CharacterizationMain.cpp` that writes `tests/golden/<model>/baseline.json` via `juce::JSON::toString(Baseline::toVar(fp))`), run it for moog + huggett, and add a drift check to `FilterCharacterizationGateTests.cpp`:

```cpp
beginTest("Self-golden drift: Moog matches committed baseline.json");
{
    juce::File bf=juce::File(BERNIE_GOLDEN_DIR).getChildFile("moog/baseline.json");
    if (!bf.existsAsFile()) { logMessage("no moog/baseline.json — skipping (generate with --write-baseline)"); }
    else {
        const double sr=48000.0; MoogLadder m; m.prepare(sr);
        testdsp::FilterUnderTest fut(m,[&](int md){m.setMode((MoogLadder::Mode)md);},
                                        [&](int sl){m.setSlope((MoogLadder::Slope)sl);});
        fut.prepare(sr);
        testdsp::CharacterizationRunner runner; auto fp=runner.run(fut,"moog",sr);
        juce::var base=juce::JSON::parse(bf.loadFileAsString()); juce::String rep;
        expect(testdsp::Baseline::compare(fp,base,rep,1.5,0.06), "Moog drifted from baseline: "+rep);
    }
}
```

Run: `cmake --build build --target k2000_filter_characterization -j4 && ./build/tests/k2000_filter_characterization --model all --write-baseline` then `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | grep -E "Self-golden|Summary"`. Expected: baselines exist + drift check passes.

- [ ] **Step 6: Commit**

```bash
git add tests/characterization/Baseline.* tests/golden/moog/baseline.json tests/golden/huggett/baseline.json tests/FilterCharacterizationGateTests.cpp tests/CharacterizationMain.cpp tests/CMakeLists.txt tests/TestDspSelfTests.cpp
git commit -m "test(harness): self-golden baselines (compact, FP-tolerant) + committed Moog/Huggett snapshots

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Phase 5 — Arturia-compare layer (dormant) + golden format

### Task 10: Arturia golden comparison (feature-based, dormant) + `selfosc.csv` importer

Compare a fingerprint against captured Arturia data on characteristic features within a CALIB tolerance block; skip-with-warning when absent; add the `selfosc.csv` importer the spec calls for.

**Files:**
- Create: `tests/characterization/GoldenCompare.h`, `tests/characterization/GoldenCompare.cpp`
- Modify: `tests/MoogLadderTests.cpp` (replace the inline ad-hoc golden block with a call into `GoldenCompare`); `tests/golden/moog/README.md` (extend); `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `struct testdsp::GoldenCompare { struct Tol { double cornerCents=200, slopeDb=4, peakDb=4, selfOscPct=0.05, shapeDb=6; }; static bool compareResponse(const ModelFingerprint&, const juce::File& responseCsv, const Tol&, juce::StringArray& issues); static bool compareSelfOsc(const ModelFingerprint&, const juce::File& selfoscCsv, const Tol&, juce::StringArray& issues); };` — gain-normalises before shape comparison; returns true when within tolerance OR file absent (logs the skip).

- [ ] **Step 1: Write the failing test — dormant when absent, passes a synthetic match**

```cpp
#include "characterization/GoldenCompare.h"

beginTest("GoldenCompare: dormant (logs skip) when Arturia file absent; matches a synthetic self-match");
{
    const double sr=48000.0; MoogLadder m; m.prepare(sr);
    testdsp::FilterUnderTest fut(m,[&](int md){m.setMode((MoogLadder::Mode)md);},
                                    [&](int sl){m.setSlope((MoogLadder::Slope)sl);});
    fut.prepare(sr);
    testdsp::CharacterizationRunner runner; auto fp=runner.run(fut,"moog",sr);
    testdsp::GoldenCompare::Tol tol; juce::StringArray issues;
    // absent file -> dormant true (no failure)
    juce::File absent("/nonexistent/response.csv");
    expect(testdsp::GoldenCompare::compareResponse(fp,absent,tol,issues), "absent golden should be dormant-pass");
    // write a synthetic selfosc.csv from the model's own pitch track -> must match itself
    auto tmp=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("k2000_golden.csv");
    juce::String csv="cutoffHz,measuredHz\n"; for (auto& p:fp.reso.pitchTrack) csv<<juce::String(p.first,2)<<","<<juce::String(p.second,2)<<"\n";
    tmp.replaceWithText(csv); issues.clear();
    expect(testdsp::GoldenCompare::compareSelfOsc(fp,tmp,tol,issues), "self-match selfosc should pass: "+issues.joinIntoString("; "));
    tmp.deleteFile();
}
```

- [ ] **Step 2: Run to verify it fails** — missing `GoldenCompare.h`.

- [ ] **Step 3: Implement `GoldenCompare.{h,cpp}`** (malformed rows → logged via the returned `issues`, not silent)

```cpp
// GoldenCompare.h
#pragma once
#include "CharacterizationRunner.h"
#include <juce_core/juce_core.h>
namespace testdsp { struct GoldenCompare {
    struct Tol { double cornerCents=200, slopeDb=4, peakDb=4, selfOscPct=0.05, shapeDb=6; };
    static bool compareResponse(const ModelFingerprint&, const juce::File&, const Tol&, juce::StringArray& issues);
    static bool compareSelfOsc (const ModelFingerprint&, const juce::File&, const Tol&, juce::StringArray& issues);
}; }
```

```cpp
// GoldenCompare.cpp
#include "GoldenCompare.h"
#include <algorithm>
#include <vector>
namespace testdsp {
static bool numeric(const juce::String& s){ return s.containsOnly("0123456789.eE+-") && s.isNotEmpty(); }
bool GoldenCompare::compareSelfOsc(const ModelFingerprint& fp, const juce::File& f, const Tol& tol, juce::StringArray& issues){
    if (!f.existsAsFile()){ issues.add("dormant: "+f.getFileName()+" absent"); return true; }
    auto track=fp.reso.pitchTrack;
    for (auto& line : juce::StringArray::fromLines(f.loadFileAsString())){
        auto c=juce::StringArray::fromTokens(line,",","");
        if (c.size()<2) continue;
        if (!numeric(c[0])){ if (!line.startsWith("cutoff")) issues.add("skipped malformed row: "+line); continue; }
        const double fc=c[0].getDoubleValue(), want=c[1].getDoubleValue();
        double best=1e9; for (auto& p:track) best=std::min(best,std::abs(p.first-fc));
        // find our measured pitch at nearest cutoff
        double got=0,bd=1e9; for (auto& p:track) if (std::abs(p.first-fc)<bd){bd=std::abs(p.first-fc); got=p.second;}
        if (std::abs(got-want)/std::max(1.0,want) > tol.selfOscPct)
            issues.add("selfosc @"+juce::String(fc)+": got "+juce::String(got,1)+" want "+juce::String(want,1));
    }
    return issues.isEmpty() || issues[0].startsWith("dormant");
}
bool GoldenCompare::compareResponse(const ModelFingerprint& fp, const juce::File& f, const Tol& tol, juce::StringArray& issues){
    if (!f.existsAsFile()){ issues.add("dormant: "+f.getFileName()+" absent"); return true; }
    // interpolate our magnitude curve (Hz,dB)
    auto ourDb=[&](double hz)->double{ const auto& c=fp.resp.magCurve; if (c.empty()) return 0.0;
        if (hz<=c.front().first) return c.front().second; if (hz>=c.back().first) return c.back().second;
        for (size_t i=1;i<c.size();++i) if (hz<=c[i].first){ const double t=(hz-c[i-1].first)/(c[i].first-c[i-1].first);
            return c[i-1].second*(1.0-t)+c[i].second*t; } return c.back().second; };
    // collect golden rows (cutoffHz,resonance,probeHz,magDb) matching our operating point
    std::vector<std::pair<double,double>> want;   // (probeHz, magDb)
    for (auto& line : juce::StringArray::fromLines(f.loadFileAsString())){
        auto c=juce::StringArray::fromTokens(line,",","");
        if (c.size()<4) continue;
        if (!numeric(c[0])){ if (!line.startsWith("cutoff")) issues.add("skipped malformed row: "+line); continue; }
        const double fc=c[0].getDoubleValue(), res=c[1].getDoubleValue(), pr=c[2].getDoubleValue(), db=c[3].getDoubleValue();
        if (std::abs(fc-fp.op.cutoffHz)<1.0 && std::abs(res-fp.op.resonance)<0.01) want.push_back({pr,db});
    }
    if (want.empty()){ issues.add("no golden rows match op (fc="+juce::String(fp.op.cutoffHz)+",res="+juce::String(fp.op.resonance)+")"); return false; }
    std::sort(want.begin(),want.end());
    // gain-normalise: offset = mean(our - want) over the lowest few probes (passband)
    double offset=0; const int nN=std::min(3,(int)want.size());
    for (int i=0;i<nN;++i) offset += ourDb(want[(size_t)i].first)-want[(size_t)i].second;
    offset/=std::max(1,nN);
    for (auto& w:want){ const double got=ourDb(w.first)-offset;
        if (std::abs(got-w.second)>tol.shapeDb)
            issues.add("response @"+juce::String(w.first,0)+"Hz: got "+juce::String(got,1)+" want "+juce::String(w.second,1)+" dB"); }
    return issues.isEmpty();
}
}
```

- [ ] **Step 4: Run to verify it passes** — `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | grep -E "GoldenCompare|Summary"`. Expected PASS.

- [ ] **Step 5: Replace the old inline golden block + extend the README**

In `tests/MoogLadderTests.cpp`, replace the existing inline `"Arturia golden match"` block with a call into `GoldenCompare::compareResponse/compareSelfOsc` against `BERNIE_GOLDEN_DIR/moog/arturia/{response,selfosc}.csv` (logging the dormant skip). Update `tests/golden/moog/README.md` to document the `arturia/` subfolder layout. Run the suite — still green (dormant).

- [ ] **Step 6: Commit**

```bash
git add tests/characterization/GoldenCompare.* tests/MoogLadderTests.cpp tests/golden/moog/README.md tests/CMakeLists.txt tests/TestDspSelfTests.cpp
git commit -m "test(harness): Arturia golden compare (feature-based, dormant) + selfosc importer

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Phase 6 — the `/characterize-filter` skill

### Task 11: The skill (interactive front door over the self-sufficient binary)

A project skill that builds + runs the executable, reads the artifacts, and reports — offering (never forcing) a baseline re-bless.

**Files:**
- Create: `.claude/skills/characterize-filter/SKILL.md`

**Interfaces:** none in code — the skill shells out to `cmake --build … -j4` + `./build/tests/k2000_filter_characterization` and reads `build/characterization/<model>/summary.json`.

- [ ] **Step 1: Write `.claude/skills/characterize-filter/SKILL.md`**

```markdown
---
name: characterize-filter
description: Build + run the opt-in filter-characterization harness for a model (or all), read the artifacts, and report headline metrics, spec-gate pass/fail, self-golden drift, and Arturia verdict. Offers (never forces) a baseline re-bless.
---

# Characterize Filter

Arguments: `<model>` — `moog`, `huggett`, or `all` (default `all`).

## Steps
1. Build the opt-in target: `cmake --build build --target k2000_filter_characterization -j4` (ALWAYS -j4).
2. Run it: `./build/tests/k2000_filter_characterization --model <model>`. Capture stdout + the exit code.
3. Read `build/characterization/<model>/summary.json` for each requested model.
4. Compare against `tests/golden/<model>/baseline.json` (the committed self-golden). If headline metrics drifted beyond tolerance, REPORT what moved (metric, old → new) and ASK whether to re-bless the baseline (`--write-baseline`). NEVER auto-update.
5. If `tests/golden/<model>/arturia/response.csv` exists, report the Arturia verdict; else note "no Arturia data — dormant".
6. Present a concise report: per-model corner / slope / peak / Q / self-osc tracking / THD / aliasing, spec-gate pass/fail (exit code), self-golden status, Arturia status.

## Boundary
The executable is self-sufficient (it prints its own summary + returns an exit code; CI calls it directly). This skill only adds interactive interpretation + the re-bless offer. Do not put measurement logic here.
```

- [ ] **Step 2: Verify the skill is discoverable** — confirm `.claude/skills/characterize-filter/SKILL.md` parses (frontmatter `name`/`description`). (Invocation is manual by the user; no automated test.)

- [ ] **Step 3: Commit**

```bash
git add .claude/skills/characterize-filter/SKILL.md
git commit -m "feat(harness): /characterize-filter skill (interactive front door over the self-sufficient binary)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Notes for the implementer

- **The L0 self-tests are the ground truth.** When an ESS/transfer-function/harmonics measurement is off, the bug is in the measurement code — fix it against the analytic oracle, never loosen the oracle tolerance.
- **Iterate measurement constants, not test bounds.** Sweep length, small-signal amplitude, window/crop choices, and bin-guard widths are the tuning knobs (like `// CALIB` in the DSP). The analytic oracles and per-model gates define "correct."
- **`-j4` always.** Bare `-j` OOMs the JUCE build.
- **Keep `FilterUnderTest` model-agnostic.** Task 7 (Huggett) is the gate for this — if you reach for a Moog-specific field, that's the signal the abstraction is wrong; fix the socket, not the call site.
- **OS axes stay dormant.** `osFactor`/`osMode`/`hostSampleRate` are recorded in `OperatingPoint` + the artifact columns but always `{1,live,48000}` until the separate oversampling effort lands.
- **Reference code is a starting point.** The implementations here are correct in structure; the TDD oracle is what proves them. Where a reference impl has a `// TODO-by-measurement` (e.g. `selfOscOnset`), it is reported-not-gated until the calibration pass.
- **Shelved after writing.** Per spec §15, this plan is parked; the next implementation effort is Moog Spec 2. Each phase is independently shippable when work resumes.
