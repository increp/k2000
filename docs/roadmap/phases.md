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
| **v3** ✅ | **Algorithm abstraction** | The fixed slot chain becomes a *selectable algorithm* = an ordered walk through a per-Layer block palette. Shipped a 4-entry library (Filter→Shaper, Shaper→Filter, Filter only, Thru) built from the existing filter+shaper palette, selectable via `layer.algorithm`; params keyed by block type (`layer.filter.*`/`layer.shaper.*`) with a cumulative v1→v2→v3 preset migration. [ADR 0008](../decisions/0008-algorithm-selection-and-param-namespace.md) + [algorithm taxonomy](../architecture/algorithm-taxonomy.md), grounded in the K2000 manual via the k2000-kb. **Shipped 2026-06-15 as v3.0.0.** See [v3 spec](../specs/2026-06-14-v3-algorithm-abstraction-design.md). |
| **v4** ✅ | **Multi-Layer Programs** | A `Program` holds up to 2 fully-parameterized Layers (structures generic over the count) played from a shared **64-voice** pool. Each layer carries key range / velocity range / MIDI channel / level, so Layer/Split/Dual *emerge* from ranges (a Summit-style mode selector is v7). Makes Summit's dual-engine reachable as a 2-Layer Program. [ADR 0009](../decisions/0009-multi-layer-program.md); cumulative v1→v4 preset migration. **Shipped 2026-06-16 as v4.0.0.** See [v4 spec](../specs/2026-06-15-v4-multi-layer-programs-design.md). |
| **v5** | **Summit-character blocks** | 3 NCO oscillators per Layer with detune / drift, analog-modeled multimode filter (the signature character), drive variants (pre/post-filter), wavetable oscillator type, noise generator as both audio source and mod source. Drop-in to any algorithm. |
| **v6** | **Modulation matrix + LFOs + looping envelopes** | Summit's full modulation surface: mod matrix with N slots × (source → destination → amount), multiple LFOs, additional envelope generators with looping (AHD-loop) variants. K2000 program-level FUNs also covered. Parameter smoothing arrives here (flagged in v2 known concerns as "must arrive no later than v3"; postponed to v6 because mod-matrix-driven modulation is where unsmoothed writes actually zipper audibly). |
| **v7** | **Algorithm library expansion + flagship Summit preset** | Add the algorithms needed to render a Summit-style patch (3-osc + dual-filter + drive + amp). Ship the first **"Summit" Program preset** — a 2-Layer Program using v5 blocks and v6 mod matrix. This is the first moment the plugin "sounds like Summit" end-to-end. Add additional K2000-style algorithms (PARAMETRIC EQ, COMB FILT, NONLIN, etc.) for the VAST experience. |
| **v8** | **FX section** | Chorus, delay, reverb. Per-Program FX rack; routing options (insert vs. send) decided during the v8 spec. |
| **v9+** | Photoreal UI, sample sources, preset browser, polish | Photo backdrop (Summit + K2000) + JSON layout + transparent JUCE controls (trademark question to be resolved before any public distribution); K2000-side sample/keymap engine; categorized preset library; macOS build target; performance polish. |

## Resolved questions

- **Can a VAST algorithm use the same function block more than once? → Yes.** Confirmed against the K2000 Musician's Guide (p. 253 via the [k2000-kb](https://github.com/increp/k2000) reference KB): algorithms are organized by **block position** (`F1`–`F4`, with pitch always first and amplitude always last), and *"one or more blocks…can have filter functions assigned to them"* — so a single algorithm can hold two filters. The K2000-faithful model is therefore **positional (per-F-block)**, not per-block-type. v3 deliberately ships the simpler **semantic** namespace (`layer.filter.*`, `layer.shaper.*`), which assumes one instance of each block type per algorithm — correct for v3's one-filter/one-shaper palette and keeps params stable across selection. The positional / per-F-block model (with union param registration per position) is required by **v7** (algorithm-library expansion with K2000-style algorithms); budget a **v3→v7 parameter migration** then.

## What this is *not*

- A commitment. Phase order can shift if a downstream phase reveals that an upstream one was over- or under-scoped.
- A deadline. No dates here.
- Permission to scope-creep an earlier phase. Each version only ships what it scopes.

## How a phase becomes real

1. When ready to start phase N, write `specs/YYYY-MM-DD-vN-<theme>-design.md` following the same shape as the v1 spec.
2. Capture the non-obvious decisions as ADRs in [`../decisions/`](../decisions/).
3. Add architecture deep dives in [`../architecture/`](../architecture/) for any subsystem that's load-bearing and non-trivial.
4. Update this file to mark the phase as in-progress, then completed.
