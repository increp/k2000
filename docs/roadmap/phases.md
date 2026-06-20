# Phase plan

Order is suggestive, not binding. Phases can re-order based on what we learn during each one. Each phase gets its own spec in [`../specs/`](../specs/) before implementation begins.

## End-state vision (re-positioned to K2061/K2088, 2026-06-16)

The plugin is a **K2061/K2088-class VAST engine bracketed by a constant Summit analog voice.** Sound *generation* is fully flexible (K2061 Dynamic VAST); sound *shaping* is always a Summit.

- **Flexibility from K2061/K2088 VAST** — Dynamic VAST: build sound from arbitrary serial/parallel DSP graphs where every source (Summit oscillators, KVA, FM, wavetable, noise — and later samples) is just a block. 32 layers, Multis, KDFX, Cascade.
- **A constant Summit voice** — a **selectable, live-switchable filter model** (Huggett default; Moog v5.3; Oberheim+ later) + drive → VCA, and the modulation system (amp/mod envelopes, LFOs, mod matrix, voice modes), are **always present** and always live. You can never reach a dead control or a patch that isn't a real synth.
- **Immediacy from Summit** — the constant spine is the permanent front panel; the variable source/DSP region's knob-clusters swap to match the active blocks. Tiered immediacy: front panel for live params, pages for the long tail.

This evolves the original "Path B / Summit-as-a-preset" framing: the engine stays **VAST-first for generation**, but the **Summit analog voice is now a top-level constant** for shaping — not merely an emergent preset. See the [v4.5(C) re-positioning spec](../specs/2026-06-16-v4.5-k2061-repositioning-design.md) and the living [engine architecture register](../architecture/engine-questions.md).

## Product naming

The shipping synthesizer is **Bernie** (repo codename `k2000` stays). Bernie's built-in effects section is **Ricky** — a Summit/KDFX-style multi-FX block reached via an *Advanced* button on Bernie's front panel (Arturia-style), inserted **after the amp/VCA**, with a subset of its FX blocks also exposed as VAST DSP blocks (see **v8**).

## Engine principles (cross-cutting)

- **The model:** `[ K2061 Dynamic VAST source + DSP graph (variable) ] → [ constant Summit spine: selectable filter model (Huggett default) + drive → VCA, with envelopes/LFOs/mod matrix/voice modes ]`, **per voice, per Layer**.
- **Locked decisions** (from the register): **full stereo throughout** · **256-voice** target · spine + modulation **per-Layer** · **synth-only** sources now (sample/keymap → v11+).
- **GUI grows with the engine, toward a fixed aesthetic** — no phase ships a feature you can't drive. The Summit UI foundation (v4.5/B) lands before v5; **each phase advances the visual design incrementally toward the target Summit aesthetic** (sketches + direction already defined) rather than deferring a "real GUI" to the end. Sustained visual polish is load-bearing for interest, not a final-phase afterthought.
- **Performance is a gate** — at 256 voices × full stereo × graph DSP, every phase meets a per-voice CPU budget with profiling as a release gate, and uses perf-friendly structures from v5 (SIMD-friendly buffers, denormals, efficient graph execution).

## Phases

| Phase | Theme | What lands |
|---|---|---|
| **v1** ✅ | Skeleton end-to-end | 1 oscillator, 2-slot DSP chain (SVF + waveshaper), ADSR amp, 8-voice polyphony, plain JUCE UI. Linux dev + Windows builds via CI. **Shipped 2026-05-30 as v1.0.0.** See [v1 spec](../specs/2026-05-25-v1-skeleton-design.md). |
| **v2** ✅ | **Layer abstraction** | `Voice` split into per-note runtime + a `Layer` owning DSP blocks + ParamSnapshot. **Shipped 2026-06-14 as v2.0.0.** See [v2 spec](../specs/2026-06-11-v2-layer-abstraction-design.md). |
| **v3** ✅ | **Algorithm abstraction** | Selectable algorithm = ordered walk through a per-Layer block palette; 4-entry library; block-type param namespace; cumulative migration. [ADR 0008](../decisions/0008-algorithm-selection-and-param-namespace.md). **Shipped 2026-06-15 as v3.0.0.** See [v3 spec](../specs/2026-06-14-v3-algorithm-abstraction-design.md). |
| **v4** ✅ | **Multi-Layer Programs** | `Program` holds 2 Layers (generic over count), shared 64-voice pool, per-layer key/vel/channel/level routing → Layer/Split/Dual. [ADR 0009](../decisions/0009-multi-layer-program.md). **Shipped 2026-06-16 as v4.0.0.** See [v4 spec](../specs/2026-06-15-v4-multi-layer-programs-design.md). |
| **v4.5** | **K2061 re-positioning + Summit UI foundation + KB fix** | *(C)* re-position the engine to K2061/K2088 VAST with the constant Summit spine ([spec](../specs/2026-06-16-v4.5-k2061-repositioning-design.md), this rewrite, ADR 0010). *(B)* the Summit-aesthetic, extensible **UI foundation** — load-bearing, lands before v5. *(A)* fix the Pirkle synth-book OCR in `k2000-kb`. |
| **v5** | **Constant Summit voice** *(keystone)* | Build the always-present spine as a **selectable `FilterModel` library with live click-free hot-swap** ([ADR-0011](../decisions/0011-selectable-spine-filter-library.md), [deep-dive](#v5-deep-dive--the-selectable-summit-spine), [v5 spec](../specs/2026-06-16-v5-constant-summit-voice-design.md)); ship **Huggett** (dual TPT SVF + separation, gray-box) as the flagship default + drive → VCA, with amp/mod envelopes, LFOs, mod matrix, voice modes — **stereo, per-Layer**. Promote the filter out of the optional palette. UI: the permanent Summit front panel. Delivered as **v5 point releases** (see the table below). |
| **v6** | **Dynamic VAST graph routing** *(keystone)* | Generalize `Algorithm` from a fixed linear list into a wired **graph**: blocks with configurable inputs/outputs, serial **+ parallel**, splits/joins, feeding the spine. v3's library becomes "factory" presets. Solves the variable-graph parameter model (register Q5). UI: the visual wiring/graph editor. |
| **v7** | **Source & DSP block library** | Sources as blocks — Summit 3-osc (default, drift/sync/FM), **KVA**, wavetable, noise — + DSP blocks (mixer, ring mod, shaper/drive variants, EQ, timbre filters, hard sync). UI: dynamic per-block knob-clusters. |
| **v8** | **Ricky — the FX section** | **Ricky**: a Summit/KDFX-style multi-FX block reached via an *Advanced* button on Bernie's panel (Arturia-style), inserted **after the amp/VCA** — per-layer insert + common insert + two aux chains, Summit effect types within them. A subset of Ricky's FX blocks are also exposed as **VAST DSP blocks** (usable inside the v6 graph). UI: the Advanced FX panel + chain editor. |
| **v9** | **Multipart + Scale + Cascade + Multis** | **Summit-style A/B multipart control** (A · B · Split · Layer) as the front-panel surface over the v4 multi-layer engine (the 2-part A/B surface can land earlier via the incremental GUI); toward **32 layers / 256 voices**; Cascade Mode; Multis (≤16 programs). UI: A/B part selector, split-point editor, layer manager + multi/zone view. |
| **v10** | **FM layers** | 6-operator FM source. UI: FM operator panel. |
| **v11+** | **Polish + commercialization** | Sample/keymap sources, preset browser, macOS, performance — and **demo mode + license unlock** (Bernie must be buyable for ~the price of a glass of beer). *(Visual design is no longer parked here — it advances incrementally each phase toward the target aesthetic.)* *(Intonation/tuning maps + KSR were dropped from the roadmap — not wanted yet.)* |

## v5 point releases

The v5 keystone (selectable spine + Huggett foundation) shipped as **v5.0.0** (Plan 1). The remaining v5 work is delivered as point releases:

| Release | Lands |
|---|---|
| **v5.0** | **Nonlinear Huggett** — three asymmetric **tanh** stages (pre-drive, self-limiting resonance saturator, post-drive) **+ a clean, always-available HP pre-filter** before the main multimode filter. [Spec](../specs/2026-06-17-v5-huggett-nonlinear-hp-prefilter-design.md). **Remediated 2026-06-20 (post-UAT):** ADAA dropped → plain tanh (measured cleaner; see [OverdriveDiagnosticTests](../../tests/OverdriveDiagnosticTests.cpp)) · `g_eff` "darken when loud" droop removed · HP drive removed (HP now clean) · master gain default −9 dB · HP resonance capped at 0.15. Oversampling deferred to v5.1. |
| **v5.1** | **HQ oversampling tiers** (Light/Normal/Heavy/Full, with independent **live** and **render** selectors) **+ an on-screen visual keyboard** (playable + incoming-MIDI display). |
| **v5.2** | **Real Separation / dual-filter** — completes the flagship Huggett: two independently-tuned 2-pole sections, proper **Separation** (two distinct resonances), and the Summit **series/parallel dual routings** (LP→HP, LP+HP, …). Works in **12 and 24 dB** (today's separation is dead in 12 dB and only shifts the corner in 24 dB). Grounded in the [Huggett dossiers](../architecture/huggett-filter.md) §41/§99. |
| **v5.3** | **Moog ladder** — second filter model *(was v5.2)*. |
| **v5.4** | **Oberheim SEM** — third filter model *(was v5.3)*. |

*Cross-cutting in the v5.0 cycle:* a docs/README/ADR audit & groom (✅ shipped), and a SAST + SCA security-scan CI baseline (queued — see follow-ups below). DSP references for v5.0: [tpt-svf-core.md](../architecture/tpt-svf-core.md), [nonlinear-filter-modeling.md](../architecture/nonlinear-filter-modeling.md). ([antialiasing-adaa.md](../architecture/antialiasing-adaa.md) is **superseded** — ADAA was removed in the 2026-06-20 remediation; the shapers are plain tanh.)

### v5.0 follow-ups — queued (not yet scheduled)

Work left on the table after v5.0 shipped, paused for the user to pick the next task. Order here is rough priority, not a commitment.

| # | Item | Type | Status | Where |
|---|---|---|---|---|
| 1 | **SAST + SCA security-scan CI baseline** | Cross-cutting CI (user-requested) | Queued — short plan, no spec needed | scanners (cppcheck/clang-tidy/CodeQL + dep/CVE scan; JUCE is a submodule) in `.github/workflows/`; run a baseline, triage |
| 2 | **HQ oversampling tiers + visual keyboard** | Point release — **v5.1** (already slotted above) | Scoped, not built; brainstorm → spec → plan → subagent execution | hand-rolled heap-free inline 2/8/32× (NOT `juce::dsp::Oversampling`, Q12); `juce::MidiKeyboardComponent` |
| 3 | **UI cosmetics** (from UAT) | Polish / bugfix | ✅ **done 2026-06-20** | `src/PluginEditor.cpp`: widened combos (LnF arrow-zone + 13 px font), master gain → horizontal slider, HP-enable toggle widened |
| 4 | **Summit A/B calibration** of the `// CALIB` constants | Manual tuning (needs hardware) | Queued — gray-box literature defaults, not yet pinned | `HuggettFilter.h`, `HuggettHpStage.h`, `NlSvfCell.h` voicing/droop constants |

## Testing harness — next deliverable (cross-cutting)

A robust, reusable **per-component test harness** for Bernie's DSP (and later Ricky's FX) is a **first-class part of the deliverable**, not ad-hoc test code — and it is the **immediate next work** (own spec → build). Requirements:

- **Grounded in state-of-the-art analog-circuit-emulation research** (VA/ZDF modelling, aliasing measurement, nonlinear-filter validation) — objective metrics, not by-ear.
- **Robust + reusable**: signal generators, FFT/aliasing + click/discontinuity detection, frequency-response & self-oscillation checks, NaN/denormal guards, and per-component **pass/fail gates** that fail CI on regression.
- **Seeded by** [`OverdriveDiagnosticTests`](../../tests/OverdriveDiagnosticTests.cpp), which already caught the v5.0 ADAA/droop/separation defects by ear-independent measurement.
- **JUCE or a standalone framework** — the choice is settled in the harness's own spec.

It grows to cover every spine / source / FX component as it lands, and gates releases.

## v5 deep-dive — the selectable Summit spine

v5 builds the spine as a **selectable `FilterModel` library** (append-only, stable-ID — the `AlgorithmLibrary`/[ADR-0008](../decisions/0008-algorithm-selection-and-param-namespace.md) idiom) with **live, click-free hot-swap**: an automatable per-Layer `spine.filterModel` selects the model, and switching crossfades two **heap-free, in-place** per-voice instances (equal-power) so it never clicks. Params are a **common core** (cutoff/res/drive/output — always front-panel + mod-targetable) plus **per-model namespaced banks**. The full architecture (interface, library, `SpineFilterSlot`, hot-swap, migration) is in the [v5 spec](../specs/2026-06-16-v5-constant-summit-voice-design.md); locked as **L7**, with the live-switch constraints recorded in **Q12/Q17–Q19**.

The **flagship model, Huggett**, is grounded in external deep research on the Chris Huggett lineage (OSCar → Peak → Summit): the full [filter architecture report](../architecture/huggett-filter.md), independently corroborated by the [Moog/Huggett research brief](../architecture/moog-ladder.md). The load-bearing conclusions, and how we adapt them to k2000:

**Architecture (what the lineage actually is).** Not a ladder. A **dual state-variable / OTA filter** built as **two linked 12 dB sections** with a **separation** offset between their cutoffs, combined per mode: **LP / BP / HP**, **12 or 24 dB/oct** slope, and **dual-filter combinations** (LP→HP, etc.). The sonic identity comes as much from **gain staging** as from the core — there are **three nonlinear stages**: pre-filter input **drive**, a **resonance-loop** saturator (tanh-class, which makes self-oscillation self-limit instead of blow up), and a **post-filter drive**. Implement the core as a **TPT/ZDF** state-variable structure (modulation-safe, simultaneous multimode outputs), *not* a Chamberlin or direct-form biquad. Public controls: cutoff (exp ≈16 Hz–20 kHz), resonance (tapered toward self-oscillation), separation, slope, input drive, post drive, key-track, divergence, output trim. Per-parameter smoothing, multiplicative for cutoff.

**Adaptation to k2000 (this is not an effect plugin).** The report frames a single mono/stereo effect instance with global oversampling. Our filter is the spine of **every voice — 256-voice target, full stereo, per-Layer** ([register](../architecture/engine-questions.md) Q1/Q2/Q3). Consequences:
- **Oversampling is the headline cost risk.** The report's "2× default / 4× HQ / 8× render" is per-instance; at 256 voices, per-voice oversampling of the nonlinear path is expensive. We do **not** assume global per-voice OS — the policy (conditional OS only when drive/resonance are engaged, cheaper per-voice anti-aliasing, or shared/SIMD strategies) is decided **behind the perf gate** (Q11), measured, not assumed.
- v5 **promotes** today's optional `SVFFilter` palette block into the constant spine slot — new spine params + a cumulative preset migration step.
- Start **gray-box** (tanh saturators, calibrated control laws); component-accurate **white-box OTA** modeling is explicitly future work, not v5.

**v5 build sub-sequence** (we already have the JUCE/APVTS skeleton + an SVF block, so we start past the report's scaffold step):
1. **`FilterModel` interface + `FilterModelLibrary`** (append-only, stable IDs) + per-voice **`SpineFilterSlot`** (two heap-free, in-place model slots); promote the filter out of the optional palette into the spine slot.
2. **Param model**: common core (cutoff/res/drive/output) + per-model namespaced banks; automatable `spine.filterModel` choice.
3. **Huggett** as the first library entry: linear **dual TPT SVF** core (LP/BP/HP), per-voice **stereo**.
4. Mode combiner + 12/24 dB + **separation** law (Summit dual modes first; OSCar modes stretch).
5. Per-parameter **smoothing** (cutoff multiplicative) — zipper-free under audio-rate modulation.
6. The **three nonlinear stages** (gray-box tanh); calibrate self-oscillation onset across pitch/sample-rate.
7. **Live hot-swap**: equal-power crossfade on model change; debounce / no-op-reselect policy (Q17); zero audio-thread allocation.
8. **AA/oversampling policy** (Q12) — *resolved 2026-06-20:* ADAA was implemented, then measured **worse** than plain tanh across k2000's drive range (it added inharmonic energy rather than removing it), so the shapers are **plain tanh**; per-voice oversampling is deferred to **v5.1** HQ tiers.
9. **Calibration + tests**: *linear-reference* (TPT math) · *musical-behavior* (drive/resonance/separation) · *hot-swap* (click-free, heap-free) · *preset-migration*.

The spine is a **curated, append-only filter-model library** ([ADR-0011](../decisions/0011-selectable-spine-filter-library.md)) — adding a model = one library entry + one class, presets stay stable. Planned lineup, all selectable in the Huggett slot:
- **v5.1 — HQ oversampling tiers + on-screen keyboard** (quality & playability — see the v5 point-releases table above).
- **v5.2 — Real Separation / dual-filter** completes the Huggett flagship before more models land (see the point-releases table above).
- **v5.3 — Moog ladder** (4-pole, self-oscillating; staged linear-ZDF → per-stage tanh → oversampling) *(was v5.2)*. Refs: [moog-ladder.md](../architecture/moog-ladder.md) + [SEM/Moog dossiers](../architecture/filter-dossiers-sem-moog.md).
- **v5.4 — Oberheim SEM** (2-pole multimode SVF, **non-self-oscillating**, LP→notch→HP morph + BP; feedback-diode limiter) *(was v5.3)*. Ref: [SEM dossier](../architecture/filter-dossiers-sem-moog.md).
- **Later** — diode ladder and other legally-clear analog topologies.

Each model is gray-box, behavior-faithful, and calibrated against reference hardware; topologies are all unpatented or patent-expired (ADR-0011).

New open questions this raises are registered as **Q12–Q19** in the [engine register](../architecture/engine-questions.md).

## Locked & resolved decisions

Engine-level decisions and open questions live in the living [engine architecture register](../architecture/engine-questions.md) (groomed for consistency before each spec/ADR). Earlier carried note:

- **Can a VAST algorithm use the same function block more than once? → Yes** (VAST permits a function block to repeat; per the K2088 lineage). v3 shipped the simpler per-block-type namespace; the positional / per-F-block model (and any param migration) is folded into the **v6/v7** Dynamic-VAST + block-library work.

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
