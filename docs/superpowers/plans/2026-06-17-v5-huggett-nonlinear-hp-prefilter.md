# Nonlinear Huggett + HP Pre-Filter Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn the spine's linear Huggett model into a true-to-life nonlinear filter (asymmetric drive shapers with ADAA, a self-limiting resonance saturator) and add an always-available, dedicated HP pre-filter in front of the main multimode filter.

**Architecture:** Approach A — small reusable primitives (`AsymSaturator`, `DcBlocker`, `NlSvfCell`) composed by both the main `HuggettFilter` (selectable model) and a new fixed `HuggettHpStage` that runs in the spine before the active model. Per-voice state stays heap-allocated at `prepare` (RT-safe; in-place migration deferred to Plan 3). Light/ADAA quality only (HQ oversampling tiers are v5.1).

**Tech Stack:** JUCE 8.0.4, C++17, CMake. JUCE `UnitTest` for tests.

## Global Constraints

- **Build with `-j4`, never bare `-j`** (bare `-j` OOMs the JUCE compile → 0-byte object → confusing link error). Configure: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`. Tests target: `cmake --build build --target k2000_tests -j4`. Run: `./build/tests/k2000_tests`.
- **No per-voice `juce::dsp::Oversampling` object** (Q12). This milestone is **Light/ADAA only**; no oversampling at all.
- **Heap-free on the audio thread.** Allocation is allowed only inside `prepare()`/`makeState()`. No allocation in `process*`, `setCommon`, `updateParameters`, or `render`.
- **Full stereo, per voice.** Every `process` handles L and R (channels 0 and 1).
- **Additive params only.** New params take defaults that leave existing presets sounding identical (`hpEnable` defaults off).
- **Self-oscillation / saturation constants are calibration constants.** Where a numeric constant is marked `// CALIB`, it is tuned during the task to pass the stated test, then later A/B'd against the user's Summit. Start from the value given.
- After adding any `.cpp`, add it to both `CMakeLists.txt` (plugin target, next to the other `src/dsp/spine/*.cpp`) and `tests/CMakeLists.txt`. Header-only files need no CMake change.

---

### Task 1: `AsymSaturator` — asymmetric tanh drive shaper with 1st-order ADAA

**Files:**
- Create: `src/dsp/spine/AsymSaturator.h`
- Test: `tests/HuggettNonlinearTests.cpp` (new)
- Modify: `tests/CMakeLists.txt` (add `HuggettNonlinearTests.cpp`)

**Interfaces:**
- Produces: `class AsymSaturator { struct State { float x1[2], G1[2]; void reset(); }; void setDrive(float drive01, float biasFixed, float maxDriveDb); bool engaged() const; float process(float x, int ch, State& s) const; };`

- [ ] **Step 1: Write the failing test**

Create `tests/HuggettNonlinearTests.cpp`:

```cpp
#include <juce_core/juce_core.h>
#include "../src/dsp/spine/AsymSaturator.h"
#include <cmath>

class HuggettNonlinearTests : public juce::UnitTest {
public:
    HuggettNonlinearTests() : juce::UnitTest("HuggettNonlinear") {}

    // RMS of a buffer.
    static float rms(const std::vector<float>& v) {
        double s = 0; for (float x : v) s += double(x) * x;
        return (float) std::sqrt(s / v.size());
    }

    void runTest() override {
        beginTest("AsymSaturator: disengaged at zero drive, engaged when driven");
        {
            AsymSaturator sat;
            sat.setDrive(0.0f, 0.0f, 30.0f);
            expect(!sat.engaged(), "zero drive is a no-op");
            sat.setDrive(0.5f, 0.18f, 30.0f);
            expect(sat.engaged(), "driven stage engages");
        }

        beginTest("AsymSaturator: adds even harmonics (asymmetry) and is bounded");
        {
            AsymSaturator sat; sat.setDrive(1.0f, 0.25f, 30.0f);
            AsymSaturator::State st; st.reset();
            const double sr = 48000.0, f = 220.0;
            float peak = 0.0f; double dcAcc = 0.0; int N = 4096;
            for (int i = 0; i < N; ++i) {
                float x = 0.7f * std::sin(2.0 * juce::MathConstants<double>::pi * f * i / sr);
                float y = sat.process(x, 0, st);
                peak = std::max(peak, std::abs(y));
                if (i > N / 2) dcAcc += y;            // asymmetric shaper -> nonzero DC
            }
            expect(peak < 2.0f, "output bounded: " + juce::String(peak));
            expect(std::abs(dcAcc) > 1.0e-3, "asymmetry produces DC offset");
        }
    }
};
static HuggettNonlinearTests huggettNonlinearTestsInstance;
```

Add `HuggettNonlinearTests.cpp` to the `add_executable(k2000_tests ...)` list in `tests/CMakeLists.txt` (after `SpineFilterTests.cpp`).

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target k2000_tests -j4`
Expected: FAIL — `AsymSaturator.h` not found / `AsymSaturator` undefined.

- [ ] **Step 3: Write the implementation**

Create `src/dsp/spine/AsymSaturator.h`:

```cpp
#pragma once
#include <cmath>
#include <algorithm>

// Asymmetric tanh drive shaper with 1st-order antiderivative antialiasing (ADAA)
// and partial RMS level compensation. Config (gain/bias/comp) is shared across
// voices; the ADAA memory is per-voice (State). g(x) = comp * tanh(gain*x + bias).
// ADAA is applied on g(x): y[n] = (G(x[n]) - G(x[n-1])) / (x[n] - x[n-1]),
// G(x) = (comp/gain) * logcosh(gain*x + bias). Midpoint fallback when x ~= x[-1].
class AsymSaturator {
public:
    struct State {
        float x1[2] = {0.0f, 0.0f};   // previous raw input, per channel
        float G1[2] = {0.0f, 0.0f};   // previous antiderivative value, per channel
        void reset() noexcept { x1[0]=x1[1]=0.0f; G1[0]=G1[1]=0.0f; }
    };

    // drive01 in [0,1] -> up to maxDriveDb of input gain. biasFixed is the stage's
    // fixed asymmetry (even-harmonic) offset. Call once per block.
    void setDrive(float drive01, float biasFixed, float maxDriveDb) noexcept {
        const float dB = std::max(0.0f, drive01) * maxDriveDb;
        gain_ = std::pow(10.0f, dB / 20.0f);
        bias_ = biasFixed;
        const float full = (gain_ > 1.0f) ? (1.0f / std::tanh(gain_)) : 1.0f;
        comp_ = 1.0f + 0.75f * (full - 1.0f);          // ~75% RMS compensation // CALIB
        engaged_ = (gain_ > 1.0001f) || (bias_ != 0.0f);
    }

    bool engaged() const noexcept { return engaged_; }

    float process(float x, int ch, State& s) const noexcept {
        const float u = gain_ * x + bias_;
        const float G = (comp_ / gain_) * logcosh(u);
        const float dx = x - s.x1[ch];
        float y;
        if (std::abs(dx) > 1.0e-5f)
            y = (G - s.G1[ch]) / dx;
        else
            y = comp_ * std::tanh(gain_ * (0.5f * (x + s.x1[ch])) + bias_); // midpoint
        s.x1[ch] = x;
        s.G1[ch] = G;
        return y;
    }

private:
    static float logcosh(float z) noexcept {
        const float a = std::abs(z);
        return a + std::log1p(std::exp(-2.0f * a)) - 0.6931472f; // ln 2
    }
    float gain_ = 1.0f, bias_ = 0.0f, comp_ = 1.0f;
    bool  engaged_ = false;
};
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests`
Expected: PASS — `HuggettNonlinear` test group reports 0 failures (and the prior suite total still passes).

- [ ] **Step 5: Commit**

```bash
git add src/dsp/spine/AsymSaturator.h tests/HuggettNonlinearTests.cpp tests/CMakeLists.txt
git commit -m "feat(spine): AsymSaturator — asymmetric tanh drive shaper with ADAA"
```

---

### Task 2: `DcBlocker` — one-pole stereo DC removal

**Files:**
- Create: `src/dsp/spine/DcBlocker.h`
- Test: `tests/HuggettNonlinearTests.cpp` (append a test)

**Interfaces:**
- Produces: `class DcBlocker { void prepare(double sr); void reset(); float process(float x, int ch); };`

- [ ] **Step 1: Write the failing test** — append inside `runTest()` of `tests/HuggettNonlinearTests.cpp` and add the include `#include "../src/dsp/spine/DcBlocker.h"` at the top:

```cpp
beginTest("DcBlocker removes a constant offset, keeps audio");
{
    DcBlocker dc; dc.prepare(48000.0); dc.reset();
    std::vector<float> out;
    for (int i = 0; i < 8192; ++i) {
        float x = 0.5f + std::sin(2.0 * juce::MathConstants<double>::pi * 200.0 * i / 48000.0);
        out.push_back(dc.process(x, 0));
    }
    double tail = 0; for (int i = 6000; i < 8192; ++i) tail += out[(size_t) i];
    expect(std::abs(tail / 2192.0) < 0.02, "DC removed from tail");
    std::vector<float> ac(out.begin() + 6000, out.end());
    expect(rms(ac) > 0.5f, "audio preserved: " + juce::String(rms(ac)));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target k2000_tests -j4`
Expected: FAIL — `DcBlocker.h` not found.

- [ ] **Step 3: Write the implementation**

Create `src/dsp/spine/DcBlocker.h`:

```cpp
#pragma once

// One-pole DC blocker, stereo. y[n] = x[n] - x[n-1] + R*y[n-1], corner ~8 Hz.
class DcBlocker {
public:
    void prepare(double sampleRate) noexcept {
        R_ = 1.0f - (float) (2.0 * 3.14159265358979 * 8.0 / sampleRate); // ~8 Hz // CALIB
    }
    void reset() noexcept { x1_[0]=x1_[1]=y1_[0]=y1_[1]=0.0f; }
    float process(float x, int ch) noexcept {
        const float y = x - x1_[ch] + R_ * y1_[ch];
        x1_[ch] = x; y1_[ch] = y;
        return y;
    }
private:
    float R_ = 0.999f;
    float x1_[2] = {0,0}, y1_[2] = {0,0};
};
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/dsp/spine/DcBlocker.h tests/HuggettNonlinearTests.cpp
git commit -m "feat(spine): DcBlocker — one-pole stereo DC removal"
```

---

### Task 3: `NlSvfCell` — nonlinear SVF cell with self-limiting resonance

**Files:**
- Create: `src/dsp/spine/NlSvfCell.h` (mirrors `TptSvfCell.h` + resonance saturator)
- Test: `tests/HuggettNonlinearTests.cpp` (append)

**Interfaces:**
- Produces: `class NlSvfCell { enum Tap{LP,HP,BP,Notch}; void prepare(double); void reset(); void setCutoff(float); void setResonance(float); void setResSat(float); void process(float& l, float& r, int tap); };`
- Note: `setResonance(r)` accepts `r` in `[0,1]`; the cell maps it to a Q range wide enough to self-oscillate near `r=1` (only safe with `setResSat > 0`).

- [ ] **Step 1: Write the failing test** — add include `#include "../src/dsp/spine/NlSvfCell.h"` and append:

```cpp
beginTest("NlSvfCell ~= linear at low resonance (no saturation drift)");
{
    NlSvfCell c; c.prepare(48000.0); c.setCutoff(1000.0f); c.setResonance(0.1f); c.setResSat(0.1f);
    float peakLow = 0, peakHigh = 0;
    auto sweep = [&](double f, float& peak){ c.reset(); const int N=8192;
        for (int i=0;i<N;++i){ float x=std::sin(2.0*juce::MathConstants<double>::pi*f*i/48000.0);
            float l=x,r=x; c.process(l,r,NlSvfCell::LP); if(i>N/2) peak=std::max(peak,std::abs(l)); } };
    sweep(100.0, peakLow); sweep(10000.0, peakHigh);
    expect(peakLow > 0.7f && peakHigh < 0.1f, "LP shape intact at low res");
}

beginTest("NlSvfCell self-oscillation is bounded at max resonance");
{
    NlSvfCell c; c.prepare(48000.0); c.setCutoff(800.0f); c.setResonance(1.0f); c.setResSat(1.0f);
    c.reset();
    float peak = 0; bool nan = false;
    // tiny impulse to kick it, then run 0.5 s of silence
    float l = 1.0f, r = 1.0f; c.process(l, r, NlSvfCell::LP);
    for (int i = 0; i < 24000; ++i) {
        float a = 0.0f, b = 0.0f; c.process(a, b, NlSvfCell::LP);
        if (!std::isfinite(a)) nan = true;
        if (i > 2000) peak = std::max(peak, std::abs(a));
    }
    expect(!nan, "no NaN/Inf");
    expect(peak < 4.0f, "self-osc amplitude bounded: " + juce::String(peak));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target k2000_tests -j4`
Expected: FAIL — `NlSvfCell.h` not found.

- [ ] **Step 3: Write the implementation**

Create `src/dsp/spine/NlSvfCell.h`:

```cpp
#pragma once
#include <juce_core/juce_core.h>
#include <cmath>
#include <algorithm>

// Nonlinear TPT state-variable cell. Linear core identical to TptSvfCell (Cytomic
// SvfLinearTrapOptimised), plus a resonance-loop saturator injected as a delta on
// the cell input (the "fbExtra" technique): preserves the closed-form solve and
// vanishes as the signal gets small, so it stays ~linear at low level. The Q range
// is widened so it can self-oscillate near max resonance — only stable because the
// saturator self-limits the loop. See docs/architecture/nonlinear-filter-modeling.md.
class NlSvfCell {
public:
    enum Tap { LP = 0, HP = 1, BP = 2, Notch = 3 };

    void prepare(double sampleRate) noexcept { sampleRate_ = sampleRate; dirty_ = true; }
    void reset() noexcept {
        ic1_[0]=ic2_[0]=ic1_[1]=ic2_[1]=0.0f; bp_[0]=bp_[1]=0.0f;
    }
    void setCutoff(float hz) noexcept    { if (hz != cutoffHz_) { cutoffHz_ = hz; dirty_ = true; } }
    void setResonance(float r) noexcept  { if (r != resonance_) { resonance_ = r; dirty_ = true; } }
    void setResSat(float amt) noexcept   { resSat_ = std::clamp(amt, 0.0f, 1.0f); }

    void process(float& left, float& right, int tap) noexcept {
        if (dirty_) recompute();
        left  = step(left,  0, tap);
        right = step(right, 1, tap);
    }

private:
    float step(float v0, int ch, int tap) noexcept {
        if (resSat_ > 0.0f) {
            const float bpPrev = bp_[ch];
            v0 -= k_ * resSat_ * (satRes(bpPrev) - bpPrev);   // nonlinear correction only
        }
        const float v3 = v0 - ic2_[ch];
        const float v1 = a1_ * ic1_[ch] + a2_ * v3;
        const float v2 = ic2_[ch] + a2_ * ic1_[ch] + a3_ * v3;
        ic1_[ch] = 2.0f * v1 - ic1_[ch];
        ic2_[ch] = 2.0f * v2 - ic2_[ch];
        bp_[ch]  = v1;
        switch (tap) {
            case HP:    return v0 - k_ * v1 - v2;
            case BP:    return v1;
            case Notch: return v0 - k_ * v1;
            case LP:
            default:    return v2;
        }
    }
    static float satRes(float x) noexcept {            // asymmetric, monotonic, bounded
        constexpr float b = 0.18f;                     // asymmetry // CALIB
        return padTanh(x + b) - padTanh(b);            // f(0)=0
    }
    static float padTanh(float x) noexcept {           // Padé 3/2 tanh, clamped
        const float x2 = x * x;
        return std::clamp(x * (27.0f + x2) / (27.0f + 9.0f * x2), -1.0f, 1.0f);
    }
    void recompute() noexcept {
        const float cutoff = std::clamp(cutoffHz_, 16.0f, float(sampleRate_ * 0.45));
        const float res    = std::clamp(resonance_, 0.0f, 0.999f);
        const float Q = 0.5f + res * res * 49.5f;      // reaches Q~50 (self-osc) // CALIB
        g_ = float(std::tan(juce::MathConstants<double>::pi * cutoff / sampleRate_));
        k_ = 1.0f / Q;
        a1_ = 1.0f / (1.0f + g_ * (g_ + k_));
        a2_ = g_ * a1_;
        a3_ = g_ * a2_;
        dirty_ = false;
    }
    double sampleRate_ = 44100.0;
    float cutoffHz_ = 1000.0f, resonance_ = 0.0f, resSat_ = 0.0f;
    float g_=0, k_=0, a1_=0, a2_=0, a3_=0;
    bool  dirty_ = true;
    float ic1_[2]={0,0}, ic2_[2]={0,0}, bp_[2]={0,0};
};
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests`
Expected: PASS. If the self-osc peak diverges, lower the `49.5f` Q ceiling or raise the satRes asymmetry/clamp until bounded (these are the `// CALIB` knobs).

- [ ] **Step 5: Commit**

```bash
git add src/dsp/spine/NlSvfCell.h tests/HuggettNonlinearTests.cpp
git commit -m "feat(spine): NlSvfCell — nonlinear SVF with self-limiting resonance"
```

---

### Task 4: Make `HuggettFilter` nonlinear (pre-drive, resonance sat, post-drive, DC blocker)

**Files:**
- Modify: `src/dsp/spine/HuggettFilter.h`, `src/dsp/spine/HuggettFilter.cpp`
- Test: `tests/HuggettNonlinearTests.cpp` (append); existing `tests/SpineFilterTests.cpp` must stay green.

**Interfaces:**
- Consumes: `AsymSaturator`, `NlSvfCell`, `DcBlocker`.
- Produces (new public API on `HuggettFilter`): `void setPostDrive(float drive01) noexcept;` — and `VoiceState` now holds `NlSvfCell a, b; AsymSaturator::State pre, post; DcBlocker dc;`. `setCommon`'s `drive` arg is now wired (pre-filter input drive).

- [ ] **Step 1: Write the failing test** — append:

```cpp
beginTest("HuggettFilter: driving changes harmonic content but stays bounded");
{
    HuggettFilter h; h.prepare(48000.0); h.setMode(HuggettFilter::Mode::LP);
    h.setSlope(HuggettFilter::Slope::db24); h.setSeparation(0.0f);
    std::unique_ptr<FilterModel::State> st(h.makeState());

    auto runSaw = [&](float drive){
        h.setCommon(2000.0f, 0.3f, drive); h.setPostDrive(drive); h.reset(*st);
        std::vector<float> out; const int N = 4096;
        for (int i = 0; i < N; ++i) {
            float ph = std::fmod(220.0 * i / 48000.0, 1.0);
            float x = 0.6f * (2.0f * (float) ph - 1.0f);     // saw
            float l = x, r = x; h.processStereo(*st, &l, &r, 1);
            out.push_back(l);
        }
        return out;
    };
    auto clean = runSaw(0.0f);
    auto dirty = runSaw(1.0f);
    expect(rms(dirty) > 0.0f, "driven output is non-silent");
    for (float v : dirty) expect(std::abs(v) < 4.0f, "driven output bounded");
    // crude harmonic-change proxy: driven differs materially from clean
    double diff = 0; for (size_t i = 0; i < clean.size(); ++i) diff += std::abs(dirty[i] - clean[i]);
    expect(diff / clean.size() > 0.01, "drive changes the signal");
}
```

Add `#include "../src/dsp/spine/HuggettFilter.h"` and `#include <vector>` / `#include <memory>` to the test if not already present (they are via earlier tasks' includes; add `<vector>` if missing).

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target k2000_tests -j4`
Expected: FAIL — `setPostDrive` undefined.

- [ ] **Step 3: Update the header**

In `src/dsp/spine/HuggettFilter.h`: replace the includes/`VoiceState`/members. New header body:

```cpp
#pragma once
#include "FilterModel.h"
#include "NlSvfCell.h"
#include "AsymSaturator.h"
#include "DcBlocker.h"

// Nonlinear Huggett: two NlSvfCells + asymmetric pre/post drive shapers (ADAA) +
// a self-limiting resonance saturator + an output DC blocker. setCommon's `drive`
// is the pre-filter input drive; post-drive is a Huggett-bank param.
class HuggettFilter : public FilterModel {
public:
    enum class Mode  { LP, BP, HP };
    enum class Slope { db12, db24 };

    struct VoiceState : public FilterModel::State {
        NlSvfCell a, b;
        AsymSaturator::State pre, post;
        DcBlocker dc;
    };

    void prepare(double sampleRate) noexcept override { sampleRate_ = sampleRate; }
    State* makeState() const override;
    void reset(State& s) const noexcept override;

    void setCommon(float cutoffHz, float resonance, float drive) noexcept override;
    void setMode(Mode m) noexcept       { mode_ = m; }
    void setSlope(Slope s) noexcept     { slope_ = s; }
    void setSeparation(float oct) noexcept { separationOct_ = oct; }
    void setPostDrive(float drive01) noexcept { postDrive_ = drive01; }

    void processStereo(State& s, float* left, float* right, int numSamples) const noexcept override;

private:
    static int tapForMode(Mode m) noexcept {
        switch (m) { case Mode::BP: return NlSvfCell::BP;
                     case Mode::HP: return NlSvfCell::HP;
                     case Mode::LP: default: return NlSvfCell::LP; }
    }
    static constexpr float kPreDriveDb  = 30.0f;   // CALIB
    static constexpr float kPostDriveDb = 24.0f;   // CALIB
    static constexpr float kPreBias  = 0.25f;      // CALIB (pre is the "dirty" end)
    static constexpr float kPostBias = 0.15f;      // CALIB

    double sampleRate_ = 44100.0;
    float  cutoffHz_ = 1000.0f, resonance_ = 0.0f, separationOct_ = 0.0f;
    float  preDrive_ = 0.0f, postDrive_ = 0.0f;
    Mode   mode_  = Mode::LP;
    Slope  slope_ = Slope::db24;
};
```

- [ ] **Step 4: Update the .cpp**

Replace `src/dsp/spine/HuggettFilter.cpp` body:

```cpp
#include "HuggettFilter.h"
#include <cmath>

FilterModel::State* HuggettFilter::makeState() const {
    auto* vs = new VoiceState();
    vs->a.prepare(sampleRate_);
    vs->b.prepare(sampleRate_);
    vs->dc.prepare(sampleRate_);
    return vs;
}

void HuggettFilter::reset(State& s) const noexcept {
    auto& vs = static_cast<VoiceState&>(s);
    vs.a.reset(); vs.b.reset();
    vs.pre.reset(); vs.post.reset();
    vs.dc.reset();
}

void HuggettFilter::setCommon(float cutoffHz, float resonance, float drive) noexcept {
    cutoffHz_  = cutoffHz;
    resonance_ = resonance;
    preDrive_  = drive;
}

void HuggettFilter::processStereo(State& s, float* left, float* right, int n) const noexcept {
    auto& vs = static_cast<VoiceState&>(s);
    const int tap = tapForMode(mode_);
    const float cutB = cutoffHz_ * std::pow(2.0f, separationOct_);

    vs.a.setCutoff(cutoffHz_); vs.a.setResonance(resonance_); vs.a.setResSat(resonance_);
    vs.b.setCutoff(cutB);      vs.b.setResonance(resonance_); vs.b.setResSat(resonance_);

    AsymSaturator pre, post;
    pre.setDrive(preDrive_,  kPreBias,  kPreDriveDb);
    post.setDrive(postDrive_, kPostBias, kPostDriveDb);
    const bool preOn  = pre.engaged();
    const bool postOn = post.engaged();

    for (int i = 0; i < n; ++i) {
        float l = left[i], r = right[i];
        if (preOn) { l = pre.process(l, 0, vs.pre); r = pre.process(r, 1, vs.pre); }
        vs.a.process(l, r, tap);
        if (slope_ == Slope::db24) vs.b.process(l, r, tap);
        if (postOn) { l = post.process(l, 0, vs.post); r = post.process(r, 1, vs.post); }
        l = vs.dc.process(l, 0); r = vs.dc.process(r, 1);
        left[i] = l; right[i] = r;
    }
}
```

- [ ] **Step 5: Run tests to verify pass (incl. existing)**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests`
Expected: PASS — the new HuggettFilter test passes AND `SpineFilter` (24>12 slope, etc.) still passes. The existing slope test uses resonance 0 → `setResSat(0)` → exactly linear, so it is unaffected. The DC blocker very slightly attenuates LF; the slope test probes 2 kHz so it's unaffected.

- [ ] **Step 6: Commit**

```bash
git add src/dsp/spine/HuggettFilter.h src/dsp/spine/HuggettFilter.cpp tests/HuggettNonlinearTests.cpp
git commit -m "feat(spine): nonlinear HuggettFilter (pre/post drive, resonance sat, DC block)"
```

---

### Task 5: `HuggettHpStage` — dedicated always-available HP pre-filter

**Files:**
- Create: `src/dsp/spine/HuggettHpStage.h`, `src/dsp/spine/HuggettHpStage.cpp`
- Modify: `CMakeLists.txt` (plugin target), `tests/CMakeLists.txt`
- Test: `tests/HpPreFilterTests.cpp` (new); add to `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `class HuggettHpStage { struct State{...}; enum class Slope{db12,db24}; void prepare(double); State* makeState() const; void reset(State&) const; void setParams(float cutoffHz, float resonance, Slope, float drive01) noexcept; void processStereo(State&, float* l, float* r, int n) const noexcept; };`
- Note: HP-only (no mode); reuses `NlSvfCell` (HP tap) + one `AsymSaturator` (pre-drive, lighter bias) + a `DcBlocker`. State is heap-allocated at `prepare` time by the owner.

- [ ] **Step 1: Write the failing test**

Create `tests/HpPreFilterTests.cpp`:

```cpp
#include <juce_core/juce_core.h>
#include "../src/dsp/spine/HuggettHpStage.h"
#include <cmath>
#include <memory>

class HpPreFilterTests : public juce::UnitTest {
public:
    HpPreFilterTests() : juce::UnitTest("HpPreFilter") {}
    void runTest() override {
        beginTest("HP stage passes highs, attenuates lows; 24 dB steeper than 12");
        {
            HuggettHpStage hp; hp.prepare(48000.0);
            std::unique_ptr<HuggettHpStage::State> st(hp.makeState());
            auto mag = [&](double f, HuggettHpStage::Slope slope){
                hp.setParams(1000.0f, 0.0f, slope, 0.0f); hp.reset(*st);
                const int N=8192; float peak=0;
                for (int i=0;i<N;++i){ float x=std::sin(2.0*juce::MathConstants<double>::pi*f*i/48000.0);
                    float l=x,r=x; hp.processStereo(*st,&l,&r,1); if(i>N/2) peak=std::max(peak,std::abs(l)); }
                return peak;
            };
            expect(mag(8000.0, HuggettHpStage::Slope::db12) > 0.7f, "highs pass");
            float low12 = mag(200.0, HuggettHpStage::Slope::db12);
            float low24 = mag(200.0, HuggettHpStage::Slope::db24);
            expect(low12 < 0.5f, "lows cut (12dB)");
            expect(low24 < low12, "24 dB steeper below corner");
        }
        beginTest("HP self-oscillation bounded at max resonance");
        {
            HuggettHpStage hp; hp.prepare(48000.0);
            std::unique_ptr<HuggettHpStage::State> st(hp.makeState());
            hp.setParams(1200.0f, 1.0f, HuggettHpStage::Slope::db24, 0.0f); hp.reset(*st);
            float l=1.0f,r=1.0f; hp.processStereo(*st,&l,&r,1);
            float peak=0; bool nan=false;
            for (int i=0;i<24000;++i){ float a=0,b=0; hp.processStereo(*st,&a,&b,1);
                if(!std::isfinite(a)) nan=true; if(i>2000) peak=std::max(peak,std::abs(a)); }
            expect(!nan && peak < 4.0f, "bounded: " + juce::String(peak));
        }
    }
};
static HpPreFilterTests hpPreFilterTestsInstance;
```

Add `HpPreFilterTests.cpp` to `tests/CMakeLists.txt` and add `../src/dsp/spine/HuggettHpStage.cpp` to its source list.

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target k2000_tests -j4`
Expected: FAIL — `HuggettHpStage.h` not found.

- [ ] **Step 3: Write the header**

Create `src/dsp/spine/HuggettHpStage.h`:

```cpp
#pragma once
#include "NlSvfCell.h"
#include "AsymSaturator.h"
#include "DcBlocker.h"

// Fixed, always-available HP-only Huggett stage that runs in the spine BEFORE the
// selectable model (mirrors the Summit HP->LP series routing). Own pre-drive at a
// lighter voicing than the main filter. Not a FilterModel (not swappable).
class HuggettHpStage {
public:
    enum class Slope { db12, db24 };

    struct State {
        NlSvfCell a, b;
        AsymSaturator::State pre;
        DcBlocker dc;
    };

    void prepare(double sampleRate) noexcept { sampleRate_ = sampleRate; }
    State* makeState() const;                 // prepare-time alloc only
    void reset(State& s) const noexcept;

    void setParams(float cutoffHz, float resonance, Slope slope, float drive01) noexcept {
        cutoffHz_ = cutoffHz; resonance_ = resonance; slope_ = slope; drive_ = drive01;
    }
    void processStereo(State& s, float* left, float* right, int numSamples) const noexcept;

private:
    static constexpr float kHpDriveDb = 24.0f;   // CALIB
    static constexpr float kHpBias    = 0.10f;   // CALIB (lighter than the main filter)
    double sampleRate_ = 44100.0;
    float  cutoffHz_ = 20.0f, resonance_ = 0.0f, drive_ = 0.0f;
    Slope  slope_ = Slope::db12;
};
```

- [ ] **Step 4: Write the .cpp**

Create `src/dsp/spine/HuggettHpStage.cpp`:

```cpp
#include "HuggettHpStage.h"

HuggettHpStage::State* HuggettHpStage::makeState() const {
    auto* st = new State();
    st->a.prepare(sampleRate_);
    st->b.prepare(sampleRate_);
    st->dc.prepare(sampleRate_);
    return st;
}

void HuggettHpStage::reset(State& s) const noexcept {
    s.a.reset(); s.b.reset(); s.pre.reset(); s.dc.reset();
}

void HuggettHpStage::processStereo(State& s, float* left, float* right, int n) const noexcept {
    s.a.setCutoff(cutoffHz_); s.a.setResonance(resonance_); s.a.setResSat(resonance_);
    s.b.setCutoff(cutoffHz_); s.b.setResonance(resonance_); s.b.setResSat(resonance_);

    AsymSaturator pre; pre.setDrive(drive_, kHpBias, kHpDriveDb);
    const bool preOn = pre.engaged();

    for (int i = 0; i < n; ++i) {
        float l = left[i], r = right[i];
        if (preOn) { l = pre.process(l, 0, s.pre); r = pre.process(r, 1, s.pre); }
        s.a.process(l, r, NlSvfCell::HP);
        if (slope_ == Slope::db24) s.b.process(l, r, NlSvfCell::HP);
        l = s.dc.process(l, 0); r = s.dc.process(r, 1);
        left[i] = l; right[i] = r;
    }
}
```

Add `../src/dsp/spine/HuggettHpStage.cpp` to the plugin target source list in `CMakeLists.txt` (next to the other `src/dsp/spine/*.cpp`).

- [ ] **Step 5: Run test to verify it passes**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/dsp/spine/HuggettHpStage.h src/dsp/spine/HuggettHpStage.cpp \
        tests/HpPreFilterTests.cpp tests/CMakeLists.txt CMakeLists.txt
git commit -m "feat(spine): HuggettHpStage — dedicated always-available HP pre-filter"
```

---

### Task 6: Parameters — add HP + post-drive params

**Files:**
- Modify: `src/params/ParamSnapshot.h`, `src/params/Parameters.h`, `src/params/Parameters.cpp`
- Test: `tests/ParamSnapshotTests.cpp` (append a case)

**Interfaces:**
- Produces (new `ParamSnapshot` fields): `int hpEnable; float hpCutoffHz; float hpResonance; int hpSlope; float hpDrive; float huggettPostDrive;`
- Produces (new `LayerIds` fields): `spineHpEnable, spineHpCutoff, spineHpResonance, spineHpSlope, spineHpDrive, spinePostDrive`.

- [ ] **Step 1: Write the failing test** — in `tests/ParamSnapshotTests.cpp`, add assertions that the new fields are read. Mirror the existing pattern; append within the existing test body:

```cpp
// New v5.0 HP + post-drive params default sanely
expect(s.hpEnable == 0, "HP disabled by default");
expect(s.huggettPostDrive == 0.0f, "post-drive defaults 0");
expect(s.hpSlope == 0, "HP slope defaults 12 dB");
```

(Use the same `apvts`/`snapshot` construction already present in that file.)

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target k2000_tests -j4`
Expected: FAIL — `s.hpEnable` etc. are not members.

- [ ] **Step 3: Extend `ParamSnapshot.h`** — add to the `// Spine filter (layer.spine.*)` block:

```cpp
    // HP pre-filter (always-available, before the main model)
    int   hpEnable      = 0;      // 0=off 1=on
    float hpCutoffHz    = 20.0f;
    float hpResonance   = 0.0f;
    int   hpSlope       = 0;      // 0=12 dB, 1=24 dB
    float hpDrive       = 0.0f;
    // Main Huggett post-filter drive (Huggett bank)
    float huggettPostDrive = 0.0f;
```

- [ ] **Step 4: Extend `Parameters.h`** — add to `struct LayerIds` the fields:

```cpp
                 spineHpEnable, spineHpCutoff, spineHpResonance, spineHpSlope, spineHpDrive,
                 spinePostDrive;
```

- [ ] **Step 5: Extend `Parameters.cpp`** — in `buildIds()` add:

```cpp
    id.spineHpEnable    = p + "spine.hp.enable";
    id.spineHpCutoff    = p + "spine.hp.cutoff";
    id.spineHpResonance = p + "spine.hp.resonance";
    id.spineHpSlope     = p + "spine.hp.slope";
    id.spineHpDrive     = p + "spine.hp.drive";
    id.spinePostDrive   = p + "spine.huggett.postDrive";
```

In `createLayout()`, inside the per-layer loop after the existing spine params, add:

```cpp
        layout.add(std::make_unique<BoolParam>(juce::ParameterID{id.spineHpEnable, 1},
            "Spine HP Enable " + juce::String(i), false));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.spineHpCutoff, 1},
            "Spine HP Cutoff " + juce::String(i),
            juce::NormalisableRange<float>{20.0f, 20000.0f, 0.0f, 0.25f}, 20.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.spineHpResonance, 1},
            "Spine HP Resonance " + juce::String(i),
            juce::NormalisableRange<float>{0.0f, 1.0f, 0.0f}, 0.0f));
        layout.add(std::make_unique<ChoiceParam>(juce::ParameterID{id.spineHpSlope, 1},
            "Spine HP Slope " + juce::String(i), juce::StringArray{"12 dB", "24 dB"}, 0));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.spineHpDrive, 1},
            "Spine HP Drive " + juce::String(i),
            juce::NormalisableRange<float>{0.0f, 1.0f, 0.0f}, 0.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.spinePostDrive, 1},
            "Spine Post Drive " + juce::String(i),
            juce::NormalisableRange<float>{0.0f, 1.0f, 0.0f}, 0.0f));
```

In `snapshot()` add:

```cpp
    s.hpEnable        = (int) raw(apvts, id.spineHpEnable);
    s.hpCutoffHz      = raw(apvts, id.spineHpCutoff);
    s.hpResonance     = raw(apvts, id.spineHpResonance);
    s.hpSlope         = (int) raw(apvts, id.spineHpSlope);
    s.hpDrive         = raw(apvts, id.spineHpDrive);
    s.huggettPostDrive = raw(apvts, id.spinePostDrive);
```

- [ ] **Step 6: Run test to verify it passes**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests`
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add src/params/ParamSnapshot.h src/params/Parameters.h src/params/Parameters.cpp tests/ParamSnapshotTests.cpp
git commit -m "feat(params): add HP pre-filter + Huggett post-drive params"
```

---

### Task 7: `Layer` — own and configure the HP stage + main post-drive

**Files:**
- Modify: `src/Layer.h`, `src/Layer.cpp`
- Test: `tests/LayerTests.cpp` (append) or `tests/MultiLayerTests.cpp` — verify the HP stage is configured/exposed.

**Interfaces:**
- Produces: `const HuggettHpStage* Layer::hpStage() const;` (returns the configured shared HP stage).

- [ ] **Step 1: Write the failing test** — append to `tests/LayerTests.cpp`:

```cpp
beginTest("Layer exposes a configured HP stage");
{
    Layer layer; layer.prepare(48000.0, 256);
    ParamSnapshot s; s.hpEnable = 1; s.hpCutoffHz = 2000.0f; s.hpResonance = 0.0f;
    s.hpSlope = 1; s.hpDrive = 0.0f;
    layer.updateParameters(s);
    expect(layer.hpStage() != nullptr, "hp stage present");
    // Filter a low tone through the exposed HP stage -> attenuated.
    std::unique_ptr<HuggettHpStage::State> st(layer.hpStage()->makeState());
    layer.hpStage()->reset(*st);
    const int N=8192; float peak=0;
    for (int i=0;i<N;++i){ float x=std::sin(2.0*juce::MathConstants<double>::pi*200.0*i/48000.0);
        float l=x,r=x; layer.hpStage()->processStereo(*st,&l,&r,1); if(i>N/2) peak=std::max(peak,std::abs(l)); }
    expect(peak < 0.6f, "HP attenuates 200 Hz: " + juce::String(peak));
}
```

Add `#include "../src/dsp/spine/HuggettHpStage.h"` and `#include <memory>` to `LayerTests.cpp` if missing.

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target k2000_tests -j4`
Expected: FAIL — `Layer::hpStage` undefined.

- [ ] **Step 3: Update `Layer.h`** — add the include `#include "dsp/spine/HuggettHpStage.h"`, the accessor, and the member:

```cpp
    const HuggettHpStage* hpStage() const { return &hpStage_; }
```
```cpp
    HuggettHpStage hpStage_;   // fixed HP pre-stage (config shared; per-voice state in the slot)
```

- [ ] **Step 4: Update `Layer.cpp`** — in `prepare()` add `hpStage_.prepare(sr);`. In `updateParameters()`, after the `huggett_` block, configure the HP stage and the main post-drive:

```cpp
    hpStage_.setParams(snapshot_.hpCutoffHz, snapshot_.hpResonance,
                       snapshot_.hpSlope == 0 ? HuggettHpStage::Slope::db12
                                              : HuggettHpStage::Slope::db24,
                       snapshot_.hpDrive);
    if (huggett_) huggett_->setPostDrive(snapshot_.huggettPostDrive);
```

- [ ] **Step 5: Run test to verify it passes**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/Layer.h src/Layer.cpp tests/LayerTests.cpp
git commit -m "feat(layer): own + configure HP pre-stage and main post-drive"
```

---

### Task 8: `SpineFilterSlot` + `Voice` — run HP before the model, enable-gated

**Files:**
- Modify: `src/dsp/spine/SpineFilterSlot.h`, `src/dsp/spine/SpineFilterSlot.cpp`, `src/Voice.cpp`
- Test: `tests/MultiLayerTests.cpp` (append) — a note plays through the HP when enabled.

**Interfaces:**
- Produces (new `SpineFilterSlot` API): `void prepare(double sr, const FilterModel* modelForState, const HuggettHpStage* hpForState); void reset(const FilterModel* model, const HuggettHpStage* hp); void processStereo(const HuggettHpStage* hp, bool hpEnabled, const FilterModel* model, float* l, float* r, int n);`

- [ ] **Step 1: Write the failing test** — append to `tests/MultiLayerTests.cpp`, mirroring its existing harness (`Program prog; ... VoiceManager vm; ...`), a case that enables the HP with a high cutoff and asserts low-frequency energy drops vs. HP disabled. (Use the file's existing `energy()` helper and `renderBlock` pattern.)

```cpp
beginTest("Enabling the HP pre-filter removes low end from layer 0");
{
    Program prog; prog.prepare(48000.0, 256);
    ParamSnapshot s = /* the file's default snapshot helper */ makeDefaultSnapshot();
    s.spineModel = 0; s.svfCutoffHz = 18000.0f; s.svfResonance = 0.0f; // main filter wide open
    s.hpEnable = 0;
    prog.slot(0).layer.updateParameters(s);
    prog.slot(0).routing = LayerRouting{};  // enabled, full range (match existing helper)
    VoiceManager vm; vm.setProgram(&prog); vm.prepare(48000.0, 256);
    // ... noteOn a low note (e.g. 36), render a block into outL/outR, measure energy ...
    float eOff = /* energy(outL) for hpEnable=0 */;

    s.hpEnable = 1; s.hpCutoffHz = 4000.0f;
    prog.slot(0).layer.updateParameters(s);
    VoiceManager vm2; vm2.setProgram(&prog); vm2.prepare(48000.0, 256);
    float eOn = /* energy with HP enabled */;
    expect(eOn < eOff, "HP at 4 kHz cuts a low note: off=" + juce::String(eOff) + " on=" + juce::String(eOn));
}
```

(Fill the render boilerplate from the existing tests in this file — `noteOn`, `renderBlock(outL,outR,N,midi)`, `energy()`. Reuse the file's snapshot/routing helpers; do not invent new ones.)

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target k2000_tests -j4`
Expected: FAIL — `processStereo` signature mismatch / HP not applied.

- [ ] **Step 3: Update `SpineFilterSlot.h`**

```cpp
#pragma once
#include <memory>
#include "FilterModel.h"
#include "HuggettHpStage.h"

class SpineFilterSlot {
public:
    void prepare(double sampleRate, const FilterModel* modelForState,
                 const HuggettHpStage* hpForState);
    void reset(const FilterModel* model, const HuggettHpStage* hp) noexcept;
    void processStereo(const HuggettHpStage* hp, bool hpEnabled,
                       const FilterModel* model, float* left, float* right, int numSamples) noexcept;

private:
    std::unique_ptr<FilterModel::State>   state_;
    std::unique_ptr<HuggettHpStage::State> hpState_;
};
```

- [ ] **Step 4: Update `SpineFilterSlot.cpp`**

```cpp
#include "SpineFilterSlot.h"

void SpineFilterSlot::prepare(double, const FilterModel* modelForState,
                              const HuggettHpStage* hpForState) {
    state_.reset(modelForState ? modelForState->makeState() : nullptr);
    hpState_.reset(hpForState ? hpForState->makeState() : nullptr);
}

void SpineFilterSlot::reset(const FilterModel* model, const HuggettHpStage* hp) noexcept {
    if (model && state_) model->reset(*state_);
    if (hp && hpState_)  hp->reset(*hpState_);
}

void SpineFilterSlot::processStereo(const HuggettHpStage* hp, bool hpEnabled,
                                    const FilterModel* model, float* l, float* r, int n) noexcept {
    if (hpEnabled && hp && hpState_) hp->processStereo(*hpState_, l, r, n);
    if (model && state_) model->processStereo(*state_, l, r, n);
}
```

- [ ] **Step 5: Update `Voice.cpp`** — update the three spine call sites:
  - `prepare`: `spine_.prepare(sr, layer_ ? layer_->spineModel() : nullptr, layer_ ? layer_->hpStage() : nullptr);`
  - `reset` and `noteOn`: `spine_.reset(layer_ ? layer_->spineModel() : nullptr, layer_ ? layer_->hpStage() : nullptr);`
  - `render` (replace the `spine_.processStereo(...)` line):

```cpp
    std::copy(tmpL, tmpL + numSamples, tmpR);
    spine_.processStereo(layer_->hpStage(), s.hpEnable != 0,
                         layer_->spineModel(), tmpL, tmpR, numSamples);
```

- [ ] **Step 6: Run tests to verify pass**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests`
Expected: PASS — new HP integration test passes; existing `SpineFilter`/`MultiLayer`/`Voice` tests still pass. Note `SpineFilterTests.cpp`'s `slot.prepare(48000.0, &h)` and `slot.processStereo(&h, ...)` calls now need updating to the new signatures — pass `nullptr, false` for the HP args:
  - `slot.prepare(48000.0, &h, nullptr);`
  - `slot.processStereo(nullptr, false, &h, &l, &r, 1);`
  Update those two lines in `tests/SpineFilterTests.cpp`.

- [ ] **Step 7: Commit**

```bash
git add src/dsp/spine/SpineFilterSlot.h src/dsp/spine/SpineFilterSlot.cpp \
        src/Voice.cpp tests/MultiLayerTests.cpp tests/SpineFilterTests.cpp
git commit -m "feat(spine): run HP pre-stage before the model in the voice slot"
```

---

### Task 9: UI — Layout B (HP band + post-drive) in the Filter section

**Files:**
- Modify: `src/PluginEditor.h`, `src/PluginEditor.cpp`
- Test: `tests/ParamBinderTests.cpp` already guards binding generically; verify build + manual smoke (no new unit test — UI layout is visual).

**Interfaces:**
- Consumes: `params::layerIds(...)` new ids (`spineHpEnable`, `spineHpCutoff`, `spineHpResonance`, `spineHpSlope`, `spineHpDrive`, `spinePostDrive`).

- [ ] **Step 1: Add members to `PluginEditor.h`** — alongside the existing `spineModel_`, `spineSlope_`, `spineSeparation_` filter-section members, add:

```cpp
    juce::ToggleButton hpEnable_;
    juce::ComboBox     hpSlope_;
    LabeledKnob        hpCutoff_, hpReso_, hpDrive_;
    LabeledKnob        spinePostDrive_;
    juce::Label        hpSectionLbl_;
```

- [ ] **Step 2: Populate + make visible in `buildStaticControls`/ctor** — mirror the existing filter-section setup (lines ~56–75 of `PluginEditor.cpp`):

```cpp
    hpSectionLbl_.setText("HP PRE", juce::dontSendNotification);
    hpSectionLbl_.setJustificationType(juce::Justification::centredLeft);
    filterSection_.addAndMakeVisible(hpSectionLbl_);
    hpEnable_.setButtonText("on");
    filterSection_.addAndMakeVisible(hpEnable_);
    hpSlope_.addItemList(juce::StringArray{ "12 dB", "24 dB" }, 1);
    filterSection_.addAndMakeVisible(hpSlope_);
    filterSection_.addAndMakeVisible(hpCutoff_);
    filterSection_.addAndMakeVisible(hpReso_);
    filterSection_.addAndMakeVisible(hpDrive_);
    filterSection_.addAndMakeVisible(spinePostDrive_);
```

- [ ] **Step 3: Bind in `bindLayer`** — mirror the existing `binder_.bind(spineModel_, ids.spineModel)` block:

```cpp
    binder_.bind(hpEnable_,            ids.spineHpEnable);
    binder_.bind(hpCutoff_.slider(),   ids.spineHpCutoff);
    binder_.bind(hpReso_.slider(),     ids.spineHpResonance);
    binder_.bind(hpSlope_,             ids.spineHpSlope);
    binder_.bind(hpDrive_.slider(),    ids.spineHpDrive);
    binder_.bind(spinePostDrive_.slider(), ids.spinePostDrive);
```

(Confirm `ParamBinder` has a `bind(juce::ToggleButton&, ...)` overload; if not, add one following the existing `bind(juce::ComboBox&, ...)` pattern — a `ButtonAttachment`.)

- [ ] **Step 4: Lay out in `resized()`** — restructure the filter-section sub-rows into Layout B: an HP band row (label · enable · HP cut · HP reso · HP slope · HP drive), then the existing main rows, adding `spinePostDrive_` to the bottom main row. Use the existing `layoutCells(...)` helper. Bump `setSize(...)` height modestly (e.g. `setSize(1040, 740)`) so the taller filter section fits.

- [ ] **Step 5: Build the standalone + VST3 and smoke**

Run: `cmake --build build --target k2000_VST3 k2000_Standalone -j4`
Expected: builds clean. Launch the Standalone; confirm the HP band renders, the enable toggle works, and the new knobs move. (Manual visual check — UI is not unit-tested.)

- [ ] **Step 6: Commit**

```bash
git add src/PluginEditor.h src/PluginEditor.cpp src/gui/ParamBinder.h src/gui/ParamBinder.cpp
git commit -m "feat(ui): Layout B — HP pre-filter band + post-drive in the Filter section"
```

---

### Task 10: `g_eff` integrator droop (subtle OTA "darkens when loud")

**Files:**
- Modify: `src/dsp/spine/NlSvfCell.h`
- Test: `tests/HuggettNonlinearTests.cpp` (append)

- [ ] **Step 1: Write the failing test** — a loud input should slightly lower the effective cutoff (more LF, less HF) vs. a quiet input at the same settings:

```cpp
beginTest("NlSvfCell: loud input droops cutoff (darker) vs quiet");
{
    auto hfMag = [](float amp){
        NlSvfCell c; c.prepare(48000.0); c.setCutoff(2000.0f); c.setResonance(0.0f); c.setResSat(0.0f);
        c.reset(); const int N=8192; float peak=0;
        for (int i=0;i<N;++i){ float x=amp*std::sin(2.0*juce::MathConstants<double>::pi*2000.0*i/48000.0);
            float l=x,r=x; c.process(l,r,NlSvfCell::LP); if(i>N/2) peak=std::max(peak,std::abs(l)); }
        return peak / amp;   // normalized gain at cutoff
    };
    expect(hfMag(2.0f) < hfMag(0.05f) * 0.99f, "loud input is darker (droop active)");
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target k2000_tests -j4`
Expected: FAIL — no droop yet (gain equal at both amplitudes).

- [ ] **Step 3: Implement the droop in `NlSvfCell`** — add a slow per-channel input-magnitude envelope and apply a per-block effective-cutoff scale in `recompute()`. Add members and update `step`/`recompute`:

```cpp
    // in step(), before the resonance saturator, track input level:
    env_[ch] += 0.0005f * (std::abs(v0) - env_[ch]);          // ~one-pole, CALIB
    // in recompute(), scale g by the droop (use the louder channel's env):
    const float drv = std::max(env_[0], env_[1]);
    const float gmScale = 1.0f / (1.0f + 0.20f * drv * drv);   // CALIB
    g_ = float(std::tan(juce::MathConstants<double>::pi * cutoff / sampleRate_)) * gmScale;
```

Add `float env_[2] = {0,0};` to members and `env_[0]=env_[1]=0.0f;` to `reset()`. Mark `dirty_ = true` once per block from the caller is unnecessary — instead recompute coefficients every block already happens via `setCutoff`/`setResonance`; to make the droop take effect, set `dirty_ = true` at the top of `process` is too often. Instead, recompute `gmScale` cheaply: keep the droop as a multiply applied to `g_` inside `recompute`, and force a recompute each block by having the owning filter call `setCutoff` every block (it already does in `processStereo`, but only flags dirty on change). Simplest: compute `gmScale` and apply it directly in `step` to a local prewarped `g` is too costly per-sample. **Resolution:** recompute coefficients once per block unconditionally in the owners' `processStereo` by calling a new `NlSvfCell::updateBlock()` that recomputes with the current env. Add:

```cpp
    void updateBlock() noexcept { dirty_ = true; }   // owners call once per block
```

and have `HuggettFilter::processStereo` / `HuggettHpStage::processStereo` call `vs.a.updateBlock(); vs.b.updateBlock();` once before the sample loop (after the `setCutoff/setResonance/setResSat` calls). `recompute()` reads `env_` for the droop.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests`
Expected: PASS — loud input measurably darker; all prior tests still green (at low amplitude `env_`→small, `gmScale`→~1, so low-level behavior is unchanged within tolerance).

- [ ] **Step 5: Commit**

```bash
git add src/dsp/spine/NlSvfCell.h src/dsp/spine/HuggettFilter.cpp src/dsp/spine/HuggettHpStage.cpp tests/HuggettNonlinearTests.cpp
git commit -m "feat(spine): g_eff integrator droop (level-dependent cutoff sag)"
```

---

### Task 11: Version surface bump

**Files:**
- Modify: `CMakeLists.txt` (`project(k2000 VERSION ...)`)
- Verify: the panel title derives from `JucePlugin_VersionString` (per `release_version_surface`); update the literal if not.

- [ ] **Step 1: Bump the version** — change `project(k2000 VERSION 5.0.0 ...)` to `project(k2000 VERSION 5.1.0 ...)` in `CMakeLists.txt`.

- [ ] **Step 2: Verify the panel label** — grep the editor for the version string:

Run: `grep -rn "VersionString\|5\.0\.0\|setText.*v5" src/PluginEditor.cpp`
If the panel title is a hard-coded literal, change it to derive from `JucePlugin_VersionString`; if it already derives, no change.

- [ ] **Step 3: Build + verify**

Run: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --target k2000_Standalone -j4`
Expected: builds; panel shows v5.1.0.

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt src/PluginEditor.cpp
git commit -m "chore(v5.1): bump version surface to 5.1.0"
```

---

### Task 12: Full build + test + smoke verification

- [ ] **Step 1: Clean configure + full test run**

Run:
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target k2000_tests -j4
./build/tests/k2000_tests
```
Expected: `Summary: N tests, 0 failed` where N is the prior 69 plus the added groups (HuggettNonlinear, HpPreFilter) and the new cases.

- [ ] **Step 2: Build the plugin + standalone**

Run: `cmake --build build --target k2000_VST3 k2000_Standalone -j4`
Expected: clean build, no warnings-as-errors.

- [ ] **Step 3: Manual smoke (Standalone)** — load a saw patch; sweep main cutoff/resonance to self-oscillation (should sing, bounded); push `spine.drive` and `postDrive` (should get dirtier, not just louder); enable the HP, raise its cutoff (low end thins); toggle HP slope 12/24. Confirm no clicks, no runaway.

- [ ] **Step 4: Windows CI smoke (trusted target)** — push the branch and run the Windows build/load test:
```bash
git push -u origin feat/v5-huggett-nonlinear-hp-prefilter
gh workflow run build.yml --ref feat/v5-huggett-nonlinear-hp-prefilter
gh run watch <id> --exit-status
```
Expected: green. Load in Ableton 12 (the trusted smoke target).

- [ ] **Step 5: Calibration pass (manual, against the user's Summit)** — A/B the `// CALIB` constants (pre/post bias, drive dB ceilings, resonance Q ceiling, satRes asymmetry, `g_eff` droop, HP bias): self-osc pitch-vs-note, 12 vs 24 dB resonance character, drive even/odd ratio, HP voicing. Adjust constants, re-run tests, commit.

---

## Self-Review

**Spec coverage:** three nonlinear stages → Tasks 1 (pre/post shaper), 3+4 (resonance saturator), 4 (post-drive wiring); ADAA → Task 1; DC blocker → Tasks 2,4; HP pre-filter → Tasks 5,7,8; params → Task 6; UI Layout B → Task 9; `g_eff` → Task 10; performance (conditional-on-drive, stereo) → Tasks 4,5 (templated `<DriveOn>` is deferred to the Q11 perf-gate refinement, as the spec allows); preset migration (additive defaults) → Task 6 (defaults) + verified in Task 12; tests → throughout; version surface → Task 11. **Deferred per spec:** HQ oversampling tiers + keyboard (v5.1), nine dual routings, white-box OTA, in-place State migration (Plan 3) — intentionally not tasks here.

**Placeholder scan:** test boilerplate in Task 8 references the existing `MultiLayerTests` helpers (`makeDefaultSnapshot`, `energy`, `renderBlock`) rather than re-inventing them — the implementer fills the render lines from the same file; this is a deliberate "reuse the file's harness" instruction, not a TODO. All DSP code is complete.

**Type consistency:** `NlSvfCell`/`AsymSaturator::State`/`DcBlocker` signatures are defined in Tasks 1–3 and consumed unchanged in 4,5; `HuggettHpStage` API defined in 5 and consumed in 7,8; `SpineFilterSlot` new signatures (Task 8) are matched at every call site (Voice + SpineFilterTests). `setResSat`, `setPostDrive`, `setParams`, `hpStage()`, `updateBlock()` names are consistent across tasks.

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-06-17-v5-huggett-nonlinear-hp-prefilter.md`.
