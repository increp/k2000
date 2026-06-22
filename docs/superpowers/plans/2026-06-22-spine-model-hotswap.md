# Spine Model Hot-Swap (v5.1) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Switch the spine `FilterModel` on a sounding voice without a click, via a per-voice equal-power crossfade, with all per-voice filter state moved off the heap into in-place storage.

**Architecture:** `FilterModel` gains an in-place state lifecycle (`stateSize`/`constructState`/`destroyState`); `makeState()` survives as a non-virtual heap helper so existing call sites are untouched. `Layer` pre-builds one instance of every registered model (alloc-free switching). `SpineFilterSlot` holds two fixed in-place state buffers and runs an equal-power crossfade between the old and new model, coalescing rapid changes to one in-flight fade. The fade time is a new global automatable parameter.

**Tech Stack:** C++17, JUCE 8.0.4, CMake, JUCE `UnitTest` harness (single `k2000_tests` binary).

## Global Constraints

- **Build:** `cmake --build build --target k2000_tests -j4` — **always `-j4`** (bare `-j` OOMs the JUCE compile → 0-byte object → confusing link failure).
- **Run tests:** `./build/tests/k2000_tests` — no per-name CLI filter; grep its stdout. Each test prints `[PASS]/[FAIL] <Name>: P passes, F failures` and ends with `Summary: N tests, M failed`. Green = `Summary: … 0 failed`.
- **RT-safety invariant:** `FilterModel::constructState`/`destroyState` MUST be heap-free and bounded — they run on the audio thread during a live switch. Only `makeState()` (the heap helper) may allocate, and only off the audio thread.
- **Non-ASCII text** at the JUCE boundary goes through `util::u8(...)` — never `juce::String(const char*)` on UTF-8.
- **No `// CALIB` constant may be changed to make a test pass** — widen the test instead.
- **TDD throughout:** failing test → minimal code → green → commit. Frequent commits.
- **Branch:** work on `feat/spine-model-hotswap` (already created).

---

### Task 1: In-place state lifecycle on `FilterModel` (+ models), `makeState` retained

Add the in-place lifecycle to the `FilterModel` interface and all three models, plus the storage-size constants and Q18 governance asserts. `makeState()` becomes a non-virtual base helper that placement-constructs into `::operator new` memory, so all 28 existing `makeState()` call sites keep compiling and passing. Pure refactor — no behavior change.

**Files:**
- Create: `src/dsp/spine/SpineState.h`
- Create: `tests/InPlaceStateTests.cpp`
- Modify: `src/dsp/spine/FilterModel.h`
- Modify: `src/dsp/spine/HuggettFilter.h`, `src/dsp/spine/HuggettFilter.cpp`
- Modify: `src/dsp/spine/cmajor/CmajorSvfFilter.h`, `src/dsp/spine/cmajor/CmajorSvfFilter.cpp`
- Modify: `src/dsp/spine/HuggettHpStage.h`, `src/dsp/spine/HuggettHpStage.cpp`
- Modify: `src/dsp/spine/FilterModelLibrary.cpp` (Q18 static_assert)
- Modify: `tests/CMakeLists.txt` (register `InPlaceStateTests.cpp`)

**Interfaces:**
- Produces (`FilterModel`): `std::size_t stateSize() const noexcept`, `std::size_t stateAlign() const noexcept`, `State* constructState(void* mem) const`, `void destroyState(State* s) const noexcept`, and non-virtual `State* makeState() const`.
- Produces (`HuggettHpStage`): `std::size_t stateSize() const noexcept`, `State* constructState(void* mem) const`, plus its existing `State* makeState() const`.
- Produces (`SpineState.h`): `kMaxSpineStateBytes`, `kSpineStateAlign`, `kSpineHpStateBytes`, `kDefaultModelFadeMs`, `kMinModelFadeMs`, `kMaxModelFadeMs`.

- [ ] **Step 1: Write `SpineState.h`**

```cpp
#pragma once
#include <cstddef>
#include <cstdint>

// Per-voice spine state storage budget + crossfade constants (register Q17/Q18).
// kMaxSpineStateBytes is GOVERNED: a per-model static_assert (see FilterModelLibrary.cpp
// and the test target) fails the build if a model's State exceeds it. On overflow a
// reviewer bumps this (cost: 2 * delta * voices of RAM) or slims/rejects the model.
// Measured: HuggettFilter::VoiceState ~= 176 B (vptr + 2*NlSvfCell(72) + DcBlocker(20)).
// 512 B gives headroom for the test SVF model and the anticipated Moog 4-pole (v5.2).
inline constexpr std::size_t kMaxSpineStateBytes = 512;
inline constexpr std::size_t kSpineStateAlign    = alignof(std::max_align_t);
inline constexpr std::size_t kSpineHpStateBytes  = 256;   // HuggettHpStage::State ~= 168 B

inline constexpr float kDefaultModelFadeMs = 25.0f;  // CALIB (default for spine.modelFadeMs)
inline constexpr float kMinModelFadeMs     = 2.0f;   // floor keeps every switch click-free
inline constexpr float kMaxModelFadeMs     = 100.0f;
```

- [ ] **Step 2: Rewrite the `FilterModel` interface** — `src/dsp/spine/FilterModel.h`

```cpp
#pragma once
#include <cstddef>
#include <new>

// Abstract per-voice, stereo, heap-free spine filter. One instance configures
// shared params (on the Layer); per-voice integrator state lives in State.
// Concrete models define their own State subtype.
//
// State lives in CALLER-PROVIDED memory via constructState() (placement-new) so a
// live model switch never allocates on the audio thread. makeState() is a non-RT
// convenience that heap-allocates + constructs (used by tests and prepare-time code).
class FilterModel {
public:
    struct State { virtual ~State() = default; };

    virtual ~FilterModel() = default;
    virtual void prepare(double sampleRate) noexcept = 0;   // cheap; no heap

    // In-place lifecycle. INVARIANT: heap-free + RT-safe (audio-thread callable).
    // constructState placement-news into mem (>= stateSize() bytes, >= stateAlign()).
    virtual std::size_t stateSize()  const noexcept = 0;
    virtual std::size_t stateAlign() const noexcept = 0;
    virtual State* constructState(void* mem) const = 0;
    virtual void   destroyState(State* s) const noexcept { if (s) s->~State(); }

    // Heap convenience (NON-RT): allocate + construct. Wrappable in unique_ptr<State>;
    // unique_ptr's delete runs ~State() then ::operator delete, matching this new.
    State* makeState() const { return constructState(::operator new(stateSize())); }

    virtual void reset(State& s) const noexcept = 0;
    virtual void setCommon(float cutoffHz, float resonance, float drive) noexcept = 0;
    virtual void processStereo(State& s, float* left, float* right, int numSamples) const noexcept = 0;
};
```

- [ ] **Step 3: Update `HuggettFilter`** — replace the `makeState` override with the in-place trio.

In `src/dsp/spine/HuggettFilter.h`, change the three declarations under `prepare(...)`:

```cpp
    void prepare(double sampleRate) noexcept override { sampleRate_ = sampleRate; }
    std::size_t stateSize()  const noexcept override { return sizeof(VoiceState); }
    std::size_t stateAlign() const noexcept override { return alignof(VoiceState); }
    FilterModel::State* constructState(void* mem) const override;
    void reset(State& s) const noexcept override;
```

In `src/dsp/spine/HuggettFilter.cpp`, replace `makeState()` with:

```cpp
FilterModel::State* HuggettFilter::constructState(void* mem) const {
    auto* vs = new (mem) VoiceState();
    vs->a.prepare(sampleRate_);
    vs->b.prepare(sampleRate_);
    vs->dc.prepare(sampleRate_);
    return vs;
}
```

- [ ] **Step 4: Update `CmajorSvfFilter`** (test-only) the same way.

In `src/dsp/spine/cmajor/CmajorSvfFilter.h`, replace `State* makeState() const override;` with:

```cpp
    std::size_t stateSize()  const noexcept override { return sizeof(VoiceState); }
    std::size_t stateAlign() const noexcept override { return alignof(VoiceState); }
    FilterModel::State* constructState(void* mem) const override;
```

In `src/dsp/spine/cmajor/CmajorSvfFilter.cpp`, replace `makeState()` with:

```cpp
FilterModel::State* CmajorSvfFilter::constructState(void* mem) const {
    auto* vs = new (mem) VoiceState();
    vs->l.prepare(sampleRate_);
    vs->r.prepare(sampleRate_);
    return vs;
}
```

- [ ] **Step 5: Add in-place lifecycle to `HuggettHpStage`** (keep `makeState`).

In `src/dsp/spine/HuggettHpStage.h`, after `makeState()`:

```cpp
    State* makeState() const;                 // heap convenience (prepare-time / tests)
    std::size_t stateSize() const noexcept { return sizeof(State); }
    State* constructState(void* mem) const;   // placement-new; RT-safe
```

In `src/dsp/spine/HuggettHpStage.cpp`, rewrite `makeState` in terms of `constructState` and add it:

```cpp
HuggettHpStage::State* HuggettHpStage::makeState() const {
    return constructState(::operator new(sizeof(State)));
}

HuggettHpStage::State* HuggettHpStage::constructState(void* mem) const {
    auto* st = new (mem) State();
    st->a.prepare(sampleRate_);
    st->b.prepare(sampleRate_);
    st->dc.prepare(sampleRate_);
    return st;
}
```

Add `#include <new>` at the top of `HuggettHpStage.cpp`.

- [ ] **Step 6: Add the Q18 governance assert** — `src/dsp/spine/FilterModelLibrary.cpp`

Add includes and a static_assert near the top (after the existing includes):

```cpp
#include "SpineState.h"
// Q18 governance: every registered model's State must fit the per-voice slot.
static_assert(sizeof(HuggettFilter::VoiceState)  <= kMaxSpineStateBytes,
              "HuggettFilter::VoiceState exceeds kMaxSpineStateBytes — bump it (Q18) or slim the model");
static_assert(alignof(HuggettFilter::VoiceState) <= kSpineStateAlign,
              "HuggettFilter::VoiceState over-aligned for the spine slot");
```

- [ ] **Step 7: Write the failing test** — `tests/InPlaceStateTests.cpp`

```cpp
#include <juce_core/juce_core.h>
#include "../src/dsp/spine/HuggettFilter.h"
#include "../src/dsp/spine/FilterModelLibrary.h"
#include "../src/dsp/spine/SpineState.h"
#include <vector>
#include <cstddef>
#include <cmath>

// Task 1: in-place state lifecycle equals the heap path, and every registered
// model fits the per-voice slot budget (Q18).
struct InPlaceStateTests : public juce::UnitTest {
    InPlaceStateTests() : juce::UnitTest("InPlaceState") {}
    static constexpr double kSR = 48000.0;

    static void runTone(const FilterModel& m, FilterModel::State& st, std::vector<float>& out) {
        for (int i = 0; i < (int) out.size(); ++i) {
            float l = 0.3f * (float) std::sin(2.0 * juce::MathConstants<double>::pi * 200.0 * i / kSR);
            float r = l;
            m.processStereo(st, &l, &r, 1);
            out[(size_t) i] = l;
        }
    }

    void runTest() override {
        beginTest("constructState (in-place) matches makeState (heap) sample-for-sample");
        HuggettFilter h; h.prepare(kSR);
        h.setMode(HuggettFilter::Mode::LP); h.setSlope(HuggettFilter::Slope::db24);
        h.setSeparation(0.0f); h.setCommon(1000.0f, 0.3f, 0.0f);

        std::unique_ptr<FilterModel::State> heap(h.makeState()); h.reset(*heap);
        std::vector<float> heapOut(4096), inplaceOut(4096);
        runTone(h, *heap, heapOut);

        alignas(kSpineStateAlign) std::byte buf[kMaxSpineStateBytes];
        FilterModel::State* st = h.constructState(buf); h.reset(*st);
        runTone(h, *st, inplaceOut);
        h.destroyState(st);

        bool identical = true;
        for (size_t i = 0; i < heapOut.size(); ++i) identical = identical && (heapOut[i] == inplaceOut[i]);
        expect(identical, "in-place output diverged from heap output");

        beginTest("every registered model fits kMaxSpineStateBytes (Q18)");
        for (std::size_t i = 0; i < FilterModelLibrary::count(); ++i) {
            auto m = FilterModelLibrary::create(i);
            expect(m->stateSize()  <= kMaxSpineStateBytes, "model " + juce::String((int) i) + " state too large");
            expect(m->stateAlign() <= kSpineStateAlign,    "model " + juce::String((int) i) + " over-aligned");
        }
    }
};
static InPlaceStateTests inPlaceStateTestsInstance;
```

Register it: add `InPlaceStateTests.cpp` to the source list in `tests/CMakeLists.txt` (after `FilterModelLibraryTests.cpp`).

- [ ] **Step 8: Build and run — expect FAIL/compile-gate first, then PASS after Steps 2–6**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | grep -E "InPlaceState|Summary"`
Expected after all edits: `[PASS] InPlaceState: …` and `Summary: … 0 failed`. (If Steps 2–6 aren't done yet, the build fails on the missing `constructState` — that's the RED.)

- [ ] **Step 9: Confirm the whole suite is still green** (refactor must not regress)

Run: `./build/tests/k2000_tests 2>&1 | tail -3`
Expected: `Summary: N tests, 0 failed` (N is the pre-existing count + 1).

- [ ] **Step 10: Commit**

```bash
git add src/dsp/spine/ tests/InPlaceStateTests.cpp tests/CMakeLists.txt
git commit -m "feat(spine): in-place FilterModel state lifecycle (heap makeState retained)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: `SpineFilterSlot` uses one in-place buffer (no fade yet)

Migrate the slot's storage from `unique_ptr` heap state to a single in-place buffer (plus the HP in-place buffer). Behavior is byte-identical; this is the foundation the dual-buffer fade builds on. `prepare()` gains a `maxBlockSize` arg (for the scratch buffers the fade will use, allocated now).

**Files:**
- Modify: `src/dsp/spine/SpineFilterSlot.h`, `src/dsp/spine/SpineFilterSlot.cpp`
- Modify: `src/Voice.cpp` (pass `maxBlock` to `spine_.prepare`)
- Test: existing `tests/SpineFilterTests.cpp` + the full suite stay green.

**Interfaces:**
- Consumes: `FilterModel::constructState/destroyState`, `HuggettHpStage::constructState`, `SpineState.h` constants (Task 1).
- Produces: `void SpineFilterSlot::prepare(double, int maxBlockSize, const FilterModel*, const HuggettHpStage*)`; `processStereo`/`reset` signatures unchanged for now.

- [ ] **Step 1: Rewrite `SpineFilterSlot.h`**

```cpp
#pragma once
#include <vector>
#include <cstddef>
#include "FilterModel.h"
#include "HuggettHpStage.h"
#include "SpineState.h"

// Per-voice spine STATE holder. Owns this voice's filter State in fixed in-place
// storage (no heap after prepare). Two buffers exist for the live model crossfade
// (Task 5); Task 2 uses only buffer 0. The active model is supplied per call from
// the voice's current Layer, so a reassigned voice filters through the right model.
class SpineFilterSlot {
public:
    SpineFilterSlot() = default;
    ~SpineFilterSlot();                                            // destroys placement-constructed states
    SpineFilterSlot(const SpineFilterSlot&) = delete;             // state_ points into buf_ — non-relocatable
    SpineFilterSlot& operator=(const SpineFilterSlot&) = delete;

    void prepare(double sampleRate, int maxBlockSize,
                 const FilterModel* modelForState, const HuggettHpStage* hpForState);
    void reset(const FilterModel* model, const HuggettHpStage* hp) noexcept;
    void processStereo(const HuggettHpStage* hp, bool hpEnabled,
                       const FilterModel* model, float* left, float* right, int numSamples) noexcept;

private:
    alignas(kSpineStateAlign) std::byte buf_[2][kMaxSpineStateBytes];
    FilterModel::State* state_[2] = {nullptr, nullptr};
    const FilterModel*  model_[2] = {nullptr, nullptr};
    int    active_ = 0;
    double sampleRate_ = 44100.0;
    std::vector<float> scratchL_, scratchR_;   // sized at prepare; used by the fade (Task 5)

    alignas(kSpineStateAlign) std::byte hpBuf_[kSpineHpStateBytes];
    HuggettHpStage::State* hpState_ = nullptr;
};
```

- [ ] **Step 2: Rewrite `SpineFilterSlot.cpp`**

```cpp
#include "SpineFilterSlot.h"

SpineFilterSlot::~SpineFilterSlot() {
    for (int i = 0; i < 2; ++i)
        if (state_[i]) model_[i]->destroyState(state_[i]);
    if (hpState_) hpState_->~State();
}

void SpineFilterSlot::prepare(double sr, int maxBlock,
                              const FilterModel* modelForState, const HuggettHpStage* hpForState) {
    sampleRate_ = sr;
    scratchL_.assign((size_t) maxBlock, 0.0f);
    scratchR_.assign((size_t) maxBlock, 0.0f);

    for (int i = 0; i < 2; ++i)
        if (state_[i]) { model_[i]->destroyState(state_[i]); state_[i] = nullptr; model_[i] = nullptr; }
    if (hpState_) { hpState_->~State(); hpState_ = nullptr; }
    active_ = 0;

    if (modelForState) { state_[0] = modelForState->constructState(buf_[0]); model_[0] = modelForState; }
    if (hpForState)    { hpState_  = hpForState->constructState(hpBuf_); }
}

void SpineFilterSlot::reset(const FilterModel* model, const HuggettHpStage* hp) noexcept {
    if (model && state_[active_]) model->reset(*state_[active_]);
    if (hp    && hpState_)        hp->reset(*hpState_);
}

void SpineFilterSlot::processStereo(const HuggettHpStage* hp, bool hpEnabled,
                                    const FilterModel* model, float* l, float* r, int n) noexcept {
    if (hpEnabled && hp && hpState_) hp->processStereo(*hpState_, l, r, n);
    if (model && state_[active_])    model->processStereo(*state_[active_], l, r, n);
}
```

Add `#include <new>` if needed for placement-new visibility (already via FilterModel.h).

- [ ] **Step 3: Update `Voice::prepare`** — `src/Voice.cpp:18`

```cpp
    spine_.prepare(sr, maxBlock, layer_ ? layer_->spineModel() : nullptr,
                       layer_ ? layer_->hpStage()    : nullptr);
```

- [ ] **Step 4: Build and run the spine + voice tests**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | grep -E "SpineFilter|Voice|Layer|Hp|Summary"`
Expected: all listed `[PASS]`, `Summary: … 0 failed`.

- [ ] **Step 5: Full suite green**

Run: `./build/tests/k2000_tests 2>&1 | tail -3`
Expected: `Summary: N tests, 0 failed`.

- [ ] **Step 6: Commit**

```bash
git add src/dsp/spine/SpineFilterSlot.h src/dsp/spine/SpineFilterSlot.cpp src/Voice.cpp
git commit -m "feat(spine): SpineFilterSlot in-place state (no heap after prepare)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 3: `Layer` pre-builds every model; alloc-free model switch

Replace the single `spineModel_` (rebuilt on the audio thread at id change) with a vector of one instance per registered model, built at `prepare()`. Switching only changes `currentModelId_`. Push the common core to all models so an outgoing model keeps tracking during a fade.

**Files:**
- Modify: `src/Layer.h`, `src/Layer.cpp`
- Test: `tests/LayerTests.cpp` (add cases)

**Interfaces:**
- Produces: `const FilterModel* Layer::spineModel() const` (current), `const FilterModel* Layer::spineModel(std::size_t id) const` (by id).

- [ ] **Step 1: Write the failing test** — append to `tests/LayerTests.cpp` inside its `runTest()`

```cpp
        beginTest("pre-built models: spineModel(id) is stable across updateParameters");
        {
            Layer layer; layer.prepare(48000.0, 512);
            const FilterModel* m0a = layer.spineModel(0);
            ParamSnapshot s; s.spineModel = 0;
            layer.updateParameters(s);
            const FilterModel* m0b = layer.spineModel(0);
            expect(m0a != nullptr, "model 0 not built");
            expect(m0a == m0b, "model instance was rebuilt on update (should be pre-built/stable)");
            expect(layer.spineModel() == m0a, "current model should be id 0");
        }
```

(Confirm `#include "../src/Layer.h"` and `#include "../src/params/ParamSnapshot.h"` are present at the top of `LayerTests.cpp`; add any missing.)

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | grep -E "Layer:|Summary"`
Expected: compile error (`spineModel(0)` overload doesn't exist) — the RED.

- [ ] **Step 3: Update `Layer.h`** — replace the spine members

Replace:
```cpp
    std::unique_ptr<FilterModel> spineModel_;
    std::size_t spineModelId_ = SIZE_MAX;   // forces first build
    HuggettFilter* huggett_ = nullptr;      // non-owning view when model 0 is active
```
with:
```cpp
    std::vector<std::unique_ptr<FilterModel>> models_;   // one per registered model (pre-built)
    std::size_t currentModelId_ = 0;
    HuggettFilter* huggett_ = nullptr;      // non-owning view of the Huggett instance
```
and replace the accessor:
```cpp
    const FilterModel* spineModel() const { return models_[currentModelId_].get(); }
    const FilterModel* spineModel(std::size_t id) const {
        return models_[id < models_.size() ? id : 0].get();
    }
```
Add `#include <vector>` to `Layer.h`.

- [ ] **Step 4: Update `Layer::prepare`** — `src/Layer.cpp`

```cpp
void Layer::prepare(double sr, int maxBlock) {
    for (auto& b : palette_)
        if (b) b->prepare(sr, maxBlock);
    sampleRate_ = sr;
    models_.clear();
    for (std::size_t i = 0; i < FilterModelLibrary::count(); ++i) {
        auto m = FilterModelLibrary::create(i);
        m->prepare(sampleRate_);
        models_.push_back(std::move(m));
    }
    currentModelId_ = 0;
    huggett_ = dynamic_cast<HuggettFilter*>(models_[0].get());
    hpStage_.prepare(sr);
}
```

- [ ] **Step 5: Update `Layer::updateParameters`** — `src/Layer.cpp`

Replace the model-rebuild block and the `huggett_` config block with:

```cpp
    currentModelId_ = (std::size_t) s.spineModel;
    if (currentModelId_ >= models_.size()) currentModelId_ = 0;

    // Common core -> ALL models, so an outgoing model keeps tracking cutoff/res/drive
    // through a live crossfade (Task 5).
    for (auto& m : models_)
        m->setCommon(s.svfCutoffHz, s.svfResonance, s.spineDrive);

    if (huggett_) {
        int routingIdx = s.huggettRouting;
        if (routingIdx == 0) {
            switch (s.svfType) { case 1: routingIdx = 2; break;
                                 case 2: routingIdx = 1; break;
                                 default: routingIdx = 0; break; }
        }
        huggett_->setRouting(static_cast<HuggettFilter::Routing>(routingIdx));
        huggett_->setSlope(s.spineSlope == 0 ? HuggettFilter::Slope::db12 : HuggettFilter::Slope::db24);
        huggett_->setSeparation(s.spineSeparationOct);
        huggett_->setPostDrive(s.huggettPostDrive);
    }
    hpStage_.setParams(s.hpCutoffHz, s.hpResonance,
                       s.hpSlope == 0 ? HuggettHpStage::Slope::db12 : HuggettHpStage::Slope::db24);
```

(Keep the leading `snapshot_ = s; activeAlgorithmId_ = (std::size_t) s.algorithmId;` and the palette loop unchanged at the top.)

- [ ] **Step 6: Run to verify PASS**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | grep -E "Layer:|Summary"`
Expected: `[PASS] Layer: …`, `Summary: … 0 failed`.

- [ ] **Step 7: Full suite green, then commit**

Run: `./build/tests/k2000_tests 2>&1 | tail -3` → `Summary: N tests, 0 failed`.

```bash
git add src/Layer.h src/Layer.cpp tests/LayerTests.cpp
git commit -m "feat(spine): Layer pre-builds all models; alloc-free model switch

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 4: Test-only counting `FilterModel` fixture

A second model is needed to exercise the crossfade. Add a header-only test fixture: a trivial one-pole filter with **construct/destroy counters** (proves no leak/double-free) and a tunable DC gain (so a steady input gives a distinguishable steady output per "model"). Header-only → no CMake source change.

**Files:**
- Create: `tests/fixtures/CountingFilterModel.h`
- Modify: `tests/InPlaceStateTests.cpp` (add a fixture self-test + its Q18 assert)

**Interfaces:**
- Produces: `class CountingFilterModel : public FilterModel` with `static int liveStates();` (constructs − destroys), `void setGain(float g);`, and a one-pole-per-channel `VoiceState`.

- [ ] **Step 1: Write `tests/fixtures/CountingFilterModel.h`**

```cpp
#pragma once
#include "../../src/dsp/spine/FilterModel.h"
#include "../../src/dsp/spine/SpineState.h"
#include <new>

// Test-only second FilterModel. A trivial one-pole LP with a DC gain, plus a global
// live-state counter so crossfade tests can assert no leak / no double-free.
class CountingFilterModel : public FilterModel {
public:
    struct VoiceState : public FilterModel::State {
        float z[2] = {0.0f, 0.0f};
        VoiceState()  { ++count(); }
        ~VoiceState() override { --count(); }
        static int& count() { static int c = 0; return c; }
    };

    static int liveStates() { return VoiceState::count(); }

    void prepare(double sr) noexcept override { sampleRate_ = sr; }
    std::size_t stateSize()  const noexcept override { return sizeof(VoiceState); }
    std::size_t stateAlign() const noexcept override { return alignof(VoiceState); }
    FilterModel::State* constructState(void* mem) const override { return new (mem) VoiceState(); }
    void reset(State& s) const noexcept override {
        auto& v = static_cast<VoiceState&>(s); v.z[0] = v.z[1] = 0.0f;
    }
    void setCommon(float cutoffHz, float, float) noexcept override {
        // one-pole coefficient from cutoff
        const float x = std::exp(-2.0f * 3.14159265f * cutoffHz / (float) sampleRate_);
        a_ = std::clamp(x, 0.0f, 0.9999f);
    }
    void setGain(float g) noexcept { gain_ = g; }
    void processStereo(State& s, float* l, float* r, int n) const noexcept override {
        auto& v = static_cast<VoiceState&>(s);
        for (int i = 0; i < n; ++i) {
            v.z[0] = a_ * v.z[0] + (1.0f - a_) * l[i]; l[i] = gain_ * v.z[0];
            v.z[1] = a_ * v.z[1] + (1.0f - a_) * r[i]; r[i] = gain_ * v.z[1];
        }
    }
private:
    double sampleRate_ = 48000.0;
    float a_ = 0.5f, gain_ = 1.0f;
};
static_assert(sizeof(CountingFilterModel::VoiceState) <= kMaxSpineStateBytes,
              "CountingFilterModel state exceeds kMaxSpineStateBytes");
```

Add `#include <cmath>` and `#include <algorithm>` to the header.

- [ ] **Step 2: Add a fixture self-test** — append to `InPlaceStateTests.cpp` `runTest()`

```cpp
        beginTest("CountingFilterModel: construct/destroy balance");
        {
            const int before = CountingFilterModel::liveStates();
            CountingFilterModel cm; cm.prepare(48000.0); cm.setCommon(1000.0f, 0.0f, 0.0f);
            alignas(kSpineStateAlign) std::byte b[kMaxSpineStateBytes];
            auto* st = cm.constructState(b);
            expect(CountingFilterModel::liveStates() == before + 1, "construct did not bump counter");
            cm.destroyState(st);
            expect(CountingFilterModel::liveStates() == before, "destroy did not balance counter");
        }
```

Add `#include "fixtures/CountingFilterModel.h"` to the top of `InPlaceStateTests.cpp`.

- [ ] **Step 3: Build, run, expect PASS**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | grep -E "InPlaceState|Summary"`
Expected: `[PASS] InPlaceState: …`, `Summary: … 0 failed`.

- [ ] **Step 4: Commit**

```bash
git add tests/fixtures/CountingFilterModel.h tests/InPlaceStateTests.cpp
git commit -m "test(spine): counting FilterModel fixture for crossfade tests

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 5: Equal-power crossfade engine + coalesce in `SpineFilterSlot`

The headline feature. The slot detects when the supplied model differs from the active one, constructs the new state in the free buffer, runs both models per sample with an equal-power blend over `fadeMs`, retires the old state, and coalesces a rapid second change to one queued (`pending_`) fade. `processStereo` gains a `fadeMs` argument.

**Files:**
- Modify: `src/dsp/spine/SpineFilterSlot.h`, `src/dsp/spine/SpineFilterSlot.cpp`
- Modify: `src/Voice.cpp` (pass `fadeMs` — temporarily `kDefaultModelFadeMs` until Task 6 wires the param)
- Create: `tests/ModelHotSwapTests.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `CountingFilterModel` (Task 4), `FilterModel` in-place lifecycle (Task 1).
- Produces: `void SpineFilterSlot::processStereo(const HuggettHpStage*, bool, const FilterModel* current, float fadeMs, float* l, float* r, int n)`; `void SpineFilterSlot::bind(const FilterModel*, const HuggettHpStage*)` (used in Task 7).

- [ ] **Step 1: Extend `SpineFilterSlot.h`** — add fade state + new signatures

Add private members after `int active_ = 0;`:
```cpp
    int   fadePos_ = 0;        // 0 = steady; 1..fadeLen_ = samples into the current fade
    int   fadeLen_ = 0;        // captured at fade-begin
    const FilterModel* pending_ = nullptr;   // coalesce depth-1
```
Change the public `processStereo` declaration to:
```cpp
    void processStereo(const HuggettHpStage* hp, bool hpEnabled,
                       const FilterModel* current, float fadeMs,
                       float* left, float* right, int numSamples) noexcept;
    void bind(const FilterModel* model, const HuggettHpStage* hp) noexcept;  // note-start; snap, no fade
```
Add a private helper declaration:
```cpp
    void beginFade(const FilterModel* target, float fadeMs) noexcept;
```

- [ ] **Step 2: Write the failing test** — `tests/ModelHotSwapTests.cpp`

```cpp
#include <juce_core/juce_core.h>
#include "../src/dsp/spine/SpineFilterSlot.h"
#include "../src/dsp/spine/SpineState.h"
#include "fixtures/CountingFilterModel.h"
#include <vector>
#include <cmath>

struct ModelHotSwapTests : public juce::UnitTest {
    ModelHotSwapTests() : juce::UnitTest("ModelHotSwap") {}
    static constexpr double kSR = 48000.0;

    // process n samples of a steady 0.5 DC-ish tone through the slot at the given fade.
    static void block(SpineFilterSlot& slot, const FilterModel* m, float fadeMs,
                      std::vector<float>& l, std::vector<float>& r) {
        std::fill(l.begin(), l.end(), 0.5f);
        std::fill(r.begin(), r.end(), 0.5f);
        slot.processStereo(nullptr, false, m, fadeMs, l.data(), r.data(), (int) l.size());
    }

    void runTest() override {
        const int before = CountingFilterModel::liveStates();

        CountingFilterModel a; a.prepare(kSR); a.setCommon(20000.0f, 0, 0); a.setGain(1.0f);
        CountingFilterModel b; b.prepare(kSR); b.setCommon(20000.0f, 0, 0); b.setGain(0.25f);

        SpineFilterSlot slot;
        slot.prepare(kSR, 64, &a, nullptr);   // start on model a
        std::vector<float> l(64), r(64);

        beginTest("steady state outputs the active model (gain 1.0)");
        block(slot, &a, 25.0f, l, r);
        expect(std::abs(l[63] - 0.5f) < 1e-3f, "model A steady output wrong");

        beginTest("click-free switch: no large sample-to-sample jump across the change");
        // begin fade A->B; capture the first fade block
        std::vector<float> prev = l;
        block(slot, &b, 25.0f, l, r);  // fadeLen = round(25ms*48k/1000)=1200 samples >> 64
        float maxJump = std::abs(l[0] - prev.back());
        for (size_t i = 1; i < l.size(); ++i) maxJump = std::max(maxJump, std::abs(l[i] - l[i-1]));
        expect(maxJump < 0.05f, "discontinuity at switch: " + juce::String(maxJump, 4));

        beginTest("fade completes toward model B (gain 0.25) within ~fadeLen");
        // 1200-sample fade; push ~1300 samples in 64-sample blocks
        for (int done = 64; done < 1400; done += 64) block(slot, &b, 25.0f, l, r);
        expect(std::abs(l[63] - 0.125f) < 5e-3f, "did not settle to model B (0.5*0.25=0.125)");

        beginTest("no leak: live states return to baseline+1 (one active buffer)");
        expect(CountingFilterModel::liveStates() == before + 1,
               "live states = " + juce::String(CountingFilterModel::liveStates() - before));

        beginTest("coalesce depth-1: A->B then mid-fade ->A settles back to A");
        {
            SpineFilterSlot s2; s2.prepare(kSR, 64, &a, nullptr);
            block(s2, &a, 25.0f, l, r);
            block(s2, &b, 25.0f, l, r);          // start A->B
            block(s2, &a, 25.0f, l, r);          // mid-fade re-target -> A (pending)
            for (int done = 0; done < 3000; done += 64) block(s2, &a, 25.0f, l, r);
            expect(std::abs(l[63] - 0.5f) < 5e-3f, "coalesced fade did not settle back to A");
        }

        beginTest("bind() snaps to a different model with no fade (Q17b unit)");
        {
            SpineFilterSlot s3; s3.prepare(kSR, 64, &a, nullptr);
            s3.bind(&b, nullptr);   // note-start onto model B (a stolen voice's new layer)
            std::fill(l.begin(), l.end(), 0.5f); std::fill(r.begin(), r.end(), 0.5f);
            s3.processStereo(nullptr, false, &b, 25.0f, l.data(), r.data(), 64);
            expect(std::isfinite(l[63]), "non-finite output after bind");
            expect(std::abs(l[63] - 0.125f) < 5e-3f, "bind did not snap to B (0.5*0.25)");
        }

        beginTest("bind() cancels an in-flight fade and leaks no state");
        {
            const int base = CountingFilterModel::liveStates();
            SpineFilterSlot s4; s4.prepare(kSR, 64, &a, nullptr);   // base+1 (A)
            s4.processStereo(nullptr, false, &b, 25.0f, l.data(), r.data(), 64);  // start A->B: base+2
            s4.bind(&a, nullptr);   // steal mid-fade back to A: frees B -> base+1
            expect(CountingFilterModel::liveStates() == base + 1,
                   "live after bind = " + juce::String(CountingFilterModel::liveStates() - base));
        }
    }
};
static ModelHotSwapTests modelHotSwapTestsInstance;
```

Register `ModelHotSwapTests.cpp` in `tests/CMakeLists.txt`. (Wrap the earlier `slot`-based blocks' temporaries so each `SpineFilterSlot` is destroyed at its block's end — the counter assertions above rely on that cleanup.)

- [ ] **Step 3: Run to verify it fails**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | grep -E "ModelHotSwap|Summary"`
Expected: compile error (`processStereo` 7-arg overload / `bind` missing) — the RED.

- [ ] **Step 4: Implement the fade engine** — rewrite `SpineFilterSlot.cpp` `processStereo` + add `beginFade`/`bind`

```cpp
#include "SpineFilterSlot.h"
#include <algorithm>
#include <cmath>

void SpineFilterSlot::beginFade(const FilterModel* target, float fadeMs) noexcept {
    const int other = 1 - active_;
    if (state_[other]) { model_[other]->destroyState(state_[other]); state_[other] = nullptr; }
    state_[other] = target->constructState(buf_[other]);
    model_[other] = target;
    const float ms = std::clamp(fadeMs, kMinModelFadeMs, kMaxModelFadeMs);
    fadeLen_ = std::max(1, (int) std::lround(ms * sampleRate_ / 1000.0));
    fadePos_ = 1;
    pending_ = nullptr;
}

void SpineFilterSlot::bind(const FilterModel* model, const HuggettHpStage* hp) noexcept {
    const int other = 1 - active_;
    if (state_[other]) { model_[other]->destroyState(state_[other]); state_[other] = nullptr; model_[other] = nullptr; }
    fadePos_ = 0; pending_ = nullptr;
    if (model != model_[active_]) {
        if (state_[active_]) model_[active_]->destroyState(state_[active_]);
        state_[active_] = model ? model->constructState(buf_[active_]) : nullptr;
        model_[active_] = model;
    }
    if (model && state_[active_]) model->reset(*state_[active_]);
    if (hp && hpState_)           hp->reset(*hpState_);
}

void SpineFilterSlot::processStereo(const HuggettHpStage* hp, bool hpEnabled,
                                    const FilterModel* current, float fadeMs,
                                    float* l, float* r, int n) noexcept {
    if (hpEnabled && hp && hpState_) hp->processStereo(*hpState_, l, r, n);
    if (current == nullptr) return;

    // switch detection
    if (fadePos_ == 0) {
        if (current != model_[active_]) beginFade(current, fadeMs);
    } else {
        const int other = 1 - active_;
        pending_ = (current != model_[other]) ? current : nullptr;   // coalesce depth-1
    }

    // steady
    if (fadePos_ == 0) {
        if (state_[active_]) model_[active_]->processStereo(*state_[active_], l, r, n);
        return;
    }

    // fading: NEW in place on (l,r); OLD on a scratch copy of the input
    const int other = 1 - active_;
    std::copy(l, l + n, scratchL_.data());
    std::copy(r, r + n, scratchR_.data());
    model_[other ]->processStereo(*state_[other ], l, r, n);
    model_[active_]->processStereo(*state_[active_], scratchL_.data(), scratchR_.data(), n);

    constexpr float kHalfPi = 1.57079632679f;
    for (int i = 0; i < n; ++i) {
        const float p    = std::min(1.0f, (float) fadePos_ / (float) fadeLen_);
        const float gOld = std::cos(p * kHalfPi);
        const float gNew = std::sin(p * kHalfPi);
        l[i] = gNew * l[i] + gOld * scratchL_[i];
        r[i] = gNew * r[i] + gOld * scratchR_[i];
        if (fadePos_ < fadeLen_) ++fadePos_;
    }

    // completion at block end
    if (fadePos_ >= fadeLen_) {
        model_[active_]->destroyState(state_[active_]);
        state_[active_] = nullptr;
        active_ = other;
        fadePos_ = 0;
        if (pending_ && pending_ != model_[active_]) {
            const FilterModel* next = pending_;
            pending_ = nullptr;
            beginFade(next, fadeMs);
        } else {
            pending_ = nullptr;
        }
    }
}
```

- [ ] **Step 5: Update `Voice::render`** — `src/Voice.cpp:87`

```cpp
    spine_.processStereo(layer_->hpStage(), s.hpEnable != 0,
                         layer_->spineModel(), kDefaultModelFadeMs, tmpL, tmpR, numSamples);
```
Add `#include "dsp/spine/SpineState.h"` to `Voice.cpp` (for `kDefaultModelFadeMs`; replaced by the real param in Task 6).

- [ ] **Step 6: Run the hot-swap tests — expect PASS**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | grep -E "ModelHotSwap|Summary"`
Expected: `[PASS] ModelHotSwap: …`, `Summary: … 0 failed`.

- [ ] **Step 7: Full suite green, then commit**

Run: `./build/tests/k2000_tests 2>&1 | tail -3` → `Summary: N tests, 0 failed`.

```bash
git add src/dsp/spine/SpineFilterSlot.h src/dsp/spine/SpineFilterSlot.cpp src/Voice.cpp tests/ModelHotSwapTests.cpp tests/CMakeLists.txt
git commit -m "feat(spine): equal-power model crossfade + coalesce in SpineFilterSlot

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 6: `spine.modelFadeMs` global automatable parameter

Wire the fade time as a global (non-per-Layer) automatable float, range 2–100 ms, default 25, carried on `ParamSnapshot` and read in `Voice::render`.

**Files:**
- Modify: `src/params/ParamSnapshot.h`, `src/params/Parameters.h`, `src/params/Parameters.cpp`
- Modify: `src/Voice.cpp`
- Test: `tests/ParamSnapshotTests.cpp` (add a case)

**Interfaces:**
- Produces: `ParamSnapshot::spineModelFadeMs` (float, default `kDefaultModelFadeMs`); APVTS id `params::spineModelFadeMs` = `"spine.modelFadeMs"`.

- [ ] **Step 1: Write the failing test** — append to `tests/ParamSnapshotTests.cpp` `runTest()`

This file already builds an `apvts` at the top of `runTest()` from `params::createLayout()` (via the in-file `DummyProc`). Reuse that in-scope `apvts` — do NOT construct a new one:

```cpp
        beginTest("spine.modelFadeMs default reaches the snapshot (25 ms) and round-trips");
        {
            auto s = params::snapshot(apvts, 0);
            expectWithinAbsoluteError(s.spineModelFadeMs, 25.0f, 1e-4f);
            if (auto* p = apvts.getParameter(params::spineModelFadeMs))
                p->setValueNotifyingHost(p->convertTo0to1(60.0f));
            s = params::snapshot(apvts, 0);
            expectWithinAbsoluteError(s.spineModelFadeMs, 60.0f, 0.1f);
        }
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | grep -E "ParamSnapshot|Summary"`
Expected: compile error (`spineModelFadeMs` not a member) — the RED.

- [ ] **Step 3: Add the snapshot field** — `src/params/ParamSnapshot.h`

After `float spineOutputDb = 0.0f;`:
```cpp
    float spineModelFadeMs   = 25.0f;   // global: spine.modelFadeMs (2..100 ms)
```

- [ ] **Step 4: Declare the global id** — `src/params/Parameters.h`

After `inline constexpr auto masterGain = "master.gain";`:
```cpp
inline constexpr auto spineModelFadeMs = "spine.modelFadeMs";
```

- [ ] **Step 5: Register + read the param** — `src/params/Parameters.cpp`

After the `masterGain` `layout.add(...)` (the `"Master Gain"` block, ~line 190):
```cpp
    layout.add(std::make_unique<FloatParam>(juce::ParameterID{spineModelFadeMs, 1},
        "Spine Model Fade",
        juce::NormalisableRange<float>{kMinModelFadeMs, kMaxModelFadeMs, 0.0f}, kDefaultModelFadeMs));
```
In `snapshot(...)`, after `s.masterGainDb = raw(apvts, masterGain);`:
```cpp
    s.spineModelFadeMs = raw(apvts, spineModelFadeMs);
```
Add `#include "../dsp/spine/SpineState.h"` to `Parameters.cpp` (for the fade constants).

- [ ] **Step 6: Use the real param in `Voice::render`** — `src/Voice.cpp`

```cpp
    spine_.processStereo(layer_->hpStage(), s.hpEnable != 0,
                         layer_->spineModel(), s.spineModelFadeMs, tmpL, tmpR, numSamples);
```
(`s` is `layer_->snapshot()`, already in scope.) The `kDefaultModelFadeMs` include from Task 5 can stay or be removed.

- [ ] **Step 7: Run — expect PASS, full suite green**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | grep -E "ParamSnapshot|Summary"`
Expected: `[PASS] ParamSnapshot: …`, `Summary: … 0 failed`.

- [ ] **Step 8: Commit**

```bash
git add src/params/ src/Voice.cpp tests/ParamSnapshotTests.cpp
git commit -m "feat(spine): global automatable spine.modelFadeMs (2-100 ms, default 25)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 7: Route `Voice::noteOn` through `bind` (Q17b cross-layer steal)

A stolen voice reassigned to a Layer with a different model type must rebuild its in-place state on note-on — no fade (the envelope starts at 0). The `bind()` logic and its different-model snap / cancel-fade / no-leak behavior are unit-tested at the slot level in Task 5. This task is the mechanical wiring: `Voice::noteOn` calls `spine_.bind(...)` instead of `spine_.reset(...)`, so the path is live when a 2nd production model lands (v5.2). The different-model steal is not end-to-end reachable today (the production library has one model), so the deliverable here is the wiring plus a Voice-level regression guard that note-start still renders cleanly.

**Files:**
- Modify: `src/Voice.cpp` (`noteOn` → `spine_.bind`)
- Test: `tests/VoiceTests.cpp` (add a regression case)

**Interfaces:**
- Consumes: `SpineFilterSlot::bind` (Task 5).

- [ ] **Step 1: Add a Voice note-start regression test** — append to `tests/VoiceTests.cpp` `runTest()`

Mirror the existing setup in this file for building a `Layer` + `Voice` (a `Voice` needs `setLayer` then `prepare`). Add:

```cpp
        beginTest("noteOn renders finite, audible output (bind path)");
        {
            Layer layer; layer.prepare(48000.0, 512);
            Voice v; v.setLayer(&layer); v.prepare(48000.0, 512);
            v.noteOn(60, 1.0f);
            std::vector<float> l(512, 0.0f), r(512, 0.0f);
            v.render(l.data(), r.data(), 512);
            float peak = 0.0f;
            for (int i = 0; i < 512; ++i) { peak = std::max(peak, std::abs(l[i])); expect(std::isfinite(l[i])); }
            expect(peak > 0.0f, "voice produced silence after noteOn");
        }
```

(Match the include list / construction helpers already used at the top of `VoiceTests.cpp`; add `#include <vector>`/`#include <cmath>` if absent.)

- [ ] **Step 2: Run to verify it passes before AND after the wiring** (regression guard, not a RED)

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | grep -E "Voice:|Summary"`
Expected: `[PASS] Voice: …` (this guards that the Step 3 change does not break note-start; it is intentionally green both before and after).

- [ ] **Step 3: Route `Voice::noteOn` through `bind`** — `src/Voice.cpp:47-48`

Replace the `spine_.reset(...)` call inside `noteOn` with:
```cpp
    spine_.bind(layer_ ? layer_->spineModel() : nullptr,
                layer_ ? layer_->hpStage()    : nullptr);
```
(Leave `Voice::reset()`’s `spine_.reset(...)` as-is — the general reset zeroes the active state without rebinding.)

- [ ] **Step 4: Run — Voice tests + full suite green**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | grep -E "Voice:|Summary"`
Expected: `[PASS] Voice: …`, `Summary: … 0 failed`.

- [ ] **Step 5: Commit**

```bash
git add src/Voice.cpp tests/VoiceTests.cpp
git commit -m "feat(spine): route Voice::noteOn through bind (Q17b cross-layer steal)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 8: Roadmap + register bookkeeping

Mark v5.1 shipped in the live roadmap and resolve Q17/Q18 in the engine-questions register.

**Files:**
- Modify: `tools/roadmap-dashboard/roadmap.json`
- Modify: `docs/architecture/engine-questions.md`

- [ ] **Step 1: Update the roadmap** — in `tools/roadmap-dashboard/roadmap.json`, set the `v5.1` item `status` to `shipped`, and update `meta.nextStep` to lead with the Moog Cmajor block (drop the now-done hot-swap clause). Keep the wording style of the surrounding entries.

- [ ] **Step 2: Resolve the register** — in `docs/architecture/engine-questions.md`, mark Q17 and Q18 resolved (🟢) with a one-line note: crossfade = per-voice equal-power, ~25 ms default via `spine.modelFadeMs` (2–100 ms), coalesce depth-1; in-place dual-buffer storage, `kMaxSpineStateBytes=512` governed by compile-time static_assert; Q17b closed by note-start snap-bind.

- [ ] **Step 3: Commit**

```bash
git add tools/roadmap-dashboard/roadmap.json docs/architecture/engine-questions.md
git commit -m "docs(spine): mark v5.1 hot-swap shipped; resolve Q17/Q18

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Notes for the implementer

- **`kMaxSpineStateBytes` is already pinned at 512** (Huggett state measured ~176 B). If a future model's `static_assert` fires, that is the Q18 governance moment — bump the constant or slim the model; do not silently shrink a state.
- **The production library still has one model**, so the crossfade is latent in the shipped plugin until Moog (v5.2). The fade engine is fully tested at the `SpineFilterSlot` level with the `CountingFilterModel` fixture — that is the intended coverage, not an end-to-end DAW switch.
- **Two voices fading at once share the model instances** (Layer-owned) but have independent `state_` — correct, and identical to how one Huggett already serves all voices.
- **`-j4` always.** Bare `-j` OOMs the JUCE build.
```
