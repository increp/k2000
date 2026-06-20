# k2000 documentation

VST3 synth plugin: a **K2061/K2088-class VAST engine** (user-wired per-voice DSP graph) bracketed by a **constant Novation Summit analog voice** (always-present filter + drive → VCA + modulation). See [ADR-0010](decisions/0010-k2061-repositioning-constant-summit-spine.md) and the [roadmap](roadmap/phases.md). (The earlier "Peak + Kurzweil K2000" framing is superseded — the K2000 is no longer a reference; "k2000" stays a repo codename.)

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

- **First time reading?** Start with the [v4.5 re-positioning spec](specs/2026-06-16-v4.5-k2061-repositioning-design.md) (the current north star) and the [roadmap](roadmap/phases.md). For history, the [v1 design](specs/2026-05-25-v1-skeleton-design.md) covers the original end-to-end skeleton.
- **Need to know why a decision was made?** Check [`decisions/`](decisions/).
- **Curious about a specific subsystem?** Check [`architecture/`](architecture/).
- **Wondering what's next?** Check [`roadmap/`](roadmap/).
