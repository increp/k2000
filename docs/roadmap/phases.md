# Phase plan

Order is suggestive, not binding. Phases can re-order based on what we learn during each one. Each phase gets its own spec in [`../specs/`](../specs/) before implementation begins.

## End-state vision (re-positioned to K2061/K2088, 2026-06-16)

The plugin is a **K2061/K2088-class VAST engine bracketed by a constant Summit analog voice.** Sound *generation* is fully flexible (K2061 Dynamic VAST); sound *shaping* is always a Summit.

- **Flexibility from K2061/K2088 VAST** — Dynamic VAST: build sound from arbitrary serial/parallel DSP graphs where every source (Summit oscillators, KVA, FM, wavetable, noise — and later samples) is just a block. 32 layers, Multis, KDFX, Cascade.
- **A constant Summit voice** — the Huggett multimode filter + drive → VCA, and the modulation system (amp/mod envelopes, LFOs, mod matrix, voice modes), are **always present** and always live. You can never reach a dead control or a patch that isn't a real synth.
- **Immediacy from Summit** — the constant spine is the permanent front panel; the variable source/DSP region's knob-clusters swap to match the active blocks. Tiered immediacy: front panel for live params, pages for the long tail.

This evolves the original "Path B / Summit-as-a-preset" framing: the engine stays **VAST-first for generation**, but the **Summit analog voice is now a top-level constant** for shaping — not merely an emergent preset. See the [v4.5(C) re-positioning spec](../specs/2026-06-16-v4.5-k2061-repositioning-design.md) and the living [engine architecture register](../architecture/engine-questions.md).

## Engine principles (cross-cutting)

- **The model:** `[ K2061 Dynamic VAST source + DSP graph (variable) ] → [ constant Summit spine: Huggett filter + drive → VCA, with envelopes/LFOs/mod matrix/voice modes ]`, **per voice, per Layer**.
- **Locked decisions** (from the register): **full stereo throughout** · **256-voice** target · spine + modulation **per-Layer** · **synth-only** sources now (sample/keymap → v12+).
- **GUI grows with the engine** — no phase ships a feature you can't drive. The Summit UI foundation (v4.5/B) lands before v5; each phase ships its own feature UI.
- **Performance is a gate** — at 256 voices × full stereo × graph DSP, every phase meets a per-voice CPU budget with profiling as a release gate, and uses perf-friendly structures from v5 (SIMD-friendly buffers, denormals, efficient graph execution).

## Phases

| Phase | Theme | What lands |
|---|---|---|
| **v1** ✅ | Skeleton end-to-end | 1 oscillator, 2-slot DSP chain (SVF + waveshaper), ADSR amp, 8-voice polyphony, plain JUCE UI. Linux dev + Windows builds via CI. **Shipped 2026-05-30 as v1.0.0.** See [v1 spec](../specs/2026-05-25-v1-skeleton-design.md). |
| **v2** ✅ | **Layer abstraction** | `Voice` split into per-note runtime + a `Layer` owning DSP blocks + ParamSnapshot. **Shipped 2026-06-14 as v2.0.0.** See [v2 spec](../specs/2026-06-11-v2-layer-abstraction-design.md). |
| **v3** ✅ | **Algorithm abstraction** | Selectable algorithm = ordered walk through a per-Layer block palette; 4-entry library; block-type param namespace; cumulative migration. [ADR 0008](../decisions/0008-algorithm-selection-and-param-namespace.md). **Shipped 2026-06-15 as v3.0.0.** See [v3 spec](../specs/2026-06-14-v3-algorithm-abstraction-design.md). |
| **v4** ✅ | **Multi-Layer Programs** | `Program` holds 2 Layers (generic over count), shared 64-voice pool, per-layer key/vel/channel/level routing → Layer/Split/Dual. [ADR 0009](../decisions/0009-multi-layer-program.md). **Shipped 2026-06-16 as v4.0.0.** See [v4 spec](../specs/2026-06-15-v4-multi-layer-programs-design.md). |
| **v4.5** | **K2061 re-positioning + Summit UI foundation + KB fix** | *(C)* re-position the engine to K2061/K2088 VAST with the constant Summit spine ([spec](../specs/2026-06-16-v4.5-k2061-repositioning-design.md), this rewrite, ADR 0010). *(B)* the Summit-aesthetic, extensible **UI foundation** — load-bearing, lands before v5. *(A)* fix the Pirkle synth-book OCR in `k2000-kb`. |
| **v5** | **Constant Summit voice** *(keystone)* | Build the always-present spine: Huggett multimode filter + drive → VCA, with amp/mod envelopes, LFOs, mod matrix, voice modes — **stereo, per-Layer**. Promote the filter out of the optional palette. UI: the permanent Summit front panel. |
| **v6** | **Dynamic VAST graph routing** *(keystone)* | Generalize `Algorithm` from a fixed linear list into a wired **graph**: blocks with configurable inputs/outputs, serial **+ parallel**, splits/joins, feeding the spine. v3's library becomes "factory" presets. Solves the variable-graph parameter model (register Q5). UI: the visual wiring/graph editor. |
| **v7** | **Source & DSP block library** | Sources as blocks — Summit 3-osc (default, drift/sync/FM), **KVA**, wavetable, noise — + DSP blocks (mixer, ring mod, shaper/drive variants, EQ, timbre filters, hard sync). UI: dynamic per-block knob-clusters. |
| **v8** | **KDFX effects** | Per-layer insert + common insert + two aux chains; Summit effect types within them. UI: FX-chain editor. |
| **v9** | **Scale + Cascade + Multis** | Toward **32 layers / 256 voices**; Cascade Mode; Multis (≤16 programs). UI: layer manager + multi/zone view. |
| **v10** | **FM layers** | 6-operator FM source. UI: FM operator panel. |
| **v11** | **Tuning** | Intonation + tuning maps, KSR. |
| **v12+** | **Polish** | Full photoreal GUI, sample/keymap sources, preset browser, macOS, performance. |

## Locked & resolved decisions

Engine-level decisions and open questions live in the living [engine architecture register](../architecture/engine-questions.md) (groomed for consistency before each spec/ADR). Earlier carried note:

- **Can a VAST algorithm use the same function block more than once? → Yes** (K2000 Guide p. 253). v3 shipped the simpler per-block-type namespace; the positional / per-F-block model (and any param migration) is folded into the **v6/v7** Dynamic-VAST + block-library work.

## What this is *not*

- A commitment. Phase order can shift if a downstream phase reveals that an upstream one was over- or under-scoped.
- A deadline. No dates here.
- Permission to scope-creep an earlier phase. Each version only ships what it scopes.

## How a phase becomes real

1. Enumerate the phase's open questions into the [engine architecture register](../architecture/engine-questions.md); ask them; record answers; **groom the register for internal consistency**.
2. Write `specs/YYYY-MM-DD-vN-<theme>-design.md` once the relevant questions are resolved.
3. Capture non-obvious decisions as ADRs in [`../decisions/`](../decisions/).
4. Add architecture deep dives in [`../architecture/`](../architecture/) for load-bearing subsystems.
5. Mark the phase in-progress, then completed, here.
