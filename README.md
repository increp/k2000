# Bernie

**Bernie** — a VST3 synth plugin: a **K2061/K2088-class VAST engine** (a user-wired per-voice DSP graph for sound *generation*) **bracketed by a constant Novation Summit analog voice** — the always-present *shaping* spine: a selectable analog filter model + drive → VCA, plus the modulation system (envelopes, LFOs, mod matrix, voice modes). You can never reach a dead control. The FX section is **Ricky**.

The end-state: build a sound from an arbitrary VAST source/DSP graph, always played through the constant Summit spine, with Summit's full modulation depth. See [ADR-0010](docs/decisions/0010-k2061-repositioning-constant-summit-spine.md). (The earlier "Summit-as-a-preset / Kurzweil K2000" framing is superseded — the K2000 is no longer a reference; `k2000` remains the repo/internal codename only, per register L6.)

Built with [JUCE](https://juce.com) 8.0.4, in C++. Linux for local development; Windows builds produced by GitHub Actions for testing in Ableton 12.

## Status

**Also shipped (process/tooling, not a plugin version):** the anti-drift harness
(`tools/drift-check`), the Bernie rename (this synth was `k2000` in-plugin;
now Bernie everywhere user-facing), and **Franklin** — Bernie's measurement/
validation product — with a live runs dashboard (progress, provenance, per-test
explanations) and purpose-driven characterization grids replacing a ~40 h
exhaustive sweep. See [`docs/franklin/`](docs/franklin/) and the
[engine register](docs/architecture/engine-questions.md).

**v5.0 shipped as 5.1.0 (2026-06-19)** — the constant Summit spine's flagship filter went **nonlinear**: a true-to-life **Huggett** (Novation Summit lineage) with three asymmetric drive stages (pre-drive · self-limiting resonance saturator · post-drive), anti-aliased with ADAA, plus an always-available dedicated **HP pre-filter** before the main multimode filter. Confirmed in Ableton. See the [spec](docs/specs/2026-06-17-v5-huggett-nonlinear-hp-prefilter-design.md).

**v5.0.0 (Plan 1)** — the spine became a **selectable, live-switchable `FilterModel` library** (Huggett default), per-Layer, full stereo. [ADR-0011](docs/decisions/0011-selectable-spine-filter-library.md).

**v4.5.0 shipped 2026-06-16** — re-positioned the engine to a **K2061/K2088 VAST engine bracketed by a constant Summit voice** ([ADR-0010](docs/decisions/0010-k2061-repositioning-constant-summit-spine.md)) and landed the Summit-aesthetic **UI foundation**.

**v4.0.0** (2026-06-16) multi-Layer Programs — a `Program` holds 2 Layers over a shared 64-voice pool with key/velocity/channel/level routing (Layer/Split/Dual). · **v3.0.0** (2026-06-15) selectable algorithm + per-Layer block palette. · **v2.0.0** (2026-06-14) Voice/Layer split. · **v1.0.0** (2026-05-30) end-to-end skeleton.

**Roadmap truth is the live dashboard** (`cd tools/roadmap-dashboard && npm run dashboard`); [`docs/roadmap/phases.md`](docs/roadmap/phases.md) is vision-only, never status.

## Documentation

All documentation lives in [`docs/`](docs/). Start there.

- [Project docs index](docs/README.md)
- [Franklin — the measurement product: charter + runs-dashboard manual](docs/franklin/)
- [Roadmap (K2061/K2088 VAST + constant Summit spine)](docs/roadmap/phases.md)
- [v5.0 spec — nonlinear Huggett + HP pre-filter](docs/specs/2026-06-17-v5-huggett-nonlinear-hp-prefilter-design.md) · [v5 constant-Summit-voice spec](docs/specs/2026-06-16-v5-constant-summit-voice-design.md)
- [v4.5 re-positioning spec](docs/specs/2026-06-16-v4.5-k2061-repositioning-design.md) · [engine register](docs/architecture/engine-questions.md)
- [v4 design spec (Multi-Layer Programs)](docs/specs/2026-06-15-v4-multi-layer-programs-design.md)
- [v3 design spec (Algorithm abstraction)](docs/specs/2026-06-14-v3-algorithm-abstraction-design.md) · [algorithm taxonomy](docs/architecture/algorithm-taxonomy.md)
- [v2 design spec (Layer abstraction)](docs/specs/2026-06-11-v2-layer-abstraction-design.md)
- [v1 design spec](docs/specs/2026-05-25-v1-skeleton-design.md)
- [Architecture decisions](docs/decisions/) — v5: [ADR 0011](docs/decisions/0011-selectable-spine-filter-library.md); v4.5: [ADR 0010](docs/decisions/0010-k2061-repositioning-constant-summit-spine.md); v4: [ADR 0009](docs/decisions/0009-multi-layer-program.md); v3: [ADR 0008](docs/decisions/0008-algorithm-selection-and-param-namespace.md); v2: [ADR 0005](docs/decisions/0005-voice-layer-split.md)–[0007](docs/decisions/0007-param-namespace-and-v1-preset-shim.md)
- Known concerns: [v4](docs/roadmap/v4-known-concerns.md) · [v2 (from v1 review)](docs/roadmap/v2-known-concerns.md)
