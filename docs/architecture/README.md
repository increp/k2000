# Architecture

Deep dives into specific architectural pieces. Where a spec answers *what* we're building, an architecture doc answers *how a subsystem works* in enough detail to read or modify it without surprises.

A subsystem earns its own architecture doc when it's load-bearing and non-trivial — something that more than one other module depends on and that wouldn't be obvious from reading the code alone.

## Index

| Doc | What it covers |
|---|---|
| [DSPBlock interface](dsp-block-interface.md) | The polymorphic abstraction every VAST processing block conforms to — the core of the VAST architecture. |
| [Engine register](engine-questions.md) | Living register of locked decisions + open questions for the K2061/Summit engine. |
| [Algorithm taxonomy](algorithm-taxonomy.md) | The VAST algorithm space and how our library samples it. |
| [Huggett filter](huggett-filter.md) | Deep research on the Summit/Peak/OSCar (Chris Huggett) filter lineage — the load-bearing reference for the v5 constant-spine filter. |
| [Huggett filter dossier](huggett-filter-dossier.md) | Second research dossier on the Huggett lineage — the v5.0 nonlinear build path. |
| [TPT/ZDF SVF core](tpt-svf-core.md) | The linear state-variable core every spine filter is built on (`TptSvfCell` / `NlSvfCell`). |
| [Nonlinear filter modeling](nonlinear-filter-modeling.md) | The gray-box Huggett nonlinear stages (measured OTA tanh, resonance saturator, drive shapers). |
| [Anti-aliasing (ADAA)](antialiasing-adaa.md) | The ADAA policy + the Light/Normal/Heavy/Full quality tiers (no per-voice JUCE `Oversampling` object). |
| [Moog ladder](moog-ladder.md) | Research brief for the Moog ladder model (v5.2). |
| [SEM + Moog dossiers](filter-dossiers-sem-moog.md) | Corroborating dossiers for the Oberheim SEM (v5.3) + Moog models. |
