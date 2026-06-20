# v5 — Huggett nonlinear stages + dedicated HP pre-filter

**Status:** Implemented — merged to main as plugin 5.1.0 (2026-06-19).
**Version:** 5.01 (doc track). **Release:** completes the **v5 Huggett phase** (Plan 2, after the shipped Plan 1 foundation); plugin bump is a minor (suggest 5.1.0), exact number TBD.

**Scope:** Turn the spine's flagship **Huggett** model from *linear* (today) into a **true-to-life, nonlinear** filter — three asymmetric tanh-class stages grounded in measured data — and add a **dedicated, always-available HP pre-filter** in front of the main multimode filter. Ships at **Light (ADAA) quality**; the HQ oversampling tiers and the on-screen keyboard are split to the next point release (see Release map). One combined plan, per the user's choice.

**Context:** Builds on [v5 — Constant Summit voice](2026-06-16-v5-constant-summit-voice-design.md) (Plan 1, merged) and resolves the deferred nonlinear work of register **Q15** + the anti-aliasing policy of **Q12** ([engine-questions.md](../architecture/engine-questions.md)). DSP detail lives in three companion architecture docs written 2026-06-17:
- [tpt-svf-core.md](../architecture/tpt-svf-core.md) — the linear SVF foundation (unchanged).
- [nonlinear-filter-modeling.md](../architecture/nonlinear-filter-modeling.md) — the gray-box nonlinear stages (measured OTA tanh, `fbExtra` resonance saturator, drive shapers, ZDF solve, fast-tanh).
- [antialiasing-adaa.md](../architecture/antialiasing-adaa.md) — ADAA + the quality-tier policy.

Primary sources (PDFs + extracted facts) in the gitignored [drop zone](../incoming_research/INDEX-huggett-sources-2026-06-17.md).

## Thesis

The Summit identity is **dirty, edgy, self-limiting resonance and gain-staged drive** — not a clean SVF. We add that character on top of the validated linear TPT core **without** breaking the heap-free, per-voice, stereo, 256-voice constraints, and we expose the Summit's HP→LP series routing as a permanent, dedicated HP stage in front of the selectable model.

## Approach (locked: A — shared primitives, two stage classes)

Factor the nonlinear machinery into small reusable primitives that **both** the main Huggett and the HP pre-stage compose. Rejected alternatives: one configurable `HuggettFilter` used twice (conflates swappable-model with fixed-stage ownership); baking HP inside `HuggettFilter` (would vanish when Moog/SEM is the active model — but the HP must stay always-available).

### Signal chain (per voice, in the spine, after the graph walk)

```
graph (mono) → dual-mono ─┐
                          │  HP PRE-STAGE  (enable-gated; fixed Huggett HP)
                          │    hp pre-drive (asym tanh + ADAA)
                          │    → HP cell(s)   12/24 dB · resonance · res-loop limited
                          ▼
                          │  MAIN MODEL  (selectable; Huggett today)
                          │    main pre-drive = spine.drive (common core, asym tanh + ADAA)
                          │    → cell A (+ cell B: 24 dB / separation)  with res-loop saturator
                          │    → post-drive  (spine.huggett.postDrive, asym tanh + ADAA)
                          ▼
                       DC blocker → output gain (spine.output) → voice mix
```

## Components & file plan

**New shared primitives (`src/dsp/spine/`):**
- `AsymmetricSaturator` (header-only value type) — asymmetric `tanh(g·x + bias)` with **1st-order ADAA** (`logcosh` + midpoint fallback for the ill-conditioned divide), partial RMS level-compensation, per-channel ADAA memory. Gated on drive being engaged.
- `DcBlocker` — one-pole per channel, `fc ≈ 5–10 Hz`.
- Nonlinear SVF cell — extend `TptSvfCell` (or a sibling `NlSvfCell`) with the **`fbExtra` resonance saturator** (delta-injection that preserves the closed-form solve and stays bit-for-bit linear at low level) + the per-block **`g_eff` integrator droop**. Linear behaviour and existing tests preserved when drive/resonance are low.

**Changed (`src/dsp/spine/`):**
- `HuggettFilter` — wire the currently-ignored `setCommon` **drive** (pre-filter input drive), add the resonance saturator, add **post-drive** (new bank param), add `g_eff` droop; **branch-free inner loop templated on `<Mode, Slope, bool DriveOn>`**; **stereo-as-2-lanes**. `DriveOn=false` path == today's linear filter.
- `HuggettHpStage` (new, **not** a `FilterModel`) — HP-only, 12/24, resonance, own pre-drive; reuses the shared primitives at **lighter voicing** (gentler asym bias, smaller/no `g_eff` droop). Owns its own per-voice state.
- `SpineFilterSlot` — gains the HP pre-stage's per-voice state; runs it (enable-gated) **before** `model->processStereo`. Still owns only state (no cached model — preserves the Plan-1 Layer-1 fix).

**Changed (engine/params/UI):**
- `Layer` — configure both stages each block from the snapshot (HP setters + the wired main drive/post-drive).
- `ParamSnapshot` / `Parameters` — add the new params (below).
- `PluginEditor` — Layout B (below).

## Parameters

New (per-Layer, spine-level — the HP is a fixed spine stage, so its params are spine-level, not in the swappable model bank):

| Param | Default | Notes |
|---|---|---|
| `spine.hpEnable` | **off** | toggle the HP pre-stage (hard-bypass → skips its CPU) |
| `spine.hpCutoff` | ~20 Hz | exp law; transparent at the low default |
| `spine.hpResonance` | 0 | tapered toward self-oscillation |
| `spine.hpSlope` | 12 dB | 12/24 |
| `spine.hpDrive` | 0 dB | HP's own pre-drive |
| `spine.huggett.postDrive` | 0 dB | main filter post-drive (Huggett bank; Q15 stage 3) |

Reused as-is: main pre-drive = existing `spine.drive`; main cutoff/res/mode = `layer.filter.*`; model/slope/separation/output already exist.
**Deferred to v5.1:** global `quality` + `renderQuality` selectors (the HQ oversampling tiers). v5.0 runs the **Light/ADAA** path only.

## UI (locked: Layout B — stacked)

The Filter section becomes a single column: an **HP PRE band** (enable toggle · HP Cut · HP Reso · HP Slope · HP Drive), a divider, then the **main filter** rows (Model · Type · Cutoff · Reso // Slope · Separation · Drive · **Post**). The section grows a bit taller; the window grows modestly (derive size from content, per the de-cramp practice). Bind the new controls in `bindLayer`; the existing cutoff/reso/type binds are unchanged (they already drive the spine).

## DSP internals (summary — full detail in the companion docs)

- **Core solve:** instantaneously-linearized ZDF — keep the Cytomic cell, add a cheap **per-block `g_eff`** droop for the OTA "darkens when loud." No per-sample Newton (full Newton rejected on cost; one-Newton-step reserved for a future HQ tier).
- **Resonance saturator (the "spit"):** inject `k·(φ(bp_prev) − bp_prev)` into the cell input — keeps the closed-form solve (~1 tanh/sample), self-limits self-oscillation as a genuine limit cycle, and is **base-rate (not ADAA'd)** because feedback-ADAA needs a per-sample solve and the products are masked near the peak.
- **Drive shapers (pre/post):** asymmetric `tanh(g·x + bias)`, fixed per-stage `bias` (HP lighter), partial RMS compensation (tone not loudness), **1st-order ADAA** (`logcosh`, midpoint fallback, ½-sample delay).
- **Grounding:** OTA tanh is **measured** (Wasp DAFx-22: `α=0.8635, β=0.9408`); resonance limiting mirrors the hardware antiparallel diodes; the Wasp's CMOS-inverter dirt is **not** ported (Summit is the clean end of the family).
- **Fast-tanh split:** monotonic Padé for the resonance loop; true `tanh`+`logcosh` for the ADAA'd shapers.
- **DC blocker:** one per channel at the voice output.

## Performance (resolves Q12 for v5.0; Q11 gate)

- **Branch-free** templated `<Mode, Slope, DriveOn>`; **stereo-as-2-lanes** SIMD; denormals via `ScopedNoDenormals` + explicit flush on the resonance state.
- AA conditional on drive (per-block branch); when all drives are off the filter is the validated linear path at zero nonlinear cost.
- **State stays heap-allocated-at-`prepare`** for this milestone (RT-safe; the interface already allows it). The in-place value-type migration the research recommended is **deferred to Plan 3** (live hot-swap), where it is actually required.
- No per-voice `juce::dsp::Oversampling` object. Profile at 256 voices × stereo behind the perf gate.

## Preset migration

Additive only: the new spine params take defaults on load (`hpEnable=off` ⇒ existing presets sound identical; `postDrive=0`, `drive` already present). The v4→v5 shim is unchanged. A test confirms an existing v5.0-foundation preset loads with the HP disabled and unchanged output.

## Testing

- **Linear-equivalence:** at zero drive / low resonance the output matches the current linear filter (guards the `fbExtra` delta + conditional-drive design); existing `SpineFilterTests` stay green.
- **Self-oscillation:** onset monotonic in resonance and repeatable across cutoff; pitch tracks cutoff within ±~1% over 4+ octaves; amplitude bounded (no NaN over 10 s at max res).
- **Drive/harmonics:** harmonic energy rises monotonically with drive; **even harmonics present** (asymmetry working); RMS stays within ±2 dB across the drive sweep (compensation); ADAA alias floor ≥ ~50–60 dB below fundamental, on-vs-off comparison.
- **DC:** output offset under threshold after the DC blocker.
- **HP stage:** slope 12 vs 24 dB; corner tracks independently of the main filter; HP self-oscillation bounded; `hpEnable=off` is true bypass (bit-identical to no-HP path).
- Harness mirrors the existing `MultiLayerTests` / `SpineFilterTests` patterns. Calibration constants (α/β, biases, `g_eff`, Q-taper, HP biases) are pinned manually against the user's Summit (a listening/measurement step, not a unit test).

## Scope boundaries

- **In (v5.0):** the three nonlinear stages, the dedicated HP pre-filter (enable/cutoff/reso/slope/drive), post-drive, Layout B, Light/ADAA quality, additive preset migration, tests.
- **Deferred — v5.1:** HQ oversampling tiers (`quality`/`renderQuality`, hand-rolled inline 2/8/32×) + the on-screen keyboard.
- **Out (later):** the nine Summit dual routings + separation law (Q14 stretch); white-box OTA; one-Newton-step HQ solve; in-place `State` migration (Plan 3 hot-swap); Moog (v5.2) / SEM (v5.3).

## Release map (this work in context)

| Release | Deliverable |
|---|---|
| **v5.0** (this spec) | Nonlinear Huggett + HP pre-filter — Light/ADAA |
| v5.1 | HQ oversampling tiers (live + render selectors) + visual keyboard |
| v5.2 | Moog ladder *(was v5.1)* |
| v5.3 | Oberheim SEM *(was v5.2)* |
| v5.0 cycle (cross-cutting) | SAST/SCA CI baseline + docs/README/ADR audit |

*"v5.0/.1/.2/.3" are roadmap **milestone labels** for sequencing — distinct from plugin SemVer (which bumps minor per feature; this work ≈ 5.1.0). Shifts Moog/SEM out one slot vs the prior roadmap.*

## Open questions / risks
- **Calibration needs the user's Summit** — the voicing constants are a manual A/B step; the code ships with measured-literature defaults until pinned.
- **Perf at 256 voices** — the nonlinear path cost is measured behind the Q11 gate; the conditional-on-drive and stereo-2-lanes design is the mitigation. If over budget, voices-as-lanes SIMD is the next lever.
