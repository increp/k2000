# Algorithm taxonomy

This doc maps our v3 algorithm model to the K2000 source material and calls out where we simplify. It pairs with [ADR 0008](../decisions/0008-algorithm-selection-and-param-namespace.md), which records the decisions; this doc explains the concepts.

## Our model

A `Layer` owns a **palette**: one `DSPBlock` instance per block type (currently `SVFFilter` and `Waveshaper`). The palette is allocated once at `prepareToPlay` and never reallocated on algorithm selection.

An `Algorithm` is passive data — an ordered list of block *types* (`BlockTypeId` enum values, e.g. `SvfFilter`, `Waveshaper`). It carries no code, no vtable. Two algorithms differ only by which types appear and in which order. This is an extension of the passive-data principle established in [ADR 0006](../decisions/0006-algorithm-as-passive-data.md).

The `AlgorithmLibrary` is an append-only registry of named `Algorithm` records. "Append-only" is a hard constraint: the `layer.algorithm` parameter stores the selected algorithm by index in APVTS, so removing or reordering library entries would silently remap saved presets.

Voice rendering walks the selected algorithm's type list in order:

```
osc output
  → for each block type in algorithm.types:
        process through palette[type] using this voice's VoiceState
  → amp
```

The `Voice` holds one `DSPBlock::VoiceState` per palette entry, pre-allocated at prepare time. On each `process()` call the Voice looks up the block by type, passes the matching state, and moves on — no branching, no allocation.

**One-instance-per-block-type constraint.** Because the palette holds exactly one block per type, a given block type can appear at most once in any algorithm. An algorithm that lists `SvfFilter` twice would hit the same palette slot both times, which is not allowed (the `AlgorithmLibrary` tests enforce no duplicate type within an algorithm). This simplification is intentional for v3 and is addressed in "Where v3 simplifies" below.

## The K2000 model (sourced)

*Source: K2000 Series Musician's Guide, pp. 47–48, 253.*

In the K2000, an algorithm is a preset "wiring" — a fixed signal path that routes a sample source through a series of DSP functions to the outputs. There are **31 algorithms**. You select an algorithm; you cannot rewire it. Each algorithm has DSP-function blocks at fixed positions labeled `F1` through `F4`. **The first DSP function always controls pitch; the last always controls final amplitude.** The configurable, musically interesting DSP sits at the positions between them.

Some algorithms **split into two parallel wires** (parallel branches). The branches may rejoin before the output or remain split as "double-output" algorithms that feed separate busses.

A single algorithm **can repeat a function category** across its positional blocks. The manual notes that "one or more blocks…can have filter functions assigned to them," so two filters in one algorithm is a real, documented K2000 use case.

The DSP function categories the K2000 defines are: FILTERS, EQ, PITCH/AMPLITUDE/PAN POSITION, MIXERS, WAVEFORMS, ADDED WAVEFORMS, NON-LINEAR FUNCTIONS, WAVEFORMS WITH NON-LINEAR INPUTS, MIXERS WITH NON-LINEAR INPUTS, and SYNCHRONIZING (HARD SYNC) FUNCTIONS.

## Where v3 simplifies

| K2000 capability | v3 status | Notes |
|---|---|---|
| Parallel / split signal paths | Not yet — linear chain only | Graph routing deferred to v4+ |
| Positional F-blocks (duplicate function categories per algorithm) | Not yet — one-instance-per-type | Deferred to v7; see ADR 0008 and roadmap "Resolved questions" |
| ~10 DSP function categories | 2 block types (SVF filter, waveshaper) | More types added incrementally from v5 onward |

The biggest simplification is the palette's **type-keyed** parameter namespace (`layer.filter.*`, `layer.shaper.*`) versus the K2000's **positional** namespace (one param set per `F`-block, regardless of which function category sits there). The type-keyed approach keeps parameter IDs stable across algorithm selection — a parameter doesn't move when you pick a different algorithm — but it breaks down the moment an algorithm needs two filters, because there's no `layer.filter2.*` namespace. That namespace and the positional param registration model it requires are v7 work.

## Links

- [ADR 0008 — Algorithm selection: palette model + semantic param namespace](../decisions/0008-algorithm-selection-and-param-namespace.md)
- [Roadmap "Resolved questions" (phases.md)](../roadmap/phases.md)
