# Cmajor Spike Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Evaluate Cmajor as a coexistence DSP layer for Bernie by piloting the linear TPT SVF through the AOT generated-C++ path, producing equivalence + 256-voice perf evidence and ADR-0012 (go/no-go/hybrid).

**Architecture:** Write the SVF in Cmajor (`SvfLinear.cmajor`), generate dependency-free C++ with `cmaj generate --target=cpp` (committed), and isolate the unknown generated-class API behind a small hand-written **`SvfLinearAdapter`** whose interface is fixed and stable. Everything downstream — an equivalence bench vs `NlSvfCell`, a `CmajorSvfFilter : FilterModel` wrapper, and a 256-voice perf bench — consumes the stable adapter interface. All spike code compiles into the **test target only**; the shipping plugin is untouched.

**Tech Stack:** C++17, JUCE 8.0.4, CMake, the existing `tests/` JUCE `UnitTest` harness + `tests/testdsp`, and the `cmaj` CLI (dev-machine only, AOT codegen — never shipped, never in CI).

## Global Constraints

- **Build:** `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` then `cmake --build build --target k2000_tests -j4` — **always `-j4`, never bare `-j`** (bare `-j` OOMs JUCE). Run: `./build/tests/k2000_tests` (expect a `Summary: N tests, 0 failed` line).
- **AOT generated-C++ only.** Never link, ship, or depend on the Cmajor JIT/engine/runtime (`libCmajPerformer`, `cmaj::Patch`, `GeneratedCppEngine`). Only the **dependency-free** `cmaj generate --target=cpp` output is used. The `cmaj` CLI is a dev-time codegen tool, never in CI or the shipping build.
- **Commit the generated C++** (`SvfLinear.h/.cpp`) AND the `SvfLinear.cmajor` source. Regenerate only when the `.cmajor` changes.
- **Test-target-only.** All spike code (generated C++, adapter, wrapper, tests) compiles into `k2000_tests` exclusively. Do **not** add any of it to the `k2000_VST3` / `k2000_Standalone` targets or register `CmajorSvfFilter` in `FilterModelLibrary`.
- **Linear core only.** Port only the linear TPT SVF (cutoff/resonance, LP/BP/HP). No nonlinear resonance-saturator, no drive, no separation.
- **Spike isolation directory:** `src/dsp/spine/cmajor/` holds the `.cmajor`, generated C++, adapter, and wrapper.
- **The generated-class API is discovered in Task 2**, not guessed. It is consumed only inside `SvfLinearAdapter.cpp`; reconcile the method names there against the actual generated header.
- **Perf is a decision input, not a pass/fail gate** — the perf bench reports a ratio and asserts only finiteness/sanity.

---

### Task 1: Acquire `cmaj` + generate→compile smoke

**Files:**
- Create: `src/dsp/spine/cmajor/Gain.cmajor` (throwaway toolchain probe)
- Create: `src/dsp/spine/cmajor/.gitignore` (ignore the throwaway probe's generated output)
- Create: `tools/cmajor/cmaj-codegen.sh` (the dev-time codegen wrapper)

**Interfaces:**
- Produces: a working **containerised `cmaj` codegen path** and proof that `cmaj generate --target=cpp` emits C++ that compiles with the project's compiler. No code consumed by later tasks (this is a toolchain gate).

**Environment note (resolved 2026-06-21):** `cmaj` v1.0.3066 is already downloaded at `~/.local/cmaj/linux/x64` (binary `cmaj` + `libCmajPerformer.so`), but it is built against the **Ubuntu-22.04** library generation (`libwebkit2gtk-4.0.so.37`, `libjavascriptcoregtk-4.0.so.18`, `libunistring.so.2`) and **does not run natively on this Ubuntu-24.04 box** (those libs were dropped in 24.04; LD_LIBRARY_PATH hacks segfault). **Decision: run `cmaj` inside an Ubuntu-22.04 Docker container for codegen.** This requires Docker (`sudo apt-get install -y docker.io && sudo usermod -aG docker $USER`, then a Claude Code session restart so shells inherit the `docker` group). Codegen is one-shot and its output is committed, so Docker is a dev-time-only dependency — never in CI or the shipping build. This toolchain friction is itself an ADR-0012 finding.

- [ ] **Step 1: Verify Docker + create the codegen wrapper**

Confirm Docker is usable (after the one-time setup above): `docker info >/dev/null 2>&1 && echo "docker OK"`. If not usable, STOP and report BLOCKED (the user must enable Docker first).

Create `tools/cmajor/cmaj-codegen.sh` — runs the already-downloaded `cmaj` inside a jammy container, mounting the repo and the cmaj install, installing the webkit deps in the container:
```bash
#!/usr/bin/env bash
# Generate dependency-free C++ from a Cmajor patch, running cmaj in an Ubuntu 22.04
# container (the prebuilt Linux cmaj needs the 22.04 lib generation). Dev-time only.
# Usage: tools/cmajor/cmaj-codegen.sh <input.cmajor> <output.h>   (paths relative to repo root)
set -euo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
CMAJ_DIR="$HOME/.local/cmaj/linux/x64"
IN="$1"; OUT="$2"
docker run --rm \
  -v "$CMAJ_DIR":/cmaj:ro \
  -v "$REPO":/work -w /work \
  ubuntu:22.04 bash -c '
    set -e
    apt-get update -qq
    DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
        libwebkit2gtk-4.0-37 libjavascriptcoregtk-4.0-18 >/dev/null
    export LD_LIBRARY_PATH=/cmaj
    /cmaj/cmaj --version
    /cmaj/cmaj generate --target=cpp --output="'"$OUT"'" "'"$IN"'"
  '
echo "generated: $OUT"
```
Make it executable: `chmod +x tools/cmajor/cmaj-codegen.sh`. (The container apt-installs webkit each run; for repeated use the implementer may build a small cached image, but the one-shot form is fine for the spike.)
Expected: `docker OK`, and the wrapper script exists. **If Docker is unavailable, report BLOCKED.**

- [ ] **Step 2: Write a trivial gain patch probe**

`src/dsp/spine/cmajor/Gain.cmajor`:
```
processor Gain
{
    input  stream float in;
    output stream float out;
    input  event  float gain [[ name: "Gain", min: 0, max: 4, init: 2 ]];

    float g = 2.0f;
    event gain (float v) { g = v; }

    void main()
    {
        loop { out <- in * g; advance(); }
    }
}
```
And `src/dsp/spine/cmajor/.gitignore` (the Task-1 probe output is throwaway; the real SVF output in Task 2 IS committed):
```
# Throwaway toolchain-probe codegen (Task 1). The committed generated SVF
# (generated/SvfLinear.*) is NOT ignored — see Task 2.
/gain_gen/
```

- [ ] **Step 3: Generate C++ and verify it compiles**

```bash
cd /home/increp/dev/k2000
mkdir -p src/dsp/spine/cmajor/gain_gen
tools/cmajor/cmaj-codegen.sh src/dsp/spine/cmajor/Gain.cmajor src/dsp/spine/cmajor/gain_gen/Gain.h
ls -la src/dsp/spine/cmajor/gain_gen/
# Compile-only check with the project's C++ standard (no link, no run):
g++ -std=c++17 -fsyntax-only -I src/dsp/spine/cmajor/gain_gen src/dsp/spine/cmajor/gain_gen/Gain.h \
  && echo "GENERATED C++ COMPILES"
```
Expected: the wrapper prints the cmaj version then writes a header (and possibly a `.cpp`); the `g++ -fsyntax-only` prints `GENERATED C++ COMPILES`. (If `cmaj generate` writes a single self-contained header vs a `.h`+`.cpp`, note which in the report — Task 2 needs to know.) **This proves the toolchain end-to-end (container codegen + compilable output) without needing the runtime API.**

- [ ] **Step 4: Record the generated-class API shape**

Open the generated header and record, in the task report, the **public API of the generated class**: the class/struct name, how you construct/initialise it (look for `initialise`/constructor taking a sample rate or `frequency`), how a value/event endpoint is set (e.g. `setValue`, an `addEvent…` method, or endpoint-handle lookup), and how audio frames are pushed/advanced/read (e.g. `setInputFrames`/`advance`/`getOutputFrames`, or an `IOData`/frame-array struct). Quote the exact signatures. **This is the single discovery the rest of the plan depends on** — `SvfLinearAdapter.cpp` (Task 2) is written against it.

- [ ] **Step 5: Commit**

```bash
cd /home/increp/dev/k2000
git add src/dsp/spine/cmajor/Gain.cmajor src/dsp/spine/cmajor/.gitignore tools/cmajor/cmaj-codegen.sh
git commit -m "spike(cmajor): containerised cmaj codegen wrapper + toolchain probe"
```
(The `gain_gen/` output is git-ignored; only the `.cmajor` probe, gitignore, and codegen wrapper are committed.)

---

### Task 2: Port the linear SVF to Cmajor + the stable adapter

**Files:**
- Create: `src/dsp/spine/cmajor/SvfLinear.cmajor`
- Create: `src/dsp/spine/cmajor/generated/SvfLinear.h` (+ `.cpp` if codegen emits one) — **committed**
- Create: `src/dsp/spine/cmajor/SvfLinearAdapter.h`
- Create: `src/dsp/spine/cmajor/SvfLinearAdapter.cpp`
- Create: `tests/CmajorSvfAdapterTests.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: the generated-class API recorded in Task 1 Step 4.
- Produces: **`SvfLinearAdapter`** — a stable, generated-API-hiding wrapper used by Tasks 3–5:
  ```cpp
  class SvfLinearAdapter {
  public:
      enum Tap { LP = 0, HP = 1, BP = 2 };
      void prepare(double sampleRate) noexcept;   // (re)create + initialise the generated instance
      void reset() noexcept;                        // clear filter state
      void setParams(float cutoffHz, float resonance, int tap) noexcept;  // block-rate
      void process(float* mono, int numSamples) noexcept;  // in-place mono render
  };
  ```

- [ ] **Step 1: Write the SVF in Cmajor**

`src/dsp/spine/cmajor/SvfLinear.cmajor` — a TPT/ZDF SVF mirroring `NlSvfCell`'s linear core (same `g/k/a1/a2/a3` math, same `Q = 0.5 + res²·49.5`):
```
processor SvfLinear
{
    input  stream float in;
    output stream float out;
    input  event float cutoffHz  [[ name: "Cutoff",    min: 16,  max: 20000, init: 1000 ]];
    input  event float resonance [[ name: "Resonance", min: 0,   max: 0.999, init: 0 ]];
    input  event int32 tap       [[ name: "Tap",       min: 0,   max: 2,     init: 0 ]];  // 0=LP 1=HP 2=BP

    float g, k, a1, a2, a3;
    float ic1, ic2;
    bool  dirty = true;
    float cutoff = 1000.0f, res = 0.0f;
    int32 tapSel = 0;

    event cutoffHz  (float v) { cutoff = v; dirty = true; }
    event resonance (float v) { res = v;    dirty = true; }
    event tap       (int32 v) { tapSel = v; }

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
            let v0 = in;
            let v3 = v0 - ic2;
            let v1 = a1 * ic1 + a2 * v3;
            let v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
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

- [ ] **Step 2: Generate the C++ and commit it**

```bash
cd /home/increp/dev/k2000
mkdir -p src/dsp/spine/cmajor/generated
tools/cmajor/cmaj-codegen.sh src/dsp/spine/cmajor/SvfLinear.cmajor src/dsp/spine/cmajor/generated/SvfLinear.h
ls -la src/dsp/spine/cmajor/generated/
g++ -std=c++17 -fsyntax-only -I src/dsp/spine/cmajor/generated src/dsp/spine/cmajor/generated/SvfLinear.h \
  && echo "SVF GENERATED C++ COMPILES"
```
Expected: a committed `generated/SvfLinear.h` (+ `.cpp` if emitted) that passes `-fsyntax-only`. If `cmaj` reports a Cmajor syntax error in `SvfLinear.cmajor`, fix the `.cmajor` (the math above is correct; syntax like `let`, `event` handlers, `processor.frequency`, and `loop { … advance(); }` are the parts to verify against `cmaj`'s diagnostics) and regenerate.

- [ ] **Step 3: Write the adapter header (stable interface)**

`src/dsp/spine/cmajor/SvfLinearAdapter.h`:
```cpp
#pragma once
#include <memory>

// Stable wrapper hiding the generated Cmajor SVF class. Tasks 3-5 depend ONLY on
// this interface; the generated-class specifics live in SvfLinearAdapter.cpp.
class SvfLinearAdapter {
public:
    enum Tap { LP = 0, HP = 1, BP = 2 };

    SvfLinearAdapter();
    ~SvfLinearAdapter();
    SvfLinearAdapter(SvfLinearAdapter&&) noexcept;
    SvfLinearAdapter& operator=(SvfLinearAdapter&&) noexcept;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    void setParams(float cutoffHz, float resonance, int tap) noexcept;
    void process(float* mono, int numSamples) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
```

- [ ] **Step 4: Write the adapter implementation (reconcile with the generated API)**

`src/dsp/spine/cmajor/SvfLinearAdapter.cpp`. **Use the EXACT generated-class name and method signatures recorded in Task 1 Step 4 / visible in `generated/SvfLinear.h`.** The structure below is the contract; replace the four marked calls with the generated class's real API (the generated cpp from `cmaj generate --target=cpp` exposes an `initialise`/`advance` render model with per-endpoint value setters and frame I/O — names vary by Cmajor version):
```cpp
#include "SvfLinearAdapter.h"
#include "generated/SvfLinear.h"   // the committed generated class

// The generated class type name as it appears in generated/SvfLinear.h.
// (e.g. `SvfLinear` — confirm and adjust the alias to match the header.)
using Generated = SvfLinear;

struct SvfLinearAdapter::Impl {
    Generated dsp;
    double sr = 48000.0;

    void prepare(double sampleRate) {
        sr = sampleRate;
        // (1) INITIALISE with the sample rate. Use the generated initialise/ctor.
        dsp.initialise (/*sessionID*/ 0, sr);
    }
    void reset() {
        // Re-initialise to clear integrator state (cheapest correct reset).
        dsp.initialise (/*sessionID*/ 0, sr);
    }
    void setParams(float cutoffHz, float resonance, int tap) {
        // (2) PUSH the three event endpoints by their generated handles/setters.
        dsp.setValue (Generated::EndpointHandles::cutoffHz,  cutoffHz,  0);
        dsp.setValue (Generated::EndpointHandles::resonance, resonance, 0);
        dsp.setValue (Generated::EndpointHandles::tap,       tap,       0);
    }
    void process(float* mono, int numSamples) {
        for (int i = 0; i < numSamples; ++i) {
            // (3) WRITE one input frame, advance one frame, (4) READ one output frame.
            dsp.setInputFrames (Generated::EndpointHandles::in, &mono[i], 1);
            dsp.advance (1);
            dsp.copyOutputFrames (Generated::EndpointHandles::out, &mono[i], 1);
        }
    }
};

SvfLinearAdapter::SvfLinearAdapter() : impl_(std::make_unique<Impl>()) {}
SvfLinearAdapter::~SvfLinearAdapter() = default;
SvfLinearAdapter::SvfLinearAdapter(SvfLinearAdapter&&) noexcept = default;
SvfLinearAdapter& SvfLinearAdapter::operator=(SvfLinearAdapter&&) noexcept = default;

void SvfLinearAdapter::prepare(double sr) noexcept { impl_->prepare(sr); }
void SvfLinearAdapter::reset() noexcept { impl_->reset(); }
void SvfLinearAdapter::setParams(float c, float r, int t) noexcept { impl_->setParams(c, r, t); }
void SvfLinearAdapter::process(float* mono, int n) noexcept { impl_->process(mono, n); }
```
> The four numbered calls are the ONLY generated-API-dependent lines. If the generated API is block-oriented (advance N frames with a frame buffer) rather than per-sample, render the whole `numSamples` block in one `advance(numSamples)` with input/output frame arrays — the adapter's external contract (`process(float*, int)`) is unchanged. Record the final real API used, in the report.

- [ ] **Step 5: Wire the spike sources + test into the TEST target only**

In `tests/CMakeLists.txt`, add to the test executable's sources (the same list that names `HuggettSeparationTests.cpp`):
```cmake
    ${CMAKE_SOURCE_DIR}/src/dsp/spine/cmajor/SvfLinearAdapter.cpp
    CmajorSvfAdapterTests.cpp
```
and ensure the generated header is on the include path for the test target (add if not already covered):
```cmake
target_include_directories(k2000_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/dsp/spine/cmajor)
```
Do **not** add these to any plugin target.

- [ ] **Step 6: Write the adapter smoke test**

`tests/CmajorSvfAdapterTests.cpp` — proves the adapter drives the generated SVF and produces sane lowpass behaviour:
```cpp
#include <juce_core/juce_core.h>
#include "../src/dsp/spine/cmajor/SvfLinearAdapter.h"
#include <cmath>
#include <vector>

struct CmajorSvfAdapterTests : public juce::UnitTest {
    CmajorSvfAdapterTests() : juce::UnitTest("CmajorSvfAdapter") {}

    static double rmsAt(double freq, float cutoff, int tap) {
        const double sr = 48000.0;
        SvfLinearAdapter a; a.prepare(sr); a.reset();
        a.setParams(cutoff, 0.0f, tap);
        const int warm = 4096, meas = 4096;
        std::vector<float> buf((size_t)(warm + meas));
        for (int i = 0; i < warm + meas; ++i)
            buf[(size_t)i] = 0.5f * (float) std::sin(2.0 * juce::MathConstants<double>::pi * freq * i / sr);
        a.process(buf.data(), warm + meas);
        double sum = 0.0; for (int i = warm; i < warm + meas; ++i) sum += double(buf[(size_t)i]) * buf[(size_t)i];
        return std::sqrt(sum / meas);
    }

    void runTest() override {
        beginTest("adapter LP passes lows, attenuates highs");
        const double low  = rmsAt(200.0,  1000.0f, SvfLinearAdapter::LP);
        const double high = rmsAt(8000.0, 1000.0f, SvfLinearAdapter::LP);
        expect(std::isfinite(low) && std::isfinite(high), "finite output");
        expect(low > 0.1, "low passes (rms " + juce::String(low, 4) + ")");
        expect(high < low * 0.5, "high attenuated below low (low " + juce::String(low,4)
               + " high " + juce::String(high,4) + ")");
    }
};
static CmajorSvfAdapterTests cmajorSvfAdapterTestsInstance;
```

- [ ] **Step 7: Build and run**

Run: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | grep -E "CmajorSvfAdapter|Summary"`
Expected: the `CmajorSvfAdapter` test passes (LP passes lows, attenuates highs); `Summary: ... 0 failed`. If linking fails on a missing generated symbol, the generated header needs its companion `.cpp` added to the CMake sources — add `${CMAKE_SOURCE_DIR}/src/dsp/spine/cmajor/generated/SvfLinear.cpp` if codegen emitted one.

- [ ] **Step 8: Commit**

```bash
git add src/dsp/spine/cmajor/SvfLinear.cmajor src/dsp/spine/cmajor/generated/ \
        src/dsp/spine/cmajor/SvfLinearAdapter.h src/dsp/spine/cmajor/SvfLinearAdapter.cpp \
        tests/CmajorSvfAdapterTests.cpp tests/CMakeLists.txt
git commit -m "spike(cmajor): linear SVF in Cmajor + stable SvfLinearAdapter + smoke"
```

---

### Task 3: Equivalence — Cmajor SVF vs NlSvfCell

**Files:**
- Create: `tests/CmajorSvfEquivalenceTests.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `SvfLinearAdapter` (Task 2); `NlSvfCell` (`src/dsp/spine/NlSvfCell.h`); `tests/testdsp` helpers.
- Produces: a CI-failing equivalence gate (magnitude response within ~0.5 dB).

- [ ] **Step 1: Write the failing equivalence test**

`tests/CmajorSvfEquivalenceTests.cpp` — compares steady-state magnitude response of the adapter vs `NlSvfCell` (resonance-sat off, i.e. `setResSat(0)` so we compare the pure linear cores) across a cutoff×freq grid:
```cpp
#include <juce_core/juce_core.h>
#include "../src/dsp/spine/cmajor/SvfLinearAdapter.h"
#include "../src/dsp/spine/NlSvfCell.h"
#include <cmath>
#include <vector>

struct CmajorSvfEquivalenceTests : public juce::UnitTest {
    CmajorSvfEquivalenceTests() : juce::UnitTest("CmajorSvfEquivalence") {}
    static constexpr double kSR = 48000.0;

    static double adapterDb(double f, float cutoff, float res, int tap) {
        SvfLinearAdapter a; a.prepare(kSR); a.reset(); a.setParams(cutoff, res, tap);
        return measureDb(f, [&](float* buf, int n){ a.process(buf, n); });
    }
    static double nlsvfDb(double f, float cutoff, float res, int tap) {
        NlSvfCell c; c.prepare(kSR); c.reset();
        c.setCutoff(cutoff); c.setResonance(res); c.setResSat(0.0f);  // linear core only
        return measureDb(f, [&](float* buf, int n){
            for (int i = 0; i < n; ++i) { float l = buf[i], r = buf[i]; c.process(l, r, tap); buf[i] = l; }
        });
    }
    template <typename Proc>
    static double measureDb(double f, Proc&& proc) {
        const int warm = 8192, meas = 8192;
        std::vector<float> buf((size_t)(warm + meas));
        for (int i = 0; i < warm + meas; ++i)
            buf[(size_t)i] = 0.3f * (float) std::sin(2.0 * juce::MathConstants<double>::pi * f * i / kSR);
        std::vector<float> in = buf;
        proc(buf.data(), warm + meas);
        double inSq = 0.0, outSq = 0.0;
        for (int i = warm; i < warm + meas; ++i) { inSq += double(in[(size_t)i])*in[(size_t)i]; outSq += double(buf[(size_t)i])*buf[(size_t)i]; }
        return 20.0 * std::log10(std::max(1e-7, std::sqrt(outSq / inSq)));
    }

    void runTest() override {
        beginTest("Cmajor SVF matches NlSvfCell linear core within 0.5 dB");
        const float cutoffs[] = { 250.0f, 1000.0f, 4000.0f };
        const double freqs[]  = { 100, 300, 1000, 3000, 8000 };
        const int taps[] = { SvfLinearAdapter::LP, SvfLinearAdapter::HP, SvfLinearAdapter::BP };
        for (float cut : cutoffs)
            for (int tap : taps)
                for (double f : freqs) {
                    const double a = adapterDb(f, cut, 0.3f, tap);
                    const double n = nlsvfDb(f, cut, 0.3f, tap);
                    expect(std::abs(a - n) < 0.5,
                        "cut " + juce::String(cut,0) + " tap " + juce::String(tap)
                        + " f " + juce::String(f,0) + ": adapter " + juce::String(a,2)
                        + " vs NlSvf " + juce::String(n,2) + " dB");
                }
    }
};
static CmajorSvfEquivalenceTests cmajorSvfEquivalenceTestsInstance;
```

- [ ] **Step 2: Register the test**

Add `CmajorSvfEquivalenceTests.cpp` to the test sources in `tests/CMakeLists.txt` (alongside `CmajorSvfAdapterTests.cpp`).

- [ ] **Step 3: Build and run**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | grep -E "CmajorSvfEquivalence|Summary"`
Expected: PASS within 0.5 dB. If a tap is off by a constant (e.g. BP scaling convention or an HP sign), the divergence pins a real semantic difference between the Cmajor port and `NlSvfCell` — fix `SvfLinear.cmajor` to match `NlSvfCell`'s tap formulas (HP `= v0 - k*v1 - v2`, BP `= v1`, LP `= v2`), regenerate, rebuild. Record any tolerance that had to be loosened (and why) in the report — it is ADR evidence.

- [ ] **Step 4: Commit**

```bash
git add tests/CmajorSvfEquivalenceTests.cpp tests/CMakeLists.txt
git commit -m "spike(cmajor): equivalence gate — Cmajor SVF vs NlSvfCell within 0.5 dB"
```

---

### Task 4: `CmajorSvfFilter : FilterModel` wrapper + integration proof

**Files:**
- Create: `src/dsp/spine/cmajor/CmajorSvfFilter.h`
- Create: `src/dsp/spine/cmajor/CmajorSvfFilter.cpp`
- Create: `tests/CmajorFilterModelTests.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `FilterModel` (`src/dsp/spine/FilterModel.h` — `prepare`/`makeState`/`reset`/`setCommon`/`processStereo`); `SvfLinearAdapter` (Task 2).
- Produces: `CmajorSvfFilter : FilterModel` proving the generated DSP slots into Bernie's per-voice spine. Stereo via two adapters per voice.

- [ ] **Step 1: Write the FilterModel wrapper**

`src/dsp/spine/cmajor/CmajorSvfFilter.h`:
```cpp
#pragma once
#include "../FilterModel.h"
#include "SvfLinearAdapter.h"

// Test-only FilterModel backed by the generated Cmajor SVF. NOT registered in
// FilterModelLibrary; compiled into the test target only. Stereo = two adapters.
class CmajorSvfFilter : public FilterModel {
public:
    struct VoiceState : public FilterModel::State {
        SvfLinearAdapter l, r;
    };
    void prepare(double sampleRate) noexcept override { sampleRate_ = sampleRate; }
    State* makeState() const override;
    void reset(State& s) const noexcept override;
    void setCommon(float cutoffHz, float resonance, float drive) noexcept override;
    void setTap(int tap) noexcept { tap_ = tap; }   // 0=LP 1=HP 2=BP (Huggett-bank-style setter)
    void processStereo(State& s, float* left, float* right, int numSamples) const noexcept override;
private:
    double sampleRate_ = 48000.0;
    float  cutoffHz_ = 1000.0f, resonance_ = 0.0f;
    int    tap_ = 0;
};
```
`src/dsp/spine/cmajor/CmajorSvfFilter.cpp`:
```cpp
#include "CmajorSvfFilter.h"

FilterModel::State* CmajorSvfFilter::makeState() const {
    auto* vs = new VoiceState();
    vs->l.prepare(sampleRate_);
    vs->r.prepare(sampleRate_);
    return vs;
}

void CmajorSvfFilter::reset(State& s) const noexcept {
    auto& vs = static_cast<VoiceState&>(s);
    vs.l.reset(); vs.r.reset();
}

void CmajorSvfFilter::setCommon(float cutoffHz, float resonance, float /*drive*/) noexcept {
    cutoffHz_  = cutoffHz;
    resonance_ = resonance;   // drive ignored — linear pilot
}

void CmajorSvfFilter::processStereo(State& s, float* left, float* right, int n) const noexcept {
    auto& vs = static_cast<VoiceState&>(s);
    vs.l.setParams(cutoffHz_, resonance_, tap_);
    vs.r.setParams(cutoffHz_, resonance_, tap_);
    vs.l.process(left,  n);
    vs.r.process(right, n);
}
```

- [ ] **Step 2: Write the integration + RT-safety test**

`tests/CmajorFilterModelTests.cpp`:
```cpp
#include <juce_core/juce_core.h>
#include "../src/dsp/spine/cmajor/CmajorSvfFilter.h"
#include <cmath>
#include <memory>
#include <vector>

struct CmajorFilterModelTests : public juce::UnitTest {
    CmajorFilterModelTests() : juce::UnitTest("CmajorFilterModel") {}

    void runTest() override {
        beginTest("CmajorSvfFilter runs through the FilterModel interface (stereo, param-driven)");
        const double sr = 48000.0;
        CmajorSvfFilter f; f.prepare(sr); f.setTap(0);
        std::unique_ptr<FilterModel::State> st(f.makeState()); f.reset(*st);
        f.setCommon(1000.0f, 0.2f, 0.0f);

        auto rms = [&](double freq) {
            f.reset(*st);
            const int warm = 4096, meas = 4096;
            std::vector<float> l((size_t)(warm+meas)), r((size_t)(warm+meas));
            for (int i = 0; i < warm + meas; ++i)
                l[(size_t)i] = r[(size_t)i] = 0.4f * (float) std::sin(2.0 * juce::MathConstants<double>::pi * freq * i / sr);
            f.processStereo(*st, l.data(), r.data(), warm + meas);
            double sl = 0.0, sr2 = 0.0;
            for (int i = warm; i < warm + meas; ++i) { sl += double(l[(size_t)i])*l[(size_t)i]; sr2 += double(r[(size_t)i])*r[(size_t)i]; }
            expect(std::abs(sl - sr2) < 1e-6, "L and R identical for identical input");
            return std::sqrt(sl / meas);
        };
        const double low = rms(200.0), high = rms(8000.0);
        expect(std::isfinite(low) && std::isfinite(high), "finite");
        expect(high < low * 0.5, "LP attenuates highs through the FilterModel path");

        beginTest("processStereo does not allocate on the audio thread");
        // Structural assertion: processStereo only calls setParams/process on pre-made
        // adapters (no makeState/new in the hot path). Verified by code review + this
        // smoke running a large block without state recreation.
        std::vector<float> big((size_t)8192, 0.1f), big2((size_t)8192, 0.1f);
        f.processStereo(*st, big.data(), big2.data(), 8192);
        expect(std::isfinite(big[8191]), "large block stays finite");
    }
};
static CmajorFilterModelTests cmajorFilterModelTestsInstance;
```

- [ ] **Step 3: Register sources + test**

In `tests/CMakeLists.txt`, add `${CMAKE_SOURCE_DIR}/src/dsp/spine/cmajor/CmajorSvfFilter.cpp` and `CmajorFilterModelTests.cpp` to the test sources.

- [ ] **Step 4: Build and run**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | grep -E "CmajorFilterModel|Summary"`
Expected: PASS (stereo identical, LP attenuates highs, large block finite); `Summary: ... 0 failed`.

- [ ] **Step 5: Commit**

```bash
git add src/dsp/spine/cmajor/CmajorSvfFilter.h src/dsp/spine/cmajor/CmajorSvfFilter.cpp \
        tests/CmajorFilterModelTests.cpp tests/CMakeLists.txt
git commit -m "spike(cmajor): CmajorSvfFilter FilterModel wrapper + integration proof"
```

---

### Task 5: 256-voice perf bench (the crux)

**Files:**
- Create: `tests/CmajorSvfPerfTests.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `SvfLinearAdapter` (Task 2), `NlSvfCell` (C++ baseline).
- Produces: a printed CPU ratio + per-instance memory note (the ADR's primary input). Asserts only finiteness/sanity.

- [ ] **Step 1: Write the perf bench**

`tests/CmajorSvfPerfTests.cpp` — processes equal work through 256 generated adapters vs 256 `NlSvfCell`s and prints the wall-clock ratio:
```cpp
#include <juce_core/juce_core.h>
#include "../src/dsp/spine/cmajor/SvfLinearAdapter.h"
#include "../src/dsp/spine/NlSvfCell.h"
#include <chrono>
#include <vector>
#include <cmath>

struct CmajorSvfPerfTests : public juce::UnitTest {
    CmajorSvfPerfTests() : juce::UnitTest("CmajorSvfPerf") {}

    void runTest() override {
        beginTest("256-voice perf: Cmajor adapter vs NlSvfCell (ratio is a decision input)");
        const double sr = 48000.0;
        const int kVoices = 256, kBlock = 128, kBlocks = 200;  // ~200 blocks of 128 @ 256 voices

        std::vector<float> block((size_t)kBlock);
        for (int i = 0; i < kBlock; ++i) block[(size_t)i] = 0.3f * (float) std::sin(0.05 * i);

        // --- Cmajor adapters ---
        std::vector<SvfLinearAdapter> cm((size_t)kVoices);
        for (auto& a : cm) { a.prepare(sr); a.reset(); a.setParams(1000.0f, 0.3f, 0); }
        auto t0 = std::chrono::high_resolution_clock::now();
        double sinkA = 0.0;
        for (int b = 0; b < kBlocks; ++b)
            for (auto& a : cm) { auto buf = block; a.process(buf.data(), kBlock); sinkA += buf[(size_t)(kBlock-1)]; }
        auto t1 = std::chrono::high_resolution_clock::now();

        // --- C++ NlSvfCell baseline ---
        std::vector<NlSvfCell> nl((size_t)kVoices);
        for (auto& c : nl) { c.prepare(sr); c.reset(); c.setCutoff(1000.0f); c.setResonance(0.3f); c.setResSat(0.0f); }
        auto t2 = std::chrono::high_resolution_clock::now();
        double sinkB = 0.0;
        for (int b = 0; b < kBlocks; ++b)
            for (auto& c : nl) { auto buf = block; for (int i = 0; i < kBlock; ++i) { float l = buf[(size_t)i], r = l; c.process(l, r, 0); buf[(size_t)i] = l; } sinkB += buf[(size_t)(kBlock-1)]; }
        auto t3 = std::chrono::high_resolution_clock::now();

        const double cmMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
        const double nlMs = std::chrono::duration<double, std::milli>(t3 - t2).count();
        std::printf("\n=== 256-voice SVF perf (%d blocks x %d samples) ===\n", kBlocks, kBlock);
        std::printf("Cmajor adapter: %8.2f ms\n", cmMs);
        std::printf("NlSvfCell C++ : %8.2f ms\n", nlMs);
        std::printf("ratio (cmaj/cpp): %6.2fx   [sinks %.3f %.3f]\n", cmMs / nlMs, sinkA, sinkB);
        std::printf("sizeof(SvfLinearAdapter)=%zu  sizeof(NlSvfCell)=%zu (note: adapter holds a unique_ptr to the generated state)\n",
                    sizeof(SvfLinearAdapter), sizeof(NlSvfCell));

        expect(std::isfinite(sinkA) && std::isfinite(sinkB), "both paths produce finite output");
        expect(cmMs > 0.0 && nlMs > 0.0, "both paths ran");
    }
};
static CmajorSvfPerfTests cmajorSvfPerfTestsInstance;
```

- [ ] **Step 2: Register the test**

Add `CmajorSvfPerfTests.cpp` to the test sources in `tests/CMakeLists.txt`.

- [ ] **Step 3: Build and run (capture the ratio)**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | grep -A6 "256-voice SVF perf"`
Expected: the bench prints both timings and the `ratio (cmaj/cpp)`; the test passes (finite, both ran). **Record the printed ratio + the two `sizeof`s verbatim in the report — they are ADR-0012's primary evidence.** (Note: per-sample `advance(1)` calls may dominate if the generated API is per-sample; if so, also try a block-render variant in the adapter and record both numbers — this distinguishes Cmajor's intrinsic DSP cost from per-call overhead.)

- [ ] **Step 4: Commit**

```bash
git add tests/CmajorSvfPerfTests.cpp tests/CMakeLists.txt
git commit -m "spike(cmajor): 256-voice perf bench — Cmajor adapter vs NlSvfCell"
```

---

### Task 6: ADR-0012 + recommendation

**Files:**
- Create: `docs/decisions/0012-cmajor-coexistence-evaluation.md`
- Modify: `docs/decisions/README.md` (add the ADR to the index, matching its format)
- Modify: `tools/roadmap-dashboard/roadmap.json` (mark `cmajor-spike` shipped; set the migration's recommendation)

**Interfaces:** none (decision record).

- [ ] **Step 1: Write ADR-0012**

`docs/decisions/0012-cmajor-coexistence-evaluation.md` — follow the existing ADR format (read `docs/decisions/0011-selectable-spine-filter-library.md` for the template: Status, Context, Decision, Consequences). Capture, with the **measured numbers from Tasks 3 & 5**:
- **Context:** the pre-v6 question (avoid building the v6 graph twice); coexistence framing (JUCE host/UI/params + Cmajor DSP); the AOT-only licensing position (generated C++ is ours; engine is GPL/commercial — §2 of the spec).
- **Evidence:** equivalence result (Task 3 — did the Cmajor SVF match `NlSvfCell` within 0.5 dB?); the 256-voice CPU ratio + per-instance memory (Task 5 — quote the numbers); integration friction (Task 4 — did it slot into `FilterModel` cleanly? RT-safety?); dev experience (was the `.cmajor` + codegen workflow pleasant vs hand-written C++?).
- **Decision:** an explicit **go / no-go / hybrid** recommendation. If hybrid/go: where the JUCE↔Cmajor boundary sits (e.g. "Cmajor for per-voice DSP blocks, JUCE for everything else"), whether v6's graph is authored in Cmajor, and the `cmajor-migration` shape/slot. If no-go: why (perf, friction, or licensing), and whether a narrow slice still warrants it.
- **Consequences:** what this commits/avoids for v6; the codegen-commit workflow if adopting; any follow-up spikes.

- [ ] **Step 2: Update the roadmap**

In `tools/roadmap-dashboard/roadmap.json`: set `cmajor-spike` `"status": "shipped"` + `"shipped": "<today>"`; update its summary to point at ADR-0012; set `meta.nextStep` to the next agreed item (per the ADR's recommendation — e.g. the v6 design or the migration). Update the `cmajor-migration` item's summary to reflect the ADR's go/no-go/hybrid verdict. Validate: `node -e "JSON.parse(require('fs').readFileSync('tools/roadmap-dashboard/roadmap.json','utf8')); console.log('OK')"`.

- [ ] **Step 3: Commit**

```bash
git add docs/decisions/0012-cmajor-coexistence-evaluation.md docs/decisions/README.md \
        tools/roadmap-dashboard/roadmap.json
git commit -m "docs(adr): ADR-0012 Cmajor coexistence evaluation + roadmap update"
```

---

## Self-Review

**Spec coverage:**
- §2 AOT-only / commit-generated-C++ / test-target-only / linear-only → Global Constraints + Tasks 1–5. ✓
- §3 files (SvfLinear.cmajor, generated/, CmajorSvfFilter, tests) → Tasks 2 & 4. ✓ (The adapter `SvfLinearAdapter` is an addition beyond §3's file list — a deliberate design refinement isolating the unknown generated API behind a stable interface; noted in the plan's Architecture.)
- §4 funnel steps 1–7 → Tasks 1 (toolchain), 2 (port), 3 (equivalence), 4 (wrapper/integration), 5 (perf), 6 (ADR). Funnel step 6 "CI compiles the committed generated C++" is satisfied implicitly: the generated C++ + adapter live in the test target, which the Windows CI builds — no separate task needed (noted in Global Constraints). ✓
- §5 testing (asserts CI-failing; perf is a decision input) → Tasks 3/4 hard asserts, Task 5 prints ratio + sanity-asserts. ✓
- §6 out-of-scope honored (no FilterModelLibrary registration, no engine, no nonlinear, no build-time codegen). ✓
- §7 success criteria → Task 6 ADR synthesizes equivalence + perf + friction + recommendation. ✓

**Placeholder scan:** No TBD/TODO. The one genuinely discovered element — the generated-class API — is explicitly isolated to the four marked calls in `SvfLinearAdapter.cpp` (Task 2 Step 4), with the discovery recorded in Task 1 Step 4; this is a real spike dependency on codegen output, not a vague placeholder. The Cmajor SVF source and all Bernie-side code (adapter interface, tests, wrapper, perf, CMake) are complete.

**Type consistency:** `SvfLinearAdapter` (prepare/reset/setParams/process, Tap enum), `CmajorSvfFilter`/`VoiceState`, and `NlSvfCell` calls (`prepare/reset/setCutoff/setResonance/setResSat/process(l,r,tap)`) match their real signatures across Tasks 2–5. `FilterModel` overrides match the real interface (prepare/makeState/reset/setCommon/processStereo). Taps LP=0/HP=1/BP=2 are consistent between the Cmajor source, the adapter, and `NlSvfCell`.

**Known spike risk (documented, not a gap):** Task 1 can return BLOCKED if no working Linux `cmaj` binary exists — that is itself a recorded ADR finding (dev-toolchain availability), not a plan defect. The exact generated-API calls in `SvfLinearAdapter.cpp` are reconciled against the real header during Task 2; if the generated API is block-oriented, the adapter renders per-block while keeping its external contract.
