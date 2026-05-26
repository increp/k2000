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
    virtual ~DSPBlock() = default;

    // Called when sample rate or block size changes. May allocate.
    virtual void prepare(double sampleRate, int maxBlockSize) = 0;

    // Called on note-off / voice steal. Reset internal state.
    // RT-safe: no allocation, no locks.
    virtual void reset() = 0;

    // Process one sub-block in-place. RT-safe.
    // numSamples <= maxBlockSize. Mono in / mono out for v1.
    virtual void process(float* buffer, int numSamples) = 0;

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

## Design choices and why

### `prepare` / `reset` are split

`prepare` is allowed to allocate (changing sample rate is a non-RT event, fired from the message thread when the host calls `prepareToPlay`). `reset` is RT-safe — it's called on the audio thread when a voice is stolen and starts a new note, so it can clear state but cannot allocate, lock, or take other slow paths.

### Block-rate parameter updates, not per-sample

Each audio block, the `Voice` calls `updateParameters` on each of its DSP blocks, passing a `ParamSnapshot` it built once for the block. Inside `process()`, the block reads its own cached parameter values — no atomic loads, no `AudioProcessorValueTreeState` lookups in the hot loop.

When per-sample modulation lands (v3, with the mod matrix), we'll add a separate `modulate(int sampleIndex, const ModBus& bus)` hook rather than changing `process`. The mod system will sit beside the per-block parameter cache, modulating it sample-by-sample without changing how blocks consume parameters at block start.

### Parameters declared by the block, namespaced by slot

A block declares its own parameter list via `getParamSpecs()`. The processor walks every slot in every voice, asks for the specs, prefixes each id with `slotN.`, and registers the full set in `AudioProcessorValueTreeState`. So slot 0's "cutoff" becomes `slot0.cutoff` and is distinct from `slot1.cutoff`.

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

1. Create `src/dsp/blocks/MyBlock.{h,cpp}`. Inherit `DSPBlock`. Implement all six virtuals.
2. Pick a stable `getTypeId()` string. Don't change it later — preset state references it by name.
3. Register the type in the block registry (introduced in v4 when slot type becomes user-selectable; in v1 the slot type is hardcoded so this step is skipped).
4. Add a unit test in `tests/MyBlockTests.cpp` covering at minimum: correct response to a known input, parameter changes take effect, `reset()` returns it to a known state.
