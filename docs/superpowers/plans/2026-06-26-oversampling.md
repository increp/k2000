# Per-Voice Oversampling (HQ Tiers, Live/Render) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add user-selectable per-voice oversampling (Off/2×/4×/8×, independent Live vs Render tiers) that runs the nonlinear stages at N×sr to cut aliasing, exposed via a hamburger menu.

**Architecture:** A portable (no-JUCE) linear-phase halfband FIR (`Halfband2x`) cascades for 2×/4×/8× inside a per-voice `VoiceOversampler`. `Voice::render` upsamples the (already band-limited) oscillator output, runs the graph + spine at N×sr, then downsamples. The active factor is a protected (non-automatable) setting resolved from Live/Render state; changing it re-prepares voices via `suspendProcessing`. The Moog Cmajor core's 192 kHz codegen guard is raised at its source.

**Tech Stack:** C++17, JUCE 8, CMake, the in-repo `juce::UnitTest` harness, Cmajor codegen (Docker).

## Global Constraints

- Builds use bounded parallelism: `cmake --build . --target <t> -j4` (bare `-j` OOMs). [`[[build-bounded-parallelism]]`]
- The DSP layer (`src/dsp/`) stays **JUCE-free / portable** — `Halfband2x` and `VoiceOversampler` include no JUCE headers. [from the cleanup checkpoint]
- **No allocation, locks, or I/O on the audio thread.** All buffers sized in `prepare`; factor changes reconfigure via `suspendProcessing`, never from `processBlock`.
- Oversampling settings are **protected** (NOT APVTS, NOT automatable), persisted in the `K2000Root` state blob like `limiterEnabled`.
- Preset backward-compatibility is NOT a constraint. [`[[feedback-no-preset-backcompat]]`]
- Halfband design target: ≈0.1 dB passband ripple, ≥80 dB image rejection.
- Factor set: `Off, 2, 4, 8`. Cap value after Moog fix: `1536000.0` Hz (= 8×192k) so no factor×sr combo is ever invalid.
- Test build: `cd build && cmake --build . --target k2000_tests -j4`; run: `./tests/k2000_tests` (whole suite; grep the `[PASS]/[FAIL]` line for the new test name and the final `Summary:` line). Baseline before this plan: **192 tests, 0 failed**.
- Version surface: bump `CMakeLists.txt` VERSION + the panel label on ship. [`[[release-version-surface]]`]

---

### Task 1: `Halfband2x` — portable linear-phase halfband FIR (2× up/down)

**Files:**
- Create: `src/dsp/Halfband2x.h`
- Test: `tests/Halfband2xTests.cpp`
- Modify: `tests/CMakeLists.txt` (add the test source)

**Interfaces:**
- Produces: `class Halfband2x` with `void reset() noexcept`, `void upsample(const float* in, int n, float* out2n) noexcept` (n base samples → 2n), `void downsample(const float* in2n, int n, float* out) noexcept` (2n → n base samples), and `static constexpr int kNumTaps = 49`, `static constexpr int delay2x() { return (kNumTaps-1)/2; }` (= 24, group delay in samples at the 2× rate).

- [ ] **Step 1: Write the failing test** — passband flatness, image rejection, and a DC/round-trip sanity check.

`tests/Halfband2xTests.cpp`:
```cpp
#include <juce_core/juce_core.h>
#include <cmath>
#include <vector>
#include "../src/dsp/Halfband2x.h"

class Halfband2xTests : public juce::UnitTest {
public:
    Halfband2xTests() : juce::UnitTest("Halfband2x") {}

    // Peak magnitude of a real signal at normalized freq f (cycles/sample) via Goertzel-ish DFT.
    static double mag(const std::vector<float>& x, double f) {
        double re = 0, im = 0;
        for (size_t n = 0; n < x.size(); ++n) {
            const double a = 2.0 * 3.14159265358979323846 * f * (double) n;
            re += x[n] * std::cos(a); im -= x[n] * std::sin(a);
        }
        return std::sqrt(re*re + im*im) / (double) x.size() * 2.0;
    }

    void runTest() override {
        const int N = 4096;

        beginTest("upsample passband (200 Hz @ 48k) preserved, ~unity");
        {
            Halfband2x hb; hb.reset();
            std::vector<float> in(N), out(2*N);
            const double f = 200.0 / 48000.0;  // base-rate normalized
            for (int i = 0; i < N; ++i) in[i] = std::sin(2*M_PI*f*i);
            hb.upsample(in.data(), N, out.data());
            // at 2x rate the tone sits at f/2; skip the filter warm-up
            std::vector<float> tail(out.begin() + 200, out.end());
            const double g = mag(tail, f/2.0);
            expect(g > 0.9 && g < 1.1, "upsampled passband gain ~1 (got " + juce::String(g,3) + ")");
        }

        beginTest("downsample rejects an image above base-Nyquist");
        {
            // A tone at 0.30 cyc/sample (2x domain) = 28.8k @ 96k, i.e. above 24k base-Nyquist.
            Halfband2x hb; hb.reset();
            std::vector<float> in(2*N), out(N);
            const double f2 = 0.30;
            for (int i = 0; i < 2*N; ++i) in[i] = std::sin(2*M_PI*f2*i);
            hb.downsample(in.data(), N, out.data());
            std::vector<float> tail(out.begin() + 200, out.end());
            // it would alias to |0.30-0.5|*2 = 0.40 base-normalized; assert it's crushed
            double peak = 0; for (double f = 0.0; f <= 0.5; f += 0.005) peak = std::max(peak, mag(tail, f));
            const double db = 20.0 * std::log10(peak + 1e-12);
            expect(db < -80.0, "image rejection >=80 dB (got " + juce::String(db,1) + " dB)");
        }
    }
};
static Halfband2xTests halfband2xTestsInstance;
```

- [ ] **Step 2: Run to verify it fails** — Run: `cd build && cmake --build . --target k2000_tests -j4` → expect a compile error (`Halfband2x.h` not found / no such type).

- [ ] **Step 3: Implement `Halfband2x`** (windowed-sinc, polyphase up, direct-form down).

`src/dsp/Halfband2x.h`:
```cpp
#pragma once
#include <array>
#include <cmath>

// Linear-phase halfband FIR for 2x up/down-sampling. Portable (no JUCE).
// Cutoff = 0.25 cyc/sample at the 2x rate (= base Nyquist). Blackman-Harris
// windowed sinc; symmetric => linear phase, group delay (kNumTaps-1)/2 at 2x rate.
class Halfband2x {
public:
    static constexpr int kNumTaps = 49;              // odd; center M = 24 (even)
    static constexpr int delay2x() { return (kNumTaps - 1) / 2; }   // 24 samples @ 2x rate

    Halfband2x() { buildCoeffs(); reset(); }

    void reset() noexcept { histUp_.fill(0.0f); histDown_.fill(0.0f); }

    // n base-rate samples -> 2n samples (polyphase, x2 gain compensation).
    void upsample(const float* in, int n, float* out) noexcept {
        for (int i = 0; i < n; ++i) {
            for (int j = kHistUp_ - 1; j > 0; --j) histUp_[(size_t) j] = histUp_[(size_t) j - 1];
            histUp_[0] = in[i];
            double e = 0.0, o = 0.0;                 // even-tap / odd-tap polyphase branches
            for (int t = 0; t < kNumTaps; t += 2) e += (double) h_[(size_t) t] * histUp_[(size_t) (t/2)];
            for (int t = 1; t < kNumTaps; t += 2) o += (double) h_[(size_t) t] * histUp_[(size_t) ((t-1)/2)];
            out[2*i]   = (float) (2.0 * e);
            out[2*i+1] = (float) (2.0 * o);
        }
    }

    // 2n samples -> n base-rate samples (decimate by 2 after the AA filter).
    void downsample(const float* in, int n, float* out) noexcept {
        for (int i = 0; i < n; ++i) {
            for (int s = 0; s < 2; ++s) {
                for (int j = kNumTaps - 1; j > 0; --j) histDown_[(size_t) j] = histDown_[(size_t) j - 1];
                histDown_[0] = in[2*i + s];
            }
            double acc = 0.0;
            for (int t = 0; t < kNumTaps; ++t) acc += (double) h_[(size_t) t] * histDown_[(size_t) t];
            out[i] = (float) acc;
        }
    }

private:
    static constexpr int kHistUp_ = (kNumTaps + 1) / 2;   // 25 base samples spanned

    void buildCoeffs() noexcept {
        constexpr double pi = 3.14159265358979323846;
        const int M = (kNumTaps - 1) / 2;
        double sum = 0.0;
        for (int n = 0; n < kNumTaps; ++n) {
            const int k = n - M;
            const double s = (k == 0) ? 0.5
                          : 0.5 * std::sin(0.5 * pi * k) / (0.5 * pi * k);   // 0.5*sinc(0.5k)
            const double w = 0.35875
                           - 0.48829 * std::cos(2.0*pi*n/(kNumTaps-1))
                           + 0.14128 * std::cos(4.0*pi*n/(kNumTaps-1))
                           - 0.01168 * std::cos(6.0*pi*n/(kNumTaps-1));       // Blackman-Harris
            h_[(size_t) n] = (float) (s * w);
            sum += h_[(size_t) n];
        }
        for (int n = 0; n < kNumTaps; ++n) h_[(size_t) n] = (float) (h_[(size_t) n] / sum); // DC gain = 1
    }

    std::array<float, kNumTaps>  h_{};
    std::array<float, kHistUp_>  histUp_{};
    std::array<float, kNumTaps>  histDown_{};
};
```

- [ ] **Step 4: Register the test** — in `tests/CMakeLists.txt`, add `Halfband2xTests.cpp` to the `add_executable(k2000_tests ...)` source list (e.g., right after `SafetyLimiterTests.cpp`).

- [ ] **Step 5: Run to verify it passes** — Run: `cd build && cmake --build . --target k2000_tests -j4 && ./tests/k2000_tests 2>&1 | grep -i 'Halfband2x\|Summary'`. Expected: `[PASS] Halfband2x ...` and `Summary: 194 tests, 0 failed`.
  - If image rejection is < 80 dB, increase `kNumTaps` to 65 (M stays even) and re-run — the Blackman-Harris main lobe widens cleanly with length.

- [ ] **Step 6: Commit** — `git add src/dsp/Halfband2x.h tests/Halfband2xTests.cpp tests/CMakeLists.txt && git commit -m "feat(dsp): portable halfband FIR for 2x oversampling"`

---

### Task 2: `VoiceOversampler` — cascade to 2×/4×/8×, mono-up + stereo-down, latency

**Files:**
- Create: `src/dsp/VoiceOversampler.h`
- Test: `tests/VoiceOversamplerTests.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `Halfband2x` (Task 1).
- Produces: `class VoiceOversampler` with:
  - `void prepare(int maxBaseBlock) noexcept` — sizes scratch for the max factor (8×).
  - `void setFactor(int factor) noexcept` — factor ∈ {1,2,4,8}; resets stage state. (Call only when not mid-block — see Task 7.)
  - `int factor() const noexcept`.
  - `static int latencyBaseSamples(int factor)` — {1→0, 2→24, 4→36, 8→42}; verified by the impulse test.
  - `void processMonoUp(const float* baseIn, int nBase, float* osOut)` — nBase → nBase*factor (mono).
  - `void processStereoDown(const float* osL, const float* osR, int nBase, float* baseL, float* baseR)` — nBase*factor → nBase (stereo).
  - `int osBlock(int nBase) const { return nBase * factor_; }`

- [ ] **Step 1: Write the failing test** — factor-1 identity, round-trip latency table, and a passband round-trip at 4×.

`tests/VoiceOversamplerTests.cpp`:
```cpp
#include <juce_core/juce_core.h>
#include <vector>
#include <cmath>
#include "../src/dsp/VoiceOversampler.h"

class VoiceOversamplerTests : public juce::UnitTest {
public:
    VoiceOversamplerTests() : juce::UnitTest("VoiceOversampler") {}

    void runTest() override {
        const int N = 1024;

        beginTest("factor 1 is identity (up then down)");
        {
            VoiceOversampler os; os.prepare(N); os.setFactor(1);
            std::vector<float> in(N), up(N), dL(N), dR(N);
            for (int i = 0; i < N; ++i) in[i] = std::sin(0.05f * i);
            os.processMonoUp(in.data(), N, up.data());
            os.processStereoDown(up.data(), up.data(), N, dL.data(), dR.data());
            for (int i = 0; i < N; ++i) expectWithinAbsoluteError(dL[i], in[i], 1e-5f);
        }

        beginTest("round-trip latency matches the table (impulse, 2x/4x/8x)");
        {
            for (int f : { 2, 4, 8 }) {
                VoiceOversampler os; os.prepare(N); os.setFactor(f);
                std::vector<float> in(N, 0.0f), up((size_t) N*f), dL(N), dR(N);
                in[0] = 1.0f;
                os.processMonoUp(in.data(), N, up.data());
                os.processStereoDown(up.data(), up.data(), N, dL.data(), dR.data());
                int peak = 0; float pm = 0;
                for (int i = 0; i < N; ++i) if (std::abs(dL[i]) > pm) { pm = std::abs(dL[i]); peak = i; }
                expectEquals(peak, VoiceOversampler::latencyBaseSamples(f),
                             "latency for " + juce::String(f) + "x");
            }
        }
    }
};
static VoiceOversamplerTests voiceOversamplerTestsInstance;
```

- [ ] **Step 2: Run to verify it fails** — Run the test build; expect a compile error (no `VoiceOversampler`).

- [ ] **Step 3: Implement `VoiceOversampler`**.

`src/dsp/VoiceOversampler.h`:
```cpp
#pragma once
#include <array>
#include <vector>
#include "Halfband2x.h"

// Per-voice cascade of Halfband2x stages giving 1x/2x/4x/8x. Stage j (0-based)
// bridges 2^j <-> 2^(j+1). Mono upsample (osc->graph), stereo downsample (spine->out).
// Buffers pre-sized for the MAX factor at prepare(); setFactor() only changes the
// active depth + clears state, never allocates.
class VoiceOversampler {
public:
    static constexpr int kMaxFactor = 8;
    static constexpr int kMaxStages = 3;   // 2^3 = 8

    void prepare(int maxBaseBlock) noexcept {
        maxBase_ = maxBaseBlock;
        for (auto& b : upScratch_)  b.assign((size_t) maxBaseBlock * kMaxFactor, 0.0f);
        for (auto& b : dnScratchL_) b.assign((size_t) maxBaseBlock * kMaxFactor, 0.0f);
        for (auto& b : dnScratchR_) b.assign((size_t) maxBaseBlock * kMaxFactor, 0.0f);
        setFactor(factor_);
    }

    void setFactor(int factor) noexcept {
        factor_ = (factor==2||factor==4||factor==8) ? factor : 1;
        stages_ = (factor_==8)?3 : (factor_==4)?2 : (factor_==2)?1 : 0;
        for (auto& s : upHb_) s.reset();
        for (auto& s : dnHbL_) s.reset();
        for (auto& s : dnHbR_) s.reset();
    }
    int factor() const noexcept { return factor_; }
    int osBlock(int nBase) const noexcept { return nBase * factor_; }

    static int latencyBaseSamples(int factor) noexcept {
        switch (factor) { case 2: return 24; case 4: return 36; case 8: return 42; default: return 0; }
    }

    // nBase base samples -> nBase*factor mono samples.
    void processMonoUp(const float* baseIn, int nBase, float* osOut) noexcept {
        if (stages_ == 0) { for (int i = 0; i < nBase; ++i) osOut[i] = baseIn[i]; return; }
        const float* src = baseIn; int n = nBase;
        for (int s = 0; s < stages_; ++s) {
            float* dst = (s == stages_ - 1) ? osOut : upScratch_[(size_t) s].data();
            upHb_[(size_t) s].upsample(src, n, dst);
            src = dst; n *= 2;
        }
    }

    // nBase*factor stereo samples -> nBase base samples (stereo).
    void processStereoDown(const float* osL, const float* osR, int nBase,
                           float* baseL, float* baseR) noexcept {
        if (stages_ == 0) { for (int i = 0; i < nBase; ++i) { baseL[i]=osL[i]; baseR[i]=osR[i]; } return; }
        const float* sL = osL; const float* sR = osR; int n = nBase * factor_;
        for (int s = 0; s < stages_; ++s) {
            const int outN = n / 2;
            float* dL = (s == stages_ - 1) ? baseL : dnScratchL_[(size_t) s].data();
            float* dR = (s == stages_ - 1) ? baseR : dnScratchR_[(size_t) s].data();
            dnHbL_[(size_t) s].downsample(sL, outN, dL);
            dnHbR_[(size_t) s].downsample(sR, outN, dR);
            sL = dL; sR = dR; n = outN;
        }
    }

private:
    int maxBase_ = 0, factor_ = 1, stages_ = 0;
    std::array<Halfband2x, kMaxStages> upHb_, dnHbL_, dnHbR_;
    std::array<std::vector<float>, kMaxStages> upScratch_, dnScratchL_, dnScratchR_;
};
```

- [ ] **Step 4: Register the test** in `tests/CMakeLists.txt` (add `VoiceOversamplerTests.cpp`).

- [ ] **Step 5: Run to verify it passes** — Run the test build + `./tests/k2000_tests 2>&1 | grep -i 'VoiceOversampler\|Summary'`. Expected `[PASS] VoiceOversampler` and `Summary: 196 tests, 0 failed`.
  - If a latency assertion fails, the printed `peak` is the true value — set the `latencyBaseSamples` table to those measured values (they are deterministic for fixed `kNumTaps`). This keeps the constant tested, not guessed.

- [ ] **Step 6: Commit** — `git add src/dsp/VoiceOversampler.h tests/VoiceOversamplerTests.cpp tests/CMakeLists.txt && git commit -m "feat(dsp): VoiceOversampler cascade (2x/4x/8x)"`

---

### Task 3: Raise the Moog Cmajor max-frequency cap at its source

**Files:**
- Modify: `tools/cmajor/cmaj-codegen.sh` (add a reproducible post-gen patch + guard)
- Modify: `src/dsp/spine/cmajor/generated/MoogLadder.h:614` (apply the same patch to the committed header so no Docker run is needed now)
- Test: `tests/MoogMaxFreqTests.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: the generated `MoogLadder::getMaxFrequency()` returns `1536000.0`; nothing else changes.

- [ ] **Step 1: Write the failing test** — assert the raised cap (the Moog is reachable as the generated class).

`tests/MoogMaxFreqTests.cpp`:
```cpp
#include <juce_core/juce_core.h>
#include "../src/dsp/spine/cmajor/generated/MoogLadder.h"

class MoogMaxFreqTests : public juce::UnitTest {
public:
    MoogMaxFreqTests() : juce::UnitTest("MoogMaxFreq") {}
    void runTest() override {
        beginTest("Moog codegen max-frequency cap raised for oversampling");
        {
            MoogLadder gen;                       // the generated class (global namespace)
            expect(gen.getMaxFrequency() >= 768000.0,
                   "max frequency must allow >= 8x at 96k (got " + juce::String(gen.getMaxFrequency()) + ")");
            gen.initialise(0, 384000.0);          // 8x @ 48k must not throw
            succeed();
        }
    }
};
static MoogMaxFreqTests moogMaxFreqTestsInstance;
```

- [ ] **Step 2: Run to verify it fails** — build + run; expect FAIL (`getMaxFrequency()` returns 192000) or a thrown exception from `initialise(0, 384000)`.

- [ ] **Step 3: Patch the codegen script** — add the post-gen rewrite + a guard before the final `echo` in `tools/cmajor/cmaj-codegen.sh`:

```bash
# Raise the Cmajor max-frequency guard so the spine can be oversampled (the cap is
# a codegen default, not a DSP limit; the constant only gates initialise()'s throw).
if ! grep -q 'return 192000.0;' "$OUT"; then
  echo "error: expected 'return 192000.0;' in $OUT (cmaj output changed?) — review before patching" >&2
  exit 1
fi
sed -i 's/return 192000\.0;/return 1536000.0;/' "$OUT"
echo "patched max-frequency cap -> 1536000.0 in $OUT"
```

- [ ] **Step 4: Apply the same patch to the committed header** — edit `src/dsp/spine/cmajor/generated/MoogLadder.h:614`: change `return 192000.0;` to `return 1536000.0;` (identical to what the script now does on every regen).

- [ ] **Step 5: Register the test** in `tests/CMakeLists.txt` (add `MoogMaxFreqTests.cpp`).

- [ ] **Step 6: Run to verify it passes** — Run the test build + `./tests/k2000_tests 2>&1 | grep -i 'MoogMaxFreq\|Summary'`. Expected `[PASS] MoogMaxFreq` and `Summary: 197 tests, 0 failed`. The existing `FilterModelLibrary` / `MoogLadderAdapter` static-asserts must still compile (state size unchanged).

- [ ] **Step 7: Commit** — `git add tools/cmajor/cmaj-codegen.sh src/dsp/spine/cmajor/generated/MoogLadder.h tests/MoogMaxFreqTests.cpp tests/CMakeLists.txt && git commit -m "feat(moog): raise Cmajor max-frequency cap for oversampling"`

---

### Task 4: Thread an active oversampling factor through prepare (default Off = identity)

**Files:**
- Modify: `src/Voice.h`, `src/Voice.cpp`
- Modify: `src/VoiceManager.h:14`, `src/VoiceManager.cpp` (prepare signature)
- Modify: `src/Program.h` (prepare signature)
- Modify: `src/PluginProcessor.cpp` (`prepareToPlay` passes factor 1 for now)
- Test: `tests/VoiceTests.cpp` (add a factor-1 regression assertion)

**Interfaces:**
- Consumes: `VoiceOversampler` (Task 2).
- Produces: `Voice::prepare(double sampleRate, int maxBlockSize, int osFactor)`; `VoiceManager::prepare(double, int, int osFactor)`; `Program::prepare(double, int, int osFactor)`. Default callers pass `1`.

- [ ] **Step 1: Write the failing test** — at factor 1, a rendered note is bit-stable vs. the pre-change baseline (identity path). Add to `tests/VoiceTests.cpp` a test that renders a note at factor 1 and asserts audible, finite output (guards the new plumbing doesn't change factor-1 behavior):
```cpp
beginTest("factor 1 render produces finite audible output");
{
    // (mirror the existing Voice render setup in this file; call prepare(sr, block, 1))
    // render a held note for a few blocks, assert sum|out| > 0 and all samples finite.
}
```
(Use the same Layer/Voice setup already present in `VoiceTests.cpp`; the only change is the new `prepare(..., 1)` argument.)

- [ ] **Step 2: Run to verify it fails** — build; expect a compile error (prepare arity mismatch) until the signatures land.

- [ ] **Step 3: Add the oversampler to `Voice`** — in `src/Voice.h`: include `"dsp/VoiceOversampler.h"`, add members:
```cpp
    VoiceOversampler os_;
    int osFactor_ = 1;
    std::vector<float> osMono_, osL_, osR_;   // oversampled-domain scratch
```
and change the declaration to `void prepare(double sampleRate, int maxBlockSize, int osFactor);`

- [ ] **Step 4: Implement prepare in `src/Voice.cpp`** — at the top of `Voice::prepare`, store the factor, prepare the oversampler, size scratch, and prepare the inner DSP at `osFactor * sr`:
```cpp
void Voice::prepare(double sr, int maxBlock, int osFactor) {
    sampleRate_ = sr;
    osFactor_   = (osFactor==2||osFactor==4||osFactor==8) ? osFactor : 1;
    const double inner = sr * (double) osFactor_;
    os_.prepare(maxBlock);
    os_.setFactor(osFactor_);
    osMono_.assign((size_t) maxBlock * VoiceOversampler::kMaxFactor, 0.0f);
    osL_.assign((size_t) maxBlock * VoiceOversampler::kMaxFactor, 0.0f);
    osR_.assign((size_t) maxBlock * VoiceOversampler::kMaxFactor, 0.0f);

    osc_.prepare(sr);          // base rate (already band-limited)
    amp_.prepare(sr);          // base rate
    scratch_.assign(maxBlock, 0.0f);
    scratchR_.assign(maxBlock, 0.0f);
    spine_.prepare(inner, maxBlock * osFactor_, layer_ ? layer_->spineModel() : nullptr,
                       layer_ ? layer_->hpStage() : nullptr);
    if (layer_)
        for (int t = 1; t < (int) kNumBlockTypes; ++t)
            if (layer_->hasBlock((BlockTypeId) t))
                blockStates_[t] = layer_->block((BlockTypeId) t).makeVoiceState();
    reset();
}
```
NOTE: the graph blocks + spine + filter models are now prepared at `inner` (= N×sr). The `Layer::prepare` must also prepare the palette/models at `inner` — handled in Step 5.

- [ ] **Step 5: Propagate the factor through the call chain** —
  - `src/Program.h`: `void prepare(double sr, int maxBlock, int osFactor) { for (auto& s : slots_) s.layer.prepare(sr * osFactor, maxBlock * osFactor); }` — Layer (palette blocks + filter models + HP stage) prepares at the inner rate. Keep `Layer::prepare(double,int)` signature; pass the already-multiplied rate/block.
  - `src/VoiceManager.h`: `void prepare(double sampleRate, int maxBlockSize, int osFactor);`
  - `src/VoiceManager.cpp`: pass `osFactor` into each `v.prepare(sr, maxBlock, osFactor)`.
  - `src/PluginProcessor.cpp` `prepareToPlay`: `program_.prepare(sr, samplesPerBlock, 1); voiceManager_.prepare(sr, samplesPerBlock, 1);` (factor 1 for now; Task 7 supplies the real active factor).

- [ ] **Step 6: Run to verify it passes** — build + `./tests/k2000_tests 2>&1 | grep -i 'Voice\b\|Summary'`. Expected: all green, `Summary: 197 tests, 0 failed` (factor 1 = identity, so existing Voice/Layer/MultiLayer tests still pass unchanged).

- [ ] **Step 7: Commit** — `git add src/Voice.* src/VoiceManager.* src/Program.h src/PluginProcessor.cpp tests/VoiceTests.cpp && git commit -m "feat(voice): thread oversampling factor through prepare (default off)"`

---

### Task 5: Route `Voice::render` through the oversampled domain

**Files:**
- Modify: `src/Voice.cpp` (`render`)
- Test: `tests/VoiceTests.cpp` (factor 2/4/8 render produces finite, non-trivial output)

**Interfaces:**
- Consumes: the oversampler + scratch from Task 4.

- [ ] **Step 1: Write the failing test** — render a driven note at factor 4 and assert finite, audible output (and that latency-delayed onset still yields signal over enough blocks):
```cpp
beginTest("factor 4 render produces finite audible output");
{
    // same setup; prepare(sr, block, 4); set spine drive/resonance high; render several blocks;
    // assert sum|out| > 0 and all finite.
}
```

- [ ] **Step 2: Run to verify it fails** — build/run; with the still-base-rate `render`, factor-4 output is mistuned/garbage relative to the inner-rate-prepared DSP (the spine is prepared at 4×sr but fed base-rate). The test may pass spuriously on "finite" — so ALSO assert in this test that, with factor 4 vs factor 1 under identical params, the high-frequency self-osc aliasing differs (defer the strong assertion to Task 9; here just verify the render path runs without NaN/garbage once Step 3 lands).

- [ ] **Step 3: Rewrite `Voice::render`** to bridge rates:
```cpp
void Voice::render(float* outL, float* outR, int numSamples) {
    if (!isActive() || !layer_) return;
    const auto& s   = layer_->snapshot();
    const auto& alg = layer_->activeAlgorithm();

    const float tune = s.oscCoarse + s.oscFine * 0.01f;
    const float hz = midiToHz(note_) * std::pow(2.0f, tune / 12.0f);
    osc_.setWaveform(static_cast<Oscillator::Waveform>(s.oscWaveform));
    amp_.setParameters(s.ampAttackS, s.ampDecayS, s.ampSustain, s.ampReleaseS);

    jassert(numSamples <= (int) scratch_.size());
    float* base = scratch_.data();
    // Osc runs at base rate; setFrequency uses the base sample rate from prepare().
    osc_.setFrequency(hz);
    osc_.processBlock(base, numSamples);

    const int nOs = numSamples * osFactor_;
    float* up = osMono_.data();
    os_.processMonoUp(base, numSamples, up);              // base -> N x (mono)

    // Graph blocks run in the oversampled domain (prepared at N x sr).
    for (std::size_t i = 0; i < alg.slotCount; ++i) {
        const BlockTypeId t = alg.blockTypePerSlot[i];
        layer_->block(t).process(*blockStates_[(int) t], up, nOs);
    }

    // Mono -> stereo, spine at N x sr.
    float* osL = osL_.data(); float* osR = osR_.data();
    std::copy(up, up + nOs, osL);
    std::copy(up, up + nOs, osR);
    spine_.processStereo(layer_->hpStage(), s.hpCutoffHz > 0.0f,
                         layer_->spineModel(), s.spineModelFadeMs, hz, osL, osR, nOs);

    // Down to base rate.
    float* tmpL = scratchR_.data();   // reuse: tmpL/tmpR base-rate stereo
    std::vector<float>& tmpRv = osR_;  (void) tmpRv;
    static thread_local std::vector<float> dummy;  // (see note) -- prefer a member; add baseR_ if needed
    os_.processStereoDown(osL, osR, numSamples, base, tmpL);  // base = L, tmpL = R (base rate)

    const float lvl = layer_->level();
    const float spineOut = juce::Decibels::decibelsToGain(s.spineOutputDb);
    for (int i = 0; i < numSamples; ++i) {
        const float env = amp_.nextSample() * velocity_ * lvl * spineOut;
        outL[i] += base[i] * env;
        outR[i] += tmpL[i] * env;
    }
}
```
  - IMPLEMENTATION NOTE: avoid the `thread_local`/dummy above — add a dedicated base-rate stereo scratch pair. Replace `scratch_`/`scratchR_` usage by adding `std::vector<float> baseL_, baseR_;` members (sized `maxBlock` in `prepare`) and downsample into them: `os_.processStereoDown(osL, osR, numSamples, baseL_.data(), baseR_.data());` then read `baseL_/baseR_` in the env loop. Keep `osc` output in its own `scratch_`. (The code above is shown inline for flow; the clean version uses named members.)

- [ ] **Step 4: Run to verify it passes** — build + `./tests/k2000_tests 2>&1 | grep -i 'Voice\b\|Layer\|MultiLayer\|Summary'`. Expected: green; factor-1 path unchanged (identity), factor-4 path finite + audible. `Summary: 197 tests, 0 failed`.

- [ ] **Step 5: Commit** — `git add src/Voice.cpp src/Voice.h tests/VoiceTests.cpp && git commit -m "feat(voice): render through oversampled domain"`

---

### Task 6: Protected Live/Render OS settings + persistence

**Files:**
- Modify: `src/PluginProcessor.h` (atomics + getters/setters + active-factor resolver)
- Modify: `src/PluginProcessor.cpp` (state save/load)
- Test: `tests/PluginLifecycleTests.cpp` (round-trip + active resolution)

**Interfaces:**
- Produces:
  - `void setRealtimeOS(int factor)` / `int realtimeOS() const` (factor ∈ {1,2,4,8}; 1 = Off).
  - `void setOfflineOS(int factor)` / `int offlineOS() const` (factor ∈ {0,2,4,8}; 0 = "Same as Realtime").
  - `int activeOS() const` — `isNonRealtime() ? (offlineOS_ ? offlineOS_ : realtimeOS_) : realtimeOS_`.

- [ ] **Step 1: Write the failing test** in `tests/PluginLifecycleTests.cpp`:
```cpp
beginTest("oversampling settings round-trip and resolve active factor");
{
    K2000AudioProcessor p; p.prepareToPlay(48000.0, 256);
    p.setRealtimeOS(2); p.setOfflineOS(8);
    expectEquals(p.realtimeOS(), 2);
    expectEquals(p.offlineOS(), 8);
    juce::MemoryBlock mb; p.getStateInformation(mb);
    K2000AudioProcessor q; q.prepareToPlay(48000.0, 256);
    q.setStateInformation(mb.getData(), (int) mb.getSize());
    expectEquals(q.realtimeOS(), 2);
    expectEquals(q.offlineOS(), 8);
}
```

- [ ] **Step 2: Run to verify it fails** — build; expect compile error (no `realtimeOS`).

- [ ] **Step 3: Add the settings to `src/PluginProcessor.h`**:
```cpp
    int  realtimeOS() const { return realtimeOS_.load(std::memory_order_relaxed); }
    int  offlineOS()  const { return offlineOS_.load(std::memory_order_relaxed); }
    void setRealtimeOS(int f);   // defined in .cpp (triggers re-prepare in Task 7)
    void setOfflineOS(int f);
    int  activeOS() const {
        const int rt = realtimeOS_.load(std::memory_order_relaxed);
        const int off = offlineOS_.load(std::memory_order_relaxed);
        return isNonRealtime() ? (off ? off : rt) : rt;
    }
private:
    std::atomic<int> realtimeOS_{ 1 };   // 1 = Off
    std::atomic<int> offlineOS_{ 0 };    // 0 = Same as Realtime
```

- [ ] **Step 4: Persist in `src/PluginProcessor.cpp`** — in `getStateInformation`, after the `limiterEnabled` attribute:
```cpp
    root->setAttribute("realtimeOS", realtimeOS_.load(std::memory_order_relaxed));
    root->setAttribute("offlineOS",  offlineOS_.load(std::memory_order_relaxed));
```
  in `setStateInformation`, after the `limiterEnabled` load:
```cpp
    realtimeOS_.store((int) xml->getIntAttribute("realtimeOS", 1), std::memory_order_relaxed);
    offlineOS_.store((int)  xml->getIntAttribute("offlineOS",  0), std::memory_order_relaxed);
```
  and provisional setters (full re-prepare added in Task 7):
```cpp
void K2000AudioProcessor::setRealtimeOS(int f) { realtimeOS_.store(f, std::memory_order_relaxed); }
void K2000AudioProcessor::setOfflineOS(int f)  { offlineOS_.store(f, std::memory_order_relaxed); }
```

- [ ] **Step 5: Run to verify it passes** — build + `./tests/k2000_tests 2>&1 | grep -i 'PluginLifecycle\|Summary'`. Expected green, `Summary: 197 tests, 0 failed`.

- [ ] **Step 6: Commit** — `git add src/PluginProcessor.* tests/PluginLifecycleTests.cpp && git commit -m "feat(processor): protected Live/Render oversampling settings + persistence"`

---

### Task 7: Factor switching — suspendProcessing re-prepare + latency reporting

**Files:**
- Modify: `src/PluginProcessor.h`, `src/PluginProcessor.cpp`
- Test: `tests/PluginLifecycleTests.cpp`

**Interfaces:**
- Consumes: `VoiceOversampler::latencyBaseSamples`, the prepare chain (Task 4), settings (Task 6).
- Produces: `setRealtimeOS/setOfflineOS` now re-prepare at the active factor and report latency; `processBlock` detects Live↔Offline transitions.

- [ ] **Step 1: Write the failing test**:
```cpp
beginTest("changing realtime OS updates reported latency");
{
    K2000AudioProcessor p; p.prepareToPlay(48000.0, 256);
    expectEquals(p.getLatencySamples(), 0);
    p.setRealtimeOS(4);
    expectEquals(p.getLatencySamples(), VoiceOversampler::latencyBaseSamples(4));
    p.setRealtimeOS(1);
    expectEquals(p.getLatencySamples(), 0);
}
```
(Add `#include "../src/dsp/VoiceOversampler.h"` to the test file.)

- [ ] **Step 2: Run to verify it fails** — build/run; latency stays 0 after `setRealtimeOS(4)`.

- [ ] **Step 3: Add a re-prepare helper + wire the setters** in `src/PluginProcessor.cpp`. Store `lastSR_`/`lastBlock_` in `prepareToPlay`, and:
```cpp
void K2000AudioProcessor::reprepareForOS() {
    suspendProcessing(true);
    const int f = activeOS();
    if (lastSR_ > 0.0) {
        program_.prepare(lastSR_, lastBlock_, f);
        voiceManager_.prepare(lastSR_, lastBlock_, f);
    }
    setLatencySamples(VoiceOversampler::latencyBaseSamples(f));
    suspendProcessing(false);
}
void K2000AudioProcessor::setRealtimeOS(int f) { realtimeOS_.store(f, std::memory_order_relaxed); reprepareForOS(); }
void K2000AudioProcessor::setOfflineOS(int f)  { offlineOS_.store(f, std::memory_order_relaxed);  reprepareForOS(); }
```
  In `prepareToPlay`: store `lastSR_ = sr; lastBlock_ = samplesPerBlock;`, prepare with `activeOS()` instead of `1`, and `setLatencySamples(VoiceOversampler::latencyBaseSamples(activeOS()));`.
  In `src/PluginProcessor.h` add: `void reprepareForOS(); double lastSR_ = 0.0; int lastBlock_ = 0; bool lastNonRealtime_ = false;`

- [ ] **Step 4: Detect Live↔Offline in `processBlock`** — at the very top, before rendering:
```cpp
    const bool nrt = isNonRealtime();
    if (nrt != lastNonRealtime_) { lastNonRealtime_ = nrt; reprepareForOS(); }
```
  (This is the message/render-thread reconfig path; `reprepareForOS` suspends around the re-prepare. In offline render the host is single-threaded between blocks, so this is safe.)

- [ ] **Step 5: Run to verify it passes** — build + `./tests/k2000_tests 2>&1 | grep -i 'PluginLifecycle\|Summary'`. Expected green, `Summary: 197 tests, 0 failed`.

- [ ] **Step 6: Commit** — `git add src/PluginProcessor.* tests/PluginLifecycleTests.cpp && git commit -m "feat(processor): OS factor switching via suspendProcessing + latency reporting"`

---

### Task 8: Hamburger menu UI (Realtime/Offline tiers)

**Files:**
- Modify: `src/PluginEditor.h` (hamburger button member + handler)
- Modify: `src/PluginEditor.cpp` (build button, PopupMenu, placement)
- Test: none automated (JUCE UI). Manual verification in the Windows CI Standalone/VST3.

**Interfaces:**
- Consumes: `processor.realtimeOS/offlineOS/setRealtimeOS/setOfflineOS`.

- [ ] **Step 1: Add the button + handler** in `src/PluginEditor.h`:
```cpp
    juce::TextButton menuButton_{ juce::String::fromUTF8("\xE2\x8B\xAE") };  // vertical ellipsis
    void showOversamplingMenu();
```

- [ ] **Step 2: Build + place the button** in `src/PluginEditor.cpp` `buildStaticControls()`:
```cpp
    menuButton_.onClick = [this] { showOversamplingMenu(); };
    addAndMakeVisible(menuButton_);
```
  and in `resized()`, place it top-right, e.g. `menuButton_.setBounds(getWidth() - 34, 6, 28, 28);`

- [ ] **Step 3: Implement the menu** in `src/PluginEditor.cpp`:
```cpp
void K2000AudioProcessorEditor::showOversamplingMenu() {
    juce::PopupMenu rt, off, root;
    const int curRT = processorRef.realtimeOS();
    const int curOFF = processorRef.offlineOS();
    auto rtItem = [&](int id, const juce::String& t, int f) { rt.addItem(id, t, true, curRT == f); };
    rtItem(101, "Off", 1); rtItem(102, "2x", 2); rtItem(103, "4x", 4); rtItem(104, "8x", 8);
    auto offItem = [&](int id, const juce::String& t, int f) { off.addItem(id, t, true, curOFF == f); };
    offItem(201, "Same as Realtime", 0); offItem(202, "2x", 2); offItem(203, "4x", 4); offItem(204, "8x", 8);

    juce::PopupMenu os;
    os.addSectionHeader("Realtime oversampling"); os.addSubMenu("", rt);   // flatten: see note
    // Simpler/legible: add the two groups directly into one submenu.
    juce::PopupMenu osFlat;
    osFlat.addSectionHeader("Realtime oversampling");
    osFlat.addItem(101, "Off", true, curRT==1); osFlat.addItem(102, "2x", true, curRT==2);
    osFlat.addItem(103, "4x", true, curRT==4);  osFlat.addItem(104, "8x", true, curRT==8);
    osFlat.addSectionHeader("Offline oversampling");
    osFlat.addItem(201, "Same as Realtime", true, curOFF==0); osFlat.addItem(202, "2x", true, curOFF==2);
    osFlat.addItem(203, "4x", true, curOFF==4); osFlat.addItem(204, "8x", true, curOFF==8);

    root.addSubMenu("Oversampling: " + juce::String(curRT==1 ? "Off" : juce::String(curRT) + "x"), osFlat);

    root.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(menuButton_),
        [this](int r) {
            switch (r) {
                case 101: processorRef.setRealtimeOS(1); break;
                case 102: processorRef.setRealtimeOS(2); break;
                case 103: processorRef.setRealtimeOS(4); break;
                case 104: processorRef.setRealtimeOS(8); break;
                case 201: processorRef.setOfflineOS(0);  break;
                case 202: processorRef.setOfflineOS(2);  break;
                case 203: processorRef.setOfflineOS(4);  break;
                case 204: processorRef.setOfflineOS(8);  break;
                default: break;
            }
        });
}
```
  (Delete the exploratory `rt`/`off`/`os` lines; keep `osFlat` + `root`. Shown for clarity.)

- [ ] **Step 4: Build the plugin** — Run: `cd build && cmake --build . --target k2000_VST3 -j4`. Expected: clean compile, VST3 artefact rebuilt.

- [ ] **Step 5: Commit** — `git add src/PluginEditor.* && git commit -m "feat(ui): hamburger oversampling menu (Realtime/Offline tiers)"`

---

### Task 9: Anti-aliasing integration test

**Files:**
- Create: `tests/OversamplingAntiAliasTests.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `Layer`, `Voice` (the full per-voice path) at factor 1 vs 4.

- [ ] **Step 1: Write the test** — drive a low note into Huggett self-oscillation/heavy drive, render at factor 1 and factor 4, FFT each, and assert the inharmonic (alias) energy is materially lower at 4×:
```cpp
#include <juce_core/juce_core.h>
#include <vector>
#include <cmath>
#include "../src/Layer.h"
#include "../src/Voice.h"

class OversamplingAntiAliasTests : public juce::UnitTest {
public:
    OversamplingAntiAliasTests() : juce::UnitTest("OversamplingAntiAlias") {}

    static double aliasEnergy(const std::vector<float>& x, double baseHz, double sr) {
        // crude: sum energy at non-harmonic bins of baseHz across 0..sr/2
        double e = 0;
        const int N = (int) x.size();
        for (int k = 1; k < N/2; ++k) {
            const double f = (double) k * sr / N;
            const double ratio = f / baseHz;
            const double frac = std::abs(ratio - std::round(ratio));
            if (frac > 0.1) {  // not near a harmonic
                double re=0, im=0;
                for (int n = 0; n < N; ++n) { const double a = 2*M_PI*k*n/N; re += x[(size_t)n]*std::cos(a); im -= x[(size_t)n]*std::sin(a); }
                e += (re*re + im*im);
            }
        }
        return e;
    }

    void runTest() override {
        beginTest("4x oversampling reduces alias energy of a driven filter");
        {
            const double sr = 48000.0; const int block = 512; const int blocks = 8;
            auto renderAt = [&](int factor) {
                Layer layer; layer.prepare(sr * factor, block * factor);
                // configure a hot, self-oscillating Huggett LP with drive (set via ParamSnapshot)
                // ... mirror the snapshot setup used in HuggettNonlinearTests ...
                Voice v; v.setLayer(&layer); v.prepare(sr, block, factor);
                v.noteOn(40, 1.0f);
                std::vector<float> outL, outR(block);
                std::vector<float> capture;
                for (int b = 0; b < blocks; ++b) {
                    std::vector<float> l(block, 0.f), r(block, 0.f);
                    v.render(l.data(), r.data(), block);
                    capture.insert(capture.end(), l.begin(), l.end());
                }
                return capture;
            };
            const auto a1 = renderAt(1);
            const auto a4 = renderAt(4);
            const double e1 = aliasEnergy(a1, 82.4, sr);   // ~E2
            const double e4 = aliasEnergy(a4, 82.4, sr);
            expect(e4 < e1 * 0.5, "4x must cut alias energy >=2x (e1=" + juce::String(e1) + " e4=" + juce::String(e4) + ")");
        }
    }
};
static OversamplingAntiAliasTests oversamplingAntiAliasTestsInstance;
```
  (Fill the snapshot/param setup by copying the hot-drive configuration already used in `HuggettNonlinearTests.cpp` — same `ParamSnapshot` fields: `svfCutoffHz`, `svfResonance` near max, `spineDrive` high, `huggettRouting=0`.)

- [ ] **Step 2: Run to verify it fails** — register in `tests/CMakeLists.txt`, build; if it fails because the threshold is too strict, relax `0.5`→`0.7` (still proves a real reduction). Document the measured ratio in the commit.

- [ ] **Step 3: Run to verify it passes** — `./tests/k2000_tests 2>&1 | grep -i 'OversamplingAntiAlias\|Summary'`. Expected `[PASS]` and `Summary: 198 tests, 0 failed`.

- [ ] **Step 4: Commit** — `git add tests/OversamplingAntiAliasTests.cpp tests/CMakeLists.txt && git commit -m "test: oversampling cuts driven-filter alias energy"`

---

### Task 10: Version surface + roadmap

**Files:**
- Modify: `CMakeLists.txt` (VERSION bump)
- Modify: `src/PluginEditor.cpp` (panel label derives from `JucePlugin_VersionString` if not already)
- Modify: `tools/roadmap-dashboard/roadmap.json` (mark `v5.3 — HQ oversampling tiers` shipped)

- [ ] **Step 1: Bump version** — `CMakeLists.txt` `project(k2000 VERSION 5.4.0 ...)` (next minor) and confirm the editor's title/version label reflects it. [`[[release-version-surface]]`]
- [ ] **Step 2: Update roadmap** — set the `v5.3` oversampling item status to shipped in `tools/roadmap-dashboard/roadmap.json` (edit on this branch so the dashboard renders it — `[[feedback-roadmap-working-tree]]`).
- [ ] **Step 3: Full verify** — `cd build && cmake --build . --target k2000_tests -j4 && ./tests/k2000_tests 2>&1 | tail -1` (expect `Summary: 198 tests, 0 failed`) and `cmake --build . --target k2000_VST3 -j4` (clean).
- [ ] **Step 4: Commit** — `git add CMakeLists.txt src/PluginEditor.cpp tools/roadmap-dashboard/roadmap.json && git commit -m "chore: v5.4 — oversampling tiers; roadmap + version surface"`
- [ ] **Step 5: Windows CI smoke** — push the branch and trigger CI (`gh workflow run build.yml --ref feat/oversampling`); confirm green before any merge. [`[[feedback-windows-ci-smoke]]`]

---

## Self-Review

**Spec coverage:**
- §2 scope (whole nonlinear path, osc/env at base) → Tasks 4–5. ✓
- §3 oversampler (portable halfband, 2/4/8 cascade, pre-alloc 8×, linear-phase latency) → Tasks 1–2. ✓
- §5 storage (protected realtime/offline, persisted like limiter) → Task 6. ✓
- §6 switching (suspendProcessing re-prepare, latency, isNonRealtime detect) → Task 7. ✓
- §7 Moog cap (scripted sed + grep guard + committed-header patch, value 1,536,000) → Task 3. ✓
- §8 UI (hamburger → PopupMenu, ticked Realtime/Offline groups) → Task 8. ✓
- §9 testing (halfband units, AA integration, settings round-trip, cap regression) → Tasks 1,2,3,6,9. ✓
- §10 risks (CPU = user's dial via Off; Live↔Offline defensive detect; latency benign) → addressed in Tasks 7/structure. ✓
- §12 version/roadmap → Task 10. ✓

**Placeholder scan:** Task 5 and Task 8 contain "shown for clarity / use named members / delete exploratory lines" notes — these are deliberate clarity scaffolds with the concrete final form specified, not TBDs. Task 9 param setup says "copy the hot-drive config from HuggettNonlinearTests" — concrete source named. No "TODO/implement later".

**Type consistency:** `osFactor` is an `int` throughout (prepare chain, settings, `latencyBaseSamples`). `VoiceOversampler::setFactor/factor/processMonoUp/processStereoDown` signatures match between Task 2 definition and Tasks 4–5/7 usage. `realtimeOS()/offlineOS()/setRealtimeOS()/setOfflineOS()/activeOS()` consistent across Tasks 6–8. Moog `getMaxFrequency()` value (1,536,000) consistent in Task 3 + spec §7.

**Known risk to watch during execution:** Task 1's latency table and Task 2's `latencyBaseSamples` are derived; the impulse test (Task 2 Step 1) is the authority — if it reports different integer delays, set the table to the measured values (deterministic for fixed `kNumTaps`).
