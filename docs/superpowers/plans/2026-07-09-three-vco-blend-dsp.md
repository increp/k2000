# Three-VCO Blend Oscillator — DSP/Backend Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give each voice three independent, blend-capable oscillators (sine/triangle/saw/pulse, proportionally mixed, with per-pulse duty cycle) instead of one single-waveform oscillator — fully working and unit-tested, with zero GUI changes. The old panel keeps working exactly as it does today throughout this plan; it just won't expose the new controls yet.

**Architecture:** Four sequential tasks, each leaving the codebase compiling and the full suite green, because each task's output is a real dependency of the next: (1) redesign `Oscillator` to blend four shapes instead of selecting one, adapting `Voice`'s existing single-oscillator call site just enough to keep behavior identical; (2) add the new per-VCO and Mixer params *alongside* the old ones (purely additive, nothing removed yet); (3) change `Voice` from one oscillator to three, consuming the new params; (4) retire the now-dead old param surface, which is the point where every file that still references it needs a small mechanical update.

**Tech Stack:** C++17, JUCE (`juce_audio_processors` for APVTS), CMake, JUCE's built-in `UnitTest` framework.

## Global Constraints

- Build with `-j4` always — bare `-j` OOMs the JUCE compile.
- This plan is DSP/backend-only. No file under `src/PluginEditor.*` gets a new
  control, layout change, or panel-visible behavior change — the one
  exception (Task 4's last step) is strictly a compile-keeping fix, not a
  feature, and is called out explicitly there.
- Internal storage for all four blend weights and all three Mixer levels is a
  continuous `0.0f–1.0f` `float` (matching this codebase's existing
  `shaperMix` param, `NormalisableRange<float>{0.0f, 1.0f, 0.0f}`) — the
  spec's "0–100%" language describes the on-screen percentage a future GUI
  slider will show, not the internal representation.
- Blend math is proportional, not additive: `effective_i = w_i / (wSine + wTri + wSaw + wPulse)`, with output `0.0f` when the four weights sum to `0`. This must never be a raw un-normalized sum.
- Every new param is per-layer (`kNumLayers = 2`, `src/params/Config.h`), following the exact `"layer" + String(layer) + "."` prefix pattern already in `src/params/Parameters.cpp`.
- Param ID strings use the `osc1`/`osc2`/`osc3` root (matching the existing `Oscillator` class name and `osc.*` param namespace) — never `vco1/2/3` in code or param IDs. Display names shown to the host/DAW use `"VCO1"/"VCO2"/"VCO3"` (the user-facing term) — this split is deliberate, not a typo.
- `oscWaveform`/`oscCoarse`/`oscFine` (the old single-oscillator params) are retired with no back-compat shim — this repo's standing decision is that preset compatibility is not a constraint.

---

## Task 1: Oscillator Blend Engine

**Files:**
- Modify: `src/dsp/Oscillator.h` (full rewrite of the public interface)
- Modify: `src/dsp/Oscillator.cpp` (full rewrite of `processSample()`)
- Modify: `src/Voice.cpp:83` (adapt the single existing call site to the new interface, preserving today's exact behavior)
- Modify: `tests/OscillatorTests.cpp` (adapt 5 existing tests to the new interface, add 4 new tests for the new behavior)

**Interfaces:**
- Consumes: nothing new.
- Produces: `Oscillator::setBlend(float sine, float tri, float saw, float pulse)` and `Oscillator::setPulseDuty(float duty)` — both public methods on `Oscillator`, used directly by `Voice` in this task and again (three times, once per VCO) in Task 3. `Oscillator::Waveform` and `Oscillator::setWaveform(Waveform)` no longer exist after this task — nothing outside `Oscillator`/`Voice` referenced them (confirmed via repo-wide grep during planning).

- [ ] **Step 1: Confirm the baseline — run the current Oscillator tests**

```bash
cmake --build build --target k2000_tests -j4
./build/tests/k2000_tests | tee build/last-test-run.log | tail -3
```

Expected: `Summary: 292 tests, 0 failed` (or whatever the current count is — the point is a clean baseline before this task's changes, not a specific number).

- [ ] **Step 2: Replace `src/dsp/Oscillator.h` in full**

```cpp
#pragma once

class Oscillator {
public:
    void prepare(double sampleRate);
    void reset();
    // Sets the four blend weights (each 0..1, typically). The four shapes are
    // evaluated at one shared phase and combined proportionally -- see
    // processSample() for the exact math. A weight of exactly 0 skips that
    // shape's computation entirely (cheap: no trig/polyBLEP call).
    void setBlend(float sine, float tri, float saw, float pulse);
    // Duty cycle for the Pulse component only, in (0, 1) -- e.g. 0.5 is a
    // standard square. Does NOT affect Triangle, which derives from its own
    // internal fixed-50%-duty square regardless of this value.
    void setPulseDuty(float duty);
    void setFrequency(float hz);

    float processSample();
    void processBlock(float* buffer, int numSamples);

private:
    double sampleRate_ = 44100.0;
    double phase_ = 0.0;      // 0..1
    double phaseInc_ = 0.0;   // freq / sampleRate
    float frequency_ = 0.0f;
    float blendSine_ = 0.0f, blendTri_ = 0.0f, blendSaw_ = 1.0f, blendPulse_ = 0.0f;
    float pulseDuty_ = 0.5f;
    double leakyInt_ = 0.0;   // triangle integrator state
};
```

- [ ] **Step 3: Replace `src/dsp/Oscillator.cpp` in full**

```cpp
#include "Oscillator.h"
#include <cmath>

namespace {
    constexpr double kTwoPi = 6.283185307179586;

    // Standard polyBLEP correction for a discontinuity AT PHASE 0. t is the
    // phase in [0,1), dt is phaseInc. To correct a discontinuity at some
    // other phase X, call polyBLEP(fmod(t - X + 1.0, 1.0), dt) instead --
    // this shifts phase so X maps to 0 from this function's point of view.
    inline double polyBLEP(double t, double dt) {
        if (t < dt) {
            t /= dt;
            return t + t - t * t - 1.0;
        } else if (t > 1.0 - dt) {
            t = (t - 1.0) / dt;
            return t * t + t + t + 1.0;
        }
        return 0.0;
    }
}

void Oscillator::prepare(double sr) {
    sampleRate_ = sr;
    phase_ = 0.0;
    phaseInc_ = double(frequency_) / sampleRate_;
    leakyInt_ = 0.0;
}

void Oscillator::reset() {
    phase_ = 0.0;
    leakyInt_ = 0.0;
}

void Oscillator::setBlend(float sine, float tri, float saw, float pulse) {
    blendSine_ = sine; blendTri_ = tri; blendSaw_ = saw; blendPulse_ = pulse;
}

void Oscillator::setPulseDuty(float duty) { pulseDuty_ = duty; }

void Oscillator::setFrequency(float hz) {
    frequency_ = hz;
    phaseInc_ = double(hz) / sampleRate_;
}

float Oscillator::processSample() {
    // Guard against degenerate / negative freq
    if (phaseInc_ <= 0.0) return 0.0f;

    const double dt = phaseInc_;
    const double t  = phase_;

    const double total = double(blendSine_) + double(blendTri_) + double(blendSaw_) + double(blendPulse_);
    double v = 0.0;

    if (total > 0.0) {
        if (blendSine_ != 0.0f) {
            v += blendSine_ * std::sin(kTwoPi * t);
        }
        if (blendTri_ != 0.0f) {
            // Integrate a polyBLEP-corrected FIXED 50%-duty square. This is
            // independent of pulseDuty_ -- Triangle always derives from its
            // own internal 50% square, never the Pulse component's duty.
            double sq = (t < 0.5) ? 1.0 : -1.0;
            sq += polyBLEP(t, dt);
            sq -= polyBLEP(std::fmod(t + 0.5, 1.0), dt);
            leakyInt_ = leakyInt_ * 0.999 + sq * 4.0 * dt;
            v += blendTri_ * leakyInt_;
        }
        if (blendSaw_ != 0.0f) {
            double saw = 2.0 * t - 1.0;
            saw -= polyBLEP(t, dt);
            v += blendSaw_ * saw;
        }
        if (blendPulse_ != 0.0f) {
            const double duty = double(pulseDuty_);
            double pulse = (t < duty) ? 1.0 : -1.0;
            pulse += polyBLEP(t, dt);
            pulse -= polyBLEP(std::fmod(t - duty + 1.0, 1.0), dt);
            v += blendPulse_ * pulse;
        }
        v /= total;
    }

    phase_ += dt;
    if (phase_ >= 1.0) phase_ -= 1.0;
    return float(v);
}

void Oscillator::processBlock(float* buf, int n) {
    for (int i = 0; i < n; ++i) buf[i] = processSample();
}
```

- [ ] **Step 4: Adapt `Voice.cpp`'s single call site to the new interface**

In `src/Voice.cpp`, inside `render()`, find:

```cpp
    osc_.setWaveform(static_cast<Oscillator::Waveform>(s.oscWaveform));
```

Replace with (this maps the OLD enum values `Saw=0, Square=1, Triangle=2, Sine=3` onto the new blend interface exactly, so this step alone changes nothing audible):

```cpp
    osc_.setBlend(s.oscWaveform == 3 ? 1.0f : 0.0f,   // sine
                  s.oscWaveform == 2 ? 1.0f : 0.0f,   // triangle
                  s.oscWaveform == 0 ? 1.0f : 0.0f,   // saw
                  s.oscWaveform == 1 ? 1.0f : 0.0f);  // pulse (old "Square" = 50% duty pulse)
    osc_.setPulseDuty(0.5f);
```

- [ ] **Step 5: Replace `tests/OscillatorTests.cpp` in full**

```cpp
#include <juce_core/juce_core.h>
#include "../src/dsp/Oscillator.h"
#include <vector>
#include <cmath>

class OscillatorTest : public juce::UnitTest {
public:
    OscillatorTest() : juce::UnitTest("Oscillator") {}

    static constexpr double SR = 48000.0;
    static constexpr int BLOCK = 512;

    void runTest() override {
        runSineFundamentalTest();
        runSawHasHarmonicsTest();
        runResetTest();
        runZeroFreqTest();
        runBlockContinuityTest();
        runBlendRatioTest();
        runZeroSumSilenceTest();
        runPulseDutyEdgeTest();
        runTriangleIndependentOfPulseDutyTest();
    }

private:
    static double rms(const std::vector<float>& buf) {
        double s = 0;
        for (float v : buf) s += double(v) * v;
        return std::sqrt(s / buf.size());
    }

    static double magAtFreq(const std::vector<float>& buf, int k) {
        const int N = (int) buf.size();
        double real = 0, imag = 0;
        for (int n = 0; n < N; ++n) {
            double ang = -2.0 * juce::MathConstants<double>::pi * k * n / N;
            real += buf[n] * std::cos(ang);
            imag += buf[n] * std::sin(ang);
        }
        return std::sqrt(real * real + imag * imag);
    }

    void runSineFundamentalTest() {
        beginTest("sine at 440Hz has FFT peak near bin 440 (1Hz res, 48k samples)");
        Oscillator osc;
        osc.prepare(SR);
        osc.setBlend(1.0f, 0.0f, 0.0f, 0.0f);
        osc.setFrequency(440.0f);

        const int N = 48000; // 1 Hz bin resolution
        std::vector<float> buf(N);
        for (int i = 0; i < N; ++i) buf[i] = osc.processSample();

        double bestMag = 0; int bestBin = 0;
        for (int k = 430; k <= 450; ++k) {
            double mag = magAtFreq(buf, k);
            if (mag > bestMag) { bestMag = mag; bestBin = k; }
        }
        expect(std::abs(bestBin - 440) <= 1,
            juce::String("expected peak near 440, got ") + juce::String(bestBin));
    }

    void runSawHasHarmonicsTest() {
        beginTest("saw at 1kHz has measurable energy at 2kHz, 3kHz harmonics");
        Oscillator osc;
        osc.prepare(SR);
        osc.setBlend(0.0f, 0.0f, 1.0f, 0.0f);
        osc.setFrequency(1000.0f);

        const int N = 48000;
        std::vector<float> buf(N);
        for (int i = 0; i < N; ++i) buf[i] = osc.processSample();

        double m1k = magAtFreq(buf, 1000);
        double m2k = magAtFreq(buf, 2000);
        double m3k = magAtFreq(buf, 3000);

        // Saw harmonics fall as 1/n, so m2k ≈ m1k/2, m3k ≈ m1k/3.
        expect(m1k > 0);
        expect(m2k > m1k * 0.2, "2kHz harmonic should be substantial");
        expect(m3k > m1k * 0.15, "3kHz harmonic should be substantial");
    }

    void runResetTest() {
        beginTest("reset returns phase to zero");
        Oscillator osc;
        osc.prepare(SR);
        osc.setBlend(1.0f, 0.0f, 0.0f, 0.0f);
        osc.setFrequency(440.0f);
        for (int i = 0; i < 100; ++i) osc.processSample();

        osc.reset();
        float s0 = osc.processSample();
        expectWithinAbsoluteError(s0, 0.0f, 1e-3f);
    }

    void runZeroFreqTest() {
        beginTest("zero frequency produces silence (or DC) without exploding");
        Oscillator osc;
        osc.prepare(SR);
        osc.setBlend(0.0f, 0.0f, 1.0f, 0.0f);
        osc.setFrequency(0.0f);
        std::vector<float> buf(BLOCK);
        for (int i = 0; i < BLOCK; ++i) buf[i] = osc.processSample();
        for (float v : buf) expect(std::abs(v) <= 1.5f, "output must be bounded");
    }

    void runBlockContinuityTest() {
        beginTest("two consecutive blocks produce continuous output (no glitch at boundary)");
        Oscillator osc;
        osc.prepare(SR);
        osc.setBlend(1.0f, 0.0f, 0.0f, 0.0f);
        osc.setFrequency(440.0f);
        std::vector<float> a(BLOCK), b(BLOCK);
        osc.processBlock(a.data(), BLOCK);
        osc.processBlock(b.data(), BLOCK);
        float delta = std::abs(b[0] - a[BLOCK - 1]);
        expectLessThan(delta, 0.1f);
    }

    void runBlendRatioTest() {
        beginTest("blend is proportional: (0.05, 0.19, 0, 0) sounds like a 5:19 sine:tri ratio, not quiet");
        // Two oscillators with the SAME ratio but different absolute weights
        // must produce the SAME waveform shape (same peak amplitude, same
        // sample-by-sample values after accounting for phase), because the
        // math divides by the weight total.
        Oscillator a, b;
        a.prepare(SR); b.prepare(SR);
        a.setBlend(0.05f, 0.19f, 0.0f, 0.0f);   // literal small weights, 5:19 ratio
        b.setBlend(0.5f, 1.9f, 0.0f, 0.0f);     // same ratio, 10x the absolute weights
        a.setFrequency(220.0f); b.setFrequency(220.0f);

        std::vector<float> bufA(BLOCK), bufB(BLOCK);
        a.processBlock(bufA.data(), BLOCK);
        b.processBlock(bufB.data(), BLOCK);
        for (int i = 0; i < BLOCK; ++i)
            expectWithinAbsoluteError(bufA[i], bufB[i], 1e-5f);

        // And a single full-weight sine (0,0,0,0 total contribution from tri/saw/pulse)
        // must reach a peak close to 1.0 -- proportional blending should not
        // silently attenuate a single-shape blend versus today's single-oscillator peak.
        Oscillator pureSine;
        pureSine.prepare(SR);
        pureSine.setBlend(1.0f, 0.0f, 0.0f, 0.0f);
        pureSine.setFrequency(220.0f);
        float peak = 0.0f;
        for (int i = 0; i < BLOCK; ++i) peak = std::max(peak, std::abs(pureSine.processSample()));
        expect(peak > 0.9f, "single full-weight sine should reach close to unity peak");
    }

    void runZeroSumSilenceTest() {
        beginTest("all four weights at zero produces silence, not NaN/inf");
        Oscillator osc;
        osc.prepare(SR);
        osc.setBlend(0.0f, 0.0f, 0.0f, 0.0f);
        osc.setFrequency(440.0f);
        std::vector<float> buf(BLOCK);
        osc.processBlock(buf.data(), BLOCK);
        for (float v : buf) {
            expect(std::isfinite(v), "zero-sum blend must not produce NaN/inf");
            expectWithinAbsoluteError(v, 0.0f, 1e-9f);
        }
    }

    void runPulseDutyEdgeTest() {
        beginTest("pulse duty cycle places the falling edge at the requested fraction of the period");
        Oscillator osc;
        osc.prepare(SR);
        osc.setBlend(0.0f, 0.0f, 0.0f, 1.0f);  // pure pulse
        osc.setPulseDuty(0.25f);
        osc.setFrequency(100.0f);              // 480 samples/cycle at 48kHz -- coarse enough to see the edge clearly

        const int periodSamples = (int) std::round(SR / 100.0);
        std::vector<float> buf(periodSamples);
        for (int i = 0; i < periodSamples; ++i) buf[i] = osc.processSample();

        // High for roughly the first 25% of the period, low for the rest
        // (polyBLEP softens a couple of samples right at each edge, so check
        // well clear of both transitions).
        const int highRegionEnd = (int) (periodSamples * 0.25 * 0.8);      // well before the falling edge
        const int lowRegionStart = (int) (periodSamples * 0.25 * 1.2);     // well after the falling edge
        for (int i = 2; i < highRegionEnd; ++i)
            expect(buf[i] > 0.8f, "expected high region within first ~25% of the period");
        for (int i = lowRegionStart; i < periodSamples - 2; ++i)
            expect(buf[i] < -0.8f, "expected low region after the 25% duty point");
    }

    void runTriangleIndependentOfPulseDutyTest() {
        beginTest("triangle output is unaffected by pulseDuty (always derives from its own fixed 50% square)");
        Oscillator a, b;
        a.prepare(SR); b.prepare(SR);
        a.setBlend(0.0f, 1.0f, 0.0f, 0.0f);
        b.setBlend(0.0f, 1.0f, 0.0f, 0.0f);
        a.setPulseDuty(0.5f);
        b.setPulseDuty(0.1f);   // very different duty -- triangle must not care
        a.setFrequency(220.0f); b.setFrequency(220.0f);

        std::vector<float> bufA(BLOCK), bufB(BLOCK);
        a.processBlock(bufA.data(), BLOCK);
        b.processBlock(bufB.data(), BLOCK);
        for (int i = 0; i < BLOCK; ++i)
            expectWithinAbsoluteError(bufA[i], bufB[i], 1e-6f);
    }
};

static OscillatorTest oscillatorTestInstance;
```

- [ ] **Step 6: Build and run the full suite**

```bash
cmake --build build --target k2000_tests -j4
./build/tests/k2000_tests | tee build/last-test-run.log | tail -3
```

Expected: all Oscillator tests pass (9 now, up from 5), and every other test still passes (this step only touched `Oscillator` and `Voice.cpp`'s one call site — nothing else should move). Total count should be baseline + 4.

- [ ] **Step 7: Commit**

```bash
git add src/dsp/Oscillator.h src/dsp/Oscillator.cpp src/Voice.cpp tests/OscillatorTests.cpp
git commit -m "$(cat <<'EOF'
feat(dsp): Oscillator blends 4 waveforms proportionally instead of selecting 1

setWaveform(Waveform) is replaced by setBlend(sine,tri,saw,pulse) +
setPulseDuty(duty). All four shapes share one phase accumulator and are
combined as a weighted average (divide by the weight total), so blend
ratios -- not absolute values -- determine the resulting shape, and the
blend never touches output level. Zero-weight components are skipped
entirely (no trig/polyBLEP call). Pulse duty cycle generalizes the
existing fixed-50%-duty square polyBLEP to an arbitrary duty; Triangle
keeps deriving from its own internal fixed-50% square, independent of
the new duty parameter.

Voice's single existing call site is adapted to reproduce today's exact
behavior (old waveform enum -> equivalent single-shape blend) -- this
commit changes no audible behavior. Voice becoming three independent
VCOs is a later task.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: New Per-VCO + Mixer Params (Additive)

**Files:**
- Modify: `src/params/Parameters.h` (add 24 new fields to `LayerIds`)
- Modify: `src/params/Parameters.cpp` (add 24 new ID assignments, 24 new `layout.add()` calls, 24 new snapshot reads)
- Modify: `src/dsp/ParamSnapshot.h` (add 24 new fields with defaults)
- Modify: `tests/ParamSnapshotTests.cpp` (add one new `beginTest` block asserting the new params exist with correct defaults)

**Interfaces:**
- Consumes: nothing from Task 1.
- Produces: 24 new `ParamSnapshot` fields Task 3 reads directly:
  `osc1Coarse, osc1Fine, osc1BlendSine, osc1BlendTriangle, osc1BlendSaw, osc1BlendPulse, osc1PulseDuty` (and the same seven names with `osc2`/`osc3` prefixes), plus `mixerOsc1Level, mixerOsc2Level, mixerOsc3Level`. All are `float`.

This task is purely additive — nothing existing is modified or removed, so nothing outside these four files can break.

- [ ] **Step 1: Add the 24 fields to `LayerIds` in `src/params/Parameters.h`**

Find:

```cpp
struct LayerIds {
    juce::String algorithm, oscWaveform, oscCoarse, oscFine,
                 filterCutoff, filterResonance,
```

Replace with:

```cpp
struct LayerIds {
    juce::String algorithm, oscWaveform, oscCoarse, oscFine,
                 osc1Coarse, osc1Fine, osc1BlendSine, osc1BlendTriangle, osc1BlendSaw, osc1BlendPulse, osc1PulseDuty,
                 osc2Coarse, osc2Fine, osc2BlendSine, osc2BlendTriangle, osc2BlendSaw, osc2BlendPulse, osc2PulseDuty,
                 osc3Coarse, osc3Fine, osc3BlendSine, osc3BlendTriangle, osc3BlendSaw, osc3BlendPulse, osc3PulseDuty,
                 mixerOsc1Level, mixerOsc2Level, mixerOsc3Level,
                 filterCutoff, filterResonance,
```

- [ ] **Step 2: Add the 24 ID string assignments in `buildIds()`, `src/params/Parameters.cpp`**

Find:

```cpp
    id.oscWaveform     = p + "osc.waveform";
    id.oscCoarse       = p + "osc.coarse";
    id.oscFine         = p + "osc.fine";
    id.filterCutoff    = p + "filter.cutoff";
```

Replace with:

```cpp
    id.oscWaveform     = p + "osc.waveform";
    id.oscCoarse       = p + "osc.coarse";
    id.oscFine         = p + "osc.fine";
    id.osc1Coarse         = p + "osc1.coarse";
    id.osc1Fine           = p + "osc1.fine";
    id.osc1BlendSine      = p + "osc1.blend.sine";
    id.osc1BlendTriangle  = p + "osc1.blend.triangle";
    id.osc1BlendSaw       = p + "osc1.blend.saw";
    id.osc1BlendPulse     = p + "osc1.blend.pulse";
    id.osc1PulseDuty      = p + "osc1.blend.pulseDuty";
    id.osc2Coarse         = p + "osc2.coarse";
    id.osc2Fine           = p + "osc2.fine";
    id.osc2BlendSine      = p + "osc2.blend.sine";
    id.osc2BlendTriangle  = p + "osc2.blend.triangle";
    id.osc2BlendSaw       = p + "osc2.blend.saw";
    id.osc2BlendPulse     = p + "osc2.blend.pulse";
    id.osc2PulseDuty      = p + "osc2.blend.pulseDuty";
    id.osc3Coarse         = p + "osc3.coarse";
    id.osc3Fine           = p + "osc3.fine";
    id.osc3BlendSine      = p + "osc3.blend.sine";
    id.osc3BlendTriangle  = p + "osc3.blend.triangle";
    id.osc3BlendSaw       = p + "osc3.blend.saw";
    id.osc3BlendPulse     = p + "osc3.blend.pulse";
    id.osc3PulseDuty      = p + "osc3.blend.pulseDuty";
    id.mixerOsc1Level     = p + "mixer.osc1.level";
    id.mixerOsc2Level     = p + "mixer.osc2.level";
    id.mixerOsc3Level     = p + "mixer.osc3.level";
    id.filterCutoff    = p + "filter.cutoff";
```

- [ ] **Step 3: Add the 24 `layout.add()` calls in `createLayout()`, `src/params/Parameters.cpp`**

Find:

```cpp
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.oscFine, 1},
            "Osc Fine " + juce::String(i),
            juce::NormalisableRange<float>{-100.0f, 100.0f, 0.1f}, 0.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.filterCutoff, 1},
```

Replace with:

```cpp
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.oscFine, 1},
            "Osc Fine " + juce::String(i),
            juce::NormalisableRange<float>{-100.0f, 100.0f, 0.1f}, 0.0f));

        // VCO1/2/3: coarse/fine tuning + 4-way proportional waveform blend + pulse duty.
        // All three default identically for coarse/fine/blend (unison pitch, 100% saw) --
        // only the Mixer level differs (VCO1 audible, VCO2/3 silent) so the default patch
        // sounds identical to today's single-oscillator saw patch.
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.osc1Coarse, 1},
            "VCO1 Coarse " + juce::String(i),
            juce::NormalisableRange<float>{-24.0f, 24.0f, 1.0f}, 0.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.osc1Fine, 1},
            "VCO1 Fine " + juce::String(i),
            juce::NormalisableRange<float>{-100.0f, 100.0f, 0.1f}, 0.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.osc1BlendSine, 1},
            "VCO1 Blend Sine " + juce::String(i),
            juce::NormalisableRange<float>{0.0f, 1.0f, 0.0f}, 0.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.osc1BlendTriangle, 1},
            "VCO1 Blend Triangle " + juce::String(i),
            juce::NormalisableRange<float>{0.0f, 1.0f, 0.0f}, 0.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.osc1BlendSaw, 1},
            "VCO1 Blend Saw " + juce::String(i),
            juce::NormalisableRange<float>{0.0f, 1.0f, 0.0f}, 1.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.osc1BlendPulse, 1},
            "VCO1 Blend Pulse " + juce::String(i),
            juce::NormalisableRange<float>{0.0f, 1.0f, 0.0f}, 0.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.osc1PulseDuty, 1},
            "VCO1 Pulse Duty " + juce::String(i),
            juce::NormalisableRange<float>{0.01f, 0.99f, 0.0f}, 0.5f));

        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.osc2Coarse, 1},
            "VCO2 Coarse " + juce::String(i),
            juce::NormalisableRange<float>{-24.0f, 24.0f, 1.0f}, 0.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.osc2Fine, 1},
            "VCO2 Fine " + juce::String(i),
            juce::NormalisableRange<float>{-100.0f, 100.0f, 0.1f}, 0.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.osc2BlendSine, 1},
            "VCO2 Blend Sine " + juce::String(i),
            juce::NormalisableRange<float>{0.0f, 1.0f, 0.0f}, 0.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.osc2BlendTriangle, 1},
            "VCO2 Blend Triangle " + juce::String(i),
            juce::NormalisableRange<float>{0.0f, 1.0f, 0.0f}, 0.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.osc2BlendSaw, 1},
            "VCO2 Blend Saw " + juce::String(i),
            juce::NormalisableRange<float>{0.0f, 1.0f, 0.0f}, 1.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.osc2BlendPulse, 1},
            "VCO2 Blend Pulse " + juce::String(i),
            juce::NormalisableRange<float>{0.0f, 1.0f, 0.0f}, 0.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.osc2PulseDuty, 1},
            "VCO2 Pulse Duty " + juce::String(i),
            juce::NormalisableRange<float>{0.01f, 0.99f, 0.0f}, 0.5f));

        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.osc3Coarse, 1},
            "VCO3 Coarse " + juce::String(i),
            juce::NormalisableRange<float>{-24.0f, 24.0f, 1.0f}, 0.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.osc3Fine, 1},
            "VCO3 Fine " + juce::String(i),
            juce::NormalisableRange<float>{-100.0f, 100.0f, 0.1f}, 0.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.osc3BlendSine, 1},
            "VCO3 Blend Sine " + juce::String(i),
            juce::NormalisableRange<float>{0.0f, 1.0f, 0.0f}, 0.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.osc3BlendTriangle, 1},
            "VCO3 Blend Triangle " + juce::String(i),
            juce::NormalisableRange<float>{0.0f, 1.0f, 0.0f}, 0.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.osc3BlendSaw, 1},
            "VCO3 Blend Saw " + juce::String(i),
            juce::NormalisableRange<float>{0.0f, 1.0f, 0.0f}, 1.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.osc3BlendPulse, 1},
            "VCO3 Blend Pulse " + juce::String(i),
            juce::NormalisableRange<float>{0.0f, 1.0f, 0.0f}, 0.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.osc3PulseDuty, 1},
            "VCO3 Pulse Duty " + juce::String(i),
            juce::NormalisableRange<float>{0.01f, 0.99f, 0.0f}, 0.5f));

        // Mixer: balances the three VCOs. VCO1 audible by default, VCO2/3 silent
        // (so a fresh patch sounds identical to today's single-oscillator default).
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.mixerOsc1Level, 1},
            "Mixer VCO1 Level " + juce::String(i),
            juce::NormalisableRange<float>{0.0f, 1.0f, 0.0f}, 1.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.mixerOsc2Level, 1},
            "Mixer VCO2 Level " + juce::String(i),
            juce::NormalisableRange<float>{0.0f, 1.0f, 0.0f}, 0.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.mixerOsc3Level, 1},
            "Mixer VCO3 Level " + juce::String(i),
            juce::NormalisableRange<float>{0.0f, 1.0f, 0.0f}, 0.0f));

        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.filterCutoff, 1},
```

- [ ] **Step 4: Add the 24 snapshot reads in `snapshot()`, `src/params/Parameters.cpp`**

Find:

```cpp
    s.oscWaveform  = (int) raw(apvts, id.oscWaveform);
    s.oscCoarse    = raw(apvts, id.oscCoarse);
    s.oscFine      = raw(apvts, id.oscFine);
    s.svfCutoffHz  = raw(apvts, id.filterCutoff);
```

Replace with:

```cpp
    s.oscWaveform  = (int) raw(apvts, id.oscWaveform);
    s.oscCoarse    = raw(apvts, id.oscCoarse);
    s.oscFine      = raw(apvts, id.oscFine);
    s.osc1Coarse        = raw(apvts, id.osc1Coarse);
    s.osc1Fine          = raw(apvts, id.osc1Fine);
    s.osc1BlendSine     = raw(apvts, id.osc1BlendSine);
    s.osc1BlendTriangle = raw(apvts, id.osc1BlendTriangle);
    s.osc1BlendSaw      = raw(apvts, id.osc1BlendSaw);
    s.osc1BlendPulse    = raw(apvts, id.osc1BlendPulse);
    s.osc1PulseDuty     = raw(apvts, id.osc1PulseDuty);
    s.osc2Coarse        = raw(apvts, id.osc2Coarse);
    s.osc2Fine          = raw(apvts, id.osc2Fine);
    s.osc2BlendSine     = raw(apvts, id.osc2BlendSine);
    s.osc2BlendTriangle = raw(apvts, id.osc2BlendTriangle);
    s.osc2BlendSaw      = raw(apvts, id.osc2BlendSaw);
    s.osc2BlendPulse    = raw(apvts, id.osc2BlendPulse);
    s.osc2PulseDuty     = raw(apvts, id.osc2PulseDuty);
    s.osc3Coarse        = raw(apvts, id.osc3Coarse);
    s.osc3Fine          = raw(apvts, id.osc3Fine);
    s.osc3BlendSine     = raw(apvts, id.osc3BlendSine);
    s.osc3BlendTriangle = raw(apvts, id.osc3BlendTriangle);
    s.osc3BlendSaw      = raw(apvts, id.osc3BlendSaw);
    s.osc3BlendPulse    = raw(apvts, id.osc3BlendPulse);
    s.osc3PulseDuty     = raw(apvts, id.osc3PulseDuty);
    s.mixerOsc1Level    = raw(apvts, id.mixerOsc1Level);
    s.mixerOsc2Level    = raw(apvts, id.mixerOsc2Level);
    s.mixerOsc3Level    = raw(apvts, id.mixerOsc3Level);
    s.svfCutoffHz  = raw(apvts, id.filterCutoff);
```

- [ ] **Step 5: Add the 24 fields to `ParamSnapshot`, `src/dsp/ParamSnapshot.h`**

Find:

```cpp
struct ParamSnapshot {
    // Oscillator
    int   oscWaveform   = 0;   // 0=saw 1=square 2=triangle 3=sine
    float oscCoarse     = 0.0f; // semitones
    float oscFine       = 0.0f; // cents

    // Filter block (layer.filter.*)
```

Replace with:

```cpp
struct ParamSnapshot {
    // Oscillator (retired -- kept until Task 4 of the three-VCO-blend plan
    // removes it, since removing it here would break every test file that
    // still constructs a ParamSnapshot directly)
    int   oscWaveform   = 0;   // 0=saw 1=square 2=triangle 3=sine
    float oscCoarse     = 0.0f; // semitones
    float oscFine       = 0.0f; // cents

    // VCO1/2/3: coarse/fine tuning (semitones/cents) + proportional 4-way
    // waveform blend (each 0..1, combined as a weighted average -- see
    // Oscillator::processSample()) + pulse duty cycle (0.01..0.99).
    // All three default to unison pitch + 100% saw; only mixerOscNLevel
    // differs between them (VCO1 audible, VCO2/3 silent) so a fresh patch
    // sounds identical to today's single-oscillator saw default.
    float osc1Coarse = 0.0f, osc1Fine = 0.0f;
    float osc1BlendSine = 0.0f, osc1BlendTriangle = 0.0f, osc1BlendSaw = 1.0f, osc1BlendPulse = 0.0f;
    float osc1PulseDuty = 0.5f;
    float osc2Coarse = 0.0f, osc2Fine = 0.0f;
    float osc2BlendSine = 0.0f, osc2BlendTriangle = 0.0f, osc2BlendSaw = 1.0f, osc2BlendPulse = 0.0f;
    float osc2PulseDuty = 0.5f;
    float osc3Coarse = 0.0f, osc3Fine = 0.0f;
    float osc3BlendSine = 0.0f, osc3BlendTriangle = 0.0f, osc3BlendSaw = 1.0f, osc3BlendPulse = 0.0f;
    float osc3PulseDuty = 0.5f;

    // Mixer: linear gain (0..1) balancing the three VCOs before they sum
    // into the algorithm-block graph.
    float mixerOsc1Level = 1.0f, mixerOsc2Level = 0.0f, mixerOsc3Level = 0.0f;

    // Filter block (layer.filter.*)
```

- [ ] **Step 6: Add a new test block to `tests/ParamSnapshotTests.cpp`**

Find:

```cpp
        beginTest("Moog bank params exist with correct defaults");
        {
            const auto& id = params::layerIds(0);
            expect(apvts.getParameter(id.spineMoogMode)       != nullptr, "spine.moog.mode missing");
            expect(apvts.getParameter(id.spineMoogBassAmount) != nullptr, "spine.moog.bassAmount missing");
            expect(apvts.getParameter(id.spineMoogBassWave)   != nullptr, "spine.moog.bassWave missing");
            expect(apvts.getParameter(id.spineMoogBassOctave) != nullptr, "spine.moog.bassOctave missing");
            s = params::snapshot(apvts, 0);
            expect(s.moogMode == 0 && std::fpclassify(s.moogBassAmount) == FP_ZERO, "moog defaults wrong");
        }
    }
};
```

Replace with:

```cpp
        beginTest("Moog bank params exist with correct defaults");
        {
            const auto& id = params::layerIds(0);
            expect(apvts.getParameter(id.spineMoogMode)       != nullptr, "spine.moog.mode missing");
            expect(apvts.getParameter(id.spineMoogBassAmount) != nullptr, "spine.moog.bassAmount missing");
            expect(apvts.getParameter(id.spineMoogBassWave)   != nullptr, "spine.moog.bassWave missing");
            expect(apvts.getParameter(id.spineMoogBassOctave) != nullptr, "spine.moog.bassOctave missing");
            s = params::snapshot(apvts, 0);
            expect(s.moogMode == 0 && std::fpclassify(s.moogBassAmount) == FP_ZERO, "moog defaults wrong");
        }

        beginTest("VCO1/2/3 + Mixer params exist, default to unison saw with only VCO1 audible");
        {
            const auto& id = params::layerIds(0);
            expect(apvts.getParameter(id.osc1BlendSaw) != nullptr, "osc1.blend.saw missing");
            expect(apvts.getParameter(id.osc2BlendSaw) != nullptr, "osc2.blend.saw missing");
            expect(apvts.getParameter(id.osc3BlendSaw) != nullptr, "osc3.blend.saw missing");
            expect(apvts.getParameter(id.osc1PulseDuty) != nullptr, "osc1.blend.pulseDuty missing");
            expect(apvts.getParameter(id.mixerOsc1Level) != nullptr, "mixer.osc1.level missing");

            s = params::snapshot(apvts, 0);
            for (float coarse : { s.osc1Coarse, s.osc2Coarse, s.osc3Coarse })
                expectWithinAbsoluteError(coarse, 0.0f, 1e-6f);
            for (float fine : { s.osc1Fine, s.osc2Fine, s.osc3Fine })
                expectWithinAbsoluteError(fine, 0.0f, 1e-6f);
            for (float saw : { s.osc1BlendSaw, s.osc2BlendSaw, s.osc3BlendSaw })
                expectWithinAbsoluteError(saw, 1.0f, 1e-6f);
            for (float sine : { s.osc1BlendSine, s.osc2BlendSine, s.osc3BlendSine })
                expectWithinAbsoluteError(sine, 0.0f, 1e-6f);
            for (float duty : { s.osc1PulseDuty, s.osc2PulseDuty, s.osc3PulseDuty })
                expectWithinAbsoluteError(duty, 0.5f, 1e-6f);

            expectWithinAbsoluteError(s.mixerOsc1Level, 1.0f, 1e-6f);
            expectWithinAbsoluteError(s.mixerOsc2Level, 0.0f, 1e-6f);
            expectWithinAbsoluteError(s.mixerOsc3Level, 0.0f, 1e-6f);
        }
    }
};
```

- [ ] **Step 7: Build and run the full suite**

```bash
cmake --build build --target k2000_tests -j4
./build/tests/k2000_tests | tee build/last-test-run.log | tail -3
```

Expected: everything from Task 1 still passes, plus the one new `ParamSnapshotTests` case. Total count = Task 1's count + 1.

- [ ] **Step 8: Commit**

```bash
git add src/params/Parameters.h src/params/Parameters.cpp src/dsp/ParamSnapshot.h tests/ParamSnapshotTests.cpp
git commit -m "$(cat <<'EOF'
feat(params): add VCO1/2/3 + Mixer params, additive alongside the old osc.*

24 new per-layer params: osc{1,2,3}.{coarse,fine,blend.{sine,triangle,
saw,pulse},blend.pulseDuty} and mixer.osc{1,2,3}.level. All three VCOs
default identically (unison pitch, 100% saw) except Mixer level (VCO1
audible, VCO2/3 silent), so a fresh patch sounds identical to today's
single-oscillator default once Voice consumes these (next task).

The old osc.waveform/osc.coarse/osc.fine params are untouched -- nothing
reads the new fields yet, and nothing stops reading the old ones yet.
Purely additive, no behavior change.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: Voice Renders Three Oscillators

**Files:**
- Modify: `src/Voice.h` (one `Oscillator osc_` becomes three; add one scratch buffer)
- Modify: `src/Voice.cpp` (`prepare()`, `reset()`, `noteOn()`, `render()`)
- Modify: `tests/VoiceTests.cpp` (2 of its `ParamSnapshot` setups need the new fields to keep producing a clean sine test signal)

**Interfaces:**
- Consumes: `Oscillator::setBlend`/`setPulseDuty` (Task 1); `ParamSnapshot::osc{1,2,3}{Coarse,Fine,Blend*,PulseDuty}` and `mixerOsc{1,2,3}Level` (Task 2).
- Produces: nothing new for later tasks — this is where the architecture lands. `Voice`'s public interface (`prepare`, `reset`, `noteOn`, `noteOff`, `isActive`, `render`) is unchanged; only its private implementation changes.

- [ ] **Step 1: Confirm baseline**

```bash
cmake --build build --target k2000_tests -j4
./build/tests/k2000_tests | tee build/last-test-run.log | tail -3
```

Expected: green, count = Task 2's count.

- [ ] **Step 2: `src/Voice.h` — three oscillators + one extra scratch buffer**

Find:

```cpp
private:
    Layer* layer_ = nullptr;  // non-owning
    Oscillator osc_;
    Envelope amp_;
```

Replace with:

```cpp
private:
    Layer* layer_ = nullptr;  // non-owning
    Oscillator osc1_, osc2_, osc3_;
    Envelope amp_;
```

Find:

```cpp
    std::vector<float> scratch_;
    SpineFilterSlot spine_;
```

Replace with:

```cpp
    std::vector<float> scratch_;
    std::vector<float> oscScratch_;   // temp buffer for osc2_/osc3_ before summing into scratch_
    SpineFilterSlot spine_;
```

- [ ] **Step 3: `src/Voice.cpp` — `prepare()`**

Find:

```cpp
    osc_.prepare(sr);          // base rate (already band-limited)
    amp_.prepare(sr);          // base rate
    scratch_.assign(maxBlock, 0.0f);
    baseL_.assign(maxBlock, 0.0f);
    baseR_.assign(maxBlock, 0.0f);
```

Replace with:

```cpp
    osc1_.prepare(sr); osc2_.prepare(sr); osc3_.prepare(sr);  // base rate (already band-limited)
    amp_.prepare(sr);          // base rate
    scratch_.assign(maxBlock, 0.0f);
    oscScratch_.assign(maxBlock, 0.0f);
    baseL_.assign(maxBlock, 0.0f);
    baseR_.assign(maxBlock, 0.0f);
```

- [ ] **Step 4: `src/Voice.cpp` — `reset()`**

Find:

```cpp
void Voice::reset() {
    osc_.reset();
    amp_.reset();
```

Replace with:

```cpp
void Voice::reset() {
    osc1_.reset(); osc2_.reset(); osc3_.reset();
    amp_.reset();
```

- [ ] **Step 5: `src/Voice.cpp` — `noteOn()`**

Find:

```cpp
void Voice::noteOn(int midiNote, float velocity) {
    note_ = midiNote;
    velocity_ = velocity;
    osc_.reset();
    amp_.reset();
```

Replace with:

```cpp
void Voice::noteOn(int midiNote, float velocity) {
    note_ = midiNote;
    velocity_ = velocity;
    osc1_.reset(); osc2_.reset(); osc3_.reset();
    amp_.reset();
```

- [ ] **Step 6: `src/Voice.cpp` — `render()`**

Find:

```cpp
    const float tune = s.oscCoarse + s.oscFine * 0.01f;
    const float hz = midiToHz(note_) * std::pow(2.0f, tune / 12.0f);
    osc_.setBlend(s.oscWaveform == 3 ? 1.0f : 0.0f,   // sine
                  s.oscWaveform == 2 ? 1.0f : 0.0f,   // triangle
                  s.oscWaveform == 0 ? 1.0f : 0.0f,   // saw
                  s.oscWaveform == 1 ? 1.0f : 0.0f);  // pulse (old "Square" = 50% duty pulse)
    osc_.setPulseDuty(0.5f);
    amp_.setParameters(s.ampAttackS, s.ampDecayS, s.ampSustain, s.ampReleaseS);

    jassert(numSamples <= (int) scratch_.size());

    // --- Base rate: oscillator --------------------------------------------------
    osc_.setFrequency(hz);
    osc_.processBlock(scratch_.data(), numSamples);
```

Replace with:

```cpp
    amp_.setParameters(s.ampAttackS, s.ampDecayS, s.ampSustain, s.ampReleaseS);

    jassert(numSamples <= (int) scratch_.size());

    // --- Base rate: three VCOs, each independently tuned/blended, summed via
    //     their Mixer level into one mono buffer -----------------------------
    const float tune1 = s.osc1Coarse + s.osc1Fine * 0.01f;
    const float hz1 = midiToHz(note_) * std::pow(2.0f, tune1 / 12.0f);
    osc1_.setBlend(s.osc1BlendSine, s.osc1BlendTriangle, s.osc1BlendSaw, s.osc1BlendPulse);
    osc1_.setPulseDuty(s.osc1PulseDuty);
    osc1_.setFrequency(hz1);
    osc1_.processBlock(scratch_.data(), numSamples);
    for (int i = 0; i < numSamples; ++i) scratch_[i] *= s.mixerOsc1Level;

    const float tune2 = s.osc2Coarse + s.osc2Fine * 0.01f;
    const float hz2 = midiToHz(note_) * std::pow(2.0f, tune2 / 12.0f);
    osc2_.setBlend(s.osc2BlendSine, s.osc2BlendTriangle, s.osc2BlendSaw, s.osc2BlendPulse);
    osc2_.setPulseDuty(s.osc2PulseDuty);
    osc2_.setFrequency(hz2);
    osc2_.processBlock(oscScratch_.data(), numSamples);
    for (int i = 0; i < numSamples; ++i) scratch_[i] += oscScratch_[i] * s.mixerOsc2Level;

    const float tune3 = s.osc3Coarse + s.osc3Fine * 0.01f;
    const float hz3 = midiToHz(note_) * std::pow(2.0f, tune3 / 12.0f);
    osc3_.setBlend(s.osc3BlendSine, s.osc3BlendTriangle, s.osc3BlendSaw, s.osc3BlendPulse);
    osc3_.setPulseDuty(s.osc3PulseDuty);
    osc3_.setFrequency(hz3);
    osc3_.processBlock(oscScratch_.data(), numSamples);
    for (int i = 0; i < numSamples; ++i) scratch_[i] += oscScratch_[i] * s.mixerOsc3Level;
```

The `hz` variable used a few lines further down (in the `spine_.processStereo(...)` call, which takes the fundamental Hz for the spine filter) must keep compiling. Find:

```cpp
    spine_.processStereo(layer_->hpStage(), s.hpCutoffHz > 0.0f,
                         layer_->spineModel(), s.spineModelFadeMs, hz,
                         osL_.data(), osR_.data(), nOs);
```

Replace with (VCO1's Hz is the filter's fundamental-tracking reference — VCO1 is the always-audible-by-default oscillator, matching what the single oscillator's `hz` meant before):

```cpp
    spine_.processStereo(layer_->hpStage(), s.hpCutoffHz > 0.0f,
                         layer_->spineModel(), s.spineModelFadeMs, hz1,
                         osL_.data(), osR_.data(), nOs);
```

- [ ] **Step 7: `tests/VoiceTests.cpp` — keep the "clean sine test signal" setups clean**

Find:

```cpp
        ParamSnapshot s;
        s.oscWaveform = 3;  // sine for clean test signal
        s.svfCutoffHz = 20000.0f; s.svfResonance = 0.0f;
```

Replace with:

```cpp
        ParamSnapshot s;
        s.osc1BlendSine = 1.0f; s.osc1BlendSaw = 0.0f;  // sine for clean test signal (overrides the struct's saw-by-default)
        s.svfCutoffHz = 20000.0f; s.svfResonance = 0.0f;
```

Find:

```cpp
            ParamSnapshot s4;
            s4.oscWaveform  = 3;     // sine
            s4.svfCutoffHz  = 8000.0f;
```

Replace with:

```cpp
            ParamSnapshot s4;
            s4.osc1BlendSine = 1.0f; s4.osc1BlendSaw = 0.0f;     // sine
            s4.svfCutoffHz  = 8000.0f;
```

No other `VoiceTests.cpp` change is needed: the "idle voice," "noteOn produces non-zero output," "factor 1 render," and "noteOff eventually silences" tests all reuse the same top-level `s` fixed above. The "bind path" test (`regLayer`/`regVoice`) never calls `updateParameters()` at all, so it uses `Layer`'s bare-default `ParamSnapshot` — which now defaults to VCO1 = 100% saw, audible (Mixer level 1.0), identical in spirit to the old bare default of `oscWaveform == 0` (saw). No change needed there.

- [ ] **Step 8: Build and run the full suite**

```bash
cmake --build build --target k2000_tests -j4
./build/tests/k2000_tests | tee build/last-test-run.log | tail -3
```

Expected: green, same total count as Task 2 (no tests added or removed in this task, only two `VoiceTests.cpp` setups edited).

- [ ] **Step 9: Commit**

```bash
git add src/Voice.h src/Voice.cpp tests/VoiceTests.cpp
git commit -m "$(cat <<'EOF'
feat(voice): render three independent VCOs, summed via Mixer levels

Voice::osc_ (one oscillator) becomes osc1_/osc2_/osc3_ (three). Each
gets its own coarse/fine -> Hz and its own 4-way blend + pulse duty
from the new osc{1,2,3}.* params (previous task), renders into its own
buffer, gets scaled by its mixer.osc{1,2,3}.level, and the three are
summed into the same mono scratch_ buffer that feeds the unchanged
downstream algorithm-block graph. The spine filter's fundamental-Hz
reference now tracks VCO1 (the always-audible-by-default oscillator).

Default patch (VCO1 100% saw at Mixer 1.0, VCO2/3 at Mixer 0.0) sounds
identical to today's single-oscillator saw patch. The old osc.waveform/
osc.coarse/osc.fine params are now unread by Voice but still exist
(retired next task).

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: Retire the Old Single-Oscillator Param Surface

**Files:**
- Modify: `src/dsp/ParamSnapshot.h` (remove `oscWaveform`, `oscCoarse`, `oscFine`)
- Modify: `src/params/Parameters.h` (remove from `LayerIds`)
- Modify: `src/params/Parameters.cpp` (remove ID assignment, `layout.add()` calls, snapshot reads)
- Modify: `tests/ParamSnapshotTests.cpp` (remove the two now-invalid assertions)
- Modify: `tests/AlgorithmRoutingTests.cpp`, `tests/LargeSignalTests.cpp`, `tests/LayerTests.cpp`, `tests/MultiLayerTests.cpp`, `tests/OversamplingAntiAliasTests.cpp`, `tests/RenderFingerprintTests.cpp`, `tests/VoiceManagerTests.cpp`, `tests/VoicePerfTests.cpp` (remove now-dead `oscWaveform`/`oscCoarse`/`oscFine` setup lines — mechanical, each file sets one of these to a value that's now either the default already, or needs the Task-3-style blend equivalent)
- Modify: `src/PluginEditor.cpp` and `src/PluginEditor.h` (the one necessary GUI touch — see note below)

**Interfaces:**
- Consumes: nothing new.
- Produces: nothing new — this task only removes.

**A note on the one GUI file in a "DSP/backend-only" plan:** `PluginEditor.cpp` binds `oscWave_`/`oscCoarse_`/`oscFine_` to `ids.oscWaveform`/`ids.oscCoarse`/`ids.oscFine`. Once this task deletes those IDs, `PluginEditor.cpp` will not compile unless those bindings are also removed. This is **not** the three-VCO GUI rebuild (that's the next, separate plan) — it is the minimal fix required to keep the whole project building. Per spec §7, `oscWave_` is fully superseded (not just hidden) by the coming Blend sliders, and `oscCoarse_`/`oscFine_` are superseded by the coming per-VCO Coarse/Fine controls — unlike the earlier Drive/Mix pre-work (where the backing params still exist and the knobs might return as-is), there is no "un-hide it later" path for these three controls, so this step deletes them outright (member declarations included) rather than just hiding them.

- [ ] **Step 1: Confirm baseline**

```bash
cmake --build build --target k2000_tests -j4
./build/tests/k2000_tests | tee build/last-test-run.log | tail -3
```

Expected: green, count = Task 3's count.

- [ ] **Step 2: Remove the three fields from `src/dsp/ParamSnapshot.h`**

Find:

```cpp
struct ParamSnapshot {
    // Oscillator (retired -- kept until Task 4 of the three-VCO-blend plan
    // removes it, since removing it here would break every test file that
    // still constructs a ParamSnapshot directly)
    int   oscWaveform   = 0;   // 0=saw 1=square 2=triangle 3=sine
    float oscCoarse     = 0.0f; // semitones
    float oscFine       = 0.0f; // cents

    // VCO1/2/3: coarse/fine tuning (semitones/cents) + proportional 4-way
```

Replace with:

```cpp
struct ParamSnapshot {
    // VCO1/2/3: coarse/fine tuning (semitones/cents) + proportional 4-way
```

- [ ] **Step 3: Remove the three fields from `LayerIds`, `src/params/Parameters.h`**

Find:

```cpp
struct LayerIds {
    juce::String algorithm, oscWaveform, oscCoarse, oscFine,
                 osc1Coarse, osc1Fine, osc1BlendSine, osc1BlendTriangle, osc1BlendSaw, osc1BlendPulse, osc1PulseDuty,
```

Replace with:

```cpp
struct LayerIds {
    juce::String algorithm,
                 osc1Coarse, osc1Fine, osc1BlendSine, osc1BlendTriangle, osc1BlendSaw, osc1BlendPulse, osc1PulseDuty,
```

- [ ] **Step 4: Remove the three ID assignments in `buildIds()`, `src/params/Parameters.cpp`**

Find:

```cpp
    id.oscWaveform     = p + "osc.waveform";
    id.oscCoarse       = p + "osc.coarse";
    id.oscFine         = p + "osc.fine";
    id.osc1Coarse         = p + "osc1.coarse";
```

Replace with:

```cpp
    id.osc1Coarse         = p + "osc1.coarse";
```

- [ ] **Step 5: Remove the three `layout.add()` calls in `createLayout()`, `src/params/Parameters.cpp`**

Find:

```cpp
        layout.add(std::make_unique<ChoiceParam>(juce::ParameterID{id.oscWaveform, 1},
            "Osc Waveform " + juce::String(i),
            juce::StringArray{"Saw", "Square", "Triangle", "Sine"}, 0));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.oscCoarse, 1},
            "Osc Coarse " + juce::String(i),
            juce::NormalisableRange<float>{-24.0f, 24.0f, 1.0f}, 0.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.oscFine, 1},
            "Osc Fine " + juce::String(i),
            juce::NormalisableRange<float>{-100.0f, 100.0f, 0.1f}, 0.0f));

        // VCO1/2/3: coarse/fine tuning + 4-way proportional waveform blend + pulse duty.
```

Replace with:

```cpp
        // VCO1/2/3: coarse/fine tuning + 4-way proportional waveform blend + pulse duty.
```

- [ ] **Step 6: Remove the three snapshot reads in `snapshot()`, `src/params/Parameters.cpp`**

Find:

```cpp
    s.oscWaveform  = (int) raw(apvts, id.oscWaveform);
    s.oscCoarse    = raw(apvts, id.oscCoarse);
    s.oscFine      = raw(apvts, id.oscFine);
    s.osc1Coarse        = raw(apvts, id.osc1Coarse);
```

Replace with:

```cpp
    s.osc1Coarse        = raw(apvts, id.osc1Coarse);
```

- [ ] **Step 7: Fix the two now-invalid assertions in `tests/ParamSnapshotTests.cpp`**

Find:

```cpp
        beginTest("defaults match expected values");
        auto s = params::snapshot(apvts, 0);
        expectWithinAbsoluteError(s.oscCoarse, 0.0f, 1e-6f);
        expectWithinAbsoluteError(s.svfCutoffHz, 1000.0f, 1e-3f);
        expectWithinAbsoluteError(s.svfResonance, 0.2f, 1e-6f);
        expectWithinAbsoluteError(s.ampSustain, 0.8f, 1e-6f);
        expectWithinAbsoluteError(s.masterGainDb, -9.0f, 1e-3f);
        expect(s.oscWaveform == 0);
```

Replace with:

```cpp
        beginTest("defaults match expected values");
        auto s = params::snapshot(apvts, 0);
        expectWithinAbsoluteError(s.osc1Coarse, 0.0f, 1e-6f);
        expectWithinAbsoluteError(s.svfCutoffHz, 1000.0f, 1e-3f);
        expectWithinAbsoluteError(s.svfResonance, 0.2f, 1e-6f);
        expectWithinAbsoluteError(s.ampSustain, 0.8f, 1e-6f);
        expectWithinAbsoluteError(s.masterGainDb, -9.0f, 1e-3f);
        expectWithinAbsoluteError(s.osc1BlendSaw, 1.0f, 1e-6f);
```

- [ ] **Step 8: Fix the 8 dependent test files (mechanical)**

Each of these sets `oscWaveform` to a value that's now handled by the struct's own defaults (saw, value `0`) or needs the Task-3-style blend equivalent (sine, value `3`). Apply each edit exactly as shown — every "Find" block below is copy-pasted from the live file, so it will match exactly one location.

**`tests/AlgorithmRoutingTests.cpp`** (was saw — the struct already defaults to saw, so this line is now redundant):

Find:
```cpp
        ParamSnapshot s;
        s.oscWaveform = 0;                 // saw — harmonically rich
        s.svfCutoffHz = 800.0f; s.svfResonance = 0.2f;
```
Replace with:
```cpp
        ParamSnapshot s;                   // defaults to VCO1 100% saw — harmonically rich
        s.svfCutoffHz = 800.0f; s.svfResonance = 0.2f;
```

**`tests/LargeSignalTests.cpp`** (saw — redundant with the default):

Find:
```cpp
            ParamSnapshot s {};
            s.oscWaveform = 0;                     // saw — harmonics land on the peak
            s.ampAttackS = 0.001f; s.ampDecayS = 0.1f;
```
Replace with:
```cpp
            ParamSnapshot s {};                    // defaults to VCO1 100% saw — harmonics land on the peak
            s.ampAttackS = 0.001f; s.ampDecayS = 0.1f;
```

**`tests/LayerTests.cpp`** (two occurrences — first is sine + explicit coarse/fine=0, both now need the blend equivalent; coarse/fine=0 is already the default so that half of the line is just dropped; second is a bare sine):

Find:
```cpp
            ParamSnapshot s {};
            s.oscWaveform = 3;          // sine
            s.oscCoarse = 0; s.oscFine = 0;
            s.svfCutoffHz = 20000.0f; s.svfResonance = 0.0f;
```
Replace with:
```cpp
            ParamSnapshot s {};
            s.osc1BlendSine = 1.0f; s.osc1BlendSaw = 0.0f;   // sine (coarse/fine already default to 0)
            s.svfCutoffHz = 20000.0f; s.svfResonance = 0.0f;
```

Find:
```cpp
            ParamSnapshot s {};
            s.oscWaveform = 3;
            s.svfResonance = 0.0f;
```
Replace with:
```cpp
            ParamSnapshot s {};
            s.osc1BlendSine = 1.0f; s.osc1BlendSaw = 0.0f;
            s.svfResonance = 0.0f;
```

**`tests/MultiLayerTests.cpp`** (sine, packed onto one line with other params):

Find:
```cpp
ParamSnapshot dspBase() {
    ParamSnapshot s;
    s.oscWaveform = 3; s.svfCutoffHz = 20000.0f; s.svfResonance = 0.0f;
    s.wsDrive = 0.0f; s.wsMix = 0.0f;
```
Replace with:
```cpp
ParamSnapshot dspBase() {
    ParamSnapshot s;
    s.osc1BlendSine = 1.0f; s.osc1BlendSaw = 0.0f;
    s.svfCutoffHz = 20000.0f; s.svfResonance = 0.0f;
    s.wsDrive = 0.0f; s.wsMix = 0.0f;
```

**`tests/OversamplingAntiAliasTests.cpp`** (saw — redundant with the default):

Find:
```cpp
                ParamSnapshot s;
                s.oscWaveform      = 0;        // saw — rich in harmonics, alias-prone
                s.svfCutoffHz      = 8000.0f;  // open enough to pass the drive harmonics
```
Replace with:
```cpp
                ParamSnapshot s;               // defaults to VCO1 100% saw — rich in harmonics, alias-prone
                s.svfCutoffHz      = 8000.0f;  // open enough to pass the drive harmonics
```

**`tests/RenderFingerprintTests.cpp`** (saw — redundant with the default):

Find:
```cpp
ParamSnapshot base() {
    ParamSnapshot s {};
    s.oscWaveform = 0;                 // saw
    s.ampAttackS = 0.001f; s.ampDecayS = 0.1f;
```
Replace with:
```cpp
ParamSnapshot base() {
    ParamSnapshot s {};                // defaults to VCO1 100% saw
    s.ampAttackS = 0.001f; s.ampDecayS = 0.1f;
```

**`tests/VoiceManagerTests.cpp`** (sine):

Find:
```cpp
        ParamSnapshot s;
        s.oscWaveform = 3;
        s.svfCutoffHz = 20000.0f;
```
Replace with:
```cpp
        ParamSnapshot s;
        s.osc1BlendSine = 1.0f; s.osc1BlendSaw = 0.0f;
        s.svfCutoffHz = 20000.0f;
```

**`tests/VoicePerfTests.cpp`** (two occurrences, both saw, both redundant with the default):

Find:
```cpp
                    ParamSnapshot s {};
                    s.oscWaveform = 0;              // saw (worst-case harmonics)
                    s.svfCutoffHz = 1000.0f;
```
Replace with:
```cpp
                    ParamSnapshot s {};              // defaults to VCO1 100% saw (worst-case harmonics)
                    s.svfCutoffHz = 1000.0f;
```

Find:
```cpp
            ParamSnapshot s {};
            s.oscWaveform = 0; s.svfCutoffHz = 1000.0f; s.svfResonance = 0.2f;
            s.spineModel = 0; s.spineSlope = 1;
```
Replace with:
```cpp
            ParamSnapshot s {};              // defaults to VCO1 100% saw
            s.svfCutoffHz = 1000.0f; s.svfResonance = 0.2f;
            s.spineModel = 0; s.spineSlope = 1;
```

- [ ] **Step 9: Build — expect `PluginEditor.cpp` to fail here (that's the point of the next step)**

```bash
cmake --build build --target k2000_tests -j4
```

Expected: `k2000_tests` (which does not compile `PluginEditor.cpp`) builds clean. If you also build `k2000_Standalone` or `k2000_VST3` at this point, expect a compile error referencing `ids.oscWaveform`/`ids.oscCoarse`/`ids.oscFine` not existing in `PluginEditor.cpp` — that confirms Step 10 below is necessary, not optional.

- [ ] **Step 10: Remove the now-orphaned GUI members and bindings, `src/PluginEditor.h`**

Find:

```cpp
    Section sourceSection_{ "VAST Source / DSP", /*spine*/ false };
    juce::ComboBox oscWave_, algo_;
    juce::Label    oscWaveLbl_, algoLbl_;
    LabeledKnob    oscCoarse_{ "Coarse" }, oscFine_{ "Fine" },
                   shaperDrive_{ "Drive" }, shaperMix_{ "Mix" };
```

Replace with:

```cpp
    Section sourceSection_{ "VAST Source / DSP", /*spine*/ false };
    juce::ComboBox algo_;
    juce::Label    algoLbl_;
    LabeledKnob    shaperDrive_{ "Drive" }, shaperMix_{ "Mix" };
```

- [ ] **Step 11: Remove the setup/bind/layout references, `src/PluginEditor.cpp`**

Find:

```cpp
    oscWaveLbl_.setText("Wave", juce::dontSendNotification);
    oscWaveLbl_.setJustificationType(juce::Justification::centred);
    oscWave_.addItemList(juce::StringArray{ "Saw", "Square", "Triangle", "Sine" }, 1);
    addToSource(oscWaveLbl_); addToSource(oscWave_);
    addToSource(oscCoarse_); addToSource(oscFine_);
    algoLbl_.setText("Algo", juce::dontSendNotification);
```

Replace with:

```cpp
    algoLbl_.setText("Algo", juce::dontSendNotification);
```

Find:

```cpp
    binder_.bind(oscWave_,             ids.oscWaveform);
    binder_.bind(oscCoarse_.slider(),  ids.oscCoarse);
    binder_.bind(oscFine_.slider(),    ids.oscFine);
    binder_.bind(algo_,                ids.algorithm);
```

Replace with:

```cpp
    binder_.bind(algo_,                ids.algorithm);
```

Find:

```cpp
        auto top = sc.removeFromTop(sc.getHeight() / 2);
        layoutCells(top, { { &oscWaveLbl_, &oscWave_ }, { nullptr, &oscCoarse_ }, { nullptr, &oscFine_ } });
        layoutCells(sc,  { { &algoLbl_, &algo_ } });
```

Replace with:

```cpp
        auto top = sc.removeFromTop(sc.getHeight() / 2);
        (void) top;  // left blank -- the three-VCO Source-section rebuild (next plan) fills this
        layoutCells(sc,  { { &algoLbl_, &algo_ } });
```

- [ ] **Step 12: Build and run the full suite, plus the Standalone target**

```bash
cmake --build build --target k2000_tests k2000_Standalone -j4
./build/tests/k2000_tests | tee build/last-test-run.log | tail -3
```

Expected: everything green, both targets build. Total suite count = Task 3's count minus zero (no tests removed, only their setup lines changed) — should exactly match Task 3's number.

- [ ] **Step 13: Live-verify nothing visibly broke**

Per this repo's standing rule, a GUI-adjacent change (even a compile-keeping one) gets a look, not just a green build. Run the Standalone build and confirm: the Source section's top half is now blank (expected — the three-VCO rebuild fills it in the next plan), the Algo dropdown still works and still occupies the bottom half exactly as it did after the pre-work plan, and the rest of the panel (Filter, Amp Env, Routing, etc.) is unchanged. This is expected to look sparse — that's the correct intermediate state, not a bug.

- [ ] **Step 14: Commit**

```bash
git add src/dsp/ParamSnapshot.h src/params/Parameters.h src/params/Parameters.cpp \
        tests/ParamSnapshotTests.cpp tests/AlgorithmRoutingTests.cpp tests/LargeSignalTests.cpp \
        tests/LayerTests.cpp tests/MultiLayerTests.cpp tests/OversamplingAntiAliasTests.cpp \
        tests/RenderFingerprintTests.cpp tests/VoiceManagerTests.cpp tests/VoicePerfTests.cpp \
        src/PluginEditor.h src/PluginEditor.cpp
git commit -m "$(cat <<'EOF'
feat(params): retire osc.waveform/osc.coarse/osc.fine (superseded by VCO1/2/3)

No back-compat shim (standing decision: preset compatibility is not a
constraint). Removes the params, the ParamSnapshot fields, and every
test-file setup line that referenced them (all mechanical -- each was
either already redundant with the new struct's default, saw=100%, or
needed the Task-3-style blend equivalent for sine).

Also removes oscWave_/oscCoarse_/oscFine_ from PluginEditor -- not a
hide-for-later like the earlier Drive/Mix pre-work, a real deletion,
since these are fully superseded by the coming per-VCO Blend/Coarse/
Fine controls (next plan), not controls that might return as-is. The
Source section's top half is intentionally blank until that plan lands.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

## Plan Self-Review

**Spec coverage:** §2 Naming → Global Constraints + every identifier chosen throughout. §3 Params → Task 2 (add) + Task 4 (retire old). §4 Blend math → Task 1 (`processSample()`) + Task 1's `runBlendRatioTest`/`runZeroSumSilenceTest`. §5 DSP implementation → Task 1 in full, including the pulse-duty polyBLEP generalization and Triangle's independence (both directly tested). §6 Voice architecture → Task 3 in full. §7 GUI → explicitly deferred to a separate plan; Task 4's PluginEditor touch is scoped and justified as compile-keeping only, not an implementation of §7. §8 Non-goals → respected throughout (no modulation routing added, no pre-optimization beyond the free zero-weight skip already in Task 1, nothing downstream of oscillator generation touched). §9 Verification → covered by each task's own test additions; the "default patch sounds identical to today" claim is verified structurally (matching default values, called out explicitly in Task 2 and Task 3's commit messages) since a byte-for-byte audio regression test isn't in the existing suite's style for this kind of change — the closest existing pattern (`RenderFingerprintTests.cpp`) is left as an explicit follow-up if the user wants it before merge.

**Placeholder scan:** No TBD/TODO/"add error handling" language anywhere. Every step shows exact before/after code verified against the live files during planning (not guessed), or an exact command with an expected result. (An earlier draft of Task 2 Step 3 had a stray transcription artifact — caught and removed during this self-review, not left in with a note.)

**Type consistency:** `ParamSnapshot` field names (`osc1BlendSine`, `osc1PulseDuty`, `mixerOsc1Level`, etc.) are identical across Task 2 (declares + assigns via APVTS), Task 3 (Voice reads), and Task 4 (removal of the old ones, unaffected new ones). `Oscillator::setBlend(sine, tri, saw, pulse)` argument order is identical in Task 1's declaration, Task 1's Voice.cpp call site, and Task 3's three call sites. No renames between tasks.
