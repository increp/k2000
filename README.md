# k2000

VST3 synth plugin. A **VAST engine** with K2000-style flexibility, Summit-style immediacy, and Summit's modern feature set (mod matrix, LFOs, looping envelopes, wavetables, oscillator drift, noise modulation).

The end-state: pick a **Program** that feels like a K2000 patch but plays through a Summit-style control surface, with Summit's full modulation depth available. Summit's signature dual-engine architecture is reachable as a 2-Layer Program; K2000's 3-Layer Programs are also reachable.

Built with [JUCE](https://juce.com) 8.0.4, in C++. Linux for local development; Windows builds produced by GitHub Actions for testing in Ableton 12.

## Status

**v2.0.0 shipped 2026-06-14** — the Layer abstraction. `Voice` is now per-note runtime state that walks a `Layer`; the Layer owns the algorithm topology, the DSP block instances, and the parameter snapshot, and a `Program` container holds the single Layer (multi-Layer Programs arrive at v4). Parameters moved under a `layer.*` namespace, with a v1→v2 preset migration shim so v1 presets still load. v1's audio behaviour is preserved end-to-end. Confirmed working in Ableton.

**v1.0.0 shipped 2026-05-30** — skeleton end-to-end: 1 oscillator → 2-slot DSP chain → ADSR → 8-voice polyphony, plain JUCE UI.

**Next — v3, algorithm abstraction.** Turn the fixed slot chain into a selectable algorithm (routing topology + block-type-per-slot). See [`docs/roadmap/phases.md`](docs/roadmap/phases.md) for the full phase plan.

## Documentation

All documentation lives in [`docs/`](docs/). Start there.

- [Project docs index](docs/README.md)
- [Roadmap (Path B — VAST-first; Summit-as-Program)](docs/roadmap/phases.md)
- [v2 design spec (Layer abstraction)](docs/specs/2026-06-11-v2-layer-abstraction-design.md)
- [v1 design spec](docs/specs/2026-05-25-v1-skeleton-design.md)
- [Architecture decisions](docs/decisions/) — v2 decisions are [ADR 0005](docs/decisions/0005-voice-layer-split.md)–[0007](docs/decisions/0007-param-namespace-and-v1-preset-shim.md)
- [v2 known concerns (carried from v1 review)](docs/roadmap/v2-known-concerns.md)
