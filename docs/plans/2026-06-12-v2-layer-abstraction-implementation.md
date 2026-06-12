# v2 (Layer abstraction) implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refactor v1's `Voice` into the K2000-faithful `Voice`/`Layer` split, introduce `Program` and `Algorithm` placeholders, and migrate the parameter namespace to `layer.*` — without changing audio behavior.

**Architecture:** `Voice` becomes per-note runtime state. `Layer` owns the algorithm topology, the DSP block instances, the `ParamSnapshot`, and the per-Layer envelope/oscillator config. `Algorithm` is a passive data struct describing topology. `Program` is a placeholder container that owns exactly one `Layer` at v2 (gains real content at v4). Each `Voice` holds a pointer to its `Layer` and per-block `VoiceState` for stateful blocks (SVF integrators).

**Tech Stack:** JUCE 8.0.4, C++17, CMake, JUCE `UnitTest` framework, ctest. Existing scaffolding from v1.

**Spec:** [docs/specs/2026-06-11-v2-layer-abstraction-design.md](../specs/2026-06-11-v2-layer-abstraction-design.md)

---

### Task 1: Three ADRs documenting v2 decisions

**Files:**
- Create: `docs/decisions/0005-voice-layer-split.md`
- Create: `docs/decisions/0006-algorithm-as-passive-data.md`
- Create: `docs/decisions/0007-param-namespace-and-v1-preset-shim.md`

Existing ADRs are at `docs/decisions/0001-juce-framework.md` through `0004-defer-photoreal-ui.md`. Follow the same short-form style (~1 page each: Context, Decision, Consequences).

- [ ] **Step 1: Write ADR 0005 — Voice/Layer split**

```markdown
# ADR 0005 — Voice/Layer split

**Status:** Accepted, 2026-06-12. Effective from v2.

## Context

v1 conflated two K2000 concepts in a single `Voice` class:
- A *Layer* is a configuration: an algorithm + DSP blocks + envelope + ParamSnapshot. It does not by itself play notes.
- A *Voice* is a runtime instance playing a particular note through a Layer. It holds note state and per-block integrator state.

In v1, `Voice` owned the block instances, which works for one voice but breaks the conceptual model: when v4 multi-Layer Programs arrive, two Voices playing the same Layer should share that Layer's config but each have their own integrators.

## Decision

Split `Voice` into:
- `Layer` — owns the algorithm topology, DSP block instances, ParamSnapshot, and envelope config. Stateless w.r.t. per-note rendering.
- `Voice` — owns note state, ADSR envelope position, and a small per-block `VoiceState` struct for each stateful block in its Layer's algorithm.

A `Voice` holds a non-owning pointer to its `Layer`. With one Layer at v2, all Voices point at the same one.

## Consequences

- v4 multi-Layer work is a clean add-on: Voices already know which Layer they play.
- Block-state-per-voice extraction (Task 3 of v2 plan) is required: SVFFilter's integrators must move out of the block instance.
- v1's preset format is forward-compatible because slot type IDs were already saved; only param IDs change (handled by the v1-preset shim, ADR 0007).
```

- [ ] **Step 2: Write ADR 0006 — Algorithm as passive data**

```markdown
# ADR 0006 — Algorithm as passive data

**Status:** Accepted, 2026-06-12. Effective from v2.

## Context

At v3 the plugin will support selecting from a library of algorithms. Each algorithm describes a routing topology (slot order, block type per slot) for the per-voice DSP chain. Two ways to represent this:

A) **Active class.** `Algorithm` is an abstract base with a virtual `render(voice, layer, out, n)` per subclass. Each algorithm in the library is a concrete class.

B) **Passive data.** `Algorithm` is a struct of `{ slot_count, block_type_per_slot, render_order }`. The Voice walks the struct and calls each slot's `process()`. Algorithms differ by data, not by code.

## Decision

Passive data (option B). At v2 there's one algorithm, but the struct shape is in place for v3.

## Consequences

- Adding an algorithm in v3 is "add a record"; no new vtable, no new translation unit.
- Algorithms with radically different routing topologies (parallel branches, feedback) will need extension to the struct (e.g., a small flow-graph encoding) — accepted as v4+ work.
- Voice rendering stays branch-light: walk the array, call each block.
```

- [ ] **Step 3: Write ADR 0007 — Param namespace and v1 preset shim**

```markdown
# ADR 0007 — Param namespace and v1 preset shim

**Status:** Accepted, 2026-06-12. Effective from v2.

## Context

v1 param IDs are flat: `osc.coarse`, `slot0.cutoff`, `amp.attack`, `master.gain`. v2 introduces the Layer abstraction; v4 will introduce multi-Layer Programs. If params stay flat through v2, the v4 rename has to touch every ID and every preset breaks.

## Decision

Move all Layer-scoped params under a `layer.*` namespace at v2. Examples:
- `osc.coarse` → `layer.osc.coarse`
- `slot0.cutoff` → `layer.slot0.cutoff`
- `amp.attack` → `layer.amp.attack`
- `master.gain` *stays* `master.gain` (it lives downstream of the Program mix).

At v4 the prefix becomes `layer[0].*`, `layer[1].*` — additive, no further rename.

A v1 → v2 preset migration shim runs in `setStateInformation`. It checks the loaded XML for a `v=2` attribute; if absent, it walks the XML rewriting old IDs to new before APVTS reads it. The shim is table-driven, ~30 lines.

## Consequences

- v1 presets continue to load.
- v2-saved presets carry `v=2` and skip the shim.
- The shim is removed in a future major version (probably v6+), after which v1 presets stop loading. Document this in the v6+ spec.
```

- [ ] **Step 4: Commit**

```bash
cd ~/dev/k2000
git add docs/decisions/0005-voice-layer-split.md docs/decisions/0006-algorithm-as-passive-data.md docs/decisions/0007-param-namespace-and-v1-preset-shim.md
git commit -m "docs(adr): ADRs 0005-0007 for v2 (Voice/Layer split, algorithm shape, param namespace)"
```

---

### Task 2: `Algorithm` passive data struct + simple test

**Files:**
- Create: `src/dsp/Algorithm.h`
- Create: `tests/AlgorithmTests.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

```cpp
// tests/AlgorithmTests.cpp
#include <juce_core/juce_core.h>
#include "../src/dsp/Algorithm.h"

class AlgorithmTests : public juce::UnitTest {
public:
    AlgorithmTests() : juce::UnitTest("Algorithm") {}

    void runTest() override {
        beginTest("V1 fixed algorithm has 2 slots in expected order");
        const Algorithm a = Algorithm::v1Fixed();
        expectEquals((int) a.slotCount, 2);
        expect(a.blockTypePerSlot[0] == BlockTypeId::SvfFilter);
        expect(a.blockTypePerSlot[1] == BlockTypeId::Waveshaper);

        beginTest("Algorithm copy-constructs cleanly (passive data)");
        Algorithm b = a;
        expectEquals((int) b.slotCount, 2);
    }
};

static AlgorithmTests algorithmTestsInstance;
```

- [ ] **Step 2: Add the test file to CMake**

Open `tests/CMakeLists.txt` and add `AlgorithmTests.cpp` to the test target sources alongside the existing test files.

- [ ] **Step 3: Run the test to verify it fails**

```bash
cd ~/dev/k2000
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Expected: compile error — `Algorithm.h` doesn't exist.

- [ ] **Step 4: Write the implementation**

```cpp
// src/dsp/Algorithm.h
#pragma once
#include <array>
#include <cstddef>

// Stable identifier for the DSP block type sitting in each slot.
// New entries appended to the end as block types are added in v5+.
enum class BlockTypeId : int {
    None        = 0,
    SvfFilter   = 1,
    Waveshaper  = 2,
};

// Passive data describing a per-voice DSP topology.
// At v2 we ship one algorithm (Algorithm::v1Fixed). v3 introduces a library
// and a selection mechanism; v4 may extend the struct to carry routing
// metadata for non-linear topologies.
struct Algorithm {
    static constexpr std::size_t kMaxSlots = 4;

    std::size_t slotCount = 0;
    std::array<BlockTypeId, kMaxSlots> blockTypePerSlot {};

    // v1's fixed chain: osc → SVF filter → Waveshaper → amp.
    static Algorithm v1Fixed() {
        Algorithm a;
        a.slotCount = 2;
        a.blockTypePerSlot[0] = BlockTypeId::SvfFilter;
        a.blockTypePerSlot[1] = BlockTypeId::Waveshaper;
        return a;
    }
};
```

- [ ] **Step 5: Run tests to verify they pass**

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure -R Algorithm
```

Expected: AlgorithmTests pass. All other v1 tests still pass.

- [ ] **Step 6: Commit**

```bash
git add src/dsp/Algorithm.h tests/AlgorithmTests.cpp tests/CMakeLists.txt
git commit -m "feat(dsp): Algorithm passive data struct + BlockTypeId enum"
```

---

### Task 3: DSPBlock gains nested `VoiceState`; SVFFilter and Waveshaper extract per-voice state

**Files:**
- Modify: `src/dsp/DSPBlock.h`
- Modify: `src/dsp/blocks/SVFFilter.h`
- Modify: `src/dsp/blocks/SVFFilter.cpp`
- Modify: `src/dsp/blocks/Waveshaper.h`
- Modify: `src/dsp/blocks/Waveshaper.cpp`
- Modify: `tests/SVFFilterTests.cpp` (signature changes; behavior assertions same)
- Modify: `tests/WaveshaperTests.cpp` (signature changes)
- Modify: `src/Voice.cpp` (call site updates; minimal)

Rationale: stateful blocks must split into (Layer-owned config) + (Voice-owned state). The block's `process()` gains a `VoiceState&` parameter. Each block type defines its own `VoiceState` struct as a public nested type.

- [ ] **Step 1: Extend DSPBlock interface**

Replace `src/dsp/DSPBlock.h` with:

```cpp
#pragma once
#include <juce_core/juce_core.h>
#include <memory>
#include <vector>
#include "ParamSpec.h"
#include "../params/ParamSnapshot.h"

// Abstract base for swappable per-voice processing units (VAST blocks).
// See docs/architecture/dsp-block-interface.md for the rationale behind
// each method.
//
// As of v2, blocks separate configuration from per-voice runtime state:
// - Configuration (sample rate, cutoff, mode, recomputed coefficients)
//   lives on the block instance, owned by the Layer.
// - Per-voice state (filter integrators, envelope phase, etc.) lives in
//   a block-specific VoiceState struct, owned by the Voice.
//
// Each concrete block defines its own VoiceState type and a factory.

class DSPBlock {
public:
    // Marker base — concrete blocks define their own VoiceState struct
    // (which inherits from this when convenient; or not at all). The
    // runtime contract is just "Voice holds a buffer of bytes large
    // enough for this block's state, and passes a pointer to process()".
    struct VoiceState {
        virtual ~VoiceState() = default;
    };

    virtual ~DSPBlock() = default;

    // Allocate-OK. Called from prepareToPlay.
    virtual void prepare(double sampleRate, int maxBlockSize) = 0;

    // Allocate-OK. Returns a fresh VoiceState for this block. Called once
    // per voice during prepareToPlay (Voice stores them; never RT-allocates).
    virtual std::unique_ptr<VoiceState> makeVoiceState() const = 0;

    // RT-safe. Called on note-on / voice-steal to clear voice-local state.
    virtual void resetVoice(VoiceState& state) = 0;

    // RT-safe. Process numSamples in-place, mono, using the supplied voice state.
    virtual void process(VoiceState& state, float* buffer, int numSamples) = 0;

    // Stable identifier for preset serialisation, e.g. "svf_filter".
    virtual juce::String getTypeId() const = 0;

    // Parameter descriptors. The processor namespaces these by slot.
    virtual std::vector<ParamSpec> getParamSpecs() const = 0;

    // RT-safe. Called once per audio block before process(). Updates
    // shared configuration (cutoff, drive, etc.) — never per-voice state.
    virtual void updateParameters(const ParamSnapshot& snapshot) = 0;
};
```

- [ ] **Step 2: Update SVFFilter to use VoiceState**

Replace `src/dsp/blocks/SVFFilter.h`:

```cpp
#pragma once
#include "../DSPBlock.h"

class SVFFilter : public DSPBlock {
public:
    struct VoiceState : public DSPBlock::VoiceState {
        float ic1eq = 0.0f;
        float ic2eq = 0.0f;
    };

    void prepare(double sampleRate, int maxBlockSize) override;
    std::unique_ptr<DSPBlock::VoiceState> makeVoiceState() const override;
    void resetVoice(DSPBlock::VoiceState& state) override;
    void process(DSPBlock::VoiceState& state, float* buffer, int numSamples) override;
    juce::String getTypeId() const override { return "svf_filter"; }
    std::vector<ParamSpec> getParamSpecs() const override;
    void updateParameters(const ParamSnapshot& snapshot) override;

private:
    double sampleRate_ = 44100.0;
    int type_ = 0;  // 0=LP 1=HP 2=BP 3=Notch
    float cutoffHz_ = 1000.0f;
    float resonance_ = 0.0f;

    // Coefficients (recomputed when cutoff/resonance change)
    float g_ = 0, k_ = 0, a1_ = 0, a2_ = 0, a3_ = 0;
    bool coefsDirty_ = true;

    void recomputeCoefs();
};
```

Replace the relevant methods in `src/dsp/blocks/SVFFilter.cpp` so that `process()` reads from and writes to the passed `VoiceState&` rather than the block's removed `ic1eq_`/`ic2eq_` members. Concretely:

```cpp
#include "SVFFilter.h"
#include <cmath>

void SVFFilter::prepare(double sr, int /*maxBlock*/) {
    sampleRate_ = sr;
    coefsDirty_ = true;
}

std::unique_ptr<DSPBlock::VoiceState> SVFFilter::makeVoiceState() const {
    return std::make_unique<SVFFilter::VoiceState>();
}

void SVFFilter::resetVoice(DSPBlock::VoiceState& s) {
    auto& vs = static_cast<SVFFilter::VoiceState&>(s);
    vs.ic1eq = 0.0f;
    vs.ic2eq = 0.0f;
}

// recomputeCoefs() and getParamSpecs() and updateParameters() stay as in v1.
// process() now uses `vs.ic1eq` and `vs.ic2eq` instead of the removed members.

void SVFFilter::process(DSPBlock::VoiceState& s, float* buf, int n) {
    if (coefsDirty_) recomputeCoefs();
    auto& vs = static_cast<SVFFilter::VoiceState&>(s);
    for (int i = 0; i < n; ++i) {
        const float v0 = buf[i];
        const float v3 = v0 - vs.ic2eq;
        const float v1 = a1_ * vs.ic1eq + a2_ * v3;
        const float v2 = vs.ic2eq + a2_ * vs.ic1eq + a3_ * v3;
        vs.ic1eq = 2.0f * v1 - vs.ic1eq;
        vs.ic2eq = 2.0f * v2 - vs.ic2eq;

        switch (type_) {
            case 0: buf[i] = v2; break;          // LP
            case 1: buf[i] = v0 - k_ * v1 - v2; break; // HP
            case 2: buf[i] = v1; break;          // BP
            default: buf[i] = v0 - k_ * v1; break; // Notch
        }
    }
}
```

(`recomputeCoefs()`, `getParamSpecs()`, `updateParameters()` from the existing `SVFFilter.cpp` are preserved as-is; only the storage of `ic1eq_`/`ic2eq_` moves into `VoiceState`.)

- [ ] **Step 3: Update Waveshaper to use empty VoiceState**

Replace `src/dsp/blocks/Waveshaper.h`:

```cpp
#pragma once
#include "../DSPBlock.h"

class Waveshaper : public DSPBlock {
public:
    struct VoiceState : public DSPBlock::VoiceState { };  // stateless

    void prepare(double sampleRate, int maxBlockSize) override;
    std::unique_ptr<DSPBlock::VoiceState> makeVoiceState() const override;
    void resetVoice(DSPBlock::VoiceState& state) override;
    void process(DSPBlock::VoiceState& state, float* buffer, int numSamples) override;
    juce::String getTypeId() const override { return "waveshaper"; }
    std::vector<ParamSpec> getParamSpecs() const override;
    void updateParameters(const ParamSnapshot& snapshot) override;

private:
    float drive_ = 0.0f;
    float mix_ = 1.0f;
};
```

Update `src/dsp/blocks/Waveshaper.cpp` similarly: process ignores its `VoiceState&` (waveshaper has no integrator state). `makeVoiceState()` returns `std::make_unique<Waveshaper::VoiceState>()`. `resetVoice()` is a no-op.

- [ ] **Step 4: Update SVFFilter and Waveshaper tests**

The existing tests at `tests/SVFFilterTests.cpp` and `tests/WaveshaperTests.cpp` invoke `block->process(buffer, n)`. Update those call sites:

```cpp
auto vs = block.makeVoiceState();
block.resetVoice(*vs);
block.process(*vs, buffer, n);
```

The actual test assertions (cutoff attenuation, distortion bounds, etc.) stay unchanged because the audio behavior is preserved.

- [ ] **Step 5: Update Voice.cpp render() to pass VoiceState into block process()**

Voice gains `std::vector<std::unique_ptr<DSPBlock::VoiceState>> blockStates_`, populated during `prepare()` by asking each block in the Layer for a fresh state. For this task, since `Voice` still owns the blocks (Layer abstraction lands in Task 5), do the simplest thing: have `Voice` instantiate the VoiceStates itself for the blocks it owns. Concretely in `Voice.h` add:

```cpp
std::array<std::unique_ptr<DSPBlock::VoiceState>, 2> blockStates_;
```

In `Voice::prepare`:

```cpp
for (size_t i = 0; i < slots_.size(); ++i)
    blockStates_[i] = slots_[i]->makeVoiceState();
```

In `Voice::reset` and `Voice::noteOn`:

```cpp
for (size_t i = 0; i < slots_.size(); ++i)
    slots_[i]->resetVoice(*blockStates_[i]);
```

In `Voice::render`:

```cpp
for (size_t i = 0; i < slots_.size(); ++i)
    slots_[i]->process(*blockStates_[i], tmp, numSamples);
```

- [ ] **Step 6: Run all tests and verify green**

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Expected: all v1 tests still pass, AlgorithmTests still pass, plus the per-voice-state extraction has not changed audio.

- [ ] **Step 7: Commit**

```bash
git add src/dsp/DSPBlock.h src/dsp/blocks/ src/Voice.h src/Voice.cpp tests/SVFFilterTests.cpp tests/WaveshaperTests.cpp
git commit -m "feat(dsp): extract per-voice state from blocks into nested VoiceState

DSPBlock.process() now takes a VoiceState& alongside the buffer.
SVFFilter moves ic1eq/ic2eq into SVFFilter::VoiceState. Waveshaper
defines an empty VoiceState (stateless). Voice instantiates a
VoiceState per slot during prepare() and passes it on every render.
Audio behavior unchanged; all v1 tests still green."
```

---

### Task 4: `Layer` and `Program` skeletons (compile-clean, not yet wired)

**Files:**
- Create: `src/Layer.h`
- Create: `src/Layer.cpp`
- Create: `src/Program.h`
- Create: `src/Program.cpp`
- Modify: `CMakeLists.txt` (add Layer.cpp, Program.cpp to sources)

These are skeletons. Compile-clean but not yet wired into `PluginProcessor` or `Voice`. Task 5 does the wire-up.

- [ ] **Step 1: Write Layer skeleton**

```cpp
// src/Layer.h
#pragma once
#include <array>
#include <memory>
#include "dsp/Algorithm.h"
#include "dsp/DSPBlock.h"
#include "params/ParamSnapshot.h"

// Configuration container. Owns the algorithm topology, the DSP block
// instances laid out per algorithm slot, and the per-Layer parameter
// snapshot. Does not by itself play notes — Voices hold a pointer to
// their Layer and walk it during render.
//
// v2: there is exactly one Layer, owned by Program. v4 introduces
// multi-Layer Programs.

class Layer {
public:
    Layer();
    ~Layer() = default;

    void prepare(double sampleRate, int maxBlockSize);

    // RT-safe.
    void updateParameters(const ParamSnapshot& snapshot);

    // Read-only access for Voices walking the algorithm.
    const Algorithm& algorithm() const { return algorithm_; }
    DSPBlock& slot(std::size_t i) { return *slots_[i]; }
    const DSPBlock& slot(std::size_t i) const { return *slots_[i]; }

    const ParamSnapshot& snapshot() const { return snapshot_; }

private:
    Algorithm algorithm_ = Algorithm::v1Fixed();
    std::array<std::unique_ptr<DSPBlock>, Algorithm::kMaxSlots> slots_;
    ParamSnapshot snapshot_;
};
```

```cpp
// src/Layer.cpp
#include "Layer.h"
#include "dsp/blocks/SVFFilter.h"
#include "dsp/blocks/Waveshaper.h"

namespace {
std::unique_ptr<DSPBlock> makeBlock(BlockTypeId t) {
    switch (t) {
        case BlockTypeId::SvfFilter:  return std::make_unique<SVFFilter>();
        case BlockTypeId::Waveshaper: return std::make_unique<Waveshaper>();
        default: return nullptr;
    }
}
}

Layer::Layer() {
    for (std::size_t i = 0; i < algorithm_.slotCount; ++i)
        slots_[i] = makeBlock(algorithm_.blockTypePerSlot[i]);
}

void Layer::prepare(double sr, int maxBlock) {
    for (std::size_t i = 0; i < algorithm_.slotCount; ++i)
        slots_[i]->prepare(sr, maxBlock);
}

void Layer::updateParameters(const ParamSnapshot& s) {
    snapshot_ = s;
    for (std::size_t i = 0; i < algorithm_.slotCount; ++i)
        slots_[i]->updateParameters(s);
}
```

- [ ] **Step 2: Write Program skeleton**

```cpp
// src/Program.h
#pragma once
#include "Layer.h"

// Container for 1..N Layers. v2 always has exactly one. v4 introduces
// Layer/Split/Dual modes and multiple Layers; for now Program is a thin
// pass-through so that PluginProcessor talks to a Program rather than
// directly to a Layer (clean v4 extension point).

class Program {
public:
    Program() = default;

    void prepare(double sampleRate, int maxBlockSize) {
        layer_.prepare(sampleRate, maxBlockSize);
    }

    Layer& layer() { return layer_; }
    const Layer& layer() const { return layer_; }

private:
    Layer layer_;
};
```

```cpp
// src/Program.cpp
// (Implementation lives in the header for now — Program is trivial at v2.)
#include "Program.h"
```

- [ ] **Step 3: Add to CMakeLists.txt**

Add `src/Layer.cpp` and `src/Program.cpp` to the JUCE plugin target sources list alongside the existing files.

- [ ] **Step 4: Build and verify it compiles**

```bash
cmake --build build -j
```

Expected: clean build. No tests yet for Layer/Program (Task 8 adds LayerTests). v1 tests should still pass.

```bash
ctest --test-dir build --output-on-failure
```

- [ ] **Step 5: Commit**

```bash
git add src/Layer.h src/Layer.cpp src/Program.h src/Program.cpp CMakeLists.txt
git commit -m "feat: Layer and Program skeletons (not yet wired into processor)"
```

---

### Task 5: Wire `PluginProcessor` and `Voice` through `Program` → `Layer`

**Files:**
- Modify: `src/PluginProcessor.h`
- Modify: `src/PluginProcessor.cpp`
- Modify: `src/VoiceManager.h`
- Modify: `src/VoiceManager.cpp`
- Modify: `src/Voice.h`
- Modify: `src/Voice.cpp`

The structural pivot. After this task: Voice no longer owns block instances or its own ParamSnapshot. It points at a Layer (via VoiceManager) and walks the Layer's algorithm. `PluginProcessor` builds a `ParamSnapshot` and calls `program_.layer().updateParameters(snapshot)` before voices render.

- [ ] **Step 1: Update Voice.h — drop owned blocks, gain Layer pointer**

```cpp
// src/Voice.h
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

    // Bind this voice to a Layer (typically called once during VoiceManager prepare()).
    void setLayer(Layer* layer) { layer_ = layer; }

    void prepare(double sampleRate, int maxBlockSize);
    void reset();

    void noteOn(int midiNote, float velocity);
    void noteOff();
    bool isActive() const;
    int  currentNote() const { return note_; }

    // RT-safe. Renders additively into `out`. Reads its Layer's algorithm,
    // block instances, and ParamSnapshot.
    void render(float* out, int numSamples);

private:
    Layer* layer_ = nullptr;        // non-owning
    Oscillator osc_;
    Envelope amp_;

    // Per-slot VoiceState, sized once in prepare() based on Layer's algorithm.
    std::array<std::unique_ptr<DSPBlock::VoiceState>, Algorithm::kMaxSlots> blockStates_;

    int note_ = -1;
    float velocity_ = 0.0f;
    double sampleRate_ = 44100.0;
    std::vector<float> scratch_;

    static float midiToHz(int note);
};
```

- [ ] **Step 2: Update Voice.cpp — drop block ownership, read from Layer**

```cpp
// src/Voice.cpp
#include "Voice.h"
#include "Layer.h"
#include <cmath>

Voice::Voice() = default;

void Voice::prepare(double sr, int maxBlock) {
    sampleRate_ = sr;
    osc_.prepare(sr);
    amp_.prepare(sr);
    scratch_.assign(maxBlock, 0.0f);

    // Pre-allocate per-slot VoiceState according to the Layer's algorithm.
    if (layer_) {
        const auto& alg = layer_->algorithm();
        for (std::size_t i = 0; i < alg.slotCount; ++i)
            blockStates_[i] = layer_->slot(i).makeVoiceState();
    }
    reset();
}

void Voice::reset() {
    osc_.reset();
    amp_.reset();
    if (layer_) {
        const auto& alg = layer_->algorithm();
        for (std::size_t i = 0; i < alg.slotCount; ++i)
            if (blockStates_[i]) layer_->slot(i).resetVoice(*blockStates_[i]);
    }
    note_ = -1;
    velocity_ = 0.0f;
}

void Voice::noteOn(int midiNote, float velocity) {
    note_ = midiNote;
    velocity_ = velocity;
    osc_.reset();
    amp_.reset();
    if (layer_) {
        const auto& alg = layer_->algorithm();
        for (std::size_t i = 0; i < alg.slotCount; ++i)
            if (blockStates_[i]) layer_->slot(i).resetVoice(*blockStates_[i]);
    }
    amp_.noteOn();
}

void Voice::noteOff() { amp_.noteOff(); }
bool Voice::isActive() const { return amp_.isActive(); }

float Voice::midiToHz(int note) {
    return 440.0f * std::pow(2.0f, (note - 69) / 12.0f);
}

void Voice::render(float* out, int numSamples) {
    if (!isActive() || !layer_) return;
    const auto& s = layer_->snapshot();
    const auto& alg = layer_->algorithm();

    const float tune = s.oscCoarse + s.oscFine * 0.01f;
    const float hz = midiToHz(note_) * std::pow(2.0f, tune / 12.0f);
    osc_.setFrequency(hz);
    osc_.setWaveform(static_cast<Oscillator::Waveform>(s.oscWaveform));
    amp_.setParameters(s.ampAttackS, s.ampDecayS, s.ampSustain, s.ampReleaseS);

    jassert(numSamples <= (int) scratch_.size());
    float* tmp = scratch_.data();
    osc_.processBlock(tmp, numSamples);

    for (std::size_t i = 0; i < alg.slotCount; ++i)
        layer_->slot(i).process(*blockStates_[i], tmp, numSamples);

    for (int i = 0; i < numSamples; ++i)
        out[i] += tmp[i] * amp_.nextSample() * velocity_;
}
```

- [ ] **Step 3: Update VoiceManager to take a Layer reference**

In `src/VoiceManager.h`, add a `setLayer(Layer*)` method that forwards to each voice. In its `prepare()`, after sizing voices, call `setLayer(programLayer_)` on each. (Take the existing render-loop method and remove the `ParamSnapshot` parameter — voices now read it via the Layer.)

In `src/VoiceManager.cpp`, the render loop becomes `voice.render(out, numSamples)` (no snapshot arg).

- [ ] **Step 4: Update PluginProcessor to own Program and route through it**

In `src/PluginProcessor.h`:

```cpp
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Program.h"
#include "VoiceManager.h"
#include "params/Parameters.h"

class K2000AudioProcessor : public juce::AudioProcessor {
public:
    K2000AudioProcessor();
    // ...same JUCE overrides as before...

    juce::AudioProcessorValueTreeState& apvts() { return apvts_; }

private:
    juce::AudioProcessorValueTreeState apvts_;
    Program program_;
    VoiceManager voiceManager_;
    std::vector<float> monoScratch_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(K2000AudioProcessor)
};
```

In `src/PluginProcessor.cpp`:
- In `prepareToPlay`: call `program_.prepare(sr, samplesPerBlock)`, bind voices to `&program_.layer()`, then call `voiceManager_.prepare(...)`.
- In `processBlock`: build a `ParamSnapshot` from APVTS atomics, call `program_.layer().updateParameters(snapshot)`, then `voiceManager_.process(...)`.

- [ ] **Step 5: Build and verify all v1 tests still pass**

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Expected: every existing test still green. Audio output of a held note should be perceptually identical to v1.

- [ ] **Step 6: Commit**

```bash
git add src/PluginProcessor.h src/PluginProcessor.cpp src/VoiceManager.h src/VoiceManager.cpp src/Voice.h src/Voice.cpp
git commit -m "refactor: route audio through Program → Layer; Voice walks Layer's algorithm

Voice no longer owns block instances or its own ParamSnapshot. Layer
owns blocks + snapshot; Voice holds per-slot VoiceState and a non-owning
pointer to its Layer. PluginProcessor builds a snapshot per block and
hands it to program_.layer().updateParameters(). All v1 tests stay green."
```

---

### Task 6: Migrate param IDs to `layer.*` namespace

**Files:**
- Modify: `src/params/Parameters.h`
- Modify: `src/params/Parameters.cpp`
- Modify: `src/PluginEditor.h`
- Modify: `src/PluginEditor.cpp`
- Modify: `tests/ParamSnapshotTests.cpp` (if it references IDs)
- Modify: `tests/PluginLifecycleTests.cpp` (uses `svfCutoff` ID per spec)

- [ ] **Step 1: Rename param IDs in Parameters.cpp**

In `src/params/Parameters.cpp`, update the `createParameterLayout()` (or equivalent) so each id becomes:

| old id | new id |
|---|---|
| `osc.waveform` | `layer.osc.waveform` |
| `osc.coarse` | `layer.osc.coarse` |
| `osc.fine` | `layer.osc.fine` |
| `slot0.type` | `layer.slot0.type` |
| `slot0.cutoff` | `layer.slot0.cutoff` |
| `slot0.resonance` | `layer.slot0.resonance` |
| `slot1.drive` | `layer.slot1.drive` |
| `slot1.mix` | `layer.slot1.mix` |
| `amp.attack` | `layer.amp.attack` |
| `amp.decay` | `layer.amp.decay` |
| `amp.sustain` | `layer.amp.sustain` |
| `amp.release` | `layer.amp.release` |
| `master.gain` | *(unchanged — top-level, not Layer-scoped)* |

Update `src/params/Parameters.h` if it exposes ID string constants — bring those constants in lockstep so call sites resolve.

- [ ] **Step 2: Update PluginEditor attachment IDs**

Wherever `PluginEditor.cpp` constructs `SliderAttachment` / `ComboBoxAttachment` with the old ID strings, update them to the new `layer.*` IDs. Do not change anything else about the UI.

- [ ] **Step 3: Update tests that reference param IDs**

In `tests/PluginLifecycleTests.cpp` (and any other test that names IDs), update the strings. Behavior assertions stay unchanged.

- [ ] **Step 4: Build and verify v1-style tests pass (except preset round-trip)**

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Expected: all tests except potentially the preset round-trip pass. v1 presets will fail to load until Task 7 adds the migration shim — that's expected for now. If `PluginLifecycleTests` includes a preset round-trip test that uses *current-version* state (saved by this build, not a v1 preset), it will still pass because save and load both use the new IDs.

- [ ] **Step 5: Commit**

```bash
git add src/params/Parameters.h src/params/Parameters.cpp src/PluginEditor.h src/PluginEditor.cpp tests/PluginLifecycleTests.cpp
git commit -m "refactor(params): namespace IDs under layer.* (master.gain unchanged)"
```

---

### Task 7: Add v1 preset migration shim

**Files:**
- Modify: `src/PluginProcessor.cpp`
- Create: `tests/PresetMigrationTests.cpp`
- Create: `tests/fixtures/v1_preset.xml` (committed binary-like fixture)
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Save a fixture v1 preset**

Build the v1.0.0 tag in a side worktree (or use any saved v1 preset you have) and capture the XML representation of its state. Commit it as `tests/fixtures/v1_preset.xml` so the migration test always has a reference. Example contents (illustrative — pull the real file from a v1 build):

```xml
<?xml version="1.0" encoding="UTF-8"?>
<APVTS>
  <PARAM id="osc.coarse" value="0.0"/>
  <PARAM id="osc.fine" value="0.0"/>
  <PARAM id="slot0.cutoff" value="1000.0"/>
  <PARAM id="slot0.resonance" value="0.3"/>
  <PARAM id="slot1.drive" value="0.5"/>
  <PARAM id="slot1.mix" value="1.0"/>
  <PARAM id="amp.attack" value="0.01"/>
  <PARAM id="amp.decay" value="0.2"/>
  <PARAM id="amp.sustain" value="0.7"/>
  <PARAM id="amp.release" value="0.5"/>
  <PARAM id="master.gain" value="0.0"/>
</APVTS>
```

- [ ] **Step 2: Write the failing migration test**

```cpp
// tests/PresetMigrationTests.cpp
#include <juce_core/juce_core.h>
#include "../src/PluginProcessor.h"

class PresetMigrationTests : public juce::UnitTest {
public:
    PresetMigrationTests() : juce::UnitTest("PresetMigration") {}

    void runTest() override {
        beginTest("Loading a v1 preset rewrites IDs and applies values");

        K2000AudioProcessor proc;
        proc.prepareToPlay(48000.0, 256);

        const juce::File fixture = juce::File(__FILE__).getParentDirectory()
                                       .getChildFile("fixtures/v1_preset.xml");
        expect(fixture.existsAsFile(), "Fixture v1 preset is missing");

        juce::MemoryBlock data;
        fixture.loadFileAsData(data);
        proc.setStateInformation(data.getData(), (int) data.getSize());

        auto& tree = proc.apvts();
        // Critical IDs from v1 round-trip into their v2 names.
        auto* cutoff = tree.getRawParameterValue("layer.slot0.cutoff");
        expect(cutoff != nullptr);
        expectWithinAbsoluteError(cutoff->load(), 1000.0f, 0.001f);

        auto* attack = tree.getRawParameterValue("layer.amp.attack");
        expect(attack != nullptr);
        expectWithinAbsoluteError(attack->load(), 0.01f, 0.001f);

        beginTest("Saving from v2 sets v=2 attribute");
        juce::MemoryBlock saved;
        proc.getStateInformation(saved);
        const juce::String savedXml { (const char*) saved.getData(), saved.getSize() };
        expect(savedXml.contains("v=\"2\""));
    }
};

static PresetMigrationTests presetMigrationTestsInstance;
```

Add `PresetMigrationTests.cpp` to `tests/CMakeLists.txt`.

- [ ] **Step 3: Run test to verify it fails**

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure -R PresetMigration
```

Expected: test fails (no migration shim yet).

- [ ] **Step 4: Add migration shim in setStateInformation**

In `src/PluginProcessor.cpp`, update `getStateInformation` to emit a root element with a `v=2` attribute, and update `setStateInformation` to handle two paths:

```cpp
namespace {
// One row per renamed ID. Add new rows here as additional renames land.
constexpr struct { const char* from; const char* to; } kV1ToV2Renames[] = {
    {"osc.waveform",    "layer.osc.waveform"},
    {"osc.coarse",      "layer.osc.coarse"},
    {"osc.fine",        "layer.osc.fine"},
    {"slot0.type",      "layer.slot0.type"},
    {"slot0.cutoff",    "layer.slot0.cutoff"},
    {"slot0.resonance", "layer.slot0.resonance"},
    {"slot1.drive",     "layer.slot1.drive"},
    {"slot1.mix",       "layer.slot1.mix"},
    {"amp.attack",      "layer.amp.attack"},
    {"amp.decay",       "layer.amp.decay"},
    {"amp.sustain",     "layer.amp.sustain"},
    {"amp.release",     "layer.amp.release"},
};

void migrateV1ToV2(juce::XmlElement& xml) {
    for (auto* p : xml.getChildWithTagNameIterator("PARAM")) {
        const juce::String id = p->getStringAttribute("id");
        for (const auto& r : kV1ToV2Renames) {
            if (id == r.from) {
                p->setAttribute("id", r.to);
                break;
            }
        }
    }
}
}  // namespace
```

In `getStateInformation`:

```cpp
auto tree = apvts_.copyState();
auto xml = tree.createXml();
xml->setAttribute("v", 2);
copyXmlToBinary(*xml, destData);
```

In `setStateInformation`:

```cpp
std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
if (xml == nullptr) return;

if (xml->getIntAttribute("v", 1) < 2) {
    migrateV1ToV2(*xml);
}

apvts_.replaceState(juce::ValueTree::fromXml(*xml));
```

- [ ] **Step 5: Run all tests**

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Expected: PresetMigrationTests passes plus all prior tests stay green.

- [ ] **Step 6: Commit**

```bash
git add src/PluginProcessor.cpp tests/PresetMigrationTests.cpp tests/fixtures/v1_preset.xml tests/CMakeLists.txt
git commit -m "feat(preset): v1→v2 ID migration shim with regression test fixture"
```

---

### Task 8: Layer-level integration test

**Files:**
- Create: `tests/LayerTests.cpp`
- Modify: `tests/CMakeLists.txt`

Verifies the Layer-driven audio path end-to-end: build a Layer, prepare it, drive a Voice through it with known parameters, assert audio characteristics.

- [ ] **Step 1: Write the test**

```cpp
// tests/LayerTests.cpp
#include <juce_core/juce_core.h>
#include <vector>
#include "../src/Layer.h"
#include "../src/Voice.h"
#include "../src/params/ParamSnapshot.h"

class LayerTests : public juce::UnitTest {
public:
    LayerTests() : juce::UnitTest("Layer") {}

    void runTest() override {
        beginTest("Layer prepares and a Voice renders through it");

        const double sr = 48000.0;
        const int   N  = 256;

        Layer layer;
        layer.prepare(sr, N);

        // Build a benign snapshot.
        ParamSnapshot s {};
        s.oscWaveform = 3;          // sine
        s.oscCoarse = 0; s.oscFine = 0;
        s.slot0Type = 0; s.slot0Cutoff = 20000.0f; s.slot0Resonance = 0.0f;
        s.slot1Drive = 0.0f; s.slot1Mix = 0.0f;
        s.ampAttackS = 0.001f; s.ampDecayS = 0.1f;
        s.ampSustain = 1.0f;   s.ampReleaseS = 0.1f;
        s.masterGainDb = 0.0f;
        layer.updateParameters(s);

        Voice v;
        v.setLayer(&layer);
        v.prepare(sr, N);
        v.noteOn(69, 1.0f);    // A4

        std::vector<float> out(N, 0.0f);
        v.render(out.data(), N);

        // Expect non-silent output at A4 / sine.
        float energy = 0.0f;
        for (float x : out) energy += x * x;
        expectGreaterThan(energy, 1e-4f, "Layer-driven Voice should produce audio");

        beginTest("Lowering the cutoff drops Nyquist energy");

        // Render the same configuration with a high cutoff vs low cutoff and
        // confirm the low-cutoff render is quieter.
        s.slot0Cutoff = 100.0f;
        layer.updateParameters(s);
        Voice v2; v2.setLayer(&layer); v2.prepare(sr, N); v2.noteOn(108, 1.0f);
        std::vector<float> outLow(N, 0.0f);
        v2.render(outLow.data(), N);

        s.slot0Cutoff = 20000.0f;
        layer.updateParameters(s);
        Voice v3; v3.setLayer(&layer); v3.prepare(sr, N); v3.noteOn(108, 1.0f);
        std::vector<float> outHigh(N, 0.0f);
        v3.render(outHigh.data(), N);

        float eLow = 0.0f, eHigh = 0.0f;
        for (int i = 0; i < N; ++i) { eLow += outLow[i]*outLow[i]; eHigh += outHigh[i]*outHigh[i]; }
        expectGreaterThan(eHigh, eLow * 2.0f, "High cutoff should yield substantially more energy than low cutoff for a high note");
    }
};

static LayerTests layerTestsInstance;
```

Add to `tests/CMakeLists.txt`.

- [ ] **Step 2: Build and run**

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure -R Layer
```

Expected: both `LayerTests` cases pass.

- [ ] **Step 3: Commit**

```bash
git add tests/LayerTests.cpp tests/CMakeLists.txt
git commit -m "test: layer-level integration coverage (Voice walks Layer.algorithm)"
```

---

### Task 9: Full suite + manual smoke + roadmap mark

This task is operational. No code changes beyond doc updates.

- [ ] **Step 1: Run the full test suite locally**

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Expected: every test green.

- [ ] **Step 2: Run a manual smoke in Carla on Linux**

```bash
cmake --install build
# Open Carla, load the k2000 plugin, send MIDI from a controller (or Carla's
# built-in keyboard widget), confirm sound. Move filter cutoff, drive,
# envelope sliders. Save and re-load a preset; confirm round-trip.
```

If anything sounds different from v1 (off-pitch, no sound, parameter does nothing), stop and investigate — likely a Layer-side wiring miss in Task 5.

- [ ] **Step 3: Trigger the Windows CI build and smoke in Ableton**

```bash
git push origin main   # CI builds artifacts
# Download the Windows .vst3 artifact from the Actions tab, drop into
# Ableton on the Windows box, confirm same behavior.
```

- [ ] **Step 4: Mark the roadmap**

In `docs/roadmap/phases.md`, update v2's row from no status to `✅` and add a one-line note pointing at the v2 spec + tag. Bump `Status:` at the top of `docs/specs/2026-06-11-v2-layer-abstraction-design.md` from "Design proposed" to "Implemented (tagged v2.0.0 on YYYY-MM-DD)".

- [ ] **Step 5: Tag v2.0.0 and commit doc updates**

```bash
git add docs/roadmap/phases.md docs/specs/2026-06-11-v2-layer-abstraction-design.md
git commit -m "docs: mark v2 as shipped"
git tag v2.0.0
git push origin main --tags
```

---

## Self-review (done)

- **Spec coverage:** every requirement in `docs/specs/2026-06-11-v2-layer-abstraction-design.md` maps to a task here. Module layout changes → Tasks 2, 3, 4, 5. Param namespace → Task 6. Migration shim → Task 7. Layer-level tests → Task 8. Three ADRs → Task 1. Roadmap update → Task 9.
- **Placeholder scan:** no "TBD" / "TODO" / vague "add validation here." All code in steps is complete (a small handful of places say "preserve existing implementation" referring to specific functions that I show are unchanged, which is correct discipline for a refactor).
- **Type consistency:** `Algorithm`, `BlockTypeId`, `DSPBlock::VoiceState`, `SVFFilter::VoiceState`, `Waveshaper::VoiceState`, `Layer`, `Program`, `Voice::setLayer`, `Voice::render(out, n)` signatures are consistent across tasks. Param-ID renames match between Parameters.cpp and the migration shim table.
- **No orphan references:** every name (e.g. `Algorithm::v1Fixed()`, `Layer::algorithm()`, `Layer::slot(i)`, `Layer::snapshot()`) is defined in an earlier task.
