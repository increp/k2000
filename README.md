# k2000

VST3 synth plugin. A **VAST engine** with K2000-style flexibility, Summit-style immediacy, and Summit's modern feature set (mod matrix, LFOs, looping envelopes, wavetables, oscillator drift, noise modulation).

The end-state: pick a **Program** that feels like a K2000 patch but plays through a Summit-style control surface, with Summit's full modulation depth available. Summit's signature dual-engine architecture is reachable as a 2-Layer Program; K2000's 3-Layer Programs are also reachable.

Built with [JUCE](https://juce.com) 8.0.4, in C++. Linux for local development; Windows builds produced by GitHub Actions for testing in Ableton 12.

## Status

**v4.0.0 shipped 2026-06-16** — multi-Layer Programs. A `Program` holds up to 2 fully-parameterized Layers played from a shared **64-voice** pool; each layer has its own algorithm/filter/shaper/osc/amp plus routing (key range, velocity range, MIDI channel, level). Layer / Split / Dual combinations *emerge* from the ranges, making Summit's dual-engine reachable as a 2-Layer Program. Params under `layer0.*`/`layer1.*` with a cumulative v1→v2→v3→v4 preset migration. Confirmed working in Ableton.

**v3.0.0 shipped 2026-06-15** — the algorithm abstraction: the per-voice chain became a *selectable algorithm* walking a per-Layer block palette (4-entry library).

**v2.0.0 shipped 2026-06-14** — the Layer abstraction: `Voice` became per-note runtime state walking a `Layer` that owns the DSP blocks and parameter snapshot; params moved under `layer.*`.

**v1.0.0 shipped 2026-05-30** — skeleton end-to-end: 1 oscillator → 2-slot DSP chain → ADSR → 8-voice polyphony, plain JUCE UI.

**Next — v5, Summit-character blocks.** 3 NCO oscillators with detune/drift, an analog-modeled multimode filter, drive variants, a wavetable oscillator, and noise. See [`docs/roadmap/phases.md`](docs/roadmap/phases.md) for the full phase plan.

## Documentation

All documentation lives in [`docs/`](docs/). Start there.

- [Project docs index](docs/README.md)
- [Roadmap (Path B — VAST-first; Summit-as-Program)](docs/roadmap/phases.md)
- [v4 design spec (Multi-Layer Programs)](docs/specs/2026-06-15-v4-multi-layer-programs-design.md)
- [v3 design spec (Algorithm abstraction)](docs/specs/2026-06-14-v3-algorithm-abstraction-design.md) · [algorithm taxonomy](docs/architecture/algorithm-taxonomy.md)
- [v2 design spec (Layer abstraction)](docs/specs/2026-06-11-v2-layer-abstraction-design.md)
- [v1 design spec](docs/specs/2026-05-25-v1-skeleton-design.md)
- [Architecture decisions](docs/decisions/) — v4: [ADR 0009](docs/decisions/0009-multi-layer-program.md); v3: [ADR 0008](docs/decisions/0008-algorithm-selection-and-param-namespace.md); v2: [ADR 0005](docs/decisions/0005-voice-layer-split.md)–[0007](docs/decisions/0007-param-namespace-and-v1-preset-shim.md)
- Known concerns: [v4](docs/roadmap/v4-known-concerns.md) · [v2 (from v1 review)](docs/roadmap/v2-known-concerns.md)
