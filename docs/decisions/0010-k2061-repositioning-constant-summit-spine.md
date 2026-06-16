# ADR 0010 — Re-position to K2061/K2088 VAST with a constant Summit spine

**Status:** Accepted, 2026-06-16. Effective from v4.5.

## Context

Through v4 the engine's *flexibility* north star was the Kurzweil **K2000** (fixed, preset algorithms; ≤3 layers; 48 voices). Studying the **K2061/K2088** manual (via the k2000-kb) revealed a much richer architecture — **Dynamic VAST** (user-wired serial/parallel DSP graphs), 32 layers, 256 voices, KDFX, KVA/FM sources, Cascade, Multis.

Separately, a flexible graph engine has a UX hazard: a patch with no filter (e.g. pure FM) leaves Summit's signature filter knob dead. A Summit-style instrument needs its analog controls to always be live.

## Decision

Re-position the engine target from K2000 → **K2061/K2088 VAST**, and adopt a **hard-bracketed hybrid model**:

- **Variable — K2061 Dynamic VAST:** all sound *generation* is a graph of DSP blocks (configurable I/O, serial + parallel). **Sources are blocks** — Summit oscillators are subsumed as one (default) source block, alongside KVA, FM, wavetable, noise.
- **Constant — the Summit spine:** all sound *shaping* is always present — the Huggett multimode filter + drive → VCA, plus the modulation system (amp/mod envelopes, LFOs, mod matrix, voice modes). The graph's output always flows through it; touch the filter and the sound changes regardless of source. This extends VAST's own "pitch first, amplitude last" into "filter + analog character always last-but-one."

Locked sub-decisions (see the [engine register](../architecture/engine-questions.md)): **full stereo throughout**, a **256-voice** target, the spine + modulation **per-Layer**, and **synth-only** sources for now (sample/keymap deferred to v12+).

Cross-cutting principles: **the GUI grows with the engine** (no feature ships without UI; Summit UI foundation lands at v4.5, before v5), and **performance is a per-phase gate** (256 × stereo × graphs demands perf-friendly structures from v5).

## Consequences

- v1–v4 carry forward intact. v3's fixed `AlgorithmLibrary` becomes the **"factory algorithm"** floor; Dynamic VAST graphs (v6) are the ceiling. The SVF filter is **promoted out** of the optional palette into the constant spine (v5).
- The roadmap is rewritten: v5 = constant Summit voice, v6 = Dynamic VAST graphs, v7 = source/DSP block library, v8 = KDFX, v9 = scale/Cascade/Multis, v10 = FM, v11 = tuning.
- Hard problems are now explicit and deferred to their phase (engine register): parameterizing variable-topology graphs (v6), mod-matrix addressing across changing graphs (v7), Summit-FX-vs-KDFX (v8), and the 256×stereo CPU budget (ongoing).
- Design proceeds **question-heavy**: open questions are registered and groomed for consistency before each spec/ADR.
