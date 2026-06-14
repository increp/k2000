# `DSPBlock` interface

The central abstraction of the synth. Every processing unit that lives inside a VAST slot — filters, shapers, EQs, comb filters, ring modulators, all of them — implements `DSPBlock`. Getting the interface right matters: every later phase that adds block types or changes routing depends on it staying stable.

## Sketch

```cpp
struct ParamSpec {
    juce::String id;          // unscoped, e.g. "cutoff"; scoped by slot at registration
    juce::String label;
    juce::NormalisableRange<float> range;
    float defaultValue;
};

class ParamSnapshot;  // forward decl — see params/ParamSnapshot.h

class DSPBlock {
public:
    // Per-voice runtime state lives in a block-defined struct, not on the
    // block instance. Marker base; each concrete block defines its own.
    struct VoiceState { virtual ~VoiceState() = default; };

    virtual ~DSPBlock() = default;

    // Called when sample rate or block size changes. May allocate.
    virtual void prepare(double sampleRate, int maxBlockSize) = 0;

    // Allocate a fresh per-voice state for this block. Called once per voice
    // from prepareToPlay; the Voice owns the returned state. May allocate.
    virtual std::unique_ptr<VoiceState> makeVoiceState() const = 0;

    // Called on note-on / voice steal. Clears the supplied voice's state.
    // RT-safe: no allocation, no locks.
    virtual void resetVoice(VoiceState& state) = 0;

    // Process one sub-block in-place using the supplied voice state. RT-safe.
    // numSamples <= maxBlockSize. Mono in / mono out for v2.
    virtual void process(VoiceState& state, float* buffer, int numSamples) = 0;

    // Stable identifier for serialization, e.g. "svf_filter", "waveshaper".
    // Used in preset state to record which block lives in each slot.
    virtual juce::String getTypeId() const = 0;

    // The block's parameters. The processor namespaces these by slot
    // when registering them in AudioProcessorValueTreeState.
    virtual std::vector<ParamSpec> getParamSpecs() const = 0;

    // Pull current parameter values from the shared snapshot.
    // Called at the start of each audio block; no atomics inside process().
    virtual void updateParameters(const ParamSnapshot& snapshot) = 0;
};
```

> **v2 update:** the block instance now holds only *configuration* (sample
> rate, cached coefficients) and is owned by the `Layer`. *Per-voice* state
> (filter integrators, etc.) moved into a block-defined `VoiceState`, owned by
> each `Voice`. This is what lets two Voices share one Layer's block config
> while keeping independent integrators — the foundation for v4's multi-Layer
> Programs. See [ADR 0005](../decisions/0005-voice-layer-split.md).

## Design choices and why

### `prepare` / `makeVoiceState` / `resetVoice` are split

`prepare` and `makeVoiceState` are allowed to allocate — both are non-RT events fired from the message thread when the host calls `prepareToPlay` (`makeVoiceState` runs once per voice as the Voice sizes its state). `resetVoice` is RT-safe: it's called on the audio thread when a voice is stolen and starts a new note, so it can clear the passed-in `VoiceState` but cannot allocate, lock, or take other slow paths.

### Configuration on the block, runtime state on the voice

As of v2 the block instance is *shared configuration* and lives on the `Layer`; anything that varies per playing note lives in the block's `VoiceState`, which the `Voice` owns and passes into every `process()` call. A stateless block (the waveshaper) defines an empty `VoiceState`; a stateful one (the SVF filter) puts its integrators there. The block never RT-allocates because each Voice pre-allocates its states in `prepare()`.

### Block-rate parameter updates, not per-sample

Each audio block, the `Layer` calls `updateParameters` once on each of its DSP blocks, passing a `ParamSnapshot` the `PluginProcessor` built once for the block; the Voices then walk the Layer's algorithm and call `process()`. Inside `process()`, the block reads its own cached parameter values — no atomic loads, no `AudioProcessorValueTreeState` lookups in the hot loop.

When per-sample modulation lands (v3, with the mod matrix), we'll add a separate `modulate(int sampleIndex, const ModBus& bus)` hook rather than changing `process`. The mod system will sit beside the per-block parameter cache, modulating it sample-by-sample without changing how blocks consume parameters at block start.

### Parameters declared by the block, namespaced by slot

A block declares its own parameter list via `getParamSpecs()`. The processor walks every slot, asks for the specs, prefixes each id, and registers the full set in `AudioProcessorValueTreeState`. As of v2 the prefix is `layer.slotN.`, so slot 0's "cutoff" becomes `layer.slot0.cutoff`, distinct from `layer.slot1.cutoff`. (v1 used a flat `slotN.` prefix; a load-time shim rewrites old IDs — see [ADR 0007](../decisions/0007-param-namespace-and-v1-preset-shim.md).) At v4 the prefix becomes `layer[N].slotM.` additively.

This means blocks know nothing about which slot they're in. The same `SVFFilter` class works in any slot.

### `getTypeId` is part of the interface even though v1 has fixed slots

Preset state saves `{ slot: 0, type: "svf_filter", params: {...} }` from day one. When v4 lets users pick a block per slot, v1 presets still load — the type field tells the loader which block class to instantiate. Without this, v4 would have to invent a migration path.

### Mono in, mono out for v1

Voices are mono internally; stereo widening happens at voice-mix time. Avoids designing channel-count handling in the block interface before there's a block that needs stereo internals (chorus, the obvious candidate, is a v5 concern).

When a stereo block lands, the likely path is a sibling `StereoDSPBlock` interface rather than retrofitting `DSPBlock` — we'd rather have two clean interfaces than one over-general one.

### What the interface intentionally does *not* support

- **Per-sample parameter smoothing inside the block.** Smoothing happens at the parameter layer, before `updateParameters`.
- **Variable I/O (multiple inputs / outputs).** Linear chain only in v1; revisit when full algorithm graphs land (v4).
- **Side-chain inputs.** Same reasoning — when needed, add a separate interface rather than complicating this one.
- **MIDI input.** Blocks process audio; MIDI-driven behavior (envelopes, LFOs) lives in dedicated voice-level components, not in DSP blocks.

## How a new block gets added

1. Create `src/dsp/blocks/MyBlock.{h,cpp}`. Inherit `DSPBlock`. Define a nested `VoiceState` (empty if the block is stateless) and implement all seven virtuals.
2. Put any per-note runtime state in `VoiceState`, not on the block — `makeVoiceState()` returns a fresh one, `resetVoice()` clears it, `process()` reads/writes the passed reference. Configuration (coefficients, cached param values) stays on the block.
3. Pick a stable `getTypeId()` string. Don't change it later — preset state references it by name.
4. Register the type in the block registry (introduced in v4 when slot type becomes user-selectable; through v3 the slot type is hardcoded so this step is skipped).
5. Add a unit test in `tests/MyBlockTests.cpp` covering at minimum: correct response to a known input, parameter changes take effect, and `resetVoice()` returns a `VoiceState` to a known state.
