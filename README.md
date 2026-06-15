# k2000

VST3 synth plugin. A **VAST engine** with K2000-style flexibility, Summit-style immediacy, and Summit's modern feature set (mod matrix, LFOs, looping envelopes, wavetables, oscillator drift, noise modulation).

The end-state: pick a **Program** that feels like a K2000 patch but plays through a Summit-style control surface, with Summit's full modulation depth available. Summit's signature dual-engine architecture is reachable as a 2-Layer Program; K2000's 3-Layer Programs are also reachable.

Built with [JUCE](https://juce.com) 8.0.4, in C++. Linux for local development; Windows builds produced by GitHub Actions for testing in Ableton 12.

## Status

**v3.0.0 shipped 2026-06-15** — the algorithm abstraction. The per-voice DSP chain is now a *selectable algorithm*: a `Layer` owns a palette of block instances (filter, shaper) and a `Voice` walks the chosen algorithm's ordered block-type list. Ships a 4-entry library (`Filter→Shaper`, `Shaper→Filter`, `Filter only`, `Thru`) selectable via `layer.algorithm`. Parameters are keyed by block type (`layer.filter.*`, `layer.shaper.*`), with a cumulative v1→v2→v3 preset migration. Confirmed working in Ableton.

**v2.0.0 shipped 2026-06-14** — the Layer abstraction: `Voice` became per-note runtime state walking a `Layer` that owns the DSP blocks and parameter snapshot; params moved under `layer.*`.

**v1.0.0 shipped 2026-05-30** — skeleton end-to-end: 1 oscillator → 2-slot DSP chain → ADSR → 8-voice polyphony, plain JUCE UI.

**Next — v4, multi-Layer Programs.** A `Program` holds 1–N Layers (Layer/Split/Dual modes); makes Summit's dual-engine reachable as a 2-Layer Program. See [`docs/roadmap/phases.md`](docs/roadmap/phases.md) for the full phase plan.

## Documentation

All documentation lives in [`docs/`](docs/). Start there.

- [Project docs index](docs/README.md)
- [Roadmap (Path B — VAST-first; Summit-as-Program)](docs/roadmap/phases.md)
- [v3 design spec (Algorithm abstraction)](docs/specs/2026-06-14-v3-algorithm-abstraction-design.md) · [algorithm taxonomy](docs/architecture/algorithm-taxonomy.md)
- [v2 design spec (Layer abstraction)](docs/specs/2026-06-11-v2-layer-abstraction-design.md)
- [v1 design spec](docs/specs/2026-05-25-v1-skeleton-design.md)
- [Architecture decisions](docs/decisions/) — v3 decision is [ADR 0008](docs/decisions/0008-algorithm-selection-and-param-namespace.md); v2 decisions are [ADR 0005](docs/decisions/0005-voice-layer-split.md)–[0007](docs/decisions/0007-param-namespace-and-v1-preset-shim.md)
- [v2 known concerns (carried from v1 review)](docs/roadmap/v2-known-concerns.md)
