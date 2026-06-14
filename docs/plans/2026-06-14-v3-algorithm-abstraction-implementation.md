# v3 (Algorithm abstraction) implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the per-voice DSP chain a *selectable algorithm* — an ordered walk through a per-Layer palette of block instances — shipping a 4-entry library, a `layer.algorithm` selector, a per-block-type parameter namespace with a cumulative v1→v2→v3 preset migration, and the taxonomy docs.

**Architecture:** A `Layer` owns a **palette** (one instance per block type: filter, shaper), each configured from its own param namespace. An `Algorithm` is passive data: an ordered list of block *types*. The `AlgorithmLibrary` is an append-only static array of algorithms. A `Voice` holds a `VoiceState` per block type and renders by walking the selected algorithm's type list, processing through `layer.block(type)`. Selecting an algorithm only changes which ordered list is walked — no reallocation, no parameter churn.

**Tech Stack:** JUCE 8.0.4, C++17, CMake, JUCE `UnitTest` framework, ctest. Builds with `cmake --build build -j4` (bounded — bare `-j` OOMs JUCE on this box).

**Spec:** [docs/specs/2026-06-14-v3-algorithm-abstraction-design.md](../specs/2026-06-14-v3-algorithm-abstraction-design.md)

**Note on green builds:** the structural refactor (Tasks 2–4) keeps the existing `layer.slot*` param IDs and a default algorithm, so audio stays identical and all tests stay green. The param rename + migration land together in Task 5 (the v2 lesson: rename and shim in one step).

---

### Task 1: Docs — ADR 0008 + algorithm taxonomy

**Files:**
- Create: `docs/decisions/0008-algorithm-selection-and-param-namespace.md`
- Create: `docs/architecture/algorithm-taxonomy.md`

Existing ADRs run `0001`–`0007`; follow the same short form (Context / Decision / Consequences).

- [ ] **Step 1: Write ADR 0008**

````markdown
# ADR 0008 — Algorithm selection: palette model + semantic param namespace

**Status:** Accepted, 2026-06-14. Effective from v3.

## Context

v3 makes the DSP chain a selectable algorithm. Two questions follow: how is an
algorithm represented at runtime, and what does a parameter belong to once
algorithms can reorder blocks?

The K2000 model (Musician's Guide pp. 47–48, 253, via the k2000-kb): an
algorithm is a fixed "wiring" of DSP functions assigned to positional blocks
`F1`–`F4`, pitch always first and amplitude always last, and a single algorithm
**can repeat a function category** (e.g. two filters).

## Decision

- **Palette + ordered algorithm.** A `Layer` owns one instance of each block
  type (the palette). An `Algorithm` is passive data — an ordered list of block
  *types*. A `Voice` walks that list, processing through the palette block for
  each type using its own per-voice state. Algorithms differ by data, not code
  (extends ADR 0006). Order is the array order; ADR 0006's never-implemented
  `render_order` field is dropped.
- **Semantic parameter namespace.** Parameters are keyed by block type
  (`layer.filter.*`, `layer.shaper.*`), stable across algorithm selection. This
  assumes **one instance of each block type per algorithm**.
- **Append-only `AlgorithmLibrary`** so the `layer.algorithm` choice index stays
  preset-stable.

## Consequences

- Selecting an algorithm is an index change — no reallocation, no parameter
  add/remove; APVTS stays fixed.
- The one-instance-per-type assumption is **not** K2000-faithful: K2000 allows
  duplicate function categories per algorithm. v3's semantic namespace is a
  deliberate simplification (v3's palette has no duplicates). The
  positional/per-F-block model with union param registration is required by v7;
  budget a v3→v7 parameter migration then. See the roadmap "Resolved questions".
- Param IDs change `layer.slot0.* / layer.slot1.* → layer.filter.* / layer.shaper.*`;
  a cumulative v1→v2→v3 migration shim keeps old presets loading.
````

- [ ] **Step 2: Write the taxonomy architecture doc**

Create `docs/architecture/algorithm-taxonomy.md` covering, in prose scaled to each point:
- **Our model:** palette (one block instance per type), `Algorithm` as an ordered block-type list, the append-only `AlgorithmLibrary`, and the Voice walk. State the one-instance-per-type constraint.
- **The K2000 model (sourced):** algorithm = fixed wiring of DSP functions; 31 algorithms; positional `F1`–`F4` blocks; pitch-first / amp-last; split/parallel "double-output" wiring; a category may repeat; the function categories list (FILTERS, EQ, PITCH/AMP/PAN, MIXERS, WAVEFORMS, ADDED WAVEFORMS, NON-LINEAR FUNCTIONS, WAVEFORMS WITH NON-LINEAR INPUTS, MIXERS WITH NON-LINEAR INPUTS, HARD SYNC). Cite "K2000 Series Musician's Guide, pp. 47–48, 253."
- **Where v3 simplifies:** linear-only wiring (graph routing → v4+), block-type addressing instead of positional (duplicate blocks → v7), 2 block types instead of ~10.
- Link ADR 0008 and the roadmap "Resolved questions" entry.

- [ ] **Step 3: Commit**

```bash
cd ~/dev/k2000
git add docs/decisions/0008-algorithm-selection-and-param-namespace.md docs/architecture/algorithm-taxonomy.md
git commit -m "docs(adr): ADR 0008 + algorithm taxonomy (palette model, K2000 mapping)"
```

---

### Task 2: `Algorithm` identity + `AlgorithmLibrary`

**Files:**
- Modify: `src/dsp/Algorithm.h`
- Create: `src/dsp/AlgorithmLibrary.h`
- Create: `src/dsp/AlgorithmLibrary.cpp`
- Modify: `tests/AlgorithmTests.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Extend the `Algorithm` struct**

Replace `src/dsp/Algorithm.h` with:

```cpp
#pragma once
#include <array>
#include <cstddef>

// Stable identifier for the DSP block type sitting in a slot.
// New entries appended to the end as block types are added in v5+.
enum class BlockTypeId : int {
    None        = 0,
    SvfFilter   = 1,
    Waveshaper  = 2,
};

// Number of BlockTypeId values (used to size per-type arrays indexed by the
// enum value). Bump when a new BlockTypeId is appended.
inline constexpr std::size_t kNumBlockTypes = 3;

// Passive data describing a per-voice DSP topology: an ordered list of block
// TYPES. Order is the array order (blockTypePerSlot[0..slotCount)). The Voice
// walks the list, processing through the Layer's palette block for each type.
struct Algorithm {
    static constexpr std::size_t kMaxSlots = 4;

    const char* id          = "";   // stable, serialised via the choice index
    const char* displayName = "";   // shown in the layer.algorithm combo
    std::size_t slotCount   = 0;
    std::array<BlockTypeId, kMaxSlots> blockTypePerSlot {};
};
```

(Note: `Algorithm::v1Fixed()` is gone; the equivalent is `AlgorithmLibrary` entry 0, `filter_then_shaper`. Layer's reference to `v1Fixed()` is removed in Task 3.)

- [ ] **Step 2: Write the failing library test**

Replace `tests/AlgorithmTests.cpp` with:

```cpp
#include <juce_core/juce_core.h>
#include "../src/dsp/AlgorithmLibrary.h"

class AlgorithmLibraryTests : public juce::UnitTest {
public:
    AlgorithmLibraryTests() : juce::UnitTest("AlgorithmLibrary") {}

    void runTest() override {
        beginTest("library has the 4 v3 algorithms; entry 0 is filter_then_shaper");
        expectEquals((int) AlgorithmLibrary::count(), 4);
        const Algorithm& a0 = AlgorithmLibrary::byIndex(0);
        expect(juce::String(a0.id) == "filter_then_shaper");
        expectEquals((int) a0.slotCount, 2);
        expect(a0.blockTypePerSlot[0] == BlockTypeId::SvfFilter);
        expect(a0.blockTypePerSlot[1] == BlockTypeId::Waveshaper);

        beginTest("ids are unique");
        for (std::size_t i = 0; i < AlgorithmLibrary::count(); ++i)
            for (std::size_t j = i + 1; j < AlgorithmLibrary::count(); ++j)
                expect(juce::String(AlgorithmLibrary::byIndex(i).id)
                       != juce::String(AlgorithmLibrary::byIndex(j).id));

        beginTest("every algorithm is well-formed: known types, no duplicate type");
        for (std::size_t i = 0; i < AlgorithmLibrary::count(); ++i) {
            const Algorithm& a = AlgorithmLibrary::byIndex(i);
            expect(a.slotCount <= Algorithm::kMaxSlots);
            bool seen[kNumBlockTypes] = {false, false, false};
            for (std::size_t s = 0; s < a.slotCount; ++s) {
                const int t = (int) a.blockTypePerSlot[s];
                expect(t > 0 && t < (int) kNumBlockTypes, "block type in palette");
                expect(!seen[t], "no duplicate block type within an algorithm (v3 constraint)");
                seen[t] = true;
            }
        }

        beginTest("thru algorithm is empty");
        const Algorithm& thru = AlgorithmLibrary::byIndex(AlgorithmLibrary::indexOfId("thru"));
        expectEquals((int) thru.slotCount, 0);
    }
};

static AlgorithmLibraryTests algorithmLibraryTestsInstance;
```

- [ ] **Step 3: Add the new sources to CMake**

In `tests/CMakeLists.txt`, the test executable already lists `AlgorithmTests.cpp`; add `../src/dsp/AlgorithmLibrary.cpp` to the source list (alongside the other `../src/...` entries).

In `CMakeLists.txt`, add `src/dsp/AlgorithmLibrary.cpp` to the `target_sources(k2000 PRIVATE ...)` list.

- [ ] **Step 4: Run the test to verify it fails**

```bash
cd ~/dev/k2000
cmake -S . -B build >/dev/null && cmake --build build -j4 2>&1 | tail -5
```

Expected: compile error — `AlgorithmLibrary.h` doesn't exist.

- [ ] **Step 5: Implement `AlgorithmLibrary`**

Create `src/dsp/AlgorithmLibrary.h`:

```cpp
#pragma once
#include <cstddef>
#include "Algorithm.h"

// The v3 algorithm library: a fixed, APPEND-ONLY set of algorithms built from
// the {filter, shaper} palette. Append-only ordering keeps the layer.algorithm
// choice index preset-stable. See ADR 0008.
namespace AlgorithmLibrary {
    std::size_t      count();
    const Algorithm& byIndex(std::size_t i);   // clamps out-of-range to 0
    std::size_t      indexOfId(const char* id); // returns 0 if not found
}
```

Create `src/dsp/AlgorithmLibrary.cpp`:

```cpp
#include "AlgorithmLibrary.h"
#include <array>
#include <cstring>

namespace {
constexpr Algorithm make(const char* id, const char* name,
                         std::size_t n, BlockTypeId a, BlockTypeId b) {
    Algorithm alg;
    alg.id = id; alg.displayName = name; alg.slotCount = n;
    alg.blockTypePerSlot[0] = a; alg.blockTypePerSlot[1] = b;
    return alg;
}

// APPEND-ONLY. Do not reorder existing entries (choice index is serialised).
const std::array<Algorithm, 4> kAlgorithms = {{
    make("filter_then_shaper", "Filter \xE2\x86\x92 Shaper", 2, BlockTypeId::SvfFilter,  BlockTypeId::Waveshaper),
    make("shaper_then_filter", "Shaper \xE2\x86\x92 Filter", 2, BlockTypeId::Waveshaper, BlockTypeId::SvfFilter),
    make("filter_only",        "Filter only",      1, BlockTypeId::SvfFilter,  BlockTypeId::None),
    make("thru",               "Thru",             0, BlockTypeId::None,       BlockTypeId::None),
}};
}  // namespace

namespace AlgorithmLibrary {
std::size_t count() { return kAlgorithms.size(); }

const Algorithm& byIndex(std::size_t i) {
    return kAlgorithms[i < kAlgorithms.size() ? i : 0];
}

std::size_t indexOfId(const char* id) {
    for (std::size_t i = 0; i < kAlgorithms.size(); ++i)
        if (std::strcmp(kAlgorithms[i].id, id) == 0) return i;
    return 0;
}
}  // namespace AlgorithmLibrary
```

- [ ] **Step 6: Run tests to verify they pass**

```bash
cmake -S . -B build >/dev/null && cmake --build build -j4 2>&1 | tail -3
ctest --test-dir build --output-on-failure 2>&1 | tail -5
```

Expected: all tests pass (AlgorithmLibrary plus all existing — `Algorithm::v1Fixed()` is still referenced by `Layer.h` but unchanged, so the build is green).

- [ ] **Step 7: Commit**

```bash
git add src/dsp/Algorithm.h src/dsp/AlgorithmLibrary.h src/dsp/AlgorithmLibrary.cpp tests/AlgorithmTests.cpp tests/CMakeLists.txt CMakeLists.txt
git commit -m "feat(dsp): Algorithm gains id/displayName; append-only AlgorithmLibrary (4 algorithms)"
```

---

### Task 3: `Layer` palette model + `Voice` walk-by-type (behaviour-preserving)

**Files:**
- Modify: `src/Layer.h`
- Modify: `src/Layer.cpp`
- Modify: `src/Voice.h`
- Modify: `src/Voice.cpp`

This refactor keeps the current params and defaults to algorithm 0, so audio is identical and every test stays green.

- [ ] **Step 1: Rewrite `Layer.h` to own a palette + selected algorithm**

Replace `src/Layer.h` with:

```cpp
#pragma once
#include <array>
#include <memory>
#include "dsp/Algorithm.h"
#include "dsp/AlgorithmLibrary.h"
#include "dsp/DSPBlock.h"
#include "params/ParamSnapshot.h"

// Configuration container. Owns a PALETTE — one DSP block instance per block
// type — and the currently selected algorithm (an ordered list of block types).
// Voices read the palette and the active algorithm during render. See ADR 0008.
class Layer {
public:
    Layer();
    ~Layer() = default;

    void prepare(double sampleRate, int maxBlockSize);

    // RT-safe. Configures each palette block and selects the active algorithm.
    void updateParameters(const ParamSnapshot& snapshot);

    const Algorithm& activeAlgorithm() const { return AlgorithmLibrary::byIndex(activeAlgorithmId_); }

    bool hasBlock(BlockTypeId t) const { return palette_[(int) t] != nullptr; }
    DSPBlock&       block(BlockTypeId t)       { return *palette_[(int) t]; }
    const DSPBlock& block(BlockTypeId t) const { return *palette_[(int) t]; }

    const ParamSnapshot& snapshot() const { return snapshot_; }

private:
    // Indexed by BlockTypeId value; null where the type isn't in the palette.
    std::array<std::unique_ptr<DSPBlock>, kNumBlockTypes> palette_;
    std::size_t activeAlgorithmId_ = 0;
    ParamSnapshot snapshot_;
};
```

- [ ] **Step 2: Rewrite `Layer.cpp`**

Replace `src/Layer.cpp` with:

```cpp
#include "Layer.h"
#include "dsp/blocks/SVFFilter.h"
#include "dsp/blocks/Waveshaper.h"

Layer::Layer() {
    palette_[(int) BlockTypeId::SvfFilter]  = std::make_unique<SVFFilter>();
    palette_[(int) BlockTypeId::Waveshaper] = std::make_unique<Waveshaper>();
}

void Layer::prepare(double sr, int maxBlock) {
    for (auto& b : palette_)
        if (b) b->prepare(sr, maxBlock);
}

void Layer::updateParameters(const ParamSnapshot& s) {
    snapshot_ = s;
    activeAlgorithmId_ = (std::size_t) s.algorithmId;
    for (auto& b : palette_)
        if (b) b->updateParameters(s);
}
```

- [ ] **Step 3: Rewrite `Voice.h` to hold per-block-type state**

Replace the private members and includes of `src/Voice.h` so it keys VoiceState by block type. Full file:

```cpp
#pragma once
#include <array>
#include <memory>
#include <vector>
#include "dsp/Oscillator.h"
#include "dsp/Envelope.h"
#include "dsp/DSPBlock.h"
#include "dsp/Algorithm.h"

class Layer;  // forward

class Voice {
public:
    Voice();
    ~Voice() = default;

    void setLayer(Layer* layer) { layer_ = layer; }

    void prepare(double sampleRate, int maxBlockSize);
    void reset();

    void noteOn(int midiNote, float velocity);
    void noteOff();
    bool isActive() const;
    int  currentNote() const { return note_; }

    // RT-safe. Renders additively into `out`. Walks the Layer's active
    // algorithm, processing through the palette block for each block type.
    void render(float* out, int numSamples);

private:
    Layer* layer_ = nullptr;  // non-owning
    Oscillator osc_;
    Envelope amp_;

    // Per-block-TYPE voice-local state (indexed by BlockTypeId value).
    std::array<std::unique_ptr<DSPBlock::VoiceState>, kNumBlockTypes> blockStates_;

    int note_ = -1;
    float velocity_ = 0.0f;
    double sampleRate_ = 44100.0;
    std::vector<float> scratch_;

    static float midiToHz(int note);
};
```

- [ ] **Step 4: Rewrite `Voice.cpp`**

Replace `src/Voice.cpp` with:

```cpp
#include "Voice.h"
#include "Layer.h"
#include <cmath>

Voice::Voice() = default;

void Voice::prepare(double sr, int maxBlock) {
    sampleRate_ = sr;
    osc_.prepare(sr);
    amp_.prepare(sr);
    scratch_.assign(maxBlock, 0.0f);

    // One VoiceState per palette block type.
    if (layer_) {
        for (int t = 1; t < (int) kNumBlockTypes; ++t)
            if (layer_->hasBlock((BlockTypeId) t))
                blockStates_[t] = layer_->block((BlockTypeId) t).makeVoiceState();
    }
    reset();
}

void Voice::reset() {
    osc_.reset();
    amp_.reset();
    if (layer_)
        for (int t = 1; t < (int) kNumBlockTypes; ++t)
            if (blockStates_[t]) layer_->block((BlockTypeId) t).resetVoice(*blockStates_[t]);
    note_ = -1;
    velocity_ = 0.0f;
}

void Voice::noteOn(int midiNote, float velocity) {
    note_ = midiNote;
    velocity_ = velocity;
    osc_.reset();
    amp_.reset();
    if (layer_)
        for (int t = 1; t < (int) kNumBlockTypes; ++t)
            if (blockStates_[t]) layer_->block((BlockTypeId) t).resetVoice(*blockStates_[t]);
    amp_.noteOn();
}

void Voice::noteOff() { amp_.noteOff(); }
bool Voice::isActive() const { return amp_.isActive(); }

float Voice::midiToHz(int note) {
    return 440.0f * std::pow(2.0f, (note - 69) / 12.0f);
}

void Voice::render(float* out, int numSamples) {
    if (!isActive() || !layer_) return;

    const auto& s   = layer_->snapshot();
    const auto& alg = layer_->activeAlgorithm();

    const float tune = s.oscCoarse + s.oscFine * 0.01f;
    const float hz = midiToHz(note_) * std::pow(2.0f, tune / 12.0f);
    osc_.setFrequency(hz);
    osc_.setWaveform(static_cast<Oscillator::Waveform>(s.oscWaveform));
    amp_.setParameters(s.ampAttackS, s.ampDecayS, s.ampSustain, s.ampReleaseS);

    jassert(numSamples <= (int) scratch_.size());
    float* tmp = scratch_.data();
    osc_.processBlock(tmp, numSamples);

    for (std::size_t i = 0; i < alg.slotCount; ++i) {
        const BlockTypeId t = alg.blockTypePerSlot[i];
        layer_->block(t).process(*blockStates_[(int) t], tmp, numSamples);
    }

    for (int i = 0; i < numSamples; ++i)
        out[i] += tmp[i] * amp_.nextSample() * velocity_;
}
```

- [ ] **Step 5: Build and run the full suite**

```bash
cmake -S . -B build >/dev/null && cmake --build build -j4 2>&1 | tail -3
ctest --test-dir build --output-on-failure 2>&1 | tail -5
```

Expected: green. `ParamSnapshot::algorithmId` is read in `Layer::updateParameters` — it is added in Task 4, so until then this references a field that does not yet exist. **Therefore add the field now** as a one-line pre-req: in `src/params/ParamSnapshot.h`, add `int algorithmId = 0;` under a `// Algorithm selection` comment (Task 4 wires the param that fills it). With that field present and defaulting to 0, the active algorithm is `filter_then_shaper` and audio is identical to v2.

- [ ] **Step 6: Commit**

```bash
git add src/Layer.h src/Layer.cpp src/Voice.h src/Voice.cpp src/params/ParamSnapshot.h
git commit -m "refactor: Layer owns a block palette; Voice walks the active algorithm by block type

Behaviour-preserving: ParamSnapshot.algorithmId defaults to 0 (filter_then_shaper),
so audio is identical to v2. All tests green."
```

---

### Task 4: Wire the `layer.algorithm` selector + routing tests + editor combo

**Files:**
- Modify: `src/params/Parameters.h`
- Modify: `src/params/Parameters.cpp`
- Modify: `src/PluginEditor.h`
- Modify: `src/PluginEditor.cpp`
- Create: `tests/AlgorithmRoutingTests.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Add the `algorithm` param id**

In `src/params/Parameters.h`, inside `namespace id`, add after `masterGain`:

```cpp
    inline constexpr auto algorithm     = "layer.algorithm";
```

- [ ] **Step 2: Register the choice param and read it into the snapshot**

In `src/params/Parameters.cpp`, add at the top (after the `using` lines):

```cpp
#include "../dsp/AlgorithmLibrary.h"
```

Add this choice param to `createLayout()` (after the `masterGain` param, before `return layout;`):

```cpp
    juce::StringArray algoNames;
    for (std::size_t i = 0; i < AlgorithmLibrary::count(); ++i)
        algoNames.add(AlgorithmLibrary::byIndex(i).displayName);
    layout.add(std::make_unique<ChoiceParam>(
        juce::ParameterID{id::algorithm, 1}, "Algorithm", algoNames, 0));
```

In `snapshot()`, add before `return s;`:

```cpp
    s.algorithmId = (int) raw(apvts, id::algorithm);
```

- [ ] **Step 3: Add the editor combo**

In `src/PluginEditor.h`, add a `LabeledCombo algo;` member alongside the existing `oscWave` / `svfType` combos.

In `src/PluginEditor.cpp` constructor, after the existing `addCombo(...)` calls, add:

```cpp
    juce::StringArray algoItems;
    for (std::size_t i = 0; i < AlgorithmLibrary::count(); ++i)
        algoItems.add(AlgorithmLibrary::byIndex(i).displayName);
    addCombo(algo, "Algo", params::id::algorithm, algoItems);
```

Add `#include "dsp/AlgorithmLibrary.h"` at the top of `PluginEditor.cpp`. In `resized()`, give `algo` a bounds rectangle — add it as a cell to the master row, e.g. change the master row layout to:

```cpp
    layoutRow(area.removeFromTop(rowH),
              {{&algo.label, &algo.combo},
               {&masterGain.label, &masterGain.slider}});
```

- [ ] **Step 4: Write the routing tests**

Create `tests/AlgorithmRoutingTests.cpp`:

```cpp
#include <juce_core/juce_core.h>
#include <vector>
#include "../src/Layer.h"
#include "../src/Voice.h"
#include "../src/dsp/AlgorithmLibrary.h"
#include "../src/params/ParamSnapshot.h"

class AlgorithmRoutingTests : public juce::UnitTest {
public:
    AlgorithmRoutingTests() : juce::UnitTest("AlgorithmRouting") {}

    static constexpr double SR = 48000.0;
    static constexpr int N = 512;

    // Render one block of a held note under a given snapshot; return the buffer.
    static std::vector<float> renderOnce(const ParamSnapshot& s) {
        Layer layer; layer.prepare(SR, N); layer.updateParameters(s);
        Voice v; v.setLayer(&layer); v.prepare(SR, N); v.noteOn(60, 1.0f);
        std::vector<float> out(N, 0.0f);
        v.render(out.data(), N);
        return out;
    }

    static ParamSnapshot base() {
        ParamSnapshot s;
        s.oscWaveform = 0;                 // saw — harmonically rich
        s.svfType = 0; s.svfCutoffHz = 800.0f; s.svfResonance = 0.2f;
        s.wsDrive = 0.9f; s.wsMix = 1.0f;  // strong shaping so order matters
        s.ampAttackS = 0.0001f; s.ampDecayS = 0.05f;
        s.ampSustain = 1.0f; s.ampReleaseS = 0.05f;
        return s;
    }

    void runTest() override {
        beginTest("ordering matters: filter_then_shaper differs from shaper_then_filter");
        ParamSnapshot a = base(); a.algorithmId = (int) AlgorithmLibrary::indexOfId("filter_then_shaper");
        ParamSnapshot b = base(); b.algorithmId = (int) AlgorithmLibrary::indexOfId("shaper_then_filter");
        auto oa = renderOnce(a), ob = renderOnce(b);
        double diff = 0.0;
        for (int i = 0; i < N; ++i) diff += std::abs(oa[i] - ob[i]);
        expect(diff > 1e-3, "block order should change the output");

        beginTest("filter_only: shaper drive has no effect");
        ParamSnapshot c = base(); c.algorithmId = (int) AlgorithmLibrary::indexOfId("filter_only");
        c.wsDrive = 0.0f; auto c0 = renderOnce(c);
        c.wsDrive = 1.0f; auto c1 = renderOnce(c);
        double d = 0.0; for (int i = 0; i < N; ++i) d += std::abs(c0[i] - c1[i]);
        expectWithinAbsoluteError((float) d, 0.0f, 1e-5f);

        beginTest("thru: filter cutoff has no effect");
        ParamSnapshot t = base(); t.algorithmId = (int) AlgorithmLibrary::indexOfId("thru");
        t.svfCutoffHz = 200.0f;   auto t0 = renderOnce(t);
        t.svfCutoffHz = 18000.0f; auto t1 = renderOnce(t);
        double dt = 0.0; for (int i = 0; i < N; ++i) dt += std::abs(t0[i] - t1[i]);
        expectWithinAbsoluteError((float) dt, 0.0f, 1e-5f);
    }
};

static AlgorithmRoutingTests algorithmRoutingTestsInstance;
```

Add `AlgorithmRoutingTests.cpp` to the test executable sources in `tests/CMakeLists.txt`.

- [ ] **Step 5: Build and run**

```bash
cmake -S . -B build >/dev/null && cmake --build build -j4 2>&1 | tail -3
ctest --test-dir build --output-on-failure 2>&1 | tail -5
```

Expected: AlgorithmRouting passes; all existing tests stay green. The editor now has an Algorithm combo.

- [ ] **Step 6: Commit**

```bash
git add src/params/Parameters.h src/params/Parameters.cpp src/PluginEditor.h src/PluginEditor.cpp tests/AlgorithmRoutingTests.cpp tests/CMakeLists.txt
git commit -m "feat: layer.algorithm selector (choice param + editor combo) with routing tests"
```

---

### Task 5: Rename param IDs to `layer.filter.*` / `layer.shaper.*` + cumulative v3 migration

**Files:**
- Modify: `src/params/Parameters.h`
- Modify: `src/PluginProcessor.cpp`
- Modify: `tests/PresetMigrationTests.cpp`

- [ ] **Step 1: Rename the id constants**

In `src/params/Parameters.h`, change the slot-scoped ids (leave `masterGain`, `algorithm`, and the comment block; only the values change):

```cpp
    inline constexpr auto oscWaveform   = "layer.osc.waveform";
    inline constexpr auto oscCoarse     = "layer.osc.coarse";
    inline constexpr auto oscFine       = "layer.osc.fine";

    inline constexpr auto svfType       = "layer.filter.type";
    inline constexpr auto svfCutoff     = "layer.filter.cutoff";
    inline constexpr auto svfResonance  = "layer.filter.resonance";

    inline constexpr auto wsDrive       = "layer.shaper.drive";
    inline constexpr auto wsMix         = "layer.shaper.mix";
```

(The `osc.*` and `amp.*` ids keep their v2 `layer.osc.*` / `layer.amp.*` names — only the slot0/slot1 ids move to `filter`/`shaper`. Everything resolves through these constants, so no other call site changes.)

- [ ] **Step 2: Extend the migration shim to v3**

In `src/PluginProcessor.cpp`, add a second rename table next to `kV1ToV2Renames`:

```cpp
// v2 layer.slot* IDs → v3 block-type IDs. master.gain, layer.osc.*, layer.amp.*
// are unchanged across v2→v3.
constexpr struct { const char* from; const char* to; } kV2ToV3Renames[] = {
    {"layer.slot0.type",      "layer.filter.type"},
    {"layer.slot0.cutoff",    "layer.filter.cutoff"},
    {"layer.slot0.resonance", "layer.filter.resonance"},
    {"layer.slot1.drive",     "layer.shaper.drive"},
    {"layer.slot1.mix",       "layer.shaper.mix"},
};
```

Add a generic apply-table helper and a v3 migrate function (place beside `migrateV1ToV2`):

```cpp
template <typename Table>
void applyRenames(juce::XmlElement& paramsRoot, const Table& table) {
    for (auto* p : paramsRoot.getChildWithTagNameIterator("PARAM")) {
        const juce::String pid = p->getStringAttribute("id");
        for (const auto& r : table)
            if (pid == r.from) { p->setAttribute("id", r.to); break; }
    }
}
```

Rewrite `migrateV1ToV2` to delegate, and add `migrateV2ToV3`:

```cpp
void migrateV1ToV2(juce::XmlElement& paramsRoot) { applyRenames(paramsRoot, kV1ToV2Renames); }
void migrateV2ToV3(juce::XmlElement& paramsRoot) { applyRenames(paramsRoot, kV2ToV3Renames); }
```

In `getStateInformation`, bump the version attribute:

```cpp
    root->setAttribute("v", 3);  // schema version; gates the cumulative load shim
```

In `setStateInformation`, replace the single migration branch with the cumulative one:

```cpp
            const int v = xml->getIntAttribute("v", 1);
            if (v < 2) migrateV1ToV2(*paramsRoot);
            if (v < 3) migrateV2ToV3(*paramsRoot);
            apvts_.replaceState(juce::ValueTree::fromXml(*paramsRoot));
```

- [ ] **Step 3: Extend the migration test (v1→v3 and v2→v3)**

In `tests/PresetMigrationTests.cpp`, update the assertions to the v3 IDs and add a v2→v3 case. Change the first test's expectations:

```cpp
            auto* cutoff = tree.getRawParameterValue("layer.filter.cutoff");
            expect(cutoff != nullptr, "migrated cutoff id should resolve");
            expectWithinAbsoluteError(cutoff->load(), 1000.0f, 0.5f);

            auto* attack = tree.getRawParameterValue("layer.amp.attack");
            expect(attack != nullptr, "migrated attack id should resolve");
            expectWithinAbsoluteError(attack->load(), 0.01f, 0.001f);
```

The v1 fixture's `makeV1Preset` uses flat v1 ids (`osc.coarse`, `slot0.cutoff`, …) and no `v` attribute, so it exercises the full v1→v2→v3 chain. Add a v2 case after it:

```cpp
        beginTest("Loading a v2 preset migrates slot* IDs to filter/shaper");
        {
            K2000AudioProcessor proc;
            proc.prepareToPlay(48000.0, 256);

            juce::XmlElement root("K2000Root");
            root.setAttribute("v", 2);
            auto* wrapper = root.createNewChildElement("Params");
            auto* pr = wrapper->createNewChildElement("PARAMS");
            auto add = [&](const char* id, double val) {
                auto* p = pr->createNewChildElement("PARAM");
                p->setAttribute("id", id); p->setAttribute("value", val);
            };
            add("layer.slot0.cutoff", 2500.0);
            add("layer.slot1.drive", 0.4);
            juce::MemoryBlock mb;
            juce::AudioProcessor::copyXmlToBinary(root, mb);
            proc.setStateInformation(mb.getData(), (int) mb.getSize());

            auto& tree = proc.apvts();
            expectWithinAbsoluteError(
                tree.getRawParameterValue("layer.filter.cutoff")->load(), 2500.0f, 0.5f);
            expectWithinAbsoluteError(
                tree.getRawParameterValue("layer.shaper.drive")->load(), 0.4f, 0.001f);
        }
```

Update the existing "Saving from v2 sets v=2" test to expect `v=3` (the save now writes 3):

```cpp
            expectEquals(xml->getIntAttribute("v", 1), 3);
```

- [ ] **Step 4: Build and run the full suite**

```bash
cmake -S . -B build >/dev/null && cmake --build build -j4 2>&1 | tail -3
ctest --test-dir build --output-on-failure 2>&1 | tail -5
```

Expected: all tests green — PresetMigration covers v1→v3 and v2→v3; routing and lifecycle tests pass with the new IDs (they resolve through `params::id::`).

- [ ] **Step 5: Commit**

```bash
git add src/params/Parameters.h src/PluginProcessor.cpp tests/PresetMigrationTests.cpp
git commit -m "feat(params): rename slot IDs to layer.filter.*/layer.shaper.*; cumulative v1->v2->v3 preset shim"
```

---

### Task 6: Full suite, smoke, docs status

This task is operational — no new feature code.

- [ ] **Step 1: Full local suite**

```bash
cmake -S . -B build >/dev/null && cmake --build build -j4 2>&1 | tail -3
ctest --test-dir build --output-on-failure
```

Expected: every test green.

- [ ] **Step 2: Manual smoke (standalone or Carla on Linux)**

```bash
cmake --install build 2>/dev/null || true
# Launch the standalone (build/k2000_artefacts/Debug/Standalone/k2000) or load
# the VST3 in Carla. Confirm: sound on noteOn; the Algorithm combo switches
# between Filter→Shaper / Shaper→Filter / Filter only / Thru with audible
# differences (drive-before-vs-after-filter, thru = clean osc). Move filter and
# drive controls. Save/reload host state; confirm params persist.
```

If anything is off (no sound, selector inert, params not persisting), stop and investigate before pushing.

- [ ] **Step 3: Trigger the Windows CI artifact and smoke in Ableton**

```bash
git push origin main   # windows-only CI builds the artifact
# Download k2000-windows-<sha>, load in Ableton, confirm the same behaviour.
```

- [ ] **Step 4: Mark docs status**

In `docs/roadmap/phases.md`, change the v3 row status from no mark to `✅` with "**Shipped <date> as v3.0.0.**" and a link to the v3 spec. In `docs/specs/2026-06-14-v3-algorithm-abstraction-design.md`, change `**Status:** Design proposed, 2026-06-14.` to `**Status:** Implemented (tagged v3.0.0 on <date>).` In `docs/specs/README.md`, change the v3 row status to `Implemented (v3.0.0)`. In the root `README.md`, move the Status section to lead with v3 and set "Next — v4".

- [ ] **Step 5: Tag and push (after smoke passes)**

```bash
git add docs/roadmap/phases.md docs/specs/2026-06-14-v3-algorithm-abstraction-design.md docs/specs/README.md README.md
git commit -m "docs: mark v3 as shipped"
git tag -a v3.0.0 -m "v3.0.0 — Algorithm abstraction"
git push origin main --follow-tags
```

---

## Self-review (done)

- **Spec coverage:** palette + ordered-algorithm model → Tasks 2–3. Library (4 algorithms, append-only) → Task 2. Selection mechanism + RT behaviour + editor → Task 4. Semantic param namespace + cumulative migration → Task 5. ADR 0008 + taxonomy doc → Task 1. Routing/migration/behaviour-preservation tests → Tasks 2, 4, 5. Docs status + tag → Task 6.
- **Placeholder scan:** no TBD/TODO; every code step shows complete code. The taxonomy doc (Task 1 Step 2) is described by required contents rather than verbatim prose — acceptable for a narrative doc, with the sourced facts and citations enumerated.
- **Type consistency:** `BlockTypeId`, `kNumBlockTypes`, `Algorithm{id,displayName,slotCount,blockTypePerSlot}`, `AlgorithmLibrary::{count,byIndex,indexOfId}`, `Layer::{activeAlgorithm,hasBlock,block,snapshot}`, `Voice::blockStates_`, `ParamSnapshot::algorithmId`, `params::id::algorithm`, and the rename tables (`kV1ToV2Renames`/`kV2ToV3Renames`) are consistent across tasks. `ParamSnapshot.algorithmId` is introduced in Task 3 Step 5 (pre-req) and consumed in Task 4.
- **Ordering for green builds:** structural refactor (2–3) keeps v2 params + default algorithm; selector (4) adds the new param; rename+shim land together (5). No task leaves the suite red.
