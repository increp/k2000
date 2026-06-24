# Moog Filter — Spec 2: Go-Live + Filter-Section Consolidation — Design

**Version:** 5.08 (artifact; distinct from plugin SemVer)
**Date:** 2026-06-23
**Status:** Approved (brainstorm) — pending spec review
**Roadmap item:** `v5.4` — Moog ladder (2nd filter model), **Spec 2 of 2**.
**Builds on:** Spec 1 (Moog DSP, merged to `main` @ `8de45b6`) — the fused `MoogLadder.cmajor` → in-place adapter → `MoogLadder : FilterModel`, validated test-target-only.
**Subsumes:** the deferred spine filter-section consolidation (`[[followup-moog-filter-consolidation]]`).
**Branch:** `feat/moog-spec2` (off `main`).

---

## 1. Purpose & premise

Spec 1 built the Moog DSP but kept it **test-target only** — it is not registered, so it doesn't appear in the plugin's filter selector (correctly: the live dropdown shows Huggett only). **Spec 2 makes Moog a real, playable filter model** and, in the same pass, consolidates the spine filter section (the per-model UI/param surgery is forced by adding a second model, so we do it once).

After Spec 2: selecting "Moog" in the Filter dropdown gives a complete, legible control surface (mode, slope, drive, and a played-note sub-osc bass voice); Huggett↔Moog crossfades click-free (the v5.1 hot-swap, now *live* with a real second model); and the dead legacy "Type" control is gone.

**Explicit non-constraint:** **preset/patch backward-compatibility is NOT a requirement** (`[[feedback-no-preset-backcompat]]`). No migration code, no "presets load unchanged" tests; param defaults/structure are chosen on UX + engineering merit, and dead params are removed outright.

## 2. Goals / non-goals

**Goals**
- Register Moog; it appears in the Filter selector and is fully playable.
- Complete Moog's mode set: **12 dB and 24 dB in all three modes** (LP/BP/HP) — a small DSP enhancement (§3).
- Moog's own controls (mode + bass voice) on the front panel, per-model show/hide (§9).
- Per-voice sub-osc fundamental wired from the played note (§7).
- Filter-section consolidation: remove dead "Type" + `SVFFilter` block; add Notch to Huggett; rename Routing → "Filter Routing" (§8).
- Huggett↔Moog live crossfade verified click-free.

**Non-goals**
- The generic per-model "bank" abstraction in `Layer` — explicit `huggett_`/`moog_` dispatch is cleaner for two models; defer the generalization until a third model lands (YAGNI).
- Preset/patch migration or backward-compat (see §1).
- Oversampling, the characterization harness (separate efforts, shelved/roadmap).

## 3. Moog DSP enhancement — 12/24 dB in all modes

Spec 1 left each mode at one fixed order (LP had both via slope; BP was 2-pole only; HP was 4-pole only). Spec 2 completes the **mode × slope grid** to 6 transfer functions, all from the existing ladder taps `y1..y4` and the clean `y0` (the HP-`y0` fix):

| Mode | 12 dB (Slope=0) | 24 dB (Slope=1) |
|------|-----------------|-----------------|
| LP | `y2` *(exists)* | `y4` *(exists)* |
| BP | BP2 = `4(y2−y3)` *(exists)* | **BP4** *(new — 4-pole pole-mix, steeper skirts)* |
| HP | **HP2 = `y0−2y1+y2`** *(new — 2-pole)* | HP4 = `y0−4y1+6y2−4y3+y4` *(exists)* |

**Change:** add the two missing variants (**BP4**, **HP2**) via standard Oberheim Xpander pole-mix weights. The `tapOut` selector — in *both* the nonlinear and linear branches, mirroring the HP-`y0` fix — becomes a `(modeSel, slopeSel)` switch. Each new variant gets its own `// CALIB` weight set so its passband/peak level sits comparable to the others (same calibration discipline Spec 1 used for BP). Exact coefficients are pinned during implementation against the tests, not in this spec.

**Untouched:** the per-stage `tanh`, the closed-form solve, resonance taper/tuning comp, the limiter + DC blocker, and the sub-osc sum — all sit before/after the tap. So this is a localized edit to tap selection + new CALIB constants, then Docker regen (`tools/cmajor/cmaj-codegen.sh … MoogLadder.cmajorpatch …`, committed with the regenerated `generated/MoogLadder.h`).

## 4. Architecture & touch-points

Spec 2 follows the existing spine patterns: shared model config on `Layer` (+ cached per-model pointers), per-voice state in `SpineFilterSlot`, active model passed per-call. Touch-points:

1. **Moog DSP** — `MoogLadder.cmajor` (+ regen) — §3.
2. **Library** — `FilterModelLibrary.cpp`: register `"moog"` + Q18 `static_assert(sizeof(MoogLadder::VoiceState) <= kMaxSpineStateBytes)`.
3. **Params** — `Parameters.cpp` / `ParamSnapshot`: add `spine.moog.*`; **remove** `filter.type`/`svfType` (§5, §8).
4. **Dispatch** — `Layer.cpp`: cached `moog_`, explicit Moog setter block; remove the `svfType` legacy fallback; add Notch routing for Huggett (§6, §8).
5. **Played-note** — `FilterModel` base hook + `Voice`/`SpineFilterSlot` wiring (§7).
6. **Consolidation** — remove `SVFFilter.{h,cpp}` + its `BlockTypeId` entry; Notch in `HuggettFilter`; rename in UI (§8).
7. **UI** — `PluginEditor.cpp`: Moog in the selector, per-model show/hide, Type combo removed, Routing → "Filter Routing" + Notch (§9).
8. **Version + roadmap** — §12.

## 5. Params

**New (Moog-namespaced):**
- `spine.moog.mode` — Choice {LP, BP, HP}, default **LP**
- `spine.moog.bassAmount` — float 0..1, default **0** (Moog = pure ladder until dialled in)
- `spine.moog.bassWave` — Choice {Sine, Triangle, Saw}, default **Sine**
- `spine.moog.bassOctave` — Choice {0, −1 oct, −2 oct}, default **0**

**Removed (dead):** `filter.type` and the `ParamSnapshot::svfType` field (the legacy LP/HP/BP/Notch combo and its Huggett-routing fallback) — deleted outright (no migration).

**Reused shared (Moog reads what exists):** `spine.filterModel` (Moog = index 1; default selection stays Huggett on UX merit), `filter.cutoff`/`filter.resonance` + `spine.drive` (→ `setCommon`), `spine.slope` (→ Moog `setSlope`, now meaningful in all modes), `spine.separation` (→ Moog `setSeparation`, a no-op it ignores), `spine.output`/`spine.hp.*`/`spine.modelFadeMs`.

**Changed (Huggett):** `spine.huggett.routing` gains a **Notch** entry → {LP, BP, HP, Notch}.

`ParamSnapshot` gains `moogMode`, `moogBassAmount`, `moogBassWave`, `moogBassOctave`; loses `svfType`.

## 6. Layer per-model dispatch

`setCommon(cutoff, res, drive)` already fans out to **all** models (so cutoff/res/drive reach Moog and the crossfade keeps tracking). Add a cached `moog_ = dynamic_cast<MoogLadder*>(...)` at `prepare`, then in `updateParameters` two explicit blocks side by side:

```cpp
if (huggett_) {
    huggett_->setRouting(static_cast<HuggettFilter::Routing>(s.huggettRouting));  // now incl. Notch
    huggett_->setSlope(s.spineSlope == 0 ? db12 : db24);
    huggett_->setSeparation(s.spineSeparationOct);
    huggett_->setPostDrive(s.huggettPostDrive);
}
if (moog_) {
    moog_->setMode (static_cast<MoogLadder::Mode>(s.moogMode));
    moog_->setSlope(s.spineSlope == 0 ? MoogLadder::Slope::db12 : MoogLadder::Slope::db24);
    moog_->setBass (s.moogBassAmount, s.moogBassWave, s.moogBassOctave);
    moog_->setSeparation(s.spineSeparationOct);   // no-op, for symmetry
}
```

The `svfType` legacy fallback (`switch(s.svfType)`) is **removed** — Huggett routing now comes straight from `spine.huggett.routing`. The generic "each model declares its banks" abstraction is **deferred** (two explicit blocks are clearer for two models).

## 7. Played-note plumbing (the sub-osc fundamental)

The sub-osc is **per-voice**; Moog's config is **shared**. Spec 1 built the per-voice half (`MoogLadder::setFundamental(State&, hz)` → lane adapters; re-forwarded each block in `processStereo`). Spec 2 adds the base hook + the wiring:

- **Base hook** (Spec 1 deferred this): `virtual void FilterModel::setVoiceContext(State&, float fundamentalHz) const noexcept {}` (default no-op). `MoogLadder` overrides → `setFundamental`. Pitch-blind models (Huggett) ignore it.
- **Wiring:** `SpineFilterSlot::processStereo` gains a `float fundamentalHz` argument; before filtering it calls `current->setVoiceContext(*activeState, fundamentalHz)` (and the incoming state during a live crossfade). `Voice::process` passes the **same `hz` it already computes for the oscillator** (`midiToHz(note) × tune`) — so the sub locks to the played note and follows the tune param, recomputed per block.
- **No per-note phase reset** — the sub free-runs; only the frequency changes on a new note (click-free). `noteReset` stays wired into the full `reset()` (prepare/voice-init), not per note.
- **Polyphony:** `bassAmount/wave/octave` are model-wide; each voice's fundamental is its own note — a chord gives each voice its own in-tune sub.

The engine has no pitch bend / glide / portamento today, so "track the played note" = the static per-note `hz` (plus tune automation).

## 8. Filter-section consolidation

- **Remove the dead "Type" control:** delete `filter.type` (param) + `ParamSnapshot::svfType` + the `Layer` `switch(s.svfType)` fallback + the retired `SVFFilter` palette block and `SVFFilter.{h,cpp}` + its `BlockTypeId::SvfFilter` enum entry (verify no algorithm in `AlgorithmLibrary` references it — v5 already retired it from the per-voice graph). Adjust `Layer::palette_` sizing accordingly.
- **Notch-after-HP for Huggett:** add `Notch` to `HuggettFilter::Routing` (enum value after `HP`), a `resolve()` case driving `NlSvfCell`'s existing Notch tap, and the routing param's item list → {LP, BP, HP, Notch}.
- **Rename:** the UI "Routing" label → **"Filter Routing"** (`spineRoutingLbl_`).

No preset migration — the Type param simply ceases to exist.

## 9. UI (`PluginEditor.cpp`)

The filter section keeps its shared controls always visible and **swaps the model-specific block** on the Filter selector (validated via the brainstorm mockup):

```
┌─ FILTER ───────────────────────────────────────────────────────┐
│  Filter [ Huggett | Moog ▾ ]   Cutoff(o)  Reso(o)  Drive(o)     │  shared
│  Slope  [ 12 | 24 dB ▾ ]       Output(o)   + HP sub-section     │  shared
│  ───────────── model-specific (show/hide) ─────────────────     │
│   Huggett →  Filter Routing[ LP/BP/HP/Notch ▾ ]  Post Drive(o)  Separation(o)
│   Moog    →  Mode[ LP/BP/HP ▾ ]  Bass(o)  Wave[ Sine ▾ ]  Octave[ 0 ▾ ]
└────────────────────────────────────────────────────────────────┘
```

- **Always visible (shared):** Filter selector (Huggett + Moog), Cutoff, Resonance, Drive, Slope, Output, HP sub-section.
- **Model-specific, toggled:** Huggett → Filter Routing + Post Drive + Separation; Moog → Mode + Bass + Wave + Octave.
- **Removed:** the "Type" combo (gone for both models — §8).
- **Mechanism:** an `updateModelVisibility(selectedModel)` called from the Filter combo's change handler + `resized()`, flipping `setVisible(...)` on the two groups. Fixed widgets bound to their params — no generic abstraction.
- **Style:** Moog's Mode/Wave/Octave are front-panel combos and Bass a knob — front-panel immediacy, no menu-diving (`[[feedback-no-menu-diving]]`).

## 10. Testing strategy

Spec 1-style behavioral + integration (no characterization harness yet; no compat tests):

- **DSP (BP/HP slope):** `MoogLadderTests` — BP24 skirts steeper than BP12; HP12 low-rolloff gentler than HP24; each variant's passband level within a sane band; **linear path bit-stable** at res=0/drive=0. Regenerate the header.
- **Library:** `FilterModelLibraryTests` — Moog registered (count/names/create); Q18 `static_assert` holds.
- **Dispatch:** `LayerTests` — selecting Moog routes mode/slope/bass; Huggett path (incl. new Notch) intact; `setCommon` fans out.
- **Played-note:** `setVoiceContext` base no-op; `MoogLadder` override sets the fundamental; a note with bass>0 produces sub-osc energy at that pitch; polyphonic.
- **Hot-swap:** `ModelHotSwapTests` — Huggett↔Moog crossfade click-free.
- **Consolidation:** `filter.type`/`SVFFilter` removed and the build is green (no dangling refs); Huggett Notch attenuates the center band.
- **Smoke:** Windows CI; then **UAT in Ableton** — the first time Moog is auditionable.

## 11. Scope boundaries / deferred

- **Deferred (YAGNI):** the generic per-model "bank" dispatch abstraction — revisit when a 3rd filter model is added.
- **Not here:** oversampling (roadmap), the SOTA characterization harness (`feat/filter-characterization-harness`, shelved), the Arturia calibration pass.
- **Removed, not migrated:** the Type control / `SVFFilter` block (no preset compat).

## 12. Version + roadmap

- Bump the plugin version (CMake `VERSION`) — Moog-live is the first user-facing Moog, a feature release — and ensure the panel label derives from `JucePlugin_VersionString` (`[[release-version-surface]]`).
- Update the roadmap dashboard (`tools/roadmap-dashboard/roadmap.json`) on this branch (`[[feedback-roadmap-working-tree]]`): mark `v5.4` Moog shipped (2nd live filter model; hot-swap now live).

## 13. Open questions (resolve during planning)

- Exact BP4 / HP2 pole-mix coefficients + level CALIB — pinned against the §10 DSP tests during implementation.
- `BlockTypeId::SvfFilter` removal mechanics — confirm no algorithm references it; decide whether to renumber the enum or leave a gap.
- Bass octave display labels ("0 / −1 oct / −2 oct" vs "Unison / Sub / Sub-2") — cosmetic, pin in the plan.

## 14. Success criteria

- "Moog" appears in the Filter selector and is fully playable; its panel shows mode + slope + the three bass controls; Huggett's controls show when Huggett is selected.
- All 6 Moog mode×slope responses behave (slope audibly changes LP, BP, and HP); linear path still bit-stable.
- A played note drives Moog's sub-osc in tune, polyphonically; Huggett↔Moog crossfade is click-free.
- The dead Type control + `SVFFilter` block are gone; Huggett has a working Notch; the Routing label reads "Filter Routing".
- Build green, Windows CI green; the version surface is bumped.
