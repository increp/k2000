# v4 (Multi-Layer Programs) implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make a `Program` hold 2 fully-parameterized `Layer`s (generic over the count), routed by per-layer key range / velocity range / MIDI channel, played from a shared 64-voice pool — delivering Summit's dual-engine as a 2-Layer Program.

**Architecture:** `Program` owns `slots[kNumLayers]` (`LayerSlot = Layer + LayerRouting`). Params live under `layer0.*` / `layer1.*`, registered by a loop. Each block, `PluginProcessor` updates every slot (per-layer snapshot + routing) and `VoiceManager` (64 voices, bound to the `Program`) allocates a pooled voice per enabled+matching layer on note-on. v3's per-voice state / Layer-config split already lets a pooled voice play any layer.

**Tech Stack:** JUCE 8.0.4, C++17, CMake, JUCE `UnitTest` + ctest. Build with `cmake --build build -j4` (bounded — bare `-j` OOMs JUCE on this box).

**Spec:** [docs/specs/2026-06-15-v4-multi-layer-programs-design.md](../specs/2026-06-15-v4-multi-layer-programs-design.md)

**Green-build sequencing:** Tasks 2–4 are additive or behavior-preserving (only `layer0` is played, `layer1` disabled → audio identical to v3). Multi-layer playback comes alive in Task 5. The param rename + v3→v4 migration land together in Task 4 (the v3 lesson).

---

### Task 1: ADR 0009 — multi-layer Program

**Files:**
- Create: `docs/decisions/0009-multi-layer-program.md`

- [ ] **Step 1: Write the ADR**

Create `docs/decisions/0009-multi-layer-program.md` (follow the short Context/Decision/Consequences form of ADRs 0005–0008):

```markdown
# ADR 0009 — Multi-Layer Programs: shared pool, range routing, per-layer namespace

**Status:** Accepted, 2026-06-15. Effective from v4.

## Context

v4 makes a Program hold multiple Layers (delivering Summit's dual-engine).
Three decisions: how layers combine, how voices are allocated, and how
per-layer parameters are named.

The K2000 (Musician's Guide p. 44) gives each layer a keyboard + velocity range
and draws polyphony from a shared pool; Layer/Split fall out of ranges. Summit
(User Guide pp. 12–13) exposes explicit Layer/Split/Dual modes and partitions
voices 8+8 per part.

## Decision

- **Range-based combination, not modes.** Each layer carries
  `{ enable, keyLo, keyHi, velLo, velHi, channel, level }`. Layer/Split/Dual
  emerge from ranges. A Summit-style mode selector is a v7 convenience.
- **Shared voice pool (64), not per-part partition.** On note-on, each enabled
  layer whose ranges/channel match the note gets a pooled voice. More flexible
  than Summit's 8+8; a quiet layer doesn't reserve voices.
- **Per-layer namespace `layer0.*` / `layer1.*`** (plain digits, not
  `layer[0]`). Registration loops over `kNumLayers` (=2 in v4); raising the
  count later is a loop-bound change. A v3→v4 *prefix rewrite*
  (`layer.* → layer0.*`) keeps v3 presets loading; `layer1` defaults disabled.

## Consequences

- v4 fully parameterizes 2 layers; the structures generalize toward the K2000's
  32 (deferred — that's a param-surface decision for later).
- Voices are rebindable across layers at no RT cost (v3 already split per-voice
  state from Layer config; all layers share the block palette).
- Deferred: voice modes (Poly2/Mono/legato), velocity crossfade, note-aware
  stealing, the mode selector (v7), per-layer FX (v8).
```

- [ ] **Step 2: Commit**

```bash
cd ~/dev/k2000
git add docs/decisions/0009-multi-layer-program.md
git commit -m "docs(adr): ADR 0009 multi-layer Program (shared pool, range routing, per-layer namespace)"
```

---

### Task 2: `LayerRouting` struct + `matches()`

**Files:**
- Create: `src/LayerRouting.h`
- Create: `tests/LayerRoutingTests.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/LayerRoutingTests.cpp`:

```cpp
#include <juce_core/juce_core.h>
#include "../src/LayerRouting.h"

class LayerRoutingTests : public juce::UnitTest {
public:
    LayerRoutingTests() : juce::UnitTest("LayerRouting") {}

    void runTest() override {
        beginTest("disabled layer never matches");
        {
            LayerRouting r;  // defaults: enabled=false in this test we set true below
            r.enable = false;
            expect(!r.matches(60, 100, 1));
        }

        beginTest("full-range enabled layer matches anything on its channel");
        {
            LayerRouting r;
            r.enable = true; r.keyLo = 0; r.keyHi = 127; r.velLo = 1; r.velHi = 127;
            r.channel = 0;  // Omni
            expect(r.matches(0, 1, 1));
            expect(r.matches(127, 127, 16));
        }

        beginTest("key range gates (split)");
        {
            LayerRouting r; r.enable = true; r.velLo = 1; r.velHi = 127; r.channel = 0;
            r.keyLo = 60; r.keyHi = 127;
            expect(!r.matches(59, 100, 1));
            expect(r.matches(60, 100, 1));
        }

        beginTest("velocity range gates");
        {
            LayerRouting r; r.enable = true; r.keyLo = 0; r.keyHi = 127; r.channel = 0;
            r.velLo = 64; r.velHi = 127;
            expect(!r.matches(60, 63, 1));
            expect(r.matches(60, 64, 1));
        }

        beginTest("channel filter: specific channel only matches that channel");
        {
            LayerRouting r; r.enable = true; r.keyLo = 0; r.keyHi = 127; r.velLo = 1; r.velHi = 127;
            r.channel = 2;  // channel 2 only
            expect(r.matches(60, 100, 2));
            expect(!r.matches(60, 100, 1));
        }
    }
};

static LayerRoutingTests layerRoutingTestsInstance;
```

Add `LayerRoutingTests.cpp` to the test executable sources in `tests/CMakeLists.txt`.

- [ ] **Step 2: Run to verify it fails**

```bash
cd ~/dev/k2000 && cmake -S . -B build >/dev/null && cmake --build build -j4 2>&1 | tail -5
```
Expected: compile error — `LayerRouting.h` not found.

- [ ] **Step 3: Implement `LayerRouting.h`**

```cpp
#pragma once

// Per-layer routing: decides whether a note plays this layer. Owned by a
// Program's LayerSlot, set each block from the layer{i}.* routing params.
// channel == 0 means Omni; 1..16 means that MIDI channel only. See ADR 0009.
struct LayerRouting {
    bool enable = false;
    int  keyLo = 0, keyHi = 127;    // inclusive MIDI note range
    int  velLo = 1, velHi = 127;    // inclusive velocity range
    int  channel = 0;               // 0 = Omni, else 1..16

    bool matches(int note, int velocity, int midiChannel) const {
        return enable
            && note >= keyLo && note <= keyHi
            && velocity >= velLo && velocity <= velHi
            && (channel == 0 || channel == midiChannel);
    }
};
```

- [ ] **Step 4: Run tests to verify they pass**

```bash
cmake --build build -j4 2>&1 | tail -3
ctest --test-dir build --output-on-failure 2>&1 | tail -5
```
Expected: LayerRouting passes; all existing tests still pass.

- [ ] **Step 5: Commit**

```bash
git add src/LayerRouting.h tests/LayerRoutingTests.cpp tests/CMakeLists.txt
git commit -m "feat: LayerRouting struct with range/channel matches() predicate"
```

---

### Task 3: `Layer` output level + `Program` owns `slots[kNumLayers]`

**Files:**
- Modify: `src/Layer.h`
- Modify: `src/Program.h`

Behavior-preserving: still only `slot 0` is played (Task 5 wires the pool), so audio is unchanged.

- [ ] **Step 1: Add an output level to `Layer`**

In `src/Layer.h`, add to the public interface (after `snapshot()`):

```cpp
    void  setLevel(float linearGain) { level_ = linearGain; }
    float level() const { return level_; }
```
and to the private members (after `snapshot_`):

```cpp
    float level_ = 1.0f;  // linear output gain, set each block from layer{i}.level
```

- [ ] **Step 2: Rewrite `Program.h` to own slots**

Replace `src/Program.h` with:

```cpp
#pragma once
#include <array>
#include <cstddef>
#include "Layer.h"
#include "LayerRouting.h"
#include "params/Parameters.h"  // for params::kNumLayers

// A Program holds kNumLayers LayerSlots. Each slot = a Layer (DSP config) plus
// its routing (which notes play it) and is the unit the VoiceManager allocates
// against. v4 fully parameterizes 2 layers; the structures are generic over the
// count. See ADR 0009.
struct LayerSlot {
    Layer layer;
    LayerRouting routing;
};

class Program {
public:
    Program() = default;

    void prepare(double sampleRate, int maxBlockSize) {
        for (auto& s : slots_) s.layer.prepare(sampleRate, maxBlockSize);
    }

    static constexpr std::size_t numLayers() { return params::kNumLayers; }
    LayerSlot&       slot(std::size_t i)       { return slots_[i]; }
    const LayerSlot& slot(std::size_t i) const { return slots_[i]; }

    // Convenience for not-yet-migrated single-layer call sites (slot 0).
    Layer& layer() { return slots_[0].layer; }
    const Layer& layer() const { return slots_[0].layer; }

private:
    std::array<LayerSlot, params::kNumLayers> slots_;
};
```

(Note: this introduces `params::kNumLayers`, defined in Task 4. To keep this task compiling on its own, add the constant now: in `src/params/Parameters.h`, inside `namespace params`, add `inline constexpr int kNumLayers = 2;` near the top. Task 4 builds the rest of the per-layer params on top of it.)

- [ ] **Step 3: Build and run the full suite**

```bash
cmake -S . -B build >/dev/null && cmake --build build -j4 2>&1 | tail -3
ctest --test-dir build --output-on-failure 2>&1 | tail -5
```
Expected: clean build; all tests pass. `PluginProcessor` still uses `program_.layer()` (slot 0) and `VoiceManager` still binds to it — audio unchanged.

- [ ] **Step 4: Commit**

```bash
git add src/Layer.h src/Program.h src/params/Parameters.h
git commit -m "feat: Program owns slots[kNumLayers] (LayerSlot = Layer + routing); Layer gains output level"
```

---

### Task 4: Per-layer parameters (`layer0.*`/`layer1.*`) + per-layer snapshot/routing + v3→v4 migration

**Files:**
- Modify: `src/params/Parameters.h`
- Modify: `src/params/Parameters.cpp`
- Modify: `src/PluginEditor.cpp`
- Modify: `src/PluginProcessor.cpp`
- Modify: `tests/PresetMigrationTests.cpp`
- Modify: any test that calls `params::snapshot(apvts)` with the old single-arg signature (`tests/` — adapt call sites)

This is the structural param refactor. After it, both layers' params exist; the processor updates each slot's DSP + routing; the editor edits `layer0`; v3 presets migrate to `layer0.*`. Only `layer0` is played (VoiceManager unchanged), so audio = v3.

- [ ] **Step 1: Replace the `id` namespace with a per-layer id table in `Parameters.h`**

Replace the `namespace id { ... }` block in `src/params/Parameters.h` with the following (keep `kNumLayers` from Task 3, and keep the file's existing includes + the `createLayout`/`snapshot` declarations, which change as shown):

```cpp
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "ParamSnapshot.h"
#include "../LayerRouting.h"

namespace params {

inline constexpr int kNumLayers = 2;

// Per-layer parameter IDs. Built once into a static table (juce::Strings), so
// snapshot()/routing() read via stable ids with no per-block string building.
struct LayerIds {
    juce::String algorithm, oscWaveform, oscCoarse, oscFine,
                 filterType, filterCutoff, filterResonance,
                 shaperDrive, shaperMix,
                 ampAttack, ampDecay, ampSustain, ampRelease,
                 enable, keyLo, keyHi, velLo, velHi, channel, level;
};

// Returns a reference to the (statically built) ids for the given layer.
const LayerIds& layerIds(int layer);

inline constexpr auto masterGain = "master.gain";

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

// DSP params for one layer → snapshot (RT-safe: reads cached atomics by id).
ParamSnapshot snapshot(const juce::AudioProcessorValueTreeState& apvts, int layer);

// Routing params for one layer → LayerRouting; also returns the layer's output
// level as a linear gain via the out-param.
LayerRouting routing(const juce::AudioProcessorValueTreeState& apvts, int layer,
                     float& levelGainOut);

} // namespace params
```

- [ ] **Step 2: Rewrite `Parameters.cpp`**

Replace `src/params/Parameters.cpp` with:

```cpp
#include "Parameters.h"
#include "../dsp/AlgorithmLibrary.h"

namespace params {

using APVTS = juce::AudioProcessorValueTreeState;
using FloatParam  = juce::AudioParameterFloat;
using ChoiceParam = juce::AudioParameterChoice;
using BoolParam   = juce::AudioParameterBool;

namespace {
juce::String pfx(int layer) { return "layer" + juce::String(layer) + "."; }

LayerIds buildIds(int layer) {
    const juce::String p = pfx(layer);
    LayerIds id;
    id.algorithm       = p + "algorithm";
    id.oscWaveform     = p + "osc.waveform";
    id.oscCoarse       = p + "osc.coarse";
    id.oscFine         = p + "osc.fine";
    id.filterType      = p + "filter.type";
    id.filterCutoff    = p + "filter.cutoff";
    id.filterResonance = p + "filter.resonance";
    id.shaperDrive     = p + "shaper.drive";
    id.shaperMix       = p + "shaper.mix";
    id.ampAttack       = p + "amp.attack";
    id.ampDecay        = p + "amp.decay";
    id.ampSustain      = p + "amp.sustain";
    id.ampRelease      = p + "amp.release";
    id.enable          = p + "enable";
    id.keyLo           = p + "keyLo";
    id.keyHi           = p + "keyHi";
    id.velLo           = p + "velLo";
    id.velHi           = p + "velHi";
    id.channel         = p + "channel";
    id.level           = p + "level";
    return id;
}

const std::array<LayerIds, kNumLayers>& idTable() {
    static const std::array<LayerIds, kNumLayers> t = [] {
        std::array<LayerIds, kNumLayers> a;
        for (int i = 0; i < kNumLayers; ++i) a[(std::size_t) i] = buildIds(i);
        return a;
    }();
    return t;
}

juce::StringArray channelChoices() {
    juce::StringArray s; s.add("Omni");
    for (int c = 1; c <= 16; ++c) s.add(juce::String(c));
    return s;
}

juce::StringArray algoNames() {
    juce::StringArray s;
    for (std::size_t i = 0; i < AlgorithmLibrary::count(); ++i)
        s.add(AlgorithmLibrary::byIndex(i).displayName);
    return s;
}

float raw(const APVTS& apvts, const juce::String& id) {
    auto* p = apvts.getRawParameterValue(id);
    jassert(p != nullptr);
    return p->load();
}
}  // namespace

const LayerIds& layerIds(int layer) { return idTable()[(std::size_t) layer]; }

APVTS::ParameterLayout createLayout() {
    APVTS::ParameterLayout layout;

    for (int i = 0; i < kNumLayers; ++i) {
        const LayerIds& id = layerIds(i);

        layout.add(std::make_unique<ChoiceParam>(juce::ParameterID{id.algorithm, 1},
            "Algorithm " + juce::String(i), algoNames(), 0));
        layout.add(std::make_unique<ChoiceParam>(juce::ParameterID{id.oscWaveform, 1},
            "Osc Waveform " + juce::String(i),
            juce::StringArray{"Saw", "Square", "Triangle", "Sine"}, 0));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.oscCoarse, 1},
            "Osc Coarse " + juce::String(i),
            juce::NormalisableRange<float>{-24.0f, 24.0f, 1.0f}, 0.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.oscFine, 1},
            "Osc Fine " + juce::String(i),
            juce::NormalisableRange<float>{-100.0f, 100.0f, 0.1f}, 0.0f));
        layout.add(std::make_unique<ChoiceParam>(juce::ParameterID{id.filterType, 1},
            "Filter Type " + juce::String(i),
            juce::StringArray{"LP", "HP", "BP", "Notch"}, 0));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.filterCutoff, 1},
            "Filter Cutoff " + juce::String(i),
            juce::NormalisableRange<float>{20.0f, 20000.0f, 0.0f, 0.25f}, 1000.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.filterResonance, 1},
            "Filter Resonance " + juce::String(i),
            juce::NormalisableRange<float>{0.0f, 1.0f, 0.0f}, 0.2f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.shaperDrive, 1},
            "Drive " + juce::String(i),
            juce::NormalisableRange<float>{0.0f, 1.0f, 0.0f}, 0.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.shaperMix, 1},
            "Drive Mix " + juce::String(i),
            juce::NormalisableRange<float>{0.0f, 1.0f, 0.0f}, 1.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.ampAttack, 1},
            "Attack " + juce::String(i),
            juce::NormalisableRange<float>{0.001f, 5.0f, 0.0f, 0.3f}, 0.005f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.ampDecay, 1},
            "Decay " + juce::String(i),
            juce::NormalisableRange<float>{0.001f, 5.0f, 0.0f, 0.3f}, 0.1f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.ampSustain, 1},
            "Sustain " + juce::String(i),
            juce::NormalisableRange<float>{0.0f, 1.0f, 0.0f}, 0.8f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.ampRelease, 1},
            "Release " + juce::String(i),
            juce::NormalisableRange<float>{0.001f, 5.0f, 0.0f, 0.3f}, 0.2f));

        // Routing. layer0 enabled by default, others off → v3 presets sound the same.
        layout.add(std::make_unique<BoolParam>(juce::ParameterID{id.enable, 1},
            "Layer " + juce::String(i) + " Enable", i == 0));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.keyLo, 1},
            "Key Low " + juce::String(i),
            juce::NormalisableRange<float>{0.0f, 127.0f, 1.0f}, 0.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.keyHi, 1},
            "Key High " + juce::String(i),
            juce::NormalisableRange<float>{0.0f, 127.0f, 1.0f}, 127.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.velLo, 1},
            "Vel Low " + juce::String(i),
            juce::NormalisableRange<float>{1.0f, 127.0f, 1.0f}, 1.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.velHi, 1},
            "Vel High " + juce::String(i),
            juce::NormalisableRange<float>{1.0f, 127.0f, 1.0f}, 127.0f));
        layout.add(std::make_unique<ChoiceParam>(juce::ParameterID{id.channel, 1},
            "MIDI Channel " + juce::String(i), channelChoices(), 0));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.level, 1},
            "Level " + juce::String(i),
            juce::NormalisableRange<float>{-60.0f, 6.0f, 0.0f}, 0.0f));
    }

    layout.add(std::make_unique<FloatParam>(juce::ParameterID{masterGain, 1},
        "Master Gain", juce::NormalisableRange<float>{-60.0f, 6.0f, 0.0f}, 0.0f));

    return layout;
}

ParamSnapshot snapshot(const APVTS& apvts, int layer) {
    const LayerIds& id = layerIds(layer);
    ParamSnapshot s;
    s.oscWaveform  = (int) raw(apvts, id.oscWaveform);
    s.oscCoarse    = raw(apvts, id.oscCoarse);
    s.oscFine      = raw(apvts, id.oscFine);
    s.svfType      = (int) raw(apvts, id.filterType);
    s.svfCutoffHz  = raw(apvts, id.filterCutoff);
    s.svfResonance = raw(apvts, id.filterResonance);
    s.wsDrive      = raw(apvts, id.shaperDrive);
    s.wsMix        = raw(apvts, id.shaperMix);
    s.ampAttackS   = raw(apvts, id.ampAttack);
    s.ampDecayS    = raw(apvts, id.ampDecay);
    s.ampSustain   = raw(apvts, id.ampSustain);
    s.ampReleaseS  = raw(apvts, id.ampRelease);
    s.algorithmId  = (int) raw(apvts, id.algorithm);
    s.masterGainDb = raw(apvts, masterGain);
    return s;
}

LayerRouting routing(const APVTS& apvts, int layer, float& levelGainOut) {
    const LayerIds& id = layerIds(layer);
    LayerRouting r;
    r.enable  = raw(apvts, id.enable) >= 0.5f;
    r.keyLo   = (int) raw(apvts, id.keyLo);
    r.keyHi   = (int) raw(apvts, id.keyHi);
    r.velLo   = (int) raw(apvts, id.velLo);
    r.velHi   = (int) raw(apvts, id.velHi);
    r.channel = (int) raw(apvts, id.channel);  // 0 = Omni, else 1..16
    levelGainOut = juce::Decibels::decibelsToGain(raw(apvts, id.level));
    return r;
}

} // namespace params
```

- [ ] **Step 3: Point the editor at `layer0`**

In `src/PluginEditor.cpp`, the constructor currently passes `params::id::X` to `addSlider`/`addCombo`. Replace each `params::id::oscCoarse` etc. with `params::layerIds(0).oscCoarse` etc. (so the editor edits layer 0). The combo for the algorithm becomes `params::layerIds(0).algorithm`; filter type `params::layerIds(0).filterType`; waveform `params::layerIds(0).oscWaveform`. `masterGain` stays `params::masterGain`. The `SliderAttachment`/`ComboBoxAttachment` constructors take a `juce::String` id, which `layerIds(0).X` provides. (Task 6 adds the layer-select combo that switches which layer the editor edits.)

- [ ] **Step 4: Update `PluginProcessor` — per-layer update + v3→v4 prefix migration**

In `src/PluginProcessor.cpp`:

(a) In `processBlock`, replace the single snapshot/update with a per-slot loop (still rendered single-layer by the unchanged VoiceManager; Task 5 generalizes rendering):

```cpp
    for (std::size_t i = 0; i < program_.numLayers(); ++i) {
        auto snap = params::snapshot(apvts_, (int) i);
        auto& slot = program_.slot(i);
        slot.layer.updateParameters(snap);
        float levelGain = 1.0f;
        slot.routing = params::routing(apvts_, (int) i, levelGain);
        slot.layer.setLevel(levelGain);
    }
    const float masterDb = params::snapshot(apvts_, 0).masterGainDb;
```
Then change the master-gain line to use `masterDb` (replace the old `snap.masterGainDb`). The `voiceManager_.renderBlock(...)` call is unchanged.

(b) Add the v3→v4 prefix migration. In the anonymous namespace, after `kV2ToV3Renames`/`migrateV2ToV3`, add:

```cpp
// v3→v4: every Layer-scoped id moves from the single "layer." prefix to
// "layer0." (the second layer is new and defaults disabled). This is a prefix
// rewrite, not a 1:1 table. master.gain is untouched (no "layer." prefix).
void migrateV3ToV4(juce::XmlElement& paramsRoot) {
    for (auto* p : paramsRoot.getChildWithTagNameIterator("PARAM")) {
        const juce::String pid = p->getStringAttribute("id");
        if (pid.startsWith("layer."))
            p->setAttribute("id", "layer0." + pid.substring(6));
    }
}
```
In `getStateInformation`, change the version attribute to 4: `root->setAttribute("v", 4);`
In `setStateInformation`, extend the cumulative chain:
```cpp
            const int v = xml->getIntAttribute("v", 1);
            if (v < 2) migrateV1ToV2(*paramsRoot);
            if (v < 3) migrateV2ToV3(*paramsRoot);
            if (v < 4) migrateV3ToV4(*paramsRoot);
            apvts_.replaceState(juce::ValueTree::fromXml(*paramsRoot));
```

- [ ] **Step 5: Fix test call sites + extend migration test**

- Any test calling `params::snapshot(apvts)` (single arg) must become `params::snapshot(apvts, 0)`. Search `tests/` and update (likely `tests/PluginLifecycleTests.cpp` and any using `params::id::`). Replace `params::id::svfCutoff` references with `params::layerIds(0).filterCutoff`, etc.
- In `tests/PresetMigrationTests.cpp`: update the v1 and v2 cases' post-migration assertions to the v4 ids (`layer0.filter.cutoff`, `layer0.amp.attack`, `layer0.shaper.drive`). Add a v3→v4 case:

```cpp
        beginTest("Loading a v3 preset prefixes layer.* to layer0.*");
        {
            K2000AudioProcessor proc;
            proc.prepareToPlay(48000.0, 256);

            juce::XmlElement root("K2000Root");
            root.setAttribute("v", 3);
            auto* wrapper = root.createNewChildElement("Params");
            auto* pr = wrapper->createNewChildElement("PARAMS");
            auto add = [&](const char* id, double val) {
                auto* p = pr->createNewChildElement("PARAM");
                p->setAttribute("id", id); p->setAttribute("value", val);
            };
            add("layer.filter.cutoff", 3200.0);
            juce::MemoryBlock mb;
            juce::AudioProcessor::copyXmlToBinary(root, mb);
            proc.setStateInformation(mb.getData(), (int) mb.getSize());

            expectWithinAbsoluteError(
                proc.apvts().getRawParameterValue("layer0.filter.cutoff")->load(), 3200.0f, 0.5f);
        }
```
Update the "Saving … sets v=N" test to expect `v=4`.

- [ ] **Step 6: Build and run the full suite**

```bash
cmake -S . -B build >/dev/null && cmake --build build -j4 2>&1 | tail -3
ctest --test-dir build --output-on-failure 2>&1 | tail -6
```
Expected: clean build; all tests pass. Migration covers v1→v4 and v3→v4. Audio is still v3 (only layer0 played, layer1 disabled).

- [ ] **Step 7: Commit**

```bash
git add src/params/Parameters.h src/params/Parameters.cpp src/PluginEditor.cpp src/PluginProcessor.cpp tests/PresetMigrationTests.cpp tests/PluginLifecycleTests.cpp
git commit -m "feat(params): per-layer layer0.*/layer1.* params + per-layer snapshot/routing + v3->v4 prefix migration"
```

---

### Task 5: Shared 64-voice pool + Program-bound routing allocation

**Files:**
- Modify: `src/VoiceManager.h`
- Modify: `src/VoiceManager.cpp`
- Modify: `src/PluginProcessor.h`
- Modify: `src/PluginProcessor.cpp`
- Modify: `src/Voice.cpp`
- Create: `tests/MultiLayerTests.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `tests/VoiceManagerTests.cpp` (bind to a Program instead of a Layer)

This makes multi-layer actually play.

- [ ] **Step 1: Apply per-layer level in `Voice::render`**

In `src/Voice.cpp`, in `render`, change the mix line to multiply by the layer's level:

```cpp
    const float lvl = layer_->level();
    for (int i = 0; i < numSamples; ++i)
        out[i] += tmp[i] * amp_.nextSample() * velocity_ * lvl;
```

- [ ] **Step 2: Rewrite `VoiceManager.h` to bind a Program + 64 voices**

Replace `src/VoiceManager.h` with:

```cpp
#pragma once
#include <array>
#include "Voice.h"

class Program;  // forward

class VoiceManager {
public:
    static constexpr int kNumVoices = 64;

    // Bind to the Program whose layers these voices play. Call before prepare().
    void setProgram(Program* program);

    void prepare(double sampleRate, int maxBlockSize);

    // RT-safe. Process MIDI at sample positions and mix voices into `out` (mono).
    void renderBlock(float* out, int numSamples, const juce::MidiBuffer& midi);

    void allNotesOff();

private:
    Program* program_ = nullptr;  // non-owning
    std::array<Voice, kNumVoices> voices_;

    // Per-voice: which Program slot it is currently playing (-1 = none).
    std::array<int, kNumVoices> voiceSlot_{};
    std::array<int, kNumVoices> voiceAge_{};
    int ageCounter_ = 0;

    int  pickVoice();                                   // free, else oldest
    void noteOn(int note, float velocity, int channel);
    void noteOff(int note, int channel);
};
```

- [ ] **Step 3: Rewrite `VoiceManager.cpp`**

Replace `src/VoiceManager.cpp` with:

```cpp
#include "VoiceManager.h"
#include "Program.h"

void VoiceManager::setProgram(Program* program) {
    program_ = program;
    // Voices can play any layer; bind each to slot 0's layer initially so
    // prepare() can size per-block VoiceState against the (shared) palette.
    if (program_)
        for (auto& v : voices_) v.setLayer(&program_->slot(0).layer);
}

void VoiceManager::prepare(double sr, int maxBlock) {
    if (program_)
        for (auto& v : voices_) { v.setLayer(&program_->slot(0).layer); v.prepare(sr, maxBlock); }
    voiceAge_.fill(0);
    voiceSlot_.fill(-1);
    ageCounter_ = 0;
}

void VoiceManager::allNotesOff() {
    for (auto& v : voices_) v.noteOff();
}

int VoiceManager::pickVoice() {
    for (int i = 0; i < kNumVoices; ++i)
        if (!voices_[i].isActive()) return i;
    int oldest = 0;
    for (int i = 1; i < kNumVoices; ++i)
        if (voiceAge_[i] < voiceAge_[oldest]) oldest = i;
    return oldest;
}

void VoiceManager::noteOn(int note, float velocity, int channel) {
    if (!program_) return;
    const int vel = (int) (velocity * 127.0f + 0.5f);
    for (std::size_t s = 0; s < program_->numLayers(); ++s) {
        auto& slot = program_->slot(s);
        if (!slot.routing.matches(note, vel, channel)) continue;
        const int v = pickVoice();
        voices_[v].setLayer(&slot.layer);
        voices_[v].noteOn(note, velocity);
        voiceSlot_[v] = (int) s;
        voiceAge_[v] = ++ageCounter_;
    }
}

void VoiceManager::noteOff(int note, int channel) {
    if (!program_) return;
    for (int i = 0; i < kNumVoices; ++i) {
        if (!voices_[i].isActive() || voices_[i].currentNote() != note) continue;
        const int s = voiceSlot_[i];
        if (s < 0) continue;
        const int ch = program_->slot((std::size_t) s).routing.channel;
        if (ch == 0 || ch == channel) voices_[i].noteOff();
    }
}

void VoiceManager::renderBlock(float* out, int numSamples, const juce::MidiBuffer& midi) {
    int cursor = 0;
    auto renderRange = [&](int from, int to) {
        if (to <= from) return;
        const int len = to - from;
        for (auto& v : voices_) v.render(out + from, len);
    };

    for (const auto meta : midi) {
        const int pos = meta.samplePosition;
        renderRange(cursor, pos);
        cursor = pos;
        const auto& m = meta.getMessage();
        if (m.isNoteOn())       noteOn(m.getNoteNumber(), m.getFloatVelocity(), m.getChannel());
        else if (m.isNoteOff()) noteOff(m.getNoteNumber(), m.getChannel());
        else if (m.isAllNotesOff()) allNotesOff();
    }
    renderRange(cursor, numSamples);
}
```

- [ ] **Step 4: Wire the processor to the Program (not a Layer)**

In `src/PluginProcessor.cpp` `prepareToPlay`, replace `voiceManager_.setLayer(&program_.layer());` with `voiceManager_.setProgram(&program_);`. (The per-slot update loop from Task 4 already updates every layer + routing each block.)

- [ ] **Step 5: Write the multi-layer tests**

Create `tests/MultiLayerTests.cpp`:

```cpp
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "../src/Program.h"
#include "../src/VoiceManager.h"
#include "../src/params/ParamSnapshot.h"
#include <vector>
#include <cmath>

namespace {
ParamSnapshot dspBase() {
    ParamSnapshot s;
    s.oscWaveform = 3; s.svfType = 0; s.svfCutoffHz = 20000.0f; s.svfResonance = 0.0f;
    s.wsDrive = 0.0f; s.wsMix = 0.0f;
    s.ampAttackS = 0.0001f; s.ampDecayS = 0.05f; s.ampSustain = 1.0f; s.ampReleaseS = 0.05f;
    return s;
}
double energy(const std::vector<float>& b) { double e = 0; for (float x : b) e += x*x; return e; }
}  // namespace

class MultiLayerTests : public juce::UnitTest {
public:
    MultiLayerTests() : juce::UnitTest("MultiLayer") {}
    static constexpr double SR = 48000.0;
    static constexpr int N = 256;

    void runTest() override {
        beginTest("split: a note only fires the layer whose key range contains it");
        {
            Program prog; prog.prepare(SR, N);
            prog.slot(0).layer.updateParameters(dspBase());
            prog.slot(1).layer.updateParameters(dspBase());
            // layer0 = lower half, layer1 = upper half
            prog.slot(0).routing = LayerRouting{true, 0, 59, 1, 127, 0};
            prog.slot(1).routing = LayerRouting{true, 60, 127, 1, 127, 0};
            prog.slot(0).layer.setLevel(1.0f); prog.slot(1).layer.setLevel(1.0f);

            VoiceManager vm; vm.setProgram(&prog); vm.prepare(SR, N);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 48, (juce::uint8) 100), 0);  // lower
            std::vector<float> out(N, 0.0f);
            vm.renderBlock(out.data(), N, midi);
            expect(energy(out) > 1e-5, "note in layer0 range should sound");
        }

        beginTest("layer level scales contribution");
        {
            Program prog; prog.prepare(SR, N);
            prog.slot(0).layer.updateParameters(dspBase());
            prog.slot(0).routing = LayerRouting{true, 0, 127, 1, 127, 0};
            prog.slot(1).routing = LayerRouting{false, 0, 127, 1, 127, 0};

            auto render = [&](float lvl) {
                prog.slot(0).layer.setLevel(lvl);
                VoiceManager vm; vm.setProgram(&prog); vm.prepare(SR, N);
                juce::MidiBuffer midi;
                midi.addEvent(juce::MidiMessage::noteOn(1, 69, (juce::uint8) 100), 0);
                std::vector<float> out(N, 0.0f);
                vm.renderBlock(out.data(), N, midi);
                return energy(out);
            };
            expect(render(1.0f) > render(0.25f) * 2.0, "higher level → more energy");
        }

        beginTest("disabled layer produces no voices");
        {
            Program prog; prog.prepare(SR, N);
            prog.slot(0).layer.updateParameters(dspBase());
            prog.slot(0).routing = LayerRouting{false, 0, 127, 1, 127, 0};
            prog.slot(1).routing = LayerRouting{false, 0, 127, 1, 127, 0};
            VoiceManager vm; vm.setProgram(&prog); vm.prepare(SR, N);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 69, (juce::uint8) 100), 0);
            std::vector<float> out(N, 0.0f);
            vm.renderBlock(out.data(), N, midi);
            expectWithinAbsoluteError((float) energy(out), 0.0f, 1e-9f);
        }
    }
};

static MultiLayerTests multiLayerTestsInstance;
```

Add `MultiLayerTests.cpp` to `tests/CMakeLists.txt`.

- [ ] **Step 6: Update `VoiceManagerTests.cpp` to bind a Program**

`tests/VoiceManagerTests.cpp` currently builds a `Layer`, `vm.setLayer(&layer)`, and drives notes. Change it to build a `Program`, enable slot 0 full-range, update slot 0's layer with the snapshot, and `vm.setProgram(&prog)`:

```cpp
    Program prog; prog.prepare(SR, BLOCK);
    prog.slot(0).layer.updateParameters(s);
    prog.slot(0).routing = LayerRouting{true, 0, 127, 1, 127, 0};
    VoiceManager vm; vm.setProgram(&prog); vm.prepare(SR, BLOCK);
```
Include `../src/Program.h`. Keep the existing behavioral assertions (silence with no MIDI, audible on noteOn, releases on noteOff, voice-stealing under load — now against the 64-pool). The "more notes than polyphony" test still works (it just needs > 64 notes, or keep it as-is asserting finite output).

- [ ] **Step 7: Build and run the full suite**

```bash
cmake -S . -B build >/dev/null && cmake --build build -j4 2>&1 | tail -3
ctest --test-dir build --output-on-failure 2>&1 | tail -6
```
Expected: clean build; MultiLayer + all existing tests pass. With a v3 preset (layer1 disabled, layer0 full-range), audio matches v3.

- [ ] **Step 8: Commit**

```bash
git add src/VoiceManager.h src/VoiceManager.cpp src/PluginProcessor.h src/PluginProcessor.cpp src/Voice.cpp tests/MultiLayerTests.cpp tests/VoiceManagerTests.cpp tests/CMakeLists.txt
git commit -m "feat: shared 64-voice pool bound to Program; per-layer routing allocation + per-layer level"
```

---

### Task 6: Editor — layer-select combo + routing strip

**Files:**
- Modify: `src/PluginEditor.h`
- Modify: `src/PluginEditor.cpp`

Minimal, utilitarian UI (photoreal is v9). An "Edit layer" combo picks which layer the DSP controls edit; a compact routing strip exposes the selected layer's routing.

- [ ] **Step 1: Add an edit-layer selector that re-points attachments**

In `src/PluginEditor.h`, add: an `int editLayer_ = 0;` member, a `LabeledCombo editLayerCombo;`, `LabeledSlider`/`LabeledCombo` members for the routing strip (`enableToggle` as a combo Off/On or a `juce::ToggleButton`, `keyLo, keyHi, velLo, velHi, level` sliders, `channel` combo), and a method `void bindLayer(int layer);` that (re)creates all the DSP + routing attachments against `params::layerIds(layer)`.

In `src/PluginEditor.cpp`:
- Factor the attachment creation in the constructor into `bindLayer(int layer)`, which destroys existing attachments (reset the `unique_ptr`s) and recreates them with `params::layerIds(layer).X`. Call `bindLayer(0)` in the constructor.
- Add `editLayerCombo` with items `{"Layer 0", "Layer 1"}`; on change, set `editLayer_` and call `bindLayer(editLayer_)`. (The edit-layer combo itself is not an APVTS param — it's editor-local UI state; use a plain `onChange` lambda, not a ComboBoxAttachment.)
- Lay out the routing strip controls in `resized()` (one extra row). Keep within the existing window or grow `setSize` modestly.

Because this is UI plumbing with no unit test, verify by building and a manual smoke (Task 7).

- [ ] **Step 2: Build (editor compiles; no test)**

```bash
cmake --build build -j4 2>&1 | tail -3
ctest --test-dir build --output-on-failure 2>&1 | tail -4
```
Expected: clean build; all existing tests still pass.

- [ ] **Step 3: Commit**

```bash
git add src/PluginEditor.h src/PluginEditor.cpp
git commit -m "feat(editor): edit-layer selector + per-layer routing strip (minimal multi-layer UI)"
```

---

### Task 7: Full suite, smoke, docs status, tag

Operational — no new feature code.

- [ ] **Step 1: Full local suite**

```bash
cmake -S . -B build >/dev/null && cmake --build build -j4 2>&1 | tail -3
ctest --test-dir build --output-on-failure
```
Expected: every test green.

- [ ] **Step 2: Manual smoke (standalone or Carla)**

```bash
# Launch the standalone / load the VST3. Confirm:
# - default (layer0 only) sounds like v3.
# - Edit-layer combo switches the panel between layer 0 and 1.
# - Enable layer1, set both full-range, give them different algorithms/filters:
#   playing a note sounds BOTH layers (Layer mode). Adjust each layer's level.
# - Set layer0 keyHi=59 and layer1 keyLo=60: low notes play layer0, high notes
#   layer1 (Split). Set different MIDI channels for Dual.
# - Save/reload host state; confirm a v3-era preset still loads (layer0 only).
```
If anything's off (no sound, selector inert, ranges not gating), stop and investigate.

- [ ] **Step 3: Trigger Windows CI + Ableton smoke**

```bash
git push origin main   # windows-only CI builds the artifact
# Download k2000-windows-<sha>, load in Ableton, confirm dual-engine behaviour.
```

- [ ] **Step 4: Mark docs shipped + bump version**

- `CMakeLists.txt`: `project(k2000 VERSION 4.0.0 ...)`.
- `src/PluginEditor.cpp`: bump the front-panel label to `k2000 — v4` (see memory `release_version_surface`; consider deriving it from `JucePlugin_VersionString` while you're here).
- `docs/roadmap/phases.md`: v4 row → `✅` + "**Shipped <date> as v4.0.0.**" + spec link.
- `docs/specs/2026-06-15-v4-multi-layer-programs-design.md`: status → `Implemented (tagged v4.0.0 on <date>)`.
- `docs/specs/README.md`: v4 row → `Implemented (v4.0.0)`.
- `README.md`: lead Status with v4; "Next — v5".

- [ ] **Step 5: Tag and push (after smoke passes)**

```bash
cmake -S . -B build >/dev/null && cmake --build build -j4 2>&1 | tail -3   # confirm version propagates
ctest --test-dir build --output-on-failure 2>&1 | tail -4
git add CMakeLists.txt src/PluginEditor.cpp docs/roadmap/phases.md docs/specs/2026-06-15-v4-multi-layer-programs-design.md docs/specs/README.md README.md
git commit -m "release: mark v4 shipped + bump version to 4.0.0"
git tag -a v4.0.0 -F <message-file>   # use a message file (avoid shell-quoting issues with multi-line -m)
git push origin main --follow-tags
```

---

## Self-review (done)

- **Spec coverage:** Program/slots + LayerSlot → Tasks 2,3. Per-layer namespace + generic registration + per-layer snapshot/routing + prefix migration → Task 4. Shared 64-pool + range/channel allocation + per-layer level → Task 5. Editor (edit-layer + routing strip) → Task 6. ADR → Task 1. Tests (routing, pool, level, migration, behavior preservation) → Tasks 2,4,5. Docs/tag → Task 7.
- **Placeholder scan:** no TBD/TODO; code shown for each code step. Task 6 (editor) is described structurally rather than verbatim because it's UI plumbing with no unit test and depends on the file's current attachment members — acceptable, with exact param ids (`params::layerIds(layer).X`) specified.
- **Type consistency:** `params::kNumLayers`, `params::layerIds(int)`, `params::snapshot(apvts,int)`, `params::routing(apvts,int,float&)`, `LayerRouting{enable,keyLo,keyHi,velLo,velHi,channel}` + `matches(note,vel,channel)`, `Program::{numLayers,slot,layer}`, `LayerSlot{layer,routing}`, `Layer::{setLevel,level}`, `VoiceManager::{setProgram,kNumVoices=64}`, `Voice` level multiply — consistent across tasks. `ParamSnapshot` fields reused unchanged (built per-layer).
- **Green-build order:** Tasks 2–4 keep audio = v3 (layer1 disabled, single-layer render); Task 5 turns on the pool. The param rename + migration land together in Task 4; the `params::kNumLayers` constant is introduced in Task 3 where `Program` first needs it.
