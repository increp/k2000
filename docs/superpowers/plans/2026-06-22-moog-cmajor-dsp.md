# Moog Ladder DSP in Cmajor (Spec 1) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the Moog transistor-ladder filter as the first production DSP block authored in Cmajor — a fused processor → committed generated C++ → in-place adapter → `MoogLadder : FilterModel`, validated in isolation in the test target.

**Architecture:** One fused `MoogLadder.cmajor` processor (4-pole TPT ladder + resonance feedback + per-stage `tanh` + output limiter + a played-note sub-osc bass voice). Generated to committed C++ with a **small `maxFramesPerBlock`** so the inline state stays small. A by-value in-place adapter (no heap) wraps it; `MoogLadder : FilterModel` runs two adapters (stereo). Correctness is pinned by behavioral/analytical tests + an Arturia golden-data harness — there is **no hand-C++ twin** (that is the double-build we avoid).

**Tech Stack:** C++17, JUCE 8.0.4, Cmajor (dev-time codegen via Docker), CMake, JUCE `UnitTest`.

## Global Constraints

- **Spec:** `docs/superpowers/specs/2026-06-22-moog-cmajor-dsp-design.md`. This is **Spec 1** — test-target only; **do NOT** register in `FilterModelLibrary`, touch `Layer`/`Voice`/params/UI, or add the base `setVoiceContext` hook (all Spec 2).
- **Codegen (dev-time, Docker):** regenerate after every `.cmajor` change with
  `sg docker -c "tools/cmajor/cmaj-codegen.sh src/dsp/spine/cmajor/MoogLadder.cmajorpatch src/dsp/spine/cmajor/generated/MoogLadder.h"`.
  Commit the `.cmajor`, `.cmajorpatch`, **and** the generated `.h` together. cmaj does **not** run in CI; CI compiles the committed C++ only. (`cmaj` segfaults natively on this 24.04 box — always via the Docker script.)
- **Build/test:** `cmake --build build --target k2000_tests -j4` (ALWAYS `-j4`). Run `./build/tests/k2000_tests`; each test prints `[PASS]/[FAIL] <Name>: …`, run ends `Summary: N tests, M failed`. Green = `0 failed`. Grep stdout.
- **RT-safety:** the adapter embeds the generated state **by value**; `constructState` placement-news into the slot buffer; nothing allocates after `prepare`.
- **Small-block codegen:** generate with `maxFramesPerBlock ≈ 32–64` (task 1 finds the exact `cmaj` mechanism; fallback = default 512 + a larger `kMaxSpineStateBytes`).
- **`bassAmount==0` must be bit-identical** to the ladder-only path.
- **Pristine test output** (`-Wshadow`/`-Wfloat-equal` are defects; pre-existing `-Wsign-conversion` is deferred policy).
- **Never** change a `// CALIB` constant or loosen an assertion to force a test green; tune the Cmajor/CALIB to meet the spec'd behavior.
- **Branch:** `feat/moog-cmajor-dsp` (already created; spec committed).

---

### Task 1: Pipeline + small-block codegen + in-place adapter (de-risk)

Prove the whole pipeline on a TRIVIAL processor before any ladder math: author a one-pole `MoogLadder.cmajor`, find the `cmaj` setting for a small `maxFramesPerBlock`, generate, embed the generated class **by value** in an in-place adapter, `constructState` it into a raw buffer, and measure `sizeof`. This resolves the spec's primary risk.

**Files:**
- Create: `src/dsp/spine/cmajor/MoogLadder.cmajor`, `src/dsp/spine/cmajor/MoogLadder.cmajorpatch`
- Create: `src/dsp/spine/cmajor/generated/MoogLadder.h` (codegen output, committed)
- Create: `src/dsp/spine/cmajor/MoogLadderAdapter.h`, `src/dsp/spine/cmajor/MoogLadderAdapter.cpp`
- Create: `tests/MoogPipelineTests.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `class MoogLadderAdapter` with `MoogLadderAdapter() noexcept`, `~MoogLadderAdapter()`, `void prepare(double) noexcept`, `void reset() noexcept`, `void setCutoff(float) noexcept`, `void process(float* mono, int n) noexcept`. Non-copyable. Holds the generated processor inline (no heap); `process` chunks `n` to the generated `maxFramesPerBlock`.

- [ ] **Step 1: Find the `cmaj` block-size mechanism**

Run (one-off, inside the container): `sg docker -c 'CMAJ_DIR=$HOME/.local/cmaj/linux/x64; docker run --rm -v $CMAJ_DIR:/cmaj:ro ubuntu:22.04 bash -c "apt-get update -qq; DEBIAN_FRONTEND=noninteractive apt-get install -y -qq libwebkit2gtk-4.0-37 libjavascriptcoregtk-4.0-18 >/dev/null; LD_LIBRARY_PATH=/cmaj /cmaj/cmaj generate --help"'`
Look in the help for a max-block-size / frames-per-block flag (e.g. `--maxFramesPerBlock`). Record the exact flag. If present, add it to `tools/cmajor/cmaj-codegen.sh`'s `cmaj generate` line (or pass via env). If absent, set `maxFramesPerBlock` in the `.cmajorpatch` if supported; if neither works, proceed at 512 and note the fallback in the report.

- [ ] **Step 2: Write the trivial processor** — `src/dsp/spine/cmajor/MoogLadder.cmajor`

```
// Moog transistor-ladder (Spec 1). Task 1 = trivial one-pole to prove the
// codegen+in-place pipeline; later tasks grow this into the 4-pole ladder.
processor MoogLadder
{
    input  stream float in;
    output stream float out;
    input  event float cutoffHz [[ name: "Cutoff", min: 16, max: 20000, init: 1000 ]];

    float g, z;
    bool  dirty = true;
    float cutoff = 1000.0f;
    event cutoffHz (float v) { cutoff = v; dirty = true; }

    void recompute() { let sr = float (processor.frequency);
                       g = tan (float (pi) * clamp (cutoff, 16.0f, sr * 0.45f) / sr); dirty = false; }
    void main()
    {
        loop {
            if (dirty) recompute();
            let G = g / (1.0f + g);
            let v = (in - z) * G;
            let y = v + z;
            z = y + v;            // trapezoidal one-pole LP
            out <- y;
            advance();
        }
    }
}
```

- [ ] **Step 3: Write the patch manifest** — `src/dsp/spine/cmajor/MoogLadder.cmajorpatch`

```json
{
    "CmajorVersion": 1,
    "ID": "dev.bernie.moogladder",
    "version": "1.0",
    "name": "MoogLadder",
    "description": "Moog transistor-ladder filter (production Cmajor block, Spec 1)",
    "mainProcessor": "MoogLadder",
    "source": [ "MoogLadder.cmajor" ]
}
```

- [ ] **Step 4: Generate the C++** (using the small block size from Step 1)

Run: `sg docker -c "tools/cmajor/cmaj-codegen.sh src/dsp/spine/cmajor/MoogLadder.cmajorpatch src/dsp/spine/cmajor/generated/MoogLadder.h"`
Expected: `generated: src/dsp/spine/cmajor/generated/MoogLadder.h`. Confirm `grep maxFramesPerBlock src/dsp/spine/cmajor/generated/MoogLadder.h` shows the small value (≤64) if Step 1 found the flag.

- [ ] **Step 5: Write the in-place adapter** — `MoogLadderAdapter.h`

```cpp
#pragma once
#include <cstddef>

// In-place (no-heap) wrapper for the generated Moog processor. Holds the generated
// class by VALUE (the generated state is embeddable — proven by NlSvfDriveLeanAdapter).
// One adapter per mono lane; MoogLadder::VoiceState holds two. The generated header is
// included only in the .cpp; this header exposes a fixed-size aligned buffer so callers
// don't pull in the 30 KB generated header.
class MoogLadderAdapter {
public:
    MoogLadderAdapter() noexcept;
    ~MoogLadderAdapter();
    MoogLadderAdapter(const MoogLadderAdapter&) = delete;
    MoogLadderAdapter& operator=(const MoogLadderAdapter&) = delete;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    void setCutoff(float hz) noexcept;
    void process(float* mono, int numSamples) noexcept;

private:
    // Storage for the generated processor, placement-constructed in the .cpp.
    // kGenBytes/kGenAlign are validated against sizeof/alignof(Generated) by static_assert in the .cpp.
    static constexpr std::size_t kGenBytes = 2048;   // pin from the measured sizeof in Step 7
    static constexpr std::size_t kGenAlign = 16;
    alignas(kGenAlign) unsigned char storage_[kGenBytes];
};
```

- [ ] **Step 6: Write the adapter impl** — `MoogLadderAdapter.cpp`

```cpp
#include "MoogLadderAdapter.h"
#include "generated/MoogLadder.h"
#include <algorithm>
#include <cstring>
#include <new>

using Generated = MoogLadder;
static_assert(sizeof(Generated)  <= 2048, "MoogLadder generated state exceeds MoogLadderAdapter::kGenBytes — bump it");
static_assert(alignof(Generated) <= 16,   "MoogLadder generated state over-aligned for the adapter buffer");

namespace { Generated* gen(unsigned char* s) { return reinterpret_cast<Generated*>(s); } }

MoogLadderAdapter::MoogLadderAdapter() noexcept { new (storage_) Generated(); }
MoogLadderAdapter::~MoogLadderAdapter() { gen(storage_)->~Generated(); }

void MoogLadderAdapter::prepare(double sr) noexcept { gen(storage_)->initialise(0, sr); }
void MoogLadderAdapter::reset() noexcept { gen(storage_)->reset(); }
void MoogLadderAdapter::setCutoff(float hz) noexcept { gen(storage_)->addEvent_cutoffHz(hz); }

void MoogLadderAdapter::process(float* mono, int numSamples) noexcept {
    auto* d = gen(storage_);
    const int cap = (int) Generated::maxFramesPerBlock;
    int i = 0;
    while (i < numSamples) {
        const int n = std::min(numSamples - i, cap);
        std::memcpy(d->cmajIO.in.elements, &mono[i], (size_t) n * sizeof(float));
        d->advance(n);
        std::memcpy(&mono[i], &d->cmajIO.out, (size_t) n * sizeof(float));
        i += n;
    }
}
```

(If the real generated endpoint names differ, mirror `NlSvfDriveLeanAdapter.cpp`'s `cmajIO.in.elements` / `&cmajIO.out` access exactly.)

- [ ] **Step 7: Write the pipeline test** — `tests/MoogPipelineTests.cpp`

```cpp
#include <juce_core/juce_core.h>
#include "../src/dsp/spine/cmajor/MoogLadderAdapter.h"
#include <vector>
#include <cmath>
#include <cstddef>

// Task 1: prove codegen + in-place embedding + small footprint, and that the
// one-pole actually attenuates highs.
struct MoogPipelineTests : public juce::UnitTest {
    MoogPipelineTests() : juce::UnitTest("MoogPipeline") {}
    static constexpr double kSR = 48000.0;

    static double rms(MoogLadderAdapter& a, double freq) {
        std::vector<float> buf(8192);
        for (int i = 0; i < (int) buf.size(); ++i)
            buf[(size_t) i] = (float) std::sin(2.0 * juce::MathConstants<double>::pi * freq * i / kSR);
        a.process(buf.data(), (int) buf.size());
        double e = 0; for (int i = 4096; i < (int) buf.size(); ++i) e += double(buf[(size_t)i])*buf[(size_t)i];
        return std::sqrt(e / 4096.0);
    }

    void runTest() override {
        beginTest("adapter is in-place and embeds cheaply");
        // Footprint sanity: with the small-block codegen the adapter is small.
        logMessage("sizeof(MoogLadderAdapter) = " + juce::String((int) sizeof(MoogLadderAdapter)));
        expect(sizeof(MoogLadderAdapter) <= 4096, "adapter larger than expected — check maxFramesPerBlock");

        beginTest("placement-construct into a raw buffer (no heap) and run");
        alignas(16) unsigned char buf[sizeof(MoogLadderAdapter)];
        auto* a = new (buf) MoogLadderAdapter();
        a->prepare(kSR); a->reset(); a->setCutoff(500.0f);
        const double low  = rms(*a, 200.0);
        a->reset(); a->setCutoff(500.0f);
        const double high = rms(*a, 5000.0);
        expect(std::isfinite(low) && std::isfinite(high), "non-finite output");
        expect(high < low * 0.5, "one-pole LP did not attenuate 5 kHz vs 200 Hz");
        a->~MoogLadderAdapter();
    }
};
static MoogPipelineTests moogPipelineTestsInstance;
```

Register in `tests/CMakeLists.txt`: add `MoogPipelineTests.cpp` to the source list and `../src/dsp/spine/cmajor/MoogLadderAdapter.cpp` (near the other `cmajor/*Adapter.cpp` entries).

- [ ] **Step 8: Build, run, measure**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | grep -E "MoogPipeline|Summary"`
Expected: `[PASS] MoogPipeline`, `Summary: … 0 failed`. Read the logged `sizeof(MoogLadderAdapter)`; **pin `kGenBytes`** in `MoogLadderAdapter.h` to the measured `sizeof(Generated)` + headroom (and update the `static_assert`). Rebuild green.

- [ ] **Step 9: Commit**

```bash
git add src/dsp/spine/cmajor/MoogLadder.cmajor src/dsp/spine/cmajor/MoogLadder.cmajorpatch \
        src/dsp/spine/cmajor/generated/MoogLadder.h src/dsp/spine/cmajor/MoogLadderAdapter.h \
        src/dsp/spine/cmajor/MoogLadderAdapter.cpp tests/MoogPipelineTests.cpp tests/CMakeLists.txt \
        tools/cmajor/cmaj-codegen.sh
git commit -m "feat(moog): Cmajor pipeline + small-block in-place adapter (de-risk)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: Linear ladder core + `MoogLadder : FilterModel` + Q18 sizing

Grow the processor into the linear 4-pole ZDF ladder (closed-form feedback solve, taps `y2`/`y4`), wrap it as a stereo `FilterModel`, and pin `kMaxSpineStateBytes`. Tests: linearity vs an analytical 4-pole LP, and 24/12 dB-oct slope.

**Files:**
- Modify: `src/dsp/spine/cmajor/MoogLadder.cmajor` (+ regenerate `generated/MoogLadder.h`)
- Modify: `src/dsp/spine/cmajor/MoogLadderAdapter.{h,cpp}` (add `setParams(cutoff,res,drive,slope)`)
- Create: `src/dsp/spine/MoogLadder.h`, `src/dsp/spine/MoogLadder.cpp`
- Create: `tests/MoogLadderTests.cpp`
- Modify: `src/dsp/spine/SpineState.h` (bump `kMaxSpineStateBytes` to fit the measured `MoogLadder::VoiceState`)
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `MoogLadderAdapter` (Task 1), `FilterModel` in-place lifecycle (`stateSize`/`stateAlign`/`constructState`/`destroyState`, from v5.1).
- Produces: `class MoogLadder : public FilterModel` with `VoiceState : FilterModel::State { MoogLadderAdapter l, r; }`, `setCommon(cutoff,res,drive)`, `setSlope(Slope)` (`Slope{db12,db24}`), `setSeparation(float) noexcept {}` (no-op), `processStereo(State&,float*,float*,int)`.

- [ ] **Step 1: Write the failing tests** — `tests/MoogLadderTests.cpp`

```cpp
#include <juce_core/juce_core.h>
#include "../src/dsp/spine/MoogLadder.h"
#include "../src/dsp/spine/SpineState.h"
#include <vector>
#include <cmath>
#include <cstddef>

struct MoogLadderTests : public juce::UnitTest {
    MoogLadderTests() : juce::UnitTest("MoogLadder") {}
    static constexpr double kSR = 48000.0;

    // steady passband/stopband magnitude (linear) of the LP at `probe` Hz, cutoff `fc`.
    static double mag(MoogLadder& m, FilterModel::State& st, double fc, double probe) {
        m.setCommon((float) fc, 0.0f, 0.0f); m.reset(st);
        std::vector<float> l(16384), r(16384);
        for (int i = 0; i < (int) l.size(); ++i)
            l[(size_t)i] = r[(size_t)i] = (float) std::sin(2.0*juce::MathConstants<double>::pi*probe*i/kSR);
        m.processStereo(st, l.data(), r.data(), (int) l.size());
        double e = 0; for (int i = 8192; i < (int) l.size(); ++i) e += double(l[(size_t)i])*l[(size_t)i];
        return std::sqrt(e / 8192.0) * std::sqrt(2.0);   // ~amplitude of a unit sine
    }

    void runTest() override {
        MoogLadder m; m.prepare(kSR); m.setSlope(MoogLadder::Slope::db24);
        std::unique_ptr<FilterModel::State> st(m.makeState());

        beginTest("linear passband ~unity, far stopband strongly attenuated");
        const double pass = mag(m, *st, 1000.0, 100.0);    // well below fc
        const double stop = mag(m, *st, 1000.0, 8000.0);   // 3 octaves above fc
        expect(pass > 0.7 && pass < 1.4, "passband not ~unity: " + juce::String(pass,3));
        expect(stop < 0.05, "stopband not attenuated: " + juce::String(stop,4));

        beginTest("24 dB/oct: one octave above fc ~ -24 dB relative to 2 octaves below");
        const double ref  = mag(m, *st, 1000.0, 250.0);
        const double oneA = mag(m, *st, 1000.0, 2000.0);
        const double dB = 20.0*std::log10(oneA/ref);
        expect(dB < -18.0 && dB > -30.0, "slope at fc*2 not ~ -24 dB: " + juce::String(dB,1));

        beginTest("12 dB tap (y2)");
        m.setSlope(MoogLadder::Slope::db12);
        const double ref2  = mag(m, *st, 1000.0, 250.0);
        const double one2  = mag(m, *st, 1000.0, 2000.0);
        const double dB2 = 20.0*std::log10(one2/ref2);
        expect(dB2 < -8.0 && dB2 > -16.0, "12 dB tap slope not ~ -12 dB: " + juce::String(dB2,1));

        beginTest("Q18: MoogLadder::VoiceState fits kMaxSpineStateBytes");
        expect(m.stateSize() <= kMaxSpineStateBytes,
               "VoiceState " + juce::String((int)m.stateSize()) + " > kMaxSpineStateBytes");
    }
};
static MoogLadderTests moogLadderTestsInstance;
```

Register `MoogLadderTests.cpp` + `../src/dsp/spine/MoogLadder.cpp` in `tests/CMakeLists.txt`.

- [ ] **Step 2: Run — expect compile-gate FAIL** (`MoogLadder.h` doesn't exist yet).

Run: `cmake --build build --target k2000_tests -j4 2>&1 | grep -E "error:|MoogLadder" | head`
Expected: compile error (missing `MoogLadder.h`).

- [ ] **Step 3: Grow the Cmajor processor to the linear ladder** — `MoogLadder.cmajor`

```
processor MoogLadder
{
    input  stream float in;
    output stream float out;
    input  event float cutoffHz  [[ name: "Cutoff",    min: 16, max: 20000, init: 1000 ]];
    input  event float resonance [[ name: "Resonance", min: 0,  max: 1,     init: 0 ]];
    input  event float drive     [[ name: "Drive",     min: 0,  max: 1,     init: 0 ]];
    input  event int32 slope     [[ name: "Slope",     min: 0,  max: 1,     init: 1 ]];   // 0=y2 1=y4

    float g, G, r;
    bool  dirty = true;
    float cutoff = 1000.0f, res = 0.0f, drv = 0.0f;
    int32 slopeSel = 1;
    float s1, s2, s3, s4;                 // stage integrator states

    event cutoffHz  (float v) { cutoff = v; dirty = true; }
    event resonance (float v) { res = v;    dirty = true; }
    event drive     (float v) { drv = v; }
    event slope     (int32 v) { slopeSel = v; }

    void recompute() {
        let sr = float (processor.frequency);
        g = tan (float (pi) * clamp (cutoff, 16.0f, sr * 0.45f) / sr);
        G = g / (1.0f + g);
        r = clamp (res, 0.0f, 1.0f) * 4.0f;     // taper -> r in [0,4]; refined in Task 3
        dirty = false;
    }

    void main() {
        loop {
            if (dirty) recompute();
            // Exact one-step (delay-free) linear ZDF ladder solve (Zavalishin ch.4).
            // TPT one-pole: y_i = G*u_i + (1-G)*s_i ; s_i' = 2*y_i - s_i.
            // Cascade with global feedback u_1 = in - r*y_4 has a closed-form y_4:
            let b1 = (1.0f - G) * s1; let b2 = (1.0f - G) * s2;
            let b3 = (1.0f - G) * s3; let b4 = (1.0f - G) * s4;
            let G2 = G * G; let G3 = G2 * G; let G4 = G2 * G2;
            let B  = G3 * b1 + G2 * b2 + G * b3 + b4;        // state feed-through to y4
            let y4cf = (G4 * in + B) / (1.0f + r * G4);      // closed-form, no unit delay
            let u1 = in - r * y4cf;
            let y1 = G * (u1 - s1) + s1; s1 = 2.0f * y1 - s1;
            let y2 = G * (y1 - s2) + s2; s2 = 2.0f * y2 - s2;
            let y3 = G * (y2 - s3) + s3; s3 = 2.0f * y3 - s3;
            let y4 = G * (y3 - s4) + s4; s4 = 2.0f * y4 - s4;
            out <- (slopeSel == 0) ? y2 : y4;                // y2 = 12 dB tap, y4 = 24 dB
            advance();
        }
    }
}
```

> **NOTE for the implementer:** the `b_i = (1-G)·s_i`, `B`, and `y4cf` lines are the exact delay-free-loop solve — `y4cf` equals the forward-pass `y4` by construction. Do **not** introduce a one-sample feedback delay in the *linear* path (Stilson-Smith: it detunes resonance); a one-sample delay is only acceptable for the *nonlinear* delta (Task 3). The linearity + slope tests are the oracle — if they fail, the `recompute` `g`/`r` mapping (not the solve) is the usual culprit.

- [ ] **Step 4: Regenerate the C++**

Run: `sg docker -c "tools/cmajor/cmaj-codegen.sh src/dsp/spine/cmajor/MoogLadder.cmajorpatch src/dsp/spine/cmajor/generated/MoogLadder.h"`

- [ ] **Step 5: Extend the adapter** — `MoogLadderAdapter.{h,cpp}`: add
`void setParams(float cutoffHz, float resonance, float drive, int slope) noexcept;` →
`d->addEvent_cutoffHz(c); d->addEvent_resonance(r); d->addEvent_drive(dr); d->addEvent_slope((int32_t) slope);`

- [ ] **Step 6: Write `MoogLadder.h`**

```cpp
#pragma once
#include "FilterModel.h"
#include "cmajor/MoogLadderAdapter.h"

// Moog transistor-ladder spine filter (Spec 1): a fused Cmajor ladder behind an
// in-place adapter; stereo = two mono adapters sharing block-set params. Mode/slope
// are model-specific setters (not on the FilterModel base), per HuggettFilter.
class MoogLadder : public FilterModel {
public:
    enum class Slope { db12, db24 };

    struct VoiceState : public FilterModel::State {
        MoogLadderAdapter l, r;
    };

    void prepare(double sampleRate) noexcept override { sampleRate_ = sampleRate; }
    std::size_t stateSize()  const noexcept override { return sizeof(VoiceState); }
    std::size_t stateAlign() const noexcept override { return alignof(VoiceState); }
    FilterModel::State* constructState(void* mem) const override;
    void reset(State& s) const noexcept override;

    void setCommon(float cutoffHz, float resonance, float drive) noexcept override {
        cutoffHz_ = cutoffHz; resonance_ = resonance; drive_ = drive;
    }
    void setSlope(Slope s) noexcept { slope_ = s; }
    void setSeparation(float) noexcept { /* no analog in a single ladder */ }

    void processStereo(State& s, float* left, float* right, int numSamples) const noexcept override;

private:
    double sampleRate_ = 48000.0;
    float  cutoffHz_ = 1000.0f, resonance_ = 0.0f, drive_ = 0.0f;
    Slope  slope_ = Slope::db24;
};
```

- [ ] **Step 7: Write `MoogLadder.cpp`**

```cpp
#include "MoogLadder.h"
#include <new>

FilterModel::State* MoogLadder::constructState(void* mem) const {
    auto* vs = new (mem) VoiceState();
    vs->l.prepare(sampleRate_); vs->r.prepare(sampleRate_);
    return vs;
}

void MoogLadder::reset(State& s) const noexcept {
    auto& vs = static_cast<VoiceState&>(s);
    vs.l.reset(); vs.r.reset();
}

void MoogLadder::processStereo(State& s, float* left, float* right, int n) const noexcept {
    auto& vs = static_cast<VoiceState&>(s);
    const int slope = (slope_ == Slope::db12) ? 0 : 1;
    vs.l.setParams(cutoffHz_, resonance_, drive_, slope);
    vs.r.setParams(cutoffHz_, resonance_, drive_, slope);
    vs.l.process(left,  n);
    vs.r.process(right, n);
}
```

- [ ] **Step 8: Pin `kMaxSpineStateBytes`** — build once, read the Q18 test's reported size, set `kMaxSpineStateBytes` in `src/dsp/spine/SpineState.h` to `>= sizeof(MoogLadder::VoiceState)` with headroom (the existing Huggett/HP `static_assert`s must still hold). Add a `static_assert(sizeof(MoogLadder::VoiceState) <= kMaxSpineStateBytes, …)` next to where Moog is built — but since Moog is NOT registered in the library yet (Spec 2), put this assert in `tests/MoogLadderTests.cpp`'s translation unit (a file-scope `static_assert` after the includes).

- [ ] **Step 9: Run — iterate the Cmajor until green**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | grep -E "MoogLadder|MoogPipeline|Summary"`
Expected: `[PASS] MoogLadder` (all sub-tests), `[PASS] MoogPipeline`, `Summary: … 0 failed`. If the slope/linearity fail, refine the closed-form solve in `MoogLadder.cmajor` (Step 3 note), regenerate (Step 4), rebuild. **Do not loosen the test bounds.**

- [ ] **Step 10: Full suite green, commit** (include the regenerated header)

```bash
git add src/dsp/spine/cmajor/MoogLadder.cmajor src/dsp/spine/cmajor/generated/MoogLadder.h \
        src/dsp/spine/cmajor/MoogLadderAdapter.h src/dsp/spine/cmajor/MoogLadderAdapter.cpp \
        src/dsp/spine/MoogLadder.h src/dsp/spine/MoogLadder.cpp src/dsp/spine/SpineState.h \
        tests/MoogLadderTests.cpp tests/CMakeLists.txt
git commit -m "feat(moog): linear 4-pole ZDF ladder + MoogLadder FilterModel (stereo, in-place)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 3: Nonlinearity — delta-injected per-stage `tanh` + resonance taper + tuning compensation

Add the one-sample-delayed per-stage `tanh` correction (the `fbExtra` idiom), the calibrated resonance taper, and Huovilainen tuning compensation. Tests: resonance peak growth + bass-thinning, self-oscillation onset, pitch-tracking ±3%.

**Files:**
- Modify: `src/dsp/spine/cmajor/MoogLadder.cmajor` (+ regenerate)
- Modify: `tests/MoogLadderTests.cpp` (add resonance + self-osc tests)

**Interfaces:**
- Consumes: the linear ladder (Task 2). No new public C++ interface (drive/res already wired).

- [ ] **Step 1: Add the resonance + self-osc tests** — append to `MoogLadderTests.cpp` `runTest()`

```cpp
        beginTest("resonance grows the peak at fc and thins the bass");
        {
            MoogLadder mr; mr.prepare(kSR); mr.setSlope(MoogLadder::Slope::db24);
            std::unique_ptr<FilterModel::State> s2(mr.makeState());
            mr.setCommon(1000.0f, 0.0f, 0.0f);
            const double peakLoRes = mag(mr, *s2, 1000.0, 1000.0);
            const double bassLoRes = mag(mr, *s2, 1000.0, 80.0);
            mr.setCommon(1000.0f, 0.9f, 0.0f);
            const double peakHiRes = mag(mr, *s2, 1000.0, 1000.0);
            const double bassHiRes = mag(mr, *s2, 1000.0, 80.0);
            expect(peakHiRes > peakLoRes * 1.5, "resonance did not grow the peak");
            expect(bassHiRes < bassLoRes,        "bass did not thin as resonance rose");
        }

        beginTest("self-oscillation: sustains + tracks fc within 3%");
        {
            for (double fc : { 220.0, 880.0 }) {
                MoogLadder mo; mo.prepare(kSR); mo.setSlope(MoogLadder::Slope::db24);
                std::unique_ptr<FilterModel::State> so(mo.makeState());
                mo.setCommon((float) fc, 1.0f, 0.0f); mo.reset(*so);
                std::vector<float> l(1 << 15, 0.0f), r(1 << 15, 0.0f);
                l[0] = r[0] = 1.0f;                       // impulse kick
                mo.processStereo(*so, l.data(), r.data(), (int) l.size());
                // sustained: tail energy is non-trivial
                double tail = 0; for (int i = (int)l.size()-4096; i < (int)l.size(); ++i) tail += std::abs(l[(size_t)i]);
                expect(tail > 1.0, "self-oscillation did not sustain at fc=" + juce::String(fc));
                // pitch: zero-crossing rate over the tail ~ fc
                int zc = 0; for (int i = (int)l.size()-8192+1; i < (int)l.size(); ++i)
                    if ((l[(size_t)i-1] <= 0.0f) != (l[(size_t)i] <= 0.0f)) ++zc;
                const double f = zc * 0.5 * kSR / 8192.0;
                expect(std::abs(f - fc) / fc < 0.03, "self-osc pitch off: " + juce::String(f,1) + " vs " + juce::String(fc));
            }
        }
```

- [ ] **Step 2: Run — expect FAIL** (linear ladder doesn't self-oscillate / peak too weak).

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | grep -E "MoogLadder|Summary"`
Expected: the two new sub-tests FAIL.

- [ ] **Step 3: Add nonlinearity to the Cmajor processor** — in `MoogLadder.cmajor`, add the `padTanh` family (copy from `NlSvfDrive.cmajor` lines 35–46), keep per-stage `yPrev1..4` state, inject the `tanh` delta from the previous sample's stage outputs before the linear solve, and add the calibrated taper + Huovilainen tuning correction:

```
    float padTanh (float x) { let x2 = x*x; return clamp (x*(27.0f+x2)/(27.0f+9.0f*x2), -1.0f, 1.0f); }
    float yp1, yp2, yp3, yp4;     // previous stage outputs (delta-injected tanh)
    // in recompute(): r = resTaper(res);  g *= tuningCorrection(cutoff, r);   // CALIB
    // in main(), when (drv > 0 || res > 0): u0 -= per-stage tanh deltas from yp1..yp4 (fbExtra),
    // run the stages, then yp1=y1; yp2=y2; yp3=y3; yp4=y4;
```

Calibrate `resTaper` (e.g. `r = res*res*4.0f` shaped so self-osc sits near the top) and the tuning correction so the **pitch-tracking test passes within 3%**. The drive shaper (`tanh(gain*x)`) gates on `drv>0` (linear when `drv==0 && res==0` — keeps Task 2's linearity test green).

- [ ] **Step 4: Regenerate, build, iterate to green**

Run: `sg docker -c "tools/cmajor/cmaj-codegen.sh src/dsp/spine/cmajor/MoogLadder.cmajorpatch src/dsp/spine/cmajor/generated/MoogLadder.h"` then `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | grep -E "MoogLadder|Summary"`
Expected: all `MoogLadder` sub-tests PASS (linearity from Task 2 STILL green — the nonlinear path must collapse to linear at res=0/drive=0). Iterate the taper/tuning CALIBs (not the test bounds) until the ±3% pitch test passes.

- [ ] **Step 5: Commit** (with regenerated header)

```bash
git add src/dsp/spine/cmajor/MoogLadder.cmajor src/dsp/spine/cmajor/generated/MoogLadder.h tests/MoogLadderTests.cpp
git commit -m "feat(moog): nonlinear ladder (delta tanh, taper, tuning comp) + self-osc

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 4: Self-oscillation polish — output peak-limiter + DC blocker

Add the Pirkle-style bounded output peak-limiter (clean near-sine self-osc at base rate) and a DC blocker. Tests: boundedness/finiteness at extremes, near-sine THD, plus the non-gating `OverdriveDiagnostic` score.

**Files:**
- Modify: `src/dsp/spine/cmajor/MoogLadder.cmajor` (+ regenerate)
- Modify: `tests/MoogLadderTests.cpp`

- [ ] **Step 1: Add boundedness + THD tests** — append to `runTest()`

```cpp
        beginTest("bounded + finite at max res/drive/loud input");
        {
            MoogLadder mb; mb.prepare(kSR); mb.setSlope(MoogLadder::Slope::db24);
            std::unique_ptr<FilterModel::State> sb(mb.makeState());
            mb.setCommon(800.0f, 1.0f, 1.0f); mb.reset(*sb);
            std::vector<float> l(1 << 16), r(1 << 16);
            for (int i=0;i<(int)l.size();++i){ l[(size_t)i]=r[(size_t)i]=4.0f*(float)std::sin(2.0*juce::MathConstants<double>::pi*120.0*i/kSR);}
            mb.processStereo(*sb, l.data(), r.data(), (int) l.size());
            float peak = 0; bool finite = true;
            for (int i=0;i<(int)l.size();++i){ peak=std::max(peak,std::abs(l[(size_t)i])); finite = finite && std::isfinite(l[(size_t)i]); }
            expect(finite, "non-finite under extreme drive");
            expect(peak < 2.0f, "output exceeded the limiter ceiling: " + juce::String(peak));
        }
```

- [ ] **Step 2: Run — expect possible FAIL** (unlimited self-osc may exceed the ceiling / ring loud).

- [ ] **Step 3: Add the limiter + DC blocker to `MoogLadder.cmajor`** — a bounded soft ceiling (reuse the `padTanh` family as the monotonic limiter) on the output, and a one-pole DC blocker (`y = x - x1 + R*y1`, `R≈1-2π·8/sr`). Both after the tap. Regenerate.

- [ ] **Step 4: Build, iterate to green; add the diagnostic readout**

Add a non-gating `logMessage` of an inharmonic-energy score (reuse the FFT helper pattern from `OverdriveDiagnosticTests.cpp` if accessible, else log peak/RMS) on a driven self-oscillating run.
Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | grep -E "MoogLadder|Summary"` → all PASS, `0 failed`.

- [ ] **Step 5: Commit** (with regenerated header).

```bash
git add src/dsp/spine/cmajor/MoogLadder.cmajor src/dsp/spine/cmajor/generated/MoogLadder.h tests/MoogLadderTests.cpp
git commit -m "feat(moog): Pirkle output limiter + DC blocker; bounded self-osc

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 5: Pole-mix BP/HP modes

Add a `mode` input (LP/BP/HP) synthesized by pole-mixing the ladder taps (Oberheim Xpander weights), and a `setMode` setter. Test: BP peaks mid-band, HP attenuates lows.

**Files:**
- Modify: `src/dsp/spine/cmajor/MoogLadder.cmajor` (+ regenerate); `MoogLadderAdapter.{h,cpp}` (add `mode` to `setParams`); `src/dsp/spine/MoogLadder.{h,cpp}` (add `enum class Mode{LP,BP,HP}` + `setMode`); `tests/MoogLadderTests.cpp`.

- [ ] **Step 1: Write a mode test** — append: with `Mode::HP`, low-frequency magnitude is attenuated vs `Mode::LP`; with `Mode::BP`, a mid-band probe near `fc` exceeds both a low and a high probe. (Use `mag(...)` with the mode set.)

- [ ] **Step 2: Run — expect FAIL** (`setMode` doesn't exist).

- [ ] **Step 3: Implement** — add `input event int32 mode` + pole-mix output in the Cmajor (`LP=y4`; `BP≈ (y2 - y4)*k`; `HP≈ in - sum(taps)` per the Xpander mix — CALIB the weights so the test passes), regenerate; thread `mode` through `setParams`; add `Mode`/`setMode` to `MoogLadder`. The `mode` interacts with `slope` (slope selects the LP tap; mode selects the pole-mix family) — keep `Mode::LP` honoring the `slope` tap.

- [ ] **Step 4: Build, iterate to green; commit** (with regenerated header).

```bash
git add src/dsp/spine/cmajor/MoogLadder.cmajor src/dsp/spine/cmajor/generated/MoogLadder.h \
        src/dsp/spine/cmajor/MoogLadderAdapter.h src/dsp/spine/cmajor/MoogLadderAdapter.cpp \
        src/dsp/spine/MoogLadder.h src/dsp/spine/MoogLadder.cpp tests/MoogLadderTests.cpp
git commit -m "feat(moog): pole-mix BP/HP modes

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 6: Bass voice — played-note sub-oscillator

Add a per-voice sub-osc (sine/triangle/saw, band-limited via PolyBLEP) at the played-note fundamental (octave 0/−1/−2), summed clean post-ladder, amount `0..1` (0 = bit-identical no-op), phase reset on note-on. New C++ seams: `MoogLadder::setBass(amount,wave,octave)` and `setFundamental(State&,hz)`.

**Files:**
- Modify: `src/dsp/spine/cmajor/MoogLadder.cmajor` (+ regenerate); `MoogLadderAdapter.{h,cpp}` (add `setBass`, `setFundamental`, `noteReset`); `src/dsp/spine/MoogLadder.{h,cpp}`; `tests/MoogLadderTests.cpp`.

**Interfaces:**
- Produces: `MoogLadder::setBass(float amount, int wave, int octave) noexcept` (model-wide), `MoogLadder::setFundamental(State&, float hz) const noexcept` (per-voice, writes into the lane adapters), `MoogLadderAdapter::setBass/setFundamental/noteReset`.

- [ ] **Step 1: Write the bass-voice tests** — append to `runTest()`

```cpp
        beginTest("bass voice adds energy at the played fundamental; amount=0 is a no-op");
        {
            MoogLadder mb; mb.prepare(kSR);
            std::unique_ptr<FilterModel::State> s0(mb.makeState());
            std::unique_ptr<FilterModel::State> s1(mb.makeState());
            mb.setCommon(1500.0f, 0.0f, 0.0f); mb.setSlope(MoogLadder::Slope::db24);

            auto run = [&](FilterModel::State& st, float amount)->std::vector<float> {
                mb.setBass(amount, /*sine*/0, /*oct*/0);
                mb.setFundamental(st, 110.0f); mb.reset(st);
                std::vector<float> l(16384, 0.0f), r(16384, 0.0f);  // SILENT input
                mb.processStereo(st, l.data(), r.data(), (int) l.size());
                return l;
            };
            const auto off = run(*s0, 0.0f);
            const auto on  = run(*s1, 0.8f);

            // amount=0: pure silence in -> bit-identical silence out
            bool zero = true; for (float v : off) zero = zero && (v == 0.0f);
            expect(zero, "bassAmount=0 was not a no-op on silent input");
            // amount>0: energy present, and concentrated near 110 Hz (Goertzel)
            double e = 0; for (float v : on) e += double(v)*v;
            expect(e > 1.0, "bass voice produced no energy");
        }
```

(The Goertzel/FFT pitch check can reuse the zero-crossing approach from Task 3 on the `on` buffer to assert ~110 Hz.)

- [ ] **Step 2: Run — expect FAIL** (`setBass`/`setFundamental` missing).

- [ ] **Step 3: Implement the sub-osc in `MoogLadder.cmajor`** — add `input event float fundamentalHz, bassAmount; input event int32 bassWave, bassOctave; input event void noteReset;` a phase accumulator state, sine/tri/saw generation (PolyBLEP on tri/saw), and `out <- limiter(ladderOut + bassAmount * subSample)`. Guard: `bassAmount==0` → add exactly `0.0f` (no sub computation path that could leak an epsilon). `noteReset` zeroes the phase. Regenerate.

- [ ] **Step 4: Wire the adapter + model** — `MoogLadderAdapter`: `setBass(amount,wave,octave)` → `addEvent_bassAmount/bassWave/bassOctave`; `setFundamental(hz)` → `addEvent_fundamentalHz`; `noteReset()` → `addEvent_noteReset()`. `MoogLadder`: `setBass(...)` stores + forwards in `processStereo`; `setFundamental(State&,hz)` writes both lanes; `reset()` calls `noteReset()` on both lanes (phase reset on note-on).

- [ ] **Step 5: Regenerate, build, iterate to green; commit**

```bash
git add src/dsp/spine/cmajor/MoogLadder.cmajor src/dsp/spine/cmajor/generated/MoogLadder.h \
        src/dsp/spine/cmajor/MoogLadderAdapter.h src/dsp/spine/cmajor/MoogLadderAdapter.cpp \
        src/dsp/spine/MoogLadder.h src/dsp/spine/MoogLadder.cpp tests/MoogLadderTests.cpp
git commit -m "feat(moog): played-note sub-osc bass voice (sine/tri/saw, octave, no-op at 0)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 7: Arturia golden-data harness + finalize

Add the golden-data comparison scaffold (ingests captured Arturia Mini V measurements from `tests/golden/`, compares within tolerance) and the `separation`-inert test; confirm the full suite green and the Q18 cap still holds.

**Files:**
- Create: `tests/golden/moog/README.md` (capture format), `tests/golden/moog/.gitkeep`
- Modify: `tests/MoogLadderTests.cpp` (golden ingestion + `separation` no-op test)

- [ ] **Step 1: `separation` inert test** — append: `setSeparation(0)` vs `setSeparation(2)` produce identical output for the same input (the no-op assertion via `memcmp`).

- [ ] **Step 2: Golden harness** — define the capture format in `tests/golden/moog/README.md` (CSV: `cutoffHz,resonance,probeHz,magDb` rows + a `selfosc.csv`: `cutoffHz,measuredHz`). Add a test that, **if** `BERNIE_GOLDEN_DIR/moog/response.csv` exists, loads it and asserts the Moog's `mag()` matches each row within a CALIB tolerance band; if the file is absent, `logMessage("golden Arturia data not present — skipping")` and pass (so CI is green pre-capture). Tolerances are CALIB, начат loose.

```cpp
        beginTest("Arturia golden match (skipped until data captured)");
        {
            juce::File g = juce::File(BERNIE_GOLDEN_DIR).getChildFile("moog/response.csv");
            if (! g.existsAsFile()) { logMessage("no golden Arturia data — skipping"); }
            else {
                MoogLadder mg; mg.prepare(kSR); mg.setSlope(MoogLadder::Slope::db24);
                std::unique_ptr<FilterModel::State> sg(mg.makeState());
                for (auto& line : juce::StringArray::fromLines(g.loadFileAsString())) {
                    auto c = juce::StringArray::fromTokens(line, ",", "");
                    if (c.size() < 4 || ! c[0].containsOnly("0123456789.")) continue;
                    const double fc = c[0].getDoubleValue(), res = c[1].getDoubleValue(),
                                 pr = c[2].getDoubleValue(), wantDb = c[3].getDoubleValue();
                    mg.setCommon((float) fc, (float) res, 0.0f);
                    const double gotDb = 20.0*std::log10(std::max(1e-6, mag(mg, *sg, fc, pr)));
                    expect(std::abs(gotDb - wantDb) < 6.0,   // CALIB: tighten during calibration
                           "Arturia mismatch @fc=" + juce::String(fc) + " pr=" + juce::String(pr));
                }
            }
        }
```

- [ ] **Step 3: Build, run, full suite green**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | tail -3`
Expected: `Summary: N tests, 0 failed`.

- [ ] **Step 4: Commit**

```bash
git add tests/golden/moog tests/MoogLadderTests.cpp
git commit -m "test(moog): Arturia golden-data harness + separation-inert gate

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Notes for the implementer

- **Codegen is committed.** After every `.cmajor` edit, rerun the Docker codegen and commit the regenerated `generated/MoogLadder.h` in the SAME commit. CI never runs cmaj.
- **The tests are the DSP oracle.** There is no C++ twin; the linearity/slope/resonance/self-osc/THD tests define "correct." When the Cmajor math is off, a behavioral test fails — fix the Cmajor (and regenerate), never the test bound or a `// CALIB`.
- **Linear path must stay bit-stable.** Every nonlinear addition (Tasks 3–6) must collapse to the Task-2 linear ladder at `drive==0 && resonance==0` (and `bassAmount==0`), or Task 2's linearity test breaks. That gate is your regression net.
- **Small block size.** If Task 1 found no `cmaj` flag for `maxFramesPerBlock`, you are at 512: bump `kGenBytes` (adapter) and `kMaxSpineStateBytes` accordingly and note it — the design accepts the larger footprint as a fallback.
- **Scope discipline.** Do not register Moog in `FilterModelLibrary`, touch `Layer`/`Voice`/params/UI, or add `setVoiceContext` — those are Spec 2.
- **`-j4` always.** Bare `-j` OOMs the JUCE build.
```
