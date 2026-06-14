# Phase plan

Order is suggestive, not binding. Phases can re-order based on what we learn during each one. Each phase gets its own spec in [`../specs/`](../specs/) before implementation begins.

## End-state vision (re-framed after v1 PoC, 2026-05-31)

The plugin is a **VAST engine**. The three product axes:

- **Flexibility from K2000** — Layer / Program / Setup hierarchy, user-selectable algorithms per Layer, composable DSP blocks, deep routing.
- **Immediacy from Summit** — performance and sound-shaping controls live on the front panel, always visible. Menu pages accepted only for the long tail of VAST flexibility. See `feedback-tiered-immediacy` in user memory.
- **Modern features from Summit** that K2000 never had — wavetables, mod matrix, LFOs, looping envelopes, oscillator drift, noise as mod source.

**Summit becomes a flagship Program preset** within the VAST engine, not a top-level architectural commitment. Summit's signature dual-engine = a 2-Layer Program. K2000's 3-Layer Programs are also reachable.

This is **Path B** ("VAST-first") chosen over **Path A** ("Summit-first, VAST grows on top"). Path A would have hardcoded a dual-engine shell at the plugin top level; Path B keeps the engine general and lets Summit emerge as a particular configuration.

## Phases

| Phase | Theme | What lands |
|---|---|---|
| **v1** ✅ | Skeleton end-to-end | 1 oscillator, 2-slot DSP chain (SVF + waveshaper), ADSR amp, 8-voice polyphony, plain JUCE UI. Linux dev + Windows builds via CI. **Shipped 2026-05-30 as v1.0.0.** See [v1 spec](../specs/2026-05-25-v1-skeleton-design.md). |
| **v2** ✅ | **Layer abstraction** | Refactor today's `Voice` into a `Layer` with a selectable algorithm slot. `PluginProcessor` manages 1 Layer for now. v1's behavior preserved end-to-end but now expressed through the new abstraction. Sets the structural foundation everything else hangs off of. **Shipped 2026-06-14 as v2.0.0.** See [v2 spec](../specs/2026-06-11-v2-layer-abstraction-design.md). |
| **v3** | **Algorithm abstraction** | Current fixed slot chain becomes "algorithm 0: osc → filter → shaper → amp." An *algorithm* = routing topology + block-type-per-slot. Still one algorithm in the library, but the selection mechanism is in place. ADR + architecture doc for the algorithm/block taxonomy (informed by K2000 manual chapter on algorithms). |
| **v4** | **Multi-Layer Programs + combination modes** | Program holds 1–N Layers. Layer / Split / Dual-MIDI modes. *This is the phase that makes Summit's dual-engine possible — as a 2-Layer Program.* Voice allocation reworked to share a pool across Layers (default 32 voices). |
| **v5** | **Summit-character blocks** | 3 NCO oscillators per Layer with detune / drift, analog-modeled multimode filter (the signature character), drive variants (pre/post-filter), wavetable oscillator type, noise generator as both audio source and mod source. Drop-in to any algorithm. |
| **v6** | **Modulation matrix + LFOs + looping envelopes** | Summit's full modulation surface: mod matrix with N slots × (source → destination → amount), multiple LFOs, additional envelope generators with looping (AHD-loop) variants. K2000 program-level FUNs also covered. Parameter smoothing arrives here (flagged in v2 known concerns as "must arrive no later than v3"; postponed to v6 because mod-matrix-driven modulation is where unsmoothed writes actually zipper audibly). |
| **v7** | **Algorithm library expansion + flagship Summit preset** | Add the algorithms needed to render a Summit-style patch (3-osc + dual-filter + drive + amp). Ship the first **"Summit" Program preset** — a 2-Layer Program using v5 blocks and v6 mod matrix. This is the first moment the plugin "sounds like Summit" end-to-end. Add additional K2000-style algorithms (PARAMETRIC EQ, COMB FILT, NONLIN, etc.) for the VAST experience. |
| **v8** | **FX section** | Chorus, delay, reverb. Per-Program FX rack; routing options (insert vs. send) decided during the v8 spec. |
| **v9+** | Photoreal UI, sample sources, preset browser, polish | Photo backdrop (Summit + K2000) + JSON layout + transparent JUCE controls (trademark question to be resolved before any public distribution); K2000-side sample/keymap engine; categorized preset library; macOS build target; performance polish. |

## What this is *not*

- A commitment. Phase order can shift if a downstream phase reveals that an upstream one was over- or under-scoped.
- A deadline. No dates here.
- Permission to scope-creep an earlier phase. Each version only ships what it scopes.

## How a phase becomes real

1. When ready to start phase N, write `specs/YYYY-MM-DD-vN-<theme>-design.md` following the same shape as the v1 spec.
2. Capture the non-obvious decisions as ADRs in [`../decisions/`](../decisions/).
3. Add architecture deep dives in [`../architecture/`](../architecture/) for any subsystem that's load-bearing and non-trivial.
4. Update this file to mark the phase as in-progress, then completed.
