# Cmajor Spike II — Nonlinear DSP + Data-Boundary Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** De-risk ADR-0012 ratification by proving (a) *nonlinear* per-voice DSP ports to Cmajor with equivalence + parity perf, and (b) array/sample data can cross the C++↔Cmajor boundary — via three small Cmajor modules, their adapters, equivalence/perf tests, and an ADR-0012 amendment.

**Architecture:** Three independent Cmajor modules (`NlSvf` = linear SVF + in-loop resonance saturator; `AsymDrive` = memoryless tanh shaper; `WtOsc` = single-cycle wavetable oscillator), each generated to committed C++ and hidden behind a hand-written stable adapter (the Spike I pattern). Equivalence is checked per-sample against the existing C++ baselines (`NlSvfCell`, `AsymSaturator`), with a harmonic-amplitude fallback. A lean/zero-copy adapter variant + a 256-voice bench settle whether Spike I's ~3% is removable. All code is test-target-only.

**Tech Stack:** C++17, JUCE 8.0.4, CMake, the existing `tests/` JUCE `UnitTest` harness, and the `cmaj` CLI (dev-machine only, AOT codegen via Docker — never shipped, never in CI).

## Global Constraints

- **Build:** `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` then `cmake --build build --target k2000_tests -j4` — **always `-j4`, never bare `-j`** (bare `-j` OOMs JUCE). Run: `./build/tests/k2000_tests` (expect a `Summary: N tests, 0 failed` line).
- **Codegen:** run `cmaj` in the Ubuntu-22.04 Docker container via `sg docker -c "tools/cmajor/cmaj-codegen.sh <patch.cmajorpatch> <out.h>"`. `cmaj generate` needs a `.cmajorpatch` MANIFEST, not a bare `.cmajor`. cmaj is at `~/.local/cmaj/linux/x64`.
- **AOT generated-C++ only.** Never link/ship/depend on the Cmajor JIT/engine/runtime. Only the dependency-free `cmaj generate --target=cpp` output is used. No Cmajor toolchain in CI.
- **Commit the generated C++** (`generated/*.h`) AND the `.cmajor` + `.cmajorpatch` source. Regenerate only when the `.cmajor` changes.
- **Test-target-only.** All spike code compiles into `k2000_tests` exclusively. Do NOT add it to `k2000_VST3`/`k2000_Standalone` or register anything in `FilterModelLibrary`.
- **Generated-class API (recorded in Spike I).** `struct <PatchName>`, `using EndpointHandle = uint32_t`, `enum class EndpointHandles { in, out, <events>... }`, `maxFramesPerBlock = 512` (`cmajIO.in/out` are `Array<float,512>`, `cmajIO` is a public member), `initialise(int32 sessionID, double frequency)`, `reset()`, `advance(int32 frames)`, `setInputFrames_in(const void* data, uint32 n, uint32 trailingToClear)`, `copyOutputFrames(EndpointHandle, void* dest, uint32 n)`, `addEvent_<name>(T value)`. Render: `addEvent_*` → `setInputFrames_in` → `advance(n≤512)` → `copyOutputFrames`. **Use the named enum + typed methods; never hardcode handle integers** (they renumber per patch).
- **Branch:** `feat/cmajor-spike` (continues Spike I). Artifact version 5.03.

---

### Task 1: `NlSvf` — linear SVF + in-loop resonance saturator + equivalence

**Files:**
- Create: `src/dsp/spine/cmajor/NlSvf.cmajor`, `src/dsp/spine/cmajor/NlSvf.cmajorpatch`
- Create (committed codegen): `src/dsp/spine/cmajor/generated/NlSvf.h`
- Create: `src/dsp/spine/cmajor/NlSvfAdapter.h`, `src/dsp/spine/cmajor/NlSvfAdapter.cpp`
- Create: `tests/NlSvfEquivalenceTests.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: the generated-class API (Global Constraints); `NlSvfCell` (`src/dsp/spine/NlSvfCell.h`) as the baseline.
- Produces: `NlSvfAdapter` — stable wrapper used by Tasks 4:
  ```cpp
  class NlSvfAdapter {
  public:
      enum Tap { LP = 0, HP = 1, BP = 2 };
      NlSvfAdapter(); ~NlSvfAdapter();
      NlSvfAdapter(NlSvfAdapter&&) noexcept; NlSvfAdapter& operator=(NlSvfAdapter&&) noexcept;
      void prepare(double sampleRate) noexcept;
      void reset() noexcept;
      void setParams(float cutoffHz, float resonance, float resSat, int tap) noexcept;
      void process(float* mono, int numSamples) noexcept;
  };
  ```

- [ ] **Step 1: Write `NlSvf.cmajor`**

`src/dsp/spine/cmajor/NlSvf.cmajor` — Spike I's `SvfLinear` plus the `NlSvfCell` resonance saturator (Padé-tanh `satRes`, applied as a delta on the cell input; `bp` = previous BP output):
```
processor NlSvf
{
    input  stream float in;
    output stream float out;
    input  event float cutoffHz  [[ name: "Cutoff",    min: 16, max: 20000, init: 1000 ]];
    input  event float resonance [[ name: "Resonance", min: 0,  max: 0.999, init: 0 ]];
    input  event float resSat    [[ name: "ResSat",    min: 0,  max: 1,     init: 0 ]];
    input  event int32 tap       [[ name: "Tap",       min: 0,  max: 2,     init: 0 ]];  // 0=LP 1=HP 2=BP

    float g, k, a1, a2, a3;
    float ic1, ic2, bp;
    bool  dirty = true;
    float cutoff = 1000.0f, res = 0.0f, rsat = 0.0f;
    int32 tapSel = 0;

    event cutoffHz  (float v) { cutoff = v; dirty = true; }
    event resonance (float v) { res = v;    dirty = true; }
    event resSat    (float v) { rsat = v; }
    event tap       (int32 v) { tapSel = v; }

    float padTanh (float x)
    {
        let x2 = x * x;
        return clamp (x * (27.0f + x2) / (27.0f + 9.0f * x2), -1.0f, 1.0f);
    }
    float padTanhDeriv (float x)
    {
        let x2  = x * x;
        let den = 27.0f + 9.0f * x2;
        let num = (27.0f + 3.0f * x2) * den - (27.0f * x + x * x2) * (18.0f * x);
        return num / (den * den);
    }
    float satRes (float x)
    {
        let b = 0.18f;
        let s = 1.0f / padTanhDeriv (b);
        return (padTanh (x + b) - padTanh (b)) * s;
    }

    void recompute()
    {
        let sr = float (processor.frequency);
        let c  = clamp (cutoff, 16.0f, sr * 0.45f);
        let r  = clamp (res,    0.0f,  0.999f);
        let Q  = 0.5f + r * r * 49.5f;
        g  = tan (float (pi) * c / sr);
        k  = 1.0f / Q;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
        dirty = false;
    }

    void main()
    {
        loop
        {
            if (dirty) recompute();
            var v0 = in;
            if (rsat > 0.0f)
            {
                let bpPrev = bp;
                v0 -= k * rsat * (satRes (bpPrev) - bpPrev);
            }
            let v3 = v0 - ic2;
            let v1 = a1 * ic1 + a2 * v3;
            let v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            bp  = v1;
            float result;
            if      (tapSel == 1) result = v0 - k * v1 - v2;  // HP
            else if (tapSel == 2) result = v1;                // BP
            else                  result = v2;                // LP
            out <- result;
            advance();
        }
    }
}
```
And `src/dsp/spine/cmajor/NlSvf.cmajorpatch`:
```
{
    "CmajorVersion": 1,
    "ID": "dev.bernie.nlsvf",
    "version": "1.0",
    "name": "NlSvf",
    "description": "Linear TPT SVF + in-loop resonance saturator (Cmajor Spike II)",
    "mainProcessor": "NlSvf",
    "source": [ "NlSvf.cmajor" ]
}
```

- [ ] **Step 2: Generate the C++ and verify it compiles**

```bash
cd /home/increp/dev/k2000
sg docker -c "tools/cmajor/cmaj-codegen.sh src/dsp/spine/cmajor/NlSvf.cmajorpatch src/dsp/spine/cmajor/generated/NlSvf.h"
g++ -std=c++17 -fsyntax-only -I src/dsp/spine/cmajor/generated src/dsp/spine/cmajor/generated/NlSvf.h && echo "NLSVF GENERATED C++ COMPILES"
```
Expected: `Loaded: NlSvf`, `generated: ...NlSvf.h`, then `NLSVF GENERATED C++ COMPILES`. If `cmaj` reports a Cmajor syntax error, fix `NlSvf.cmajor` (functions, `var`/`let`, `clamp`/`tan`/`pi`) and regenerate.

- [ ] **Step 3: Write the adapter header**

`src/dsp/spine/cmajor/NlSvfAdapter.h`:
```cpp
#pragma once
#include <memory>

// Stable wrapper hiding the generated Cmajor NlSvf class (linear SVF + resonance saturator).
class NlSvfAdapter {
public:
    enum Tap { LP = 0, HP = 1, BP = 2 };

    NlSvfAdapter();
    ~NlSvfAdapter();
    NlSvfAdapter(NlSvfAdapter&&) noexcept;
    NlSvfAdapter& operator=(NlSvfAdapter&&) noexcept;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    void setParams(float cutoffHz, float resonance, float resSat, int tap) noexcept;
    void process(float* mono, int numSamples) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
```

- [ ] **Step 4: Write the adapter implementation**

`src/dsp/spine/cmajor/NlSvfAdapter.cpp`:
```cpp
#include "NlSvfAdapter.h"
#include "generated/NlSvf.h"

#include <algorithm>
#include <cstdint>

using Generated = NlSvf;

struct NlSvfAdapter::Impl {
    Generated dsp;
    double sr = 48000.0;

    static constexpr int      kMaxBlock  = (int) Generated::maxFramesPerBlock;
    static constexpr uint32_t kOutHandle = static_cast<uint32_t>(Generated::EndpointHandles::out);

    void prepare(double sampleRate) { sr = sampleRate; dsp.initialise(0, sr); }
    void reset() { dsp.reset(); }
    void setParams(float cutoffHz, float resonance, float resSat, int tap) {
        dsp.addEvent_cutoffHz(cutoffHz);
        dsp.addEvent_resonance(resonance);
        dsp.addEvent_resSat(resSat);
        dsp.addEvent_tap((int32_t) tap);
    }
    void process(float* mono, int numSamples) {
        int i = 0;
        while (i < numSamples) {
            const int n = std::min(numSamples - i, kMaxBlock);
            dsp.setInputFrames_in(&mono[i], (uint32_t) n, 0);
            dsp.advance(n);
            dsp.copyOutputFrames(kOutHandle, &mono[i], (uint32_t) n);
            i += n;
        }
    }
};

NlSvfAdapter::NlSvfAdapter() : impl_(std::make_unique<Impl>()) {}
NlSvfAdapter::~NlSvfAdapter() = default;
NlSvfAdapter::NlSvfAdapter(NlSvfAdapter&&) noexcept = default;
NlSvfAdapter& NlSvfAdapter::operator=(NlSvfAdapter&&) noexcept = default;

void NlSvfAdapter::prepare(double sr) noexcept { impl_->prepare(sr); }
void NlSvfAdapter::reset() noexcept { impl_->reset(); }
void NlSvfAdapter::setParams(float c, float r, float rs, int t) noexcept { impl_->setParams(c, r, rs, t); }
void NlSvfAdapter::process(float* mono, int n) noexcept { impl_->process(mono, n); }
```
> The generated-API-dependent lines are `initialise`/`reset`/`addEvent_*`/`setInputFrames_in`/`advance`/`copyOutputFrames`. If a generated name differs, reconcile against `generated/NlSvf.h` (it won't — Spike I confirmed this exact shape).

- [ ] **Step 5: Wire sources + test into the TEST target**

In `tests/CMakeLists.txt`, add `NlSvfEquivalenceTests.cpp` to the test-cpp list (near `CmajorSvfPerfTests.cpp`) and `${CMAKE_SOURCE_DIR}/src/dsp/spine/cmajor/NlSvfAdapter.cpp` to the source list (near `SvfLinearAdapter.cpp`). Do NOT add to any plugin target.

- [ ] **Step 6: Write the failing equivalence test**

`tests/NlSvfEquivalenceTests.cpp` — per-sample match vs `NlSvfCell` (resSat on) across cutoff×resonance×level; prints the worst error; harmonic fallback noted in-comment:
```cpp
#include <juce_core/juce_core.h>
#include "../src/dsp/spine/cmajor/NlSvfAdapter.h"
#include "../src/dsp/spine/NlSvfCell.h"
#include <cmath>
#include <vector>

struct NlSvfEquivalenceTests : public juce::UnitTest {
    NlSvfEquivalenceTests() : juce::UnitTest("NlSvfEquivalence") {}
    static constexpr double kSR = 48000.0;

    // Per-sample max-abs error between the Cmajor adapter and NlSvfCell for one config.
    static double maxErr(double f, float amp, float cutoff, float resAndSat, int tap) {
        const int n = 8192;
        std::vector<float> a((size_t)n), b((size_t)n);
        for (int i = 0; i < n; ++i)
            a[(size_t)i] = b[(size_t)i] = amp * (float) std::sin(2.0*juce::MathConstants<double>::pi*f*i/kSR);

        NlSvfAdapter ad; ad.prepare(kSR); ad.reset();
        ad.setParams(cutoff, resAndSat, resAndSat, tap);
        ad.process(a.data(), n);

        NlSvfCell c; c.prepare(kSR); c.reset();
        c.setCutoff(cutoff); c.setResonance(resAndSat); c.setResSat(resAndSat);
        for (int i = 0; i < n; ++i) { float l = b[(size_t)i], r = l; c.process(l, r, tap); b[(size_t)i] = l; }

        double m = 0.0;
        for (int i = 0; i < n; ++i) m = std::max(m, (double) std::abs(a[(size_t)i] - b[(size_t)i]));
        return m;
    }

    void runTest() override {
        beginTest("Cmajor NlSvf matches NlSvfCell (resonance saturator on), per-sample");
        const float cutoffs[] = { 250.0f, 1000.0f, 4000.0f };
        const float rs[]      = { 0.3f, 0.7f, 0.95f };   // resonance == resSat (engages the nonlinearity)
        const float amps[]    = { 0.1f, 0.5f, 0.9f };    // level-dependent: sweep amplitude
        const int   taps[]    = { NlSvfAdapter::LP, NlSvfAdapter::HP, NlSvfAdapter::BP };
        double worst = 0.0;
        for (float cut : cutoffs) for (float r : rs) for (float amp : amps) for (int tap : taps) {
            const double m = maxErr(1000.0, amp, cut, r, tap);
            worst = std::max(worst, m);
            expect(std::isfinite(m), "finite output");
            // Padé saturator is pure arithmetic; only tan() per-recompute is transcendental.
            // If this bound proves flaky from tan()/FMA drift through the loop, switch to a
            // harmonic-amplitude comparison (see spec section 4 step 1) and record the change.
            expect(m < 2.0e-3, "per-sample within 2e-3 (cut " + juce::String(cut,0)
                   + " res/sat " + juce::String(r,2) + " amp " + juce::String(amp,2)
                   + " tap " + juce::String(tap) + "): max err " + juce::String(m, 7));
        }
        logMessage("NlSvf worst per-sample error: " + juce::String(worst, 8));
    }
};
static NlSvfEquivalenceTests nlSvfEquivalenceTestsInstance;
```

- [ ] **Step 7: Build and run**

Run: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | grep -E "NlSvfEquivalence|Summary"`
Expected: `NlSvfEquivalence` passes; `Summary: ... 0 failed`. If it FAILS on per-sample drift (the logged worst error is small but >2e-3), that pins the achievable tightness — record it; if the drift is musically negligible, replace the per-sample assert with a harmonic-amplitude comparison (compare the first ~8 harmonic bin magnitudes of a steady tone within ~0.5 dB) and note why in the report. If the error is LARGE (>0.1), it's a porting bug — fix `NlSvf.cmajor` (check the `satRes`/`bp` update order against `NlSvfCell::step`), regenerate, rebuild.

- [ ] **Step 8: Commit**

```bash
git add src/dsp/spine/cmajor/NlSvf.cmajor src/dsp/spine/cmajor/NlSvf.cmajorpatch \
        src/dsp/spine/cmajor/generated/NlSvf.h \
        src/dsp/spine/cmajor/NlSvfAdapter.h src/dsp/spine/cmajor/NlSvfAdapter.cpp \
        tests/NlSvfEquivalenceTests.cpp tests/CMakeLists.txt
git commit -m "spike(cmajor): NlSvf (SVF + resonance saturator) + adapter + per-sample equivalence"
```

---

### Task 2: `AsymDrive` — memoryless tanh shaper + equivalence

**Files:**
- Create: `src/dsp/spine/cmajor/AsymDrive.cmajor`, `src/dsp/spine/cmajor/AsymDrive.cmajorpatch`
- Create (committed codegen): `src/dsp/spine/cmajor/generated/AsymDrive.h`
- Create: `src/dsp/spine/cmajor/AsymDriveAdapter.h`, `src/dsp/spine/cmajor/AsymDriveAdapter.cpp`
- Create: `tests/AsymDriveEquivalenceTests.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: the generated-class API; `AsymSaturator` (`src/dsp/spine/AsymSaturator.h`) as baseline.
- Produces: `AsymDriveAdapter`:
  ```cpp
  class AsymDriveAdapter {
  public:
      AsymDriveAdapter(); ~AsymDriveAdapter();
      AsymDriveAdapter(AsymDriveAdapter&&) noexcept; AsymDriveAdapter& operator=(AsymDriveAdapter&&) noexcept;
      void prepare(double sampleRate) noexcept;
      void reset() noexcept;
      void setParams(float drive01, float biasFixed, float maxDriveDb) noexcept;
      void process(float* mono, int numSamples) noexcept;
  };
  ```

- [ ] **Step 1: Write `AsymDrive.cmajor`**

`src/dsp/spine/cmajor/AsymDrive.cmajor` — mirrors `AsymSaturator::setDrive`/`process`, computing coefficients inside Cmajor (exercises `pow`/`tanh`):
```
processor AsymDrive
{
    input  stream float in;
    output stream float out;
    input  event float drive01    [[ name: "Drive",      min: 0,  max: 1,  init: 0 ]];
    input  event float biasFixed  [[ name: "Bias",       min: -1, max: 1,  init: 0 ]];
    input  event float maxDriveDb [[ name: "MaxDriveDb", min: 0,  max: 48, init: 30 ]];

    float gain = 1.0f, bias = 0.0f, comp = 1.0f;
    bool  dirty = true;
    float d01 = 0.0f, biasF = 0.0f, maxDb = 30.0f;

    event drive01    (float v) { d01 = v;   dirty = true; }
    event biasFixed  (float v) { biasF = v; dirty = true; }
    event maxDriveDb (float v) { maxDb = v; dirty = true; }

    void recompute()
    {
        let dB = max (0.0f, d01) * maxDb;
        gain = pow (10.0f, dB / 20.0f);
        bias = biasF;
        let full = (gain > 1.0f) ? (1.0f / tanh (gain)) : 1.0f;
        comp = 1.0f + 0.75f * (full - 1.0f);
        dirty = false;
    }

    void main()
    {
        loop
        {
            if (dirty) recompute();
            out <- comp * tanh (gain * in + bias);
            advance();
        }
    }
}
```
And `src/dsp/spine/cmajor/AsymDrive.cmajorpatch`:
```
{
    "CmajorVersion": 1,
    "ID": "dev.bernie.asymdrive",
    "version": "1.0",
    "name": "AsymDrive",
    "description": "Memoryless asymmetric tanh drive shaper (Cmajor Spike II)",
    "mainProcessor": "AsymDrive",
    "source": [ "AsymDrive.cmajor" ]
}
```

- [ ] **Step 2: Generate the C++ and verify it compiles**

```bash
cd /home/increp/dev/k2000
sg docker -c "tools/cmajor/cmaj-codegen.sh src/dsp/spine/cmajor/AsymDrive.cmajorpatch src/dsp/spine/cmajor/generated/AsymDrive.h"
g++ -std=c++17 -fsyntax-only -I src/dsp/spine/cmajor/generated src/dsp/spine/cmajor/generated/AsymDrive.h && echo "ASYMDRIVE GENERATED C++ COMPILES"
```
Expected: `ASYMDRIVE GENERATED C++ COMPILES`. If `cmaj` rejects `pow`/`tanh`/`max`, check they are spelled as the Cmajor intrinsics (they are std intrinsics, available unqualified) and regenerate.

- [ ] **Step 3: Write the adapter header**

`src/dsp/spine/cmajor/AsymDriveAdapter.h`:
```cpp
#pragma once
#include <memory>

// Stable wrapper hiding the generated Cmajor AsymDrive class (memoryless tanh shaper).
class AsymDriveAdapter {
public:
    AsymDriveAdapter();
    ~AsymDriveAdapter();
    AsymDriveAdapter(AsymDriveAdapter&&) noexcept;
    AsymDriveAdapter& operator=(AsymDriveAdapter&&) noexcept;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    void setParams(float drive01, float biasFixed, float maxDriveDb) noexcept;
    void process(float* mono, int numSamples) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
```

- [ ] **Step 4: Write the adapter implementation**

`src/dsp/spine/cmajor/AsymDriveAdapter.cpp`:
```cpp
#include "AsymDriveAdapter.h"
#include "generated/AsymDrive.h"

#include <algorithm>
#include <cstdint>

using Generated = AsymDrive;

struct AsymDriveAdapter::Impl {
    Generated dsp;
    double sr = 48000.0;

    static constexpr int      kMaxBlock  = (int) Generated::maxFramesPerBlock;
    static constexpr uint32_t kOutHandle = static_cast<uint32_t>(Generated::EndpointHandles::out);

    void prepare(double sampleRate) { sr = sampleRate; dsp.initialise(0, sr); }
    void reset() { dsp.reset(); }
    void setParams(float drive01, float biasFixed, float maxDriveDb) {
        dsp.addEvent_drive01(drive01);
        dsp.addEvent_biasFixed(biasFixed);
        dsp.addEvent_maxDriveDb(maxDriveDb);
    }
    void process(float* mono, int numSamples) {
        int i = 0;
        while (i < numSamples) {
            const int n = std::min(numSamples - i, kMaxBlock);
            dsp.setInputFrames_in(&mono[i], (uint32_t) n, 0);
            dsp.advance(n);
            dsp.copyOutputFrames(kOutHandle, &mono[i], (uint32_t) n);
            i += n;
        }
    }
};

AsymDriveAdapter::AsymDriveAdapter() : impl_(std::make_unique<Impl>()) {}
AsymDriveAdapter::~AsymDriveAdapter() = default;
AsymDriveAdapter::AsymDriveAdapter(AsymDriveAdapter&&) noexcept = default;
AsymDriveAdapter& AsymDriveAdapter::operator=(AsymDriveAdapter&&) noexcept = default;

void AsymDriveAdapter::prepare(double sr) noexcept { impl_->prepare(sr); }
void AsymDriveAdapter::reset() noexcept { impl_->reset(); }
void AsymDriveAdapter::setParams(float d, float b, float m) noexcept { impl_->setParams(d, b, m); }
void AsymDriveAdapter::process(float* mono, int n) noexcept { impl_->process(mono, n); }
```

- [ ] **Step 5: Wire sources + test into the TEST target**

In `tests/CMakeLists.txt`, add `AsymDriveEquivalenceTests.cpp` to the test-cpp list and `${CMAKE_SOURCE_DIR}/src/dsp/spine/cmajor/AsymDriveAdapter.cpp` to the source list.

- [ ] **Step 6: Write the failing equivalence test**

`tests/AsymDriveEquivalenceTests.cpp` — per-sample vs `AsymSaturator`; harmonic-amplitude fallback wired in (this is the real-`tanh` path where it may be needed):
```cpp
#include <juce_core/juce_core.h>
#include "../src/dsp/spine/cmajor/AsymDriveAdapter.h"
#include "../src/dsp/spine/AsymSaturator.h"
#include <cmath>
#include <vector>

struct AsymDriveEquivalenceTests : public juce::UnitTest {
    AsymDriveEquivalenceTests() : juce::UnitTest("AsymDriveEquivalence") {}
    static constexpr double kSR = 48000.0;

    void runTest() override {
        beginTest("Cmajor AsymDrive matches AsymSaturator");
        const float drives[] = { 0.0f, 0.25f, 0.5f, 1.0f };
        const float bias = 0.25f, maxDb = 30.0f;
        const int n = 8192;
        double worst = 0.0;
        for (float d : drives) {
            std::vector<float> a((size_t)n), b((size_t)n);
            for (int i = 0; i < n; ++i)
                a[(size_t)i] = b[(size_t)i] = 0.6f * (float) std::sin(2.0*juce::MathConstants<double>::pi*220.0*i/kSR);

            AsymDriveAdapter ad; ad.prepare(kSR); ad.reset(); ad.setParams(d, bias, maxDb);
            ad.process(a.data(), n);

            AsymSaturator s; s.setDrive(d, bias, maxDb);
            for (int i = 0; i < n; ++i) b[(size_t)i] = s.process(b[(size_t)i]);

            double m = 0.0;
            for (int i = 0; i < n; ++i) m = std::max(m, (double) std::abs(a[(size_t)i] - b[(size_t)i]));
            worst = std::max(worst, m);
            expect(std::isfinite(m), "finite");
            expect(m < 1.0e-3, "per-sample within 1e-3 at drive " + juce::String(d,2)
                   + ": max err " + juce::String(m, 7));
        }
        logMessage("AsymDrive worst per-sample error: " + juce::String(worst, 8));
        // If a drive level exceeds 1e-3 purely from tanh/pow intrinsic differences (output still
        // sane, error tiny+smooth), that is the documented harmonic-fallback case: replace the
        // per-sample assert with a harmonic-amplitude comparison (first ~8 harmonics within ~0.5 dB)
        // and record the tanh/pow last-bit behavior in the report (primary ADR evidence).
    }
};
static AsymDriveEquivalenceTests asymDriveEquivalenceTestsInstance;
```

- [ ] **Step 7: Build and run**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | grep -E "AsymDriveEquivalence|Summary"`
Expected: PASS, `Summary: ... 0 failed`. If it fails by a tiny smooth margin (Cmajor `tanh`/`pow` vs `std::`), apply the harmonic fallback per the in-test comment and record the numeric drift. If it fails large, it's a coefficient bug — verify `recompute()` matches `AsymSaturator::setDrive`, regenerate.

- [ ] **Step 8: Commit**

```bash
git add src/dsp/spine/cmajor/AsymDrive.cmajor src/dsp/spine/cmajor/AsymDrive.cmajorpatch \
        src/dsp/spine/cmajor/generated/AsymDrive.h \
        src/dsp/spine/cmajor/AsymDriveAdapter.h src/dsp/spine/cmajor/AsymDriveAdapter.cpp \
        tests/AsymDriveEquivalenceTests.cpp tests/CMakeLists.txt
git commit -m "spike(cmajor): AsymDrive tanh shaper + adapter + equivalence (real-tanh path)"
```

---

### Task 3: `WtOsc` — wavetable oscillator + data-boundary discovery

**Files:**
- Create: `src/dsp/spine/cmajor/WtOsc.cmajor`, `src/dsp/spine/cmajor/WtOsc.cmajorpatch`
- Create (committed codegen): `src/dsp/spine/cmajor/generated/WtOsc.h`
- Create: `src/dsp/spine/cmajor/WtOscAdapter.h`, `src/dsp/spine/cmajor/WtOscAdapter.cpp`
- Create: `tests/WtOscTests.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: the generated-class API; the discovered external-data mechanism (Step 1).
- Produces: `WtOscAdapter`:
  ```cpp
  class WtOscAdapter {
  public:
      WtOscAdapter(); ~WtOscAdapter();
      WtOscAdapter(WtOscAdapter&&) noexcept; WtOscAdapter& operator=(WtOscAdapter&&) noexcept;
      void prepare(double sampleRate) noexcept;
      void reset() noexcept;
      void setTable(const float* table, int n) noexcept;  // hides external-vs-stream/event
      void setFrequency(float hz) noexcept;
      void process(float* mono, int numSamples) noexcept;
  };
  ```

- [ ] **Step 1: Discover how external array data surfaces in generated C++**

Write `src/dsp/spine/cmajor/WtOsc.cmajor` (uses an `external float[]` table) and its patch, generate, then INSPECT the generated header to learn how the external is exposed.
`src/dsp/spine/cmajor/WtOsc.cmajor`:
```
processor WtOsc
{
    output stream float out;
    input  event float frequency [[ name: "Freq", min: 1, max: 20000, init: 220 ]];

    external float[] table;     // supplied by the host; size known at load

    float phase = 0.0f;
    float freq  = 220.0f;
    event frequency (float v) { freq = v; }

    void main()
    {
        let n = int (table.size);
        loop
        {
            let pos  = phase * float (n);
            let i0   = wrap (int (pos), n);
            let i1   = wrap (i0 + 1, n);
            let frac = pos - float (int (pos));
            out <- table.at (i0) * (1.0f - frac) + table.at (i1) * frac;
            phase += freq / float (processor.frequency);
            while (phase >= 1.0f) phase -= 1.0f;
            advance();
        }
    }
}
```
`src/dsp/spine/cmajor/WtOsc.cmajorpatch`:
```
{
    "CmajorVersion": 1,
    "ID": "dev.bernie.wtosc",
    "version": "1.0",
    "name": "WtOsc",
    "description": "Single-cycle wavetable oscillator (Cmajor Spike II data-boundary probe)",
    "mainProcessor": "WtOsc",
    "source": [ "WtOsc.cmajor" ]
}
```
Generate + inspect:
```bash
cd /home/increp/dev/k2000
sg docker -c "tools/cmajor/cmaj-codegen.sh src/dsp/spine/cmajor/WtOsc.cmajorpatch src/dsp/spine/cmajor/generated/WtOsc.h"
g++ -std=c++17 -fsyntax-only -I src/dsp/spine/cmajor/generated src/dsp/spine/cmajor/generated/WtOsc.h && echo "WTOSC GENERATED C++ COMPILES"
grep -nE "external|table|setExternal|Externals|Slice|table.*=|setValue" src/dsp/spine/cmajor/generated/WtOsc.h
```
**Record in the report which mechanism the generated class exposes:**
- **(A) Runtime-settable** — a method/member like `setExternalData`, `setExternalVariable`, or a public `table` Slice/pointer the host assigns. → preferred; `setTable()` uses it.
- **(B) Baked at codegen** — the external is resolved from the manifest at generate time (no runtime setter). → record this finding; for the spike, exercise the fixed-data path by adding `"externals": { "WtOsc::table": [...] }` to the patch and regenerating, and note that *user-loaded* tables would instead use a stream/event load (out of scope to build here; the finding is what matters).

If Cmajor rejects `external float[]` sizing or `table.at`/`wrap` for `--target=cpp`, record the exact diagnostic — that itself is the data-boundary finding — and fall back to a fixed-size `external float[1024] table;` (or the manifest-baked form) so the module still compiles and plays.

- [ ] **Step 2: Write the adapter header**

`src/dsp/spine/cmajor/WtOscAdapter.h`:
```cpp
#pragma once
#include <memory>

// Stable wrapper hiding the generated Cmajor WtOsc class. setTable() hides whichever
// data-in mechanism Task 3 Step 1 discovered (runtime external-set, or fixed/baked).
class WtOscAdapter {
public:
    WtOscAdapter();
    ~WtOscAdapter();
    WtOscAdapter(WtOscAdapter&&) noexcept;
    WtOscAdapter& operator=(WtOscAdapter&&) noexcept;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    void setTable(const float* table, int n) noexcept;
    void setFrequency(float hz) noexcept;
    void process(float* mono, int numSamples) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
```

- [ ] **Step 3: Write the adapter implementation**

`src/dsp/spine/cmajor/WtOscAdapter.cpp`. Fill the two marked lines using the mechanism recorded in Step 1 — **(A)** the discovered runtime external setter, or **(B)** the no-op fixed/baked path (the table is compiled in; `setTable` only stores `n`). The frequency/output path is identical regardless:
```cpp
#include "WtOscAdapter.h"
#include "generated/WtOsc.h"

#include <algorithm>
#include <cstdint>
#include <vector>

using Generated = WtOsc;

struct WtOscAdapter::Impl {
    Generated dsp;
    double sr = 48000.0;
    std::vector<float> table;

    static constexpr int      kMaxBlock  = (int) Generated::maxFramesPerBlock;
    static constexpr uint32_t kOutHandle = static_cast<uint32_t>(Generated::EndpointHandles::out);

    void prepare(double sampleRate) { sr = sampleRate; dsp.initialise(0, sr); }
    void reset() { dsp.initialise(0, sr); }   // re-init clears phase
    void setTable(const float* t, int n) {
        table.assign(t, t + n);
        // (DISCOVERED IN STEP 1) push table into the generated instance:
        //   (A) runtime:  dsp.setExternalData("WtOsc::table", table.data(), (uint32_t) table.size());
        //   (B) baked:    /* no-op: table compiled in via the manifest; nothing to push */
        // Use exactly the form Step 1 recorded; remove the other.
    }
    void setFrequency(float hz) { dsp.addEvent_frequency(hz); }
    void process(float* mono, int numSamples) {
        int i = 0;
        while (i < numSamples) {
            const int n = std::min(numSamples - i, kMaxBlock);
            dsp.advance(n);
            dsp.copyOutputFrames(kOutHandle, &mono[i], (uint32_t) n);
            i += n;
        }
    }
};

WtOscAdapter::WtOscAdapter() : impl_(std::make_unique<Impl>()) {}
WtOscAdapter::~WtOscAdapter() = default;
WtOscAdapter::WtOscAdapter(WtOscAdapter&&) noexcept = default;
WtOscAdapter& WtOscAdapter::operator=(WtOscAdapter&&) noexcept = default;

void WtOscAdapter::prepare(double sr) noexcept { impl_->prepare(sr); }
void WtOscAdapter::reset() noexcept { impl_->reset(); }
void WtOscAdapter::setTable(const float* t, int n) noexcept { impl_->setTable(t, n); }
void WtOscAdapter::setFrequency(float hz) noexcept { impl_->setFrequency(hz); }
void WtOscAdapter::process(float* mono, int n) noexcept { impl_->process(mono, n); }
```
> NOTE on ordering: for mechanism (A), `setTable` must run after `prepare` (the instance must exist) and before `process`. If the discovered setter requires setting external data *before* `initialise`, move the push into `prepare` using a cached table and document it. The test below calls `prepare → setTable → setFrequency → process`, which both mechanisms support.

- [ ] **Step 4: Wire sources + test into the TEST target**

In `tests/CMakeLists.txt`, add `WtOscTests.cpp` to the test-cpp list and `${CMAKE_SOURCE_DIR}/src/dsp/spine/cmajor/WtOscAdapter.cpp` to the source list.

- [ ] **Step 5: Write the failing test (proves data crossed)**

`tests/WtOscTests.cpp` — push a known single-cycle sine table from C++, play at a known frequency, assert the output is a sine at that frequency (dominant energy near the fundamental, sane RMS):
```cpp
#include <juce_core/juce_core.h>
#include "../src/dsp/spine/cmajor/WtOscAdapter.h"
#include <cmath>
#include <vector>

struct WtOscTests : public juce::UnitTest {
    WtOscTests() : juce::UnitTest("WtOsc") {}

    // Goertzel single-bin magnitude at frequency f over buf.
    static double bin(const std::vector<float>& buf, double f, double sr) {
        const double w = 2.0 * juce::MathConstants<double>::pi * f / sr;
        const double c = 2.0 * std::cos(w);
        double s0 = 0, s1 = 0, s2 = 0;
        for (float x : buf) { s0 = x + c * s1 - s2; s2 = s1; s1 = s0; }
        return std::sqrt(s1*s1 + s2*s2 - c*s1*s2);
    }

    void runTest() override {
        beginTest("table pushed from C++ plays back as the expected single-cycle waveform");
        const double sr = 48000.0;
        const int N = 256;
        std::vector<float> table((size_t)N);
        for (int i = 0; i < N; ++i)
            table[(size_t)i] = (float) std::sin(2.0*juce::MathConstants<double>::pi*i/N);

        WtOscAdapter o; o.prepare(sr); o.setTable(table.data(), N); o.setFrequency(440.0f); o.reset();
        // re-push table + freq after reset() (reset re-inits the instance):
        o.setTable(table.data(), N); o.setFrequency(440.0f);

        const int n = 16384;
        std::vector<float> buf((size_t)n);
        o.process(buf.data(), n);

        double rms = 0.0; for (float x : buf) rms += (double)x*x; rms = std::sqrt(rms / n);
        const double atF   = bin(buf, 440.0, sr);
        const double atOff = bin(buf, 1500.0, sr);   // an unrelated frequency: should be far lower
        expect(std::isfinite(rms) && rms > 0.1, "non-trivial output (rms " + juce::String(rms,4) + ")");
        expect(atF > atOff * 8.0, "energy concentrated at 440 Hz (atF " + juce::String(atF,2)
               + " vs off " + juce::String(atOff,2) + ") — proves the table crossed and is used");
    }
};
static WtOscTests wtOscTestsInstance;
```

- [ ] **Step 6: Build and run**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | grep -E "WtOsc|Summary"`
Expected: `WtOsc` passes (output is a 440 Hz sine); `Summary: ... 0 failed`. If the output is silent/garbage, the table didn't cross — revisit the Step 1 mechanism (ordering of `setTable` vs `initialise`, or switch (A)↔(B)). Record the working mechanism in the report.

- [ ] **Step 7: Commit**

```bash
git add src/dsp/spine/cmajor/WtOsc.cmajor src/dsp/spine/cmajor/WtOsc.cmajorpatch \
        src/dsp/spine/cmajor/generated/WtOsc.h \
        src/dsp/spine/cmajor/WtOscAdapter.h src/dsp/spine/cmajor/WtOscAdapter.cpp \
        tests/WtOscTests.cpp tests/CMakeLists.txt
git commit -m "spike(cmajor): WtOsc wavetable oscillator + data-boundary crossing proof"
```

---

### Task 4: Lean/zero-copy adapter + 256-voice nonlinear perf

**Files:**
- Create: `src/dsp/spine/cmajor/NlSvfLeanAdapter.h`, `src/dsp/spine/cmajor/NlSvfLeanAdapter.cpp`
- Create: `tests/NlSvfPerfTests.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: the generated `NlSvf` class directly (`generated/NlSvf.h`); `NlSvfAdapter` (Task 1); `NlSvfCell`.
- Produces: `NlSvfLeanAdapter` — a zero-copy variant that writes/reads the generated `cmajIO` buffers directly (still PIMPL/heap state; in-place state is deferred):
  ```cpp
  class NlSvfLeanAdapter {
  public:
      NlSvfLeanAdapter(); ~NlSvfLeanAdapter();
      NlSvfLeanAdapter(NlSvfLeanAdapter&&) noexcept; NlSvfLeanAdapter& operator=(NlSvfLeanAdapter&&) noexcept;
      void prepare(double sampleRate) noexcept;
      void reset() noexcept;
      void setParams(float cutoffHz, float resonance, float resSat, int tap) noexcept;
      int  maxBlock() const noexcept;            // == Generated::maxFramesPerBlock (512)
      float* inBlock() noexcept;                 // write up to maxBlock() input samples here
      const float* outBlock() const noexcept;    // valid after advanceBlock()
      void advanceBlock(int n) noexcept;         // n <= maxBlock(): render in place, no copies
      void process(float* mono, int numSamples) noexcept;  // convenience (one copy pair, like NlSvfAdapter)
  };
  ```

- [ ] **Step 1: Write the lean adapter header**

`src/dsp/spine/cmajor/NlSvfLeanAdapter.h` — exactly the interface above (copy verbatim).

- [ ] **Step 2: Write the lean adapter implementation**

`src/dsp/spine/cmajor/NlSvfLeanAdapter.cpp` — direct access to the public `cmajIO` buffers (no `setInputFrames_in`/`copyOutputFrames` memcpy on the zero-copy path):
```cpp
#include "NlSvfLeanAdapter.h"
#include "generated/NlSvf.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

using Generated = NlSvf;

struct NlSvfLeanAdapter::Impl {
    Generated dsp;
    double sr = 48000.0;
    static constexpr int      kMaxBlock  = (int) Generated::maxFramesPerBlock;
    static constexpr uint32_t kOutHandle = static_cast<uint32_t>(Generated::EndpointHandles::out);

    void prepare(double sampleRate) { sr = sampleRate; dsp.initialise(0, sr); }
    void reset() { dsp.reset(); }
    void setParams(float c, float r, float rs, int t) {
        dsp.addEvent_cutoffHz(c); dsp.addEvent_resonance(r); dsp.addEvent_resSat(rs); dsp.addEvent_tap((int32_t) t);
    }
    // Zero-copy: caller writes straight into the generated input buffer, advance renders in
    // place, caller reads straight from the generated output buffer. (cmajIO is public.)
    float*       inBlock()  { return dsp.cmajIO.in.elements; }
    const float* outBlock() const { return reinterpret_cast<const float*>(&dsp.cmajIO.out); }
    void advanceBlock(int n) { dsp.advance(n); }

    // Convenience path mirrors NlSvfAdapter (one copy in, one out) for apples-to-apples use.
    void process(float* mono, int numSamples) {
        int i = 0;
        while (i < numSamples) {
            const int n = std::min(numSamples - i, kMaxBlock);
            std::memcpy(dsp.cmajIO.in.elements, &mono[i], (size_t) n * sizeof(float));
            dsp.advance(n);
            std::memcpy(&mono[i], &dsp.cmajIO.out, (size_t) n * sizeof(float));
            i += n;
        }
    }
};

NlSvfLeanAdapter::NlSvfLeanAdapter() : impl_(std::make_unique<Impl>()) {}
NlSvfLeanAdapter::~NlSvfLeanAdapter() = default;
NlSvfLeanAdapter::NlSvfLeanAdapter(NlSvfLeanAdapter&&) noexcept = default;
NlSvfLeanAdapter& NlSvfLeanAdapter::operator=(NlSvfLeanAdapter&&) noexcept = default;

void NlSvfLeanAdapter::prepare(double sr) noexcept { impl_->prepare(sr); }
void NlSvfLeanAdapter::reset() noexcept { impl_->reset(); }
void NlSvfLeanAdapter::setParams(float c, float r, float rs, int t) noexcept { impl_->setParams(c, r, rs, t); }
int  NlSvfLeanAdapter::maxBlock() const noexcept { return Impl::kMaxBlock; }
float* NlSvfLeanAdapter::inBlock() noexcept { return impl_->inBlock(); }
const float* NlSvfLeanAdapter::outBlock() const noexcept { return impl_->outBlock(); }
void NlSvfLeanAdapter::advanceBlock(int n) noexcept { impl_->advanceBlock(n); }
void NlSvfLeanAdapter::process(float* mono, int n) noexcept { impl_->process(mono, n); }
```
> The lean header struct member must be a `std::unique_ptr<Impl> impl_;` with a `struct Impl;` forward decl — add those two private lines to the header (same PIMPL shape as `NlSvfAdapter.h`).

- [ ] **Step 3: Wire sources + test into the TEST target**

In `tests/CMakeLists.txt`, add `NlSvfPerfTests.cpp` to the test-cpp list and `${CMAKE_SOURCE_DIR}/src/dsp/spine/cmajor/NlSvfLeanAdapter.cpp` to the source list.

- [ ] **Step 4: Write the perf bench**

`tests/NlSvfPerfTests.cpp` — 256 voices, copy adapter vs lean (zero-copy) adapter vs `NlSvfCell`(resSat); prints all ratios + sizeofs:
```cpp
#include <juce_core/juce_core.h>
#include "../src/dsp/spine/cmajor/NlSvfAdapter.h"
#include "../src/dsp/spine/cmajor/NlSvfLeanAdapter.h"
#include "../src/dsp/spine/NlSvfCell.h"
#include <chrono>
#include <vector>
#include <cmath>

struct NlSvfPerfTests : public juce::UnitTest {
    NlSvfPerfTests() : juce::UnitTest("NlSvfPerf") {}

    void runTest() override {
        beginTest("256-voice nonlinear perf: copy vs lean(zero-copy) adapter vs NlSvfCell");
        const double sr = 48000.0;
        const int kVoices = 256, kBlock = 128, kBlocks = 200;

        std::vector<float> block((size_t)kBlock);
        for (int i = 0; i < kBlock; ++i) block[(size_t)i] = 0.3f * (float) std::sin(0.05 * i);

        using clock = std::chrono::high_resolution_clock;

        // --- copy adapter ---
        std::vector<NlSvfAdapter> cp((size_t)kVoices);
        for (auto& a : cp) { a.prepare(sr); a.reset(); a.setParams(1000.0f, 0.7f, 0.7f, 0); }
        auto t0 = clock::now(); double sinkCp = 0.0;
        for (int b = 0; b < kBlocks; ++b) for (auto& a : cp) { auto buf = block; a.process(buf.data(), kBlock); sinkCp += buf[(size_t)(kBlock-1)]; }
        auto t1 = clock::now();

        // --- lean / zero-copy adapter (write into inBlock, advance, read outBlock) ---
        std::vector<NlSvfLeanAdapter> ln((size_t)kVoices);
        for (auto& a : ln) { a.prepare(sr); a.reset(); a.setParams(1000.0f, 0.7f, 0.7f, 0);
                             std::copy(block.begin(), block.end(), a.inBlock()); }   // load input once
        auto t2 = clock::now(); double sinkLn = 0.0;
        for (int b = 0; b < kBlocks; ++b) for (auto& a : ln) { a.advanceBlock(kBlock); sinkLn += a.outBlock()[kBlock-1]; }
        auto t3 = clock::now();

        // --- NlSvfCell baseline (resSat on) ---
        std::vector<NlSvfCell> nl((size_t)kVoices);
        for (auto& c : nl) { c.prepare(sr); c.reset(); c.setCutoff(1000.0f); c.setResonance(0.7f); c.setResSat(0.7f); }
        auto t4 = clock::now(); double sinkNl = 0.0;
        for (int b = 0; b < kBlocks; ++b) for (auto& c : nl) { auto buf = block; for (int i = 0; i < kBlock; ++i) { float l = buf[(size_t)i], r = l; c.process(l, r, 0); buf[(size_t)i] = l; } sinkNl += buf[(size_t)(kBlock-1)]; }
        auto t5 = clock::now();

        const double cpMs = std::chrono::duration<double,std::milli>(t1-t0).count();
        const double lnMs = std::chrono::duration<double,std::milli>(t3-t2).count();
        const double nlMs = std::chrono::duration<double,std::milli>(t5-t4).count();
        std::printf("\n=== 256-voice NlSvf perf (%d blocks x %d samples) ===\n", kBlocks, kBlock);
        std::printf("copy adapter : %8.2f ms  (%.2fx vs cpp)\n", cpMs, cpMs/nlMs);
        std::printf("lean adapter : %8.2f ms  (%.2fx vs cpp)\n", lnMs, lnMs/nlMs);
        std::printf("NlSvfCell C++: %8.2f ms\n", nlMs);
        std::printf("sizeof: NlSvfAdapter=%zu NlSvfLeanAdapter=%zu NlSvfCell=%zu [sinks %.3f %.3f %.3f]\n",
                    sizeof(NlSvfAdapter), sizeof(NlSvfLeanAdapter), sizeof(NlSvfCell), sinkCp, sinkLn, sinkNl);

        expect(std::isfinite(sinkCp) && std::isfinite(sinkLn) && std::isfinite(sinkNl), "all paths finite");
        expect(cpMs > 0.0 && lnMs > 0.0 && nlMs > 0.0, "all paths ran");
    }
};
static NlSvfPerfTests nlSvfPerfTestsInstance;
```

- [ ] **Step 5: Build and run (capture the verdict)**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | grep -A6 "256-voice NlSvf perf"`
Expected: prints copy, lean, and baseline timings + ratios. **Record all three verbatim in the report**, and state the verdict: does the lean (zero-copy) path close Spike I's ~3% (→ glue, removable) or not (→ structural to the generated IO model)? Both are valid ADR evidence.

- [ ] **Step 6: Commit**

```bash
git add src/dsp/spine/cmajor/NlSvfLeanAdapter.h src/dsp/spine/cmajor/NlSvfLeanAdapter.cpp \
        tests/NlSvfPerfTests.cpp tests/CMakeLists.txt
git commit -m "spike(cmajor): lean/zero-copy adapter + 256-voice nonlinear perf bench"
```

---

### Task 5: Amend ADR-0012 + roadmap

**Files:**
- Modify: `docs/decisions/0012-cmajor-coexistence-evaluation.md`
- Modify: `tools/roadmap-dashboard/roadmap.json`

**Interfaces:** none (decision record).

- [ ] **Step 1: Amend ADR-0012**

Add a dated **"Spike II addendum (Nonlinear + Data-Boundary)"** subsection under Evidence in `docs/decisions/0012-cmajor-coexistence-evaluation.md`, capturing the measured results from Tasks 1–4:
- **Nonlinear equivalence:** did `NlSvf` match `NlSvfCell` (resSat on) per-sample (quote the worst error + the bound), and `AsymDrive` match `AsymSaturator` (per-sample or harmonic — quote which and the `tanh`/`pow` drift)?
- **Data boundary:** which mechanism crossed the table (runtime external-set vs baked) — the reusable answer for wavetables/granular.
- **Perf:** the 256-voice copy/lean/baseline ratios; the verdict on whether the ~3% is removable or structural.
- **Recommendation update:** does this confirm the GO/hybrid (raise confidence, note any caveats), or downgrade it? If confirmed, the ADR Status may move `Proposed → Accepted` **only on the user's explicit sign-off** — leave Status as Proposed and add a line "Ready to ratify pending user sign-off" unless the user has said otherwise.

- [ ] **Step 2: Update the roadmap**

In `tools/roadmap-dashboard/roadmap.json`: update the `cmajor-spike` summary to reference the Spike II results; if the user has ratified, set `cmajor-migration` `firmness` accordingly; otherwise leave it tentative with the summary noting "Spike II evidence in; pending sign-off." Update `meta.nextStep`. Validate: `node -e "JSON.parse(require('fs').readFileSync('tools/roadmap-dashboard/roadmap.json','utf8')); console.log('OK')"`.

- [ ] **Step 3: Commit**

```bash
git add docs/decisions/0012-cmajor-coexistence-evaluation.md tools/roadmap-dashboard/roadmap.json
git commit -m "docs(adr): ADR-0012 Spike II addendum — nonlinear + data-boundary + lean perf"
```

---

## Self-Review

**Spec coverage:**
- §2 decision "port BOTH nonlinear flavors" → Task 1 (in-loop resSat) + Task 2 (AsymDrive). ✓
- §2 "per-sample primary, harmonic fallback" → Tasks 1 & 2 tests assert per-sample, with the harmonic-fallback path documented in-test. ✓
- §2 "tiny wavetable oscillator" data probe → Task 3 (with the external-mechanism discovery as Step 1). ✓
- §2 "lean/zero-copy adapter, chase the 3%; in-place deferred" → Task 4 (zero-copy via direct `cmajIO`; PIMPL/heap retained). ✓
- §4 funnel steps 1–5 → Tasks 1, 2, 3, 4, 5. ✓
- §6 out-of-scope honored (no full HuggettFilter, no granular engine, no in-place state, no FilterModelLibrary, no plugin inclusion). ✓
- §7 success criteria → Task 5 amends ADR-0012 with both-stage equivalence + data mechanism + perf verdict. ✓

**Placeholder scan:** No "TBD/TODO". The one genuinely discovered element — the external-data mechanism — is isolated to Task 3 Step 1 (discovery) + the two clearly-marked alternative lines in `WtOscAdapter::setTable` (complete code for both (A) and (B), pick per the recorded finding), mirroring Spike I's generated-API isolation. Not a vague placeholder.

**Type consistency:** `NlSvfAdapter`/`NlSvfLeanAdapter` share `setParams(float,float,float,int)` + `process(float*,int)`; the lean adapter adds `maxBlock/inBlock/outBlock/advanceBlock`. `AsymDriveAdapter::setParams(float,float,float)`, `WtOscAdapter::setTable(const float*,int)`/`setFrequency(float)`. `NlSvfCell` calls (`prepare/reset/setCutoff/setResonance/setResSat/process(l,r,tap)`) and `AsymSaturator` calls (`setDrive(drive01,bias,maxDb)/process(x)`) match their real signatures (verified against the headers). Taps LP=0/HP=1/BP=2 consistent across the Cmajor source, adapters, and baseline. Generated-API method names match the Spike I-recorded shape; per-patch handle renumbering is avoided by using the named enum + typed `addEvent_*`/`setInputFrames_in`.

**Known spike risks (documented, not gaps):** (1) per-sample equivalence tightness is empirical — the tests log the worst error and carry a documented harmonic fallback. (2) The external-data mechanism is a genuine codegen discovery (Task 3 Step 1) with complete code for both outcomes. (3) The lean adapter may reveal the ~3% is structural — that is a recorded finding, not a failure.
