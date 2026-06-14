# k2000 documentation

VST3 synth plugin: hybrid of Novation Peak (NCO oscillators + analog-modeled filter character) and Kurzweil K2000 VAST (user-configurable per-voice DSP graph).

Built with JUCE, in C++. Linux for local development; Windows builds produced by GitHub Actions for testing in Ableton 12.

## Layout

| Folder | What lives here |
|---|---|
| [`specs/`](specs/) | Design specifications for major pieces of work. The v1 and v2 designs are here. |
| [`architecture/`](architecture/) | Deep dives into specific architectural pieces. The DSPBlock interface lives here. |
| [`decisions/`](decisions/) | Architecture Decision Records (ADRs) — short documents capturing each non-obvious choice and *why* it was made. |
| [`roadmap/`](roadmap/) | What's coming after v1, and roughly in what order. |
| [`dsp/`](dsp/) | DSP math notes — derivations, references, tuning notes for filters/oscillators. Grows as DSP is implemented. |

## Conventions

- Every major piece of work gets a spec (`specs/YYYY-MM-DD-<topic>-design.md`) before implementation.
- Every non-obvious decision gets an ADR (`decisions/NNNN-<slug>.md`) so the *why* survives turnover and time.
- Each subfolder has a `README.md` index.
- One concern per file. If a file is doing two unrelated jobs, split it.
- Link liberally between docs (relative markdown links).

## Start here

- **First time reading?** Start with the [v1 design](specs/2026-05-25-v1-skeleton-design.md) for the end-to-end skeleton, then the [v2 design](specs/2026-06-11-v2-layer-abstraction-design.md) for the current Layer/Program abstraction.
- **Need to know why a decision was made?** Check [`decisions/`](decisions/).
- **Curious about a specific subsystem?** Check [`architecture/`](architecture/).
- **Wondering what's next?** Check [`roadmap/`](roadmap/).
