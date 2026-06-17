# v5 Spine Filter Foundation (Plan 1 of 3) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the selectable-spine-filter foundation and ship a **linear Huggett** model as the always-on, per-voice, **stereo** post-graph filter with **static (preset-time) model selection**.

**Architecture:** A new `src/dsp/spine/` subsystem: a heap-free stereo `TptSvfCell` (reusing the proven `SVFFilter` TPT math), a `FilterModel` interface, a linear `HuggettFilter` (two cells + separation + mode/slope), an append-only `FilterModelLibrary` (stable IDs, the `AlgorithmLibrary` idiom), and a per-voice `SpineFilterSlot` that runs the active model. The `Layer` owns the model config; each `Voice` holds the per-voice slot state and applies it after the graph walk. The render path becomes stereo. The optional graph `SvfFilter` is retired in favour of the spine.

**Tech Stack:** C++17, JUCE 8.0.4 (APVTS, `dsp` math), CMake, JUCE `UnitTest` + ctest. Follows ADR-0002 (config/voice-state split) and ADR-0008 (stable-ID library + param namespace).

---

## Integration decisions (review these first)

- **D1 — Stereo render path.** `Voice::render`, `VoiceManager::renderBlock`, and the processor scratch become **stereo** (`L`,`R`). The per-voice source/graph stays mono (sources go stereo in later phases); the spine receives the mono voice signal on both channels and is free to diverge L/R later. The processor renders into a stereo scratch instead of copying mono to all channels.
- **D2 — Filter promotion + param reuse.** The spine is the always-on filter. It **reuses** the existing per-layer params `layer{i}.filter.cutoff` / `filter.resonance` / `filter.type` as its common cutoff/resonance + Huggett mode (so **no value migration** for these — they simply drive the spine now). **New** params: `layer{i}.spine.filterModel` (choice), `spine.separation`, `spine.slope`, `spine.drive`, `spine.output`. The graph `SvfFilter` block is **removed from every `AlgorithmLibrary` entry** (choice indices preserved; display names updated), eliminating graph-side filtering. Schema bumps to `v=5`; the v4→v5 shim defaults the new spine params and is otherwise a no-op for reused ids.
- **Out of scope (later plans):** Huggett nonlinear stages + calibration + anti-aliasing (Plan 2); live crossfade hot-swap + Moog (Plan 3, v5.1). `SpineFilterSlot` is therefore built with a **single** active model now, but with its model stored behind a pointer so Plan 3 can add the second (outgoing) slot without reshaping callers.

## File structure

| File | Responsibility |
|---|---|
| `src/dsp/spine/TptSvfCell.h` | One heap-free **stereo** TPT state-variable cell (LP/BP/HP/Notch taps). Reuses `SVFFilter`'s coefficient math. |
| `src/dsp/spine/FilterModel.h` | Abstract per-voice stereo filter interface + `FilterModel::State` marker. |
| `src/dsp/spine/HuggettFilter.h` / `.cpp` | Linear dual-cell Huggett model: mode (LP/BP/HP), 12/24 dB slope, separation. Implements `FilterModel`. |
| `src/dsp/spine/FilterModelLibrary.h` / `.cpp` | Append-only registry: stable id ↔ display name ↔ in-place factory. Huggett = entry 0. |
| `src/dsp/spine/SpineFilterSlot.h` / `.cpp` | Per-voice holder: the active model's `State`, `setCommon`, model-bank setters, `processStereo`. |
| `src/params/Parameters.{h,cpp}` | Add spine ids + layout entries + `spine*` snapshot fields. |
| `src/params/ParamSnapshot.h` | Add spine fields. |
| `src/Layer.{h,cpp}` | Own the spine model config; configure it in `updateParameters`. |
| `src/Voice.{h,cpp}` | Hold per-voice `SpineFilterSlot::State`; render **stereo**; apply spine after the graph. |
| `src/VoiceManager.{h,cpp}` | `renderBlock` becomes stereo. |
| `src/PluginProcessor.cpp` | Stereo scratch render; `migrateV4ToV5`; schema `v=5`. |
| `src/dsp/AlgorithmLibrary.cpp` | Drop `SvfFilter` from every entry. |
| `src/PluginEditor.cpp` | Rebind the FILTER section to spine params; add the model selector. |
| `tests/SpineFilterTests.cpp`, `tests/FilterModelLibraryTests.cpp` | New unit tests. |

---

### Task 1: `TptSvfCell` — heap-free stereo TPT cell

The reusable core. Same TPT topology as `SVFFilter` ([src/dsp/blocks/SVFFilter.cpp:53-70](../../../src/dsp/blocks/SVFFilter.cpp)) but a value type holding its own L/R state, so a model can own several cells per voice.

**Files:**
- Create: `src/dsp/spine/TptSvfCell.h`
- Create: `tests/SpineFilterTests.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/SpineFilterTests.cpp`:

```cpp
#include <juce_core/juce_core.h>
#include "../src/dsp/spine/TptSvfCell.h"
#include <cmath>

// Measure steady-state magnitude of a cell at a probe frequency.
static float cellMagAt(TptSvfCell& cell, int tap, double sr, double freqHz) {
    cell.reset();
    const int N = 8192;
    float peak = 0.0f;
    for (int i = 0; i < N; ++i) {
        const float x = std::sin(2.0 * juce::MathConstants<double>::pi * freqHz * i / sr);
        float l = x, r = x;
        cell.process(l, r, tap);
        if (i > N / 2) peak = std::max(peak, std::abs(l));
    }
    return peak;  // ~amplitude (input amplitude 1.0)
}

class SpineFilterTests : public juce::UnitTest {
public:
    SpineFilterTests() : juce::UnitTest("SpineFilter") {}
    void runTest() override {
        beginTest("LP cell passes lows, attenuates highs");
        TptSvfCell cell;
        cell.prepare(48000.0);
        cell.setCutoff(1000.0f);
        cell.setResonance(0.0f);
        const float lowMag  = cellMagAt(cell, TptSvfCell::LP, 48000.0, 100.0);
        const float highMag = cellMagAt(cell, TptSvfCell::LP, 48000.0, 10000.0);
        expect(lowMag > 0.7f, "low passes: " + juce::String(lowMag));
        expect(highMag < 0.1f, "high cut: " + juce::String(highMag));
    }
};
static SpineFilterTests spineFilterTestsInstance;
```

- [ ] **Step 2: Run the test to verify it fails**

Add `SpineFilterTests.cpp` after `AlgorithmNameTests.cpp` in `tests/CMakeLists.txt`, then:

Run: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --target k2000_tests -j4`
Expected: FAIL to compile — `TptSvfCell.h` not found.

- [ ] **Step 3: Write the implementation**

Create `src/dsp/spine/TptSvfCell.h`:

```cpp
#pragma once
#include <juce_core/juce_core.h>
#include <cmath>
#include <algorithm>

// One heap-free stereo TPT state-variable cell. Same topology as the v1
// SVFFilter block, but a value type with its own L/R integrator state so a
// model can hold several. Taps: LP/BP/HP/Notch.
class TptSvfCell {
public:
    enum Tap { LP = 0, HP = 1, BP = 2, Notch = 3 };

    void prepare(double sampleRate) noexcept { sampleRate_ = sampleRate; dirty_ = true; }
    void reset() noexcept { ic1_[0] = ic2_[0] = ic1_[1] = ic2_[1] = 0.0f; }

    void setCutoff(float hz) noexcept    { if (hz != cutoffHz_)  { cutoffHz_  = hz; dirty_ = true; } }
    void setResonance(float r) noexcept  { if (r != resonance_)  { resonance_ = r;  dirty_ = true; } }

    // Process one stereo sample in place at the given tap.
    void process(float& left, float& right, int tap) noexcept {
        if (dirty_) recompute();
        left  = step(left,  0, tap);
        right = step(right, 1, tap);
    }

private:
    float step(float v0, int ch, int tap) noexcept {
        const float v3 = v0 - ic2_[ch];
        const float v1 = a1_ * ic1_[ch] + a2_ * v3;
        const float v2 = ic2_[ch] + a2_ * ic1_[ch] + a3_ * v3;
        ic1_[ch] = 2.0f * v1 - ic1_[ch];
        ic2_[ch] = 2.0f * v2 - ic2_[ch];
        switch (tap) {
            case HP:    return v0 - k_ * v1 - v2;
            case BP:    return v1;
            case Notch: return v0 - k_ * v1;
            case LP:
            default:    return v2;
        }
    }

    void recompute() noexcept {
        const float cutoff = std::clamp(cutoffHz_, 16.0f, float(sampleRate_ * 0.45));
        const float res    = std::clamp(resonance_, 0.0f, 0.999f);
        const float Q = 0.5f + res * res * 8.5f;   // matches SVFFilter's calibrated range
        g_ = float(std::tan(juce::MathConstants<double>::pi * cutoff / sampleRate_));
        k_ = 1.0f / Q;
        a1_ = 1.0f / (1.0f + g_ * (g_ + k_));
        a2_ = g_ * a1_;
        a3_ = g_ * a2_;
        dirty_ = false;
    }

    double sampleRate_ = 44100.0;
    float cutoffHz_ = 1000.0f, resonance_ = 0.0f;
    float g_ = 0, k_ = 0, a1_ = 0, a2_ = 0, a3_ = 0;
    bool dirty_ = true;
    float ic1_[2] = {0, 0}, ic2_[2] = {0, 0};
};
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests`
Expected: `[PASS] SpineFilter:` lines; `Summary: … 0 failed`.

- [ ] **Step 5: Commit**

```bash
git add src/dsp/spine/TptSvfCell.h tests/SpineFilterTests.cpp tests/CMakeLists.txt
git commit -m "feat(spine): heap-free stereo TPT SVF cell"
```

---

### Task 2: `FilterModel` interface

**Files:**
- Create: `src/dsp/spine/FilterModel.h`

- [ ] **Step 1: Create the header** (no test — exercised by Task 3's model)

```cpp
#pragma once
#include <cstddef>

// Abstract per-voice, stereo, heap-free spine filter. One instance configures
// shared params (on the Layer); per-voice integrator state lives in State.
// Concrete models define their own State subtype.
class FilterModel {
public:
    struct State { virtual ~State() = default; };

    virtual ~FilterModel() = default;
    virtual void prepare(double sampleRate) noexcept = 0;   // cheap; no heap
    virtual State* makeState() const = 0;                   // allocate-OK (prepare time)
    virtual void reset(State& s) const noexcept = 0;

    // Common core, set per block from the spine's common params.
    virtual void setCommon(float cutoffHz, float resonance, float drive) noexcept = 0;

    // Process numSamples of stereo audio in place using this voice's state.
    virtual void processStereo(State& s, float* left, float* right, int numSamples) const noexcept = 0;
};
```

- [ ] **Step 2: Commit**

```bash
git add src/dsp/spine/FilterModel.h
git commit -m "feat(spine): FilterModel interface (stereo, per-voice state)"
```

---

### Task 3: `HuggettFilter` — linear dual-cell model

Two `TptSvfCell`s. Mode picks the tap; **slope** runs one or both cells (12 vs 24 dB); **separation** offsets the second cell's cutoff in octaves.

**Files:**
- Create: `src/dsp/spine/HuggettFilter.h`, `.cpp`
- Modify: `tests/SpineFilterTests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test** (append inside `runTest()` in `tests/SpineFilterTests.cpp`)

```cpp
        beginTest("Huggett 24 dB LP attenuates an octave above cutoff more than 12 dB");
        {
            HuggettFilter h;
            h.prepare(48000.0);
            std::unique_ptr<FilterModel::State> st(h.makeState());
            h.setMode(HuggettFilter::Mode::LP);
            h.setCommon(1000.0f, 0.0f, 0.0f);
            h.setSeparation(0.0f);

            auto magAtSlope = [&](HuggettFilter::Slope slope) {
                h.setSlope(slope);
                h.reset(*st);
                const int N = 8192; float peak = 0.0f;
                for (int i = 0; i < N; ++i) {
                    float x = std::sin(2.0 * juce::MathConstants<double>::pi * 2000.0 * i / 48000.0);
                    float l = x, r = x;
                    h.processStereo(*st, &l, &r, 1);
                    if (i > N / 2) peak = std::max(peak, std::abs(l));
                }
                return peak;
            };
            const float m12 = magAtSlope(HuggettFilter::Slope::db12);
            const float m24 = magAtSlope(HuggettFilter::Slope::db24);
            expect(m24 < m12, "24 dB steeper: 12=" + juce::String(m12) + " 24=" + juce::String(m24));
        }
```

Add `#include "../src/dsp/spine/HuggettFilter.h"` and `#include <memory>` at the top of the test file.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --target k2000_tests -j4`
Expected: FAIL to compile — `HuggettFilter.h` not found.

- [ ] **Step 3: Write the header** — `src/dsp/spine/HuggettFilter.h`:

```cpp
#pragma once
#include "FilterModel.h"
#include "TptSvfCell.h"

// Linear Huggett model: two TPT cells. Mode selects the tap; Slope runs one
// (12 dB) or both (24 dB) cells in series; Separation offsets cell B's cutoff.
// Nonlinear stages (Plan 2) and hot-swap (Plan 3) are out of scope here.
class HuggettFilter : public FilterModel {
public:
    enum class Mode  { LP, BP, HP };
    enum class Slope { db12, db24 };

    struct VoiceState : public FilterModel::State {
        TptSvfCell a, b;
    };

    void prepare(double sampleRate) noexcept override { sampleRate_ = sampleRate; }
    State* makeState() const override;
    void reset(State& s) const noexcept override;

    void setCommon(float cutoffHz, float resonance, float drive) noexcept override;
    void setMode(Mode m) noexcept       { mode_ = m; }
    void setSlope(Slope s) noexcept     { slope_ = s; }
    void setSeparation(float oct) noexcept { separationOct_ = oct; }

    void processStereo(State& s, float* left, float* right, int numSamples) const noexcept override;

private:
    static int tapForMode(Mode m) noexcept {
        switch (m) { case Mode::BP: return TptSvfCell::BP;
                     case Mode::HP: return TptSvfCell::HP;
                     case Mode::LP: default: return TptSvfCell::LP; }
    }
    double sampleRate_ = 44100.0;
    float  cutoffHz_ = 1000.0f, resonance_ = 0.0f, separationOct_ = 0.0f;
    Mode   mode_  = Mode::LP;
    Slope  slope_ = Slope::db24;
};
```

- [ ] **Step 4: Write the implementation** — `src/dsp/spine/HuggettFilter.cpp`:

```cpp
#include "HuggettFilter.h"
#include <cmath>

FilterModel::State* HuggettFilter::makeState() const {
    auto* vs = new VoiceState();
    vs->a.prepare(sampleRate_);
    vs->b.prepare(sampleRate_);
    return vs;
}

void HuggettFilter::reset(State& s) const noexcept {
    auto& vs = static_cast<VoiceState&>(s);
    vs.a.reset();
    vs.b.reset();
}

void HuggettFilter::setCommon(float cutoffHz, float resonance, float /*drive*/) noexcept {
    cutoffHz_  = cutoffHz;
    resonance_ = resonance;
}

void HuggettFilter::processStereo(State& s, float* left, float* right, int n) const noexcept {
    auto& vs = static_cast<VoiceState&>(s);
    const int tap = tapForMode(mode_);
    const float cutB = cutoffHz_ * std::pow(2.0f, separationOct_);
    vs.a.setCutoff(cutoffHz_); vs.a.setResonance(resonance_);
    vs.b.setCutoff(cutB);      vs.b.setResonance(resonance_);

    for (int i = 0; i < n; ++i) {
        float l = left[i], r = right[i];
        vs.a.process(l, r, tap);
        if (slope_ == Slope::db24)
            vs.b.process(l, r, tap);
        left[i] = l; right[i] = r;
    }
}
```

- [ ] **Step 5: Wire the targets**

In `CMakeLists.txt`, after `src/dsp/AlgorithmLibrary.cpp` in the plugin target's sources, add `src/dsp/spine/HuggettFilter.cpp`. In `tests/CMakeLists.txt`, add `../src/dsp/spine/HuggettFilter.cpp` to the test sources.

- [ ] **Step 6: Run to verify it passes**

Run: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests`
Expected: `[PASS] SpineFilter:` (now 2 sub-tests); `0 failed`.

- [ ] **Step 7: Commit**

```bash
git add src/dsp/spine/HuggettFilter.h src/dsp/spine/HuggettFilter.cpp tests/SpineFilterTests.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(spine): linear dual-cell Huggett model (mode/slope/separation)"
```

---

### Task 4: `FilterModelLibrary` — append-only registry

Stable IDs for preset serialisation, the `AlgorithmLibrary` idiom. Huggett = entry 0. Names via `util::u8`.

**Files:**
- Create: `src/dsp/spine/FilterModelLibrary.h`, `.cpp`
- Create: `tests/FilterModelLibraryTests.cpp`
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test** — `tests/FilterModelLibraryTests.cpp`:

```cpp
#include <juce_core/juce_core.h>
#include "../src/dsp/spine/FilterModelLibrary.h"

class FilterModelLibraryTests : public juce::UnitTest {
public:
    FilterModelLibraryTests() : juce::UnitTest("FilterModelLibrary") {}
    void runTest() override {
        beginTest("entry 0 is Huggett and is stable");
        expect(FilterModelLibrary::count() >= 1);
        expect(FilterModelLibrary::id(0) == juce::String("huggett"));

        beginTest("names() count matches and is non-empty");
        const auto names = FilterModelLibrary::names();
        expectEquals(names.size(), (int) FilterModelLibrary::count());
        expect(names[0].isNotEmpty());

        beginTest("create() returns a usable model");
        auto m = FilterModelLibrary::create(0);
        expect(m != nullptr);
    }
};
static FilterModelLibraryTests filterModelLibraryTestsInstance;
```

- [ ] **Step 2: Run to verify it fails**

Add `FilterModelLibraryTests.cpp` to `tests/CMakeLists.txt`, then `cmake --build build --target k2000_tests -j4`.
Expected: FAIL to compile — `FilterModelLibrary.h` not found.

- [ ] **Step 3: Write the header** — `src/dsp/spine/FilterModelLibrary.h`:

```cpp
#pragma once
#include <juce_core/juce_core.h>
#include <memory>
#include <cstddef>
#include "FilterModel.h"

// Append-only registry of spine filter models. Index is the stable serialised
// id (never reorder; append only) — the AlgorithmLibrary/ADR-0008 idiom.
namespace FilterModelLibrary {
    std::size_t          count();
    juce::String         id(std::size_t i);          // stable string id, e.g. "huggett"
    juce::StringArray    names();                     // display names (UTF-8, for UI/param)
    std::unique_ptr<FilterModel> create(std::size_t i); // factory (prepare-time alloc)
}
```

- [ ] **Step 4: Write the implementation** — `src/dsp/spine/FilterModelLibrary.cpp`:

```cpp
#include "FilterModelLibrary.h"
#include "HuggettFilter.h"
#include "../../util/Utf8.h"

namespace {
struct Entry {
    const char* id;
    const char* displayName;
    std::unique_ptr<FilterModel> (*make)();
};
const Entry kEntries[] = {
    { "huggett", "Huggett", []() -> std::unique_ptr<FilterModel> { return std::make_unique<HuggettFilter>(); } },
};
}  // namespace

namespace FilterModelLibrary {
std::size_t count() { return std::size(kEntries); }

juce::String id(std::size_t i) {
    return i < count() ? juce::String(kEntries[i].id) : juce::String(kEntries[0].id);
}

juce::StringArray names() {
    juce::StringArray s;
    for (const auto& e : kEntries) s.add(util::u8(e.displayName));
    return s;
}

std::unique_ptr<FilterModel> create(std::size_t i) {
    return kEntries[i < count() ? i : 0].make();
}
}  // namespace FilterModelLibrary
```

- [ ] **Step 5: Wire the targets**

Add `src/dsp/spine/FilterModelLibrary.cpp` to the plugin target in `CMakeLists.txt` and `../src/dsp/spine/FilterModelLibrary.cpp` to `tests/CMakeLists.txt`.

- [ ] **Step 6: Run to verify it passes**

Run: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests`
Expected: `[PASS] FilterModelLibrary:`; `0 failed`.

- [ ] **Step 7: Commit**

```bash
git add src/dsp/spine/FilterModelLibrary.h src/dsp/spine/FilterModelLibrary.cpp tests/FilterModelLibraryTests.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(spine): append-only FilterModelLibrary (stable IDs)"
```

---

### Task 5: `SpineFilterSlot` — per-voice holder

Owns the active model's `State` (allocated at prepare) and forwards processing. Built with a single active model now; Plan 3 adds the outgoing slot + crossfade without changing this API.

**Files:**
- Create: `src/dsp/spine/SpineFilterSlot.h`, `.cpp`
- Modify: `tests/SpineFilterTests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test** (append to `runTest()` in `tests/SpineFilterTests.cpp`)

```cpp
        beginTest("SpineFilterSlot filters using the active model");
        {
            HuggettFilter h; h.prepare(48000.0); h.setMode(HuggettFilter::Mode::LP);
            h.setSlope(HuggettFilter::Slope::db24); h.setCommon(500.0f, 0.0f, 0.0f);
            SpineFilterSlot slot;
            slot.prepare(48000.0, &h);
            const int N = 8192; float peak = 0.0f;
            for (int i = 0; i < N; ++i) {
                float x = std::sin(2.0 * juce::MathConstants<double>::pi * 8000.0 * i / 48000.0);
                float l = x, r = x;
                slot.processStereo(&l, &r, 1);
                if (i > N / 2) peak = std::max(peak, std::abs(l));
            }
            expect(peak < 0.1f, "high freq cut by spine: " + juce::String(peak));
        }
```

Add `#include "../src/dsp/spine/SpineFilterSlot.h"` to the test file.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --target k2000_tests -j4` → FAIL: `SpineFilterSlot.h` not found.

- [ ] **Step 3: Write the header** — `src/dsp/spine/SpineFilterSlot.h`:

```cpp
#pragma once
#include <memory>
#include "FilterModel.h"

// Per-voice spine holder. Points at the Layer-owned active FilterModel and owns
// this voice's State for it. (Plan 3 adds a second outgoing slot + crossfade.)
class SpineFilterSlot {
public:
    // active is owned by the Layer and must outlive this slot.
    void prepare(double sampleRate, const FilterModel* active);
    void reset() noexcept;
    void processStereo(float* left, float* right, int numSamples) noexcept;

private:
    const FilterModel* model_ = nullptr;
    std::unique_ptr<FilterModel::State> state_;
};
```

- [ ] **Step 4: Write the implementation** — `src/dsp/spine/SpineFilterSlot.cpp`:

```cpp
#include "SpineFilterSlot.h"

void SpineFilterSlot::prepare(double, const FilterModel* active) {
    model_ = active;
    state_.reset(active ? active->makeState() : nullptr);  // prepare-time alloc only
}

void SpineFilterSlot::reset() noexcept {
    if (model_ && state_) model_->reset(*state_);
}

void SpineFilterSlot::processStereo(float* left, float* right, int n) noexcept {
    if (model_ && state_) model_->processStereo(*state_, left, right, n);
}
```

- [ ] **Step 5: Wire + run + verify pass**

Add `src/dsp/spine/SpineFilterSlot.cpp` to both targets, then:
Run: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests`
Expected: `0 failed`.

- [ ] **Step 6: Commit**

```bash
git add src/dsp/spine/SpineFilterSlot.h src/dsp/spine/SpineFilterSlot.cpp tests/SpineFilterTests.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(spine): per-voice SpineFilterSlot (single active model)"
```

---

### Task 6: Spine parameters

Add the new spine params and snapshot fields. Reuse `id.filterCutoff/filterResonance/filterType` (D2); add `id.spineModel/spineSeparation/spineSlope/spineDrive/spineOutput`.

**Files:**
- Modify: `src/params/Parameters.h` (LayerIds + new ids), `src/params/Parameters.cpp` (buildIds, createLayout, snapshot), `src/params/ParamSnapshot.h`
- Modify: `tests/ParamSnapshotTests.cpp`

- [ ] **Step 1: Write the failing test** (append a case to `tests/ParamSnapshotTests.cpp`'s `runTest()`)

```cpp
        beginTest("snapshot carries spine model + separation");
        {
            K2000AudioProcessor p;
            auto& apvts = p.apvts();
            const auto& id = params::layerIds(0);
            apvts.getParameter(id.spineSeparation)->setValueNotifyingHost(
                apvts.getParameter(id.spineSeparation)->convertTo0to1(0.5f));
            auto s = params::snapshot(apvts, 0);
            expectWithinAbsoluteError(s.spineSeparationOct, 0.5f, 0.01f);
            expectEquals(s.spineModel, 0);
        }
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --target k2000_tests -j4` → FAIL: `spineSeparation` / `spineSeparationOct` undefined.

- [ ] **Step 3: Add the ids** — in `src/params/Parameters.h`, extend the `LayerIds` struct fields list with:

```cpp
                 spineModel, spineSeparation, spineSlope, spineDrive, spineOutput;
```

(append to the existing `juce::String …;` member declaration).

- [ ] **Step 4: Build the ids** — in `src/params/Parameters.cpp` `buildIds()`, after the existing filter ids, add:

```cpp
    id.spineModel      = p + "spine.filterModel";
    id.spineSeparation = p + "spine.separation";
    id.spineSlope      = p + "spine.slope";
    id.spineDrive      = p + "spine.drive";
    id.spineOutput     = p + "spine.output";
```

- [ ] **Step 5: Add layout entries** — in `createLayout()`, inside the per-layer loop (after the routing block), add:

```cpp
        layout.add(std::make_unique<ChoiceParam>(juce::ParameterID{id.spineModel, 1},
            "Spine Filter " + juce::String(i), algoNamesSpine(), 0));
        layout.add(std::make_unique<ChoiceParam>(juce::ParameterID{id.spineSlope, 1},
            "Spine Slope " + juce::String(i), juce::StringArray{"12 dB", "24 dB"}, 1));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.spineSeparation, 1},
            "Spine Separation " + juce::String(i),
            juce::NormalisableRange<float>{-2.0f, 2.0f, 0.0f}, 0.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.spineDrive, 1},
            "Spine Drive " + juce::String(i),
            juce::NormalisableRange<float>{0.0f, 1.0f, 0.0f}, 0.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.spineOutput, 1},
            "Spine Output " + juce::String(i),
            juce::NormalisableRange<float>{-24.0f, 24.0f, 0.0f}, 0.0f));
```

Add a free helper next to `algoNames()` in `Parameters.cpp` (and declare it in `Parameters.h`):

```cpp
juce::StringArray algoNamesSpine() { return FilterModelLibrary::names(); }
```

Add `#include "../dsp/spine/FilterModelLibrary.h"` to `Parameters.cpp`.

- [ ] **Step 6: Add snapshot fields** — in `src/params/ParamSnapshot.h`, add:

```cpp
    // Spine filter (layer.spine.*)
    int   spineModel         = 0;
    float spineSeparationOct = 0.0f;
    int   spineSlope         = 1;   // 0=12 dB, 1=24 dB
    float spineDrive         = 0.0f;
    float spineOutputDb      = 0.0f;
```

In `Parameters.cpp` `snapshot()`, add:

```cpp
    s.spineModel         = (int) raw(apvts, id.spineModel);
    s.spineSeparationOct = raw(apvts, id.spineSeparation);
    s.spineSlope         = (int) raw(apvts, id.spineSlope);
    s.spineDrive         = raw(apvts, id.spineDrive);
    s.spineOutputDb      = raw(apvts, id.spineOutput);
```

- [ ] **Step 7: Run to verify it passes**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests`
Expected: `0 failed`.

- [ ] **Step 8: Commit**

```bash
git add src/params/ tests/ParamSnapshotTests.cpp
git commit -m "feat(spine): spine params (model/slope/separation/drive/output) + snapshot"
```

---

### Task 7: Layer owns the spine model; Voice renders stereo through it

The Layer creates the active model from `spineModel` and configures it each block; the Voice holds a `SpineFilterSlot` and renders stereo.

**Files:**
- Modify: `src/Layer.h`, `src/Layer.cpp`, `src/Voice.h`, `src/Voice.cpp`

- [ ] **Step 1: Layer holds + configures the model** — in `src/Layer.h` add includes `"dsp/spine/FilterModel.h"` and `"dsp/spine/SpineFilterSlot.h"`, and members:

```cpp
    const FilterModel* spineModel() const { return spineModel_.get(); }
private:
    std::unique_ptr<FilterModel> spineModel_;
    std::size_t spineModelId_ = SIZE_MAX;   // forces first build
    HuggettFilter* huggett_ = nullptr;      // non-owning view when model 0 is active
```

(Include `"dsp/spine/HuggettFilter.h"` and `"dsp/spine/FilterModelLibrary.h"`.)

- [ ] **Step 2: Configure in `Layer::updateParameters`** — in `src/Layer.cpp`, after storing the snapshot, add:

```cpp
    if (snapshot_.spineModel != (int) spineModelId_) {
        spineModelId_ = (std::size_t) snapshot_.spineModel;
        spineModel_ = FilterModelLibrary::create(spineModelId_);
        spineModel_->prepare(sampleRate_);
        huggett_ = dynamic_cast<HuggettFilter*>(spineModel_.get());
    }
    if (huggett_) {
        huggett_->setCommon(snapshot_.svfCutoffHz, snapshot_.svfResonance, snapshot_.spineDrive);
        huggett_->setMode(static_cast<HuggettFilter::Mode>(juce::jlimit(0, 2, snapshot_.svfType)));
        huggett_->setSlope(snapshot_.spineSlope == 0 ? HuggettFilter::Slope::db12 : HuggettFilter::Slope::db24);
        huggett_->setSeparation(snapshot_.spineSeparationOct);
    }
```

Store `sampleRate_` in `Layer::prepare` (add a `double sampleRate_ = 44100.0;` member and set it). Build the model once in `prepare` too, so a voice prepared before the first `updateParameters` has a model:

```cpp
    sampleRate_ = sampleRate;
    spineModelId_ = 0;
    spineModel_ = FilterModelLibrary::create(0);
    spineModel_->prepare(sampleRate_);
    huggett_ = dynamic_cast<HuggettFilter*>(spineModel_.get());
```

- [ ] **Step 3: Voice holds the slot + renders stereo** — in `src/Voice.h`, change `render` and add the slot:

```cpp
    void render(float* outL, float* outR, int numSamples);
private:
    SpineFilterSlot spine_;
    std::vector<float> scratchR_;   // stereo scratch
```

Include `"dsp/spine/SpineFilterSlot.h"`.

- [ ] **Step 4: Implement stereo render** — in `src/Voice.cpp`:

In `prepare`, after sizing `scratch_`, add `scratchR_.assign(maxBlock, 0.0f);` and `spine_.prepare(sr, layer_ ? layer_->spineModel() : nullptr);`. In `reset` and `noteOn`, add `spine_.reset();`.

Replace `render`:

```cpp
void Voice::render(float* outL, float* outR, int numSamples) {
    if (!isActive() || !layer_) return;
    const auto& s   = layer_->snapshot();
    const auto& alg = layer_->activeAlgorithm();

    const float tune = s.oscCoarse + s.oscFine * 0.01f;
    const float hz = midiToHz(note_) * std::pow(2.0f, tune / 12.0f);
    osc_.setFrequency(hz);
    osc_.setWaveform(static_cast<Oscillator::Waveform>(s.oscWaveform));
    amp_.setParameters(s.ampAttackS, s.ampDecayS, s.ampSustain, s.ampReleaseS);

    jassert(numSamples <= (int) scratch_.size());
    float* tmpL = scratch_.data();
    float* tmpR = scratchR_.data();
    osc_.processBlock(tmpL, numSamples);

    for (std::size_t i = 0; i < alg.slotCount; ++i) {
        const BlockTypeId t = alg.blockTypePerSlot[i];
        layer_->block(t).process(*blockStates_[(int) t], tmpL, numSamples);
    }
    // Mono graph → stereo spine input (dual mono; L/R diverge in later phases).
    std::copy(tmpL, tmpL + numSamples, tmpR);
    spine_.processStereo(tmpL, tmpR, numSamples);

    const float lvl = layer_->level();
    const float spineOut = juce::Decibels::decibelsToGain(s.spineOutputDb);
    for (int i = 0; i < numSamples; ++i) {
        const float env = amp_.nextSample() * velocity_ * lvl * spineOut;
        outL[i] += tmpL[i] * env;
        outR[i] += tmpR[i] * env;
    }
}
```

Add `#include <algorithm>` and `#include <juce_audio_basics/juce_audio_basics.h>` (for `Decibels`) if not present.

- [ ] **Step 5: Build (compile-only; VoiceManager updated next task)**

Run: `cmake --build build --target k2000_tests -j4 2>&1 | tail -5`
Expected: errors only in `VoiceManager` (caller of `render`) — fixed in Task 8. (If the spine/Voice units themselves error, fix here.)

- [ ] **Step 6: Commit**

```bash
git add src/Layer.h src/Layer.cpp src/Voice.h src/Voice.cpp
git commit -m "feat(spine): Layer owns spine model; Voice renders stereo through it"
```

---

### Task 8: Stereo render path (VoiceManager + processor)

**Files:**
- Modify: `src/VoiceManager.h`, `src/VoiceManager.cpp`, `src/PluginProcessor.h`, `src/PluginProcessor.cpp`

- [ ] **Step 1: VoiceManager renders stereo** — change `renderBlock` signature in `src/VoiceManager.h` to `void renderBlock(float* outL, float* outR, int numSamples, juce::MidiBuffer& midi);` and in `src/VoiceManager.cpp` pass both channels to each `voice.render(outL, outR, n)`. (Mirror the existing per-voice loop; each active voice adds into both buffers.)

- [ ] **Step 2: Processor renders into stereo scratch** — in `src/PluginProcessor.h`, replace `std::vector<float> monoScratch_;` with `std::vector<float> scratchL_, scratchR_;`. In `prepareToPlay`, `scratchL_.assign(samplesPerBlock, 0.0f); scratchR_.assign(samplesPerBlock, 0.0f);`.

- [ ] **Step 3: Update `processBlock`** — replace the render + fan-out section:

```cpp
    jassert((int) scratchL_.size() >= n);
    std::fill(scratchL_.begin(), scratchL_.begin() + n, 0.0f);
    std::fill(scratchR_.begin(), scratchR_.begin() + n, 0.0f);
    voiceManager_.renderBlock(scratchL_.data(), scratchR_.data(), n, midi);

    const float gainLin = juce::Decibels::decibelsToGain(masterDb);
    for (int c = 0; c < outCh; ++c) {
        float* ch = buffer.getWritePointer(c);
        const float* src = (c == 1 && outCh > 1) ? scratchR_.data() : scratchL_.data();
        for (int i = 0; i < n; ++i) ch[i] = src[i] * gainLin;
    }
```

- [ ] **Step 4: Build + run all tests**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests`
Expected: builds clean; existing voice/multilayer tests still `0 failed`. (Voice/VoiceManager tests that called the mono `render`/`renderBlock` need their calls updated to the stereo signature — update them to pass two buffers and assert on `outL`.)

- [ ] **Step 5: Commit**

```bash
git add src/VoiceManager.h src/VoiceManager.cpp src/PluginProcessor.h src/PluginProcessor.cpp tests/
git commit -m "feat(spine): stereo render path (VoiceManager + processor)"
```

---

### Task 9: Retire the graph filter + preset migration

Drop `SvfFilter` from every `AlgorithmLibrary` entry (indices preserved) and add the v4→v5 schema shim.

**Files:**
- Modify: `src/dsp/AlgorithmLibrary.cpp`, `tests/AlgorithmTests.cpp`, `src/PluginProcessor.cpp`, `tests/PresetMigrationTests.cpp`

- [ ] **Step 1: Update the algorithm-library test first** — in `tests/AlgorithmTests.cpp`, change the entry-0 expectation: entry 0 (`filter_then_shaper`) now has `slotCount == 1` and slot 0 is `BlockTypeId::Waveshaper` (the filter moved to the spine). Keep the id `"filter_then_shaper"` stable. Adjust the other entries' expectations to drop `SvfFilter` similarly.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests` → AlgorithmLibrary test FAILS (still has the filter).

- [ ] **Step 3: Remove `SvfFilter` from the entries** — in `src/dsp/AlgorithmLibrary.cpp`, rewrite each `make(...)` so no entry includes `BlockTypeId::SvfFilter`. Keep ids and order (stable indices); update display names (e.g. `"Filter \xE2\x86\x92 Shaper"` → `"Shaper"`). Entries that become empty collapse to the `thru`-equivalent (slotCount 0).

- [ ] **Step 4: Add the migration shim** — in `src/PluginProcessor.cpp`, add after `migrateV3ToV4`:

```cpp
// v4->v5: filtering moves from the graph SvfFilter into the always-on spine.
// The reused filter.* ids keep their names (no rename); new spine.* params take
// their layout defaults. This shim is a marker for the version bump — the
// algorithm semantics changed in code, and presets keep their stored values.
void migrateV4ToV5(juce::XmlElement&) { /* no id rewrites needed (D2) */ }
```

In `setStateInformation`, add `if (v < 5) migrateV4ToV5(*paramsRoot);`. In `getStateInformation`, bump `root->setAttribute("v", 5);`.

- [ ] **Step 5: Add a migration test** — in `tests/PresetMigrationTests.cpp`, add a case: a `v=4` state with `layer0.filter.cutoff=500` loads under the current processor, and after load `params::snapshot(apvts,0).svfCutoffHz == 500` (the value now drives the spine) and `spineModel == 0`.

- [ ] **Step 6: Run to verify all pass**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests`
Expected: `0 failed`.

- [ ] **Step 7: Commit**

```bash
git add src/dsp/AlgorithmLibrary.cpp tests/AlgorithmTests.cpp src/PluginProcessor.cpp tests/PresetMigrationTests.cpp
git commit -m "feat(spine): retire graph SvfFilter; v4->v5 migration to the spine"
```

---

### Task 10: UI — rebind the FILTER section to the spine + model selector

**Files:**
- Modify: `src/PluginEditor.h`, `src/PluginEditor.cpp`

- [ ] **Step 1: Add the spine model + slope combos and separation knob** — in `src/PluginEditor.h`, in the filter section group, add `juce::ComboBox spineModel_, spineSlope_; juce::Label spineModelLbl_, spineSlopeLbl_; LabeledKnob spineSeparation_{ "Sep" };`.

- [ ] **Step 2: Populate + bind** — in `buildStaticControls()`, fill `spineModel_` from `params::algoNamesSpine()` and `spineSlope_` from `{"12 dB","24 dB"}`; add them to `filterSection_`. In `bindLayer(layer)`, add:

```cpp
    binder_.bind(spineModel_,            ids.spineModel);
    binder_.bind(spineSlope_,            ids.spineSlope);
    binder_.bind(spineSeparation_.slider(), ids.spineSeparation);
```

(The existing cutoff/res/type binds already point at the reused `filter.*` ids, which now drive the spine — no change needed there.)

- [ ] **Step 3: Lay them out** — in `resized()`, extend the filter section's `layoutCells` row to include the model combo, slope combo, and separation knob alongside cutoff/reso.

- [ ] **Step 4: Build the plugin + Standalone**

Run: `cmake --build build --target k2000_VST3 k2000_Standalone -j4 2>&1 | tail -3`
Expected: builds clean.

- [ ] **Step 5: Commit**

```bash
git add src/PluginEditor.h src/PluginEditor.cpp
git commit -m "feat(spine): UI — spine model selector + slope + separation in the Filter section"
```

---

### Task 11: Full verification + version surface

- [ ] **Step 1: Full suite + plugin build**

Run: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --target k2000_tests k2000_VST3 k2000_Standalone -j4 && ./build/tests/k2000_tests`
Expected: all targets build; `Summary: … 0 failed`.

- [ ] **Step 2: Bump the version surface** (memory: release-version-surface) — set `project(k2000 VERSION 5.0.0 …)` in `CMakeLists.txt` (the panel title derives from `JucePlugin_VersionString`). Rebuild so the title reads `v5.0.0`.

- [ ] **Step 3: Manual smoke** (Windows via CI, per ADR-0003): load in a host; confirm the spine filters every voice, the model selector reads "Huggett", separation/slope respond, and a v4 preset loads with its old cutoff now driving the spine.

- [ ] **Step 4: Update the spec status** — in `docs/specs/README.md`, mark the v5 spec row's progress (e.g. "In progress — Plan 1 (foundation) implemented").

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt docs/specs/README.md
git commit -m "chore(release): v5 spine filter foundation (Plan 1) + version 5.0.0"
```

---

## Self-Review

**Spec coverage (Plan-1 slice):** `FilterModel`/`FilterModelLibrary`/`SpineFilterSlot` (Tasks 2,4,5) ✓ · common-core + per-model banks param model (Task 6, reusing filter.* per D2) ✓ · Huggett dual TPT + modes/slope/separation, linear (Tasks 1,3; Q13/Q14) ✓ · stereo per-Layer (Tasks 7,8; Q1) ✓ · promotion out of the palette + migration (Task 9; Q16) ✓ · UI (Task 10; L5) ✓. **Deferred by design:** nonlinear stages/Q12 (Plan 2), live hot-swap/Q17 (Plan 3) — `SpineFilterSlot` reserves the seam.

**Placeholder scan:** every code step shows complete code; commands show expected output; the `migrateV4ToV5` no-op is intentional and documented (D2). ✓

**Type consistency:** `FilterModel` methods (`prepare/makeState/reset/setCommon/processStereo`) are used identically in `HuggettFilter`, `SpineFilterSlot`, and `Voice`. `TptSvfCell::process(l,r,tap)` and taps `LP/HP/BP/Notch` match across Tasks 1/3. Param ids (`spineModel/spineSeparation/spineSlope/spineDrive/spineOutput`) match between `Parameters.h`, `createLayout`, `snapshot`, `ParamSnapshot`, `Layer`, and `PluginEditor`. ✓

**Note for the implementer:** Tasks 7–9 touch existing voice/render/migration tests; update their call sites to the stereo `render`/`renderBlock` signatures as you go (the plan flags this in Task 8 Step 4).
