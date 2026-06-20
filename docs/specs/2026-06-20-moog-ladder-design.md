# v5.3 — Moog transistor-ladder FilterModel

> One-line purpose: specify **MoogLadder**, the second selectable spine `FilterModel` — a 4-pole, self-oscillating transistor-ladder LP built as a staged linear-ZDF core with per-stage `tanh` saturation, mirroring `HuggettFilter`'s structure and the project's current (no-OS) anti-aliasing stance.

**Version:** 5.01 (doc track)
**Status:** Design — not yet implemented. Targets roadmap **v5.3** ([phases.md](../roadmap/phases.md) v5.3 row + [v5 deep-dive](../roadmap/phases.md#v5-deep-dive--the-selectable-summit-spine)).
**Plugin SemVer:** a minor bump on ship (suggest 5.3.0; library is additive so it cannot break presets).

## Context & grounding

The spine is a curated, **append-only, stable-ID** `FilterModel` library ([ADR-0011](../decisions/0011-selectable-spine-filter-library.md), register **L7**). Huggett (id `"huggett"`) is entry 0 and the flagship; this spec adds the **second entry** (id `"moog"`). The Moog transistor-ladder topology is legally clear — Robert Moog's US 3,475,623 expired **1986** ([dossier](filter-dossiers-sem-moog.md) §"Patent / IP status", ADR-0011).

Ground sources (read before implementing):
- [filter-dossiers-sem-moog.md](../architecture/filter-dossiers-sem-moog.md) **Dossier 2** — the load-bearing Moog dossier (topology, self-oscillation at `k≈4`, bass-thinning, per-stage `tanh`, the differential-equation form, Pirkle's limiter trick, the `ddiakopoulos/MoogLadders` repo).
- [moog-ladder.md](../architecture/moog-ladder.md) — the corroborating research brief (Stilson-Smith delay-free-loop, Huovilainen, Zavalishin, D'Angelo-Välimäki).
- [nonlinear-filter-modeling.md](../architecture/nonlinear-filter-modeling.md) — the project's gray-box stance, the `fbExtra` delta technique, the **ZDF solve cost table**, fast-`tanh`, and the 256-voice perf notes. The Moog reuses this philosophy.
- [tpt-svf-core.md](../architecture/tpt-svf-core.md) — the prewarped-`g` TPT one-pole foundation.

Reference implementation to mirror in *structure* (not algorithm): `src/dsp/spine/HuggettFilter.{h,cpp}` + `src/dsp/spine/NlSvfCell.h`.

### State-of-the-art summary (external research)

| Source | Contribution | What we take |
|---|---|---|
| **Zavalishin**, *The Art of VA Filter Design* ch. 4 | Linear & nonlinear **ZDF ladder**: the four one-pole TPT stages, the global feedback, and the closed-form **delay-free-loop solve** for the linear case; Newton iteration for the nonlinear case. | The linear ZDF core (one-step solve) is our baseline; we keep the closed form and inject nonlinearity as a delta (see "Solve approach"). |
| **Huovilainen**, DAFx-04 *Non-Linear Digital Implementation of the Moog Ladder* | Per-stage `tanh` embedded in each pole; Euler integration; tuning compensation; explicitly notes oversampling is needed to tame the aliasing the `tanh` generates. | Per-stage `tanh` placement and the resonance/tuning compensation curves. We defer OS (see AA stance). |
| **Stilson & Smith**, ICMC-96 | Continuous-time analysis; shows bilinear/backward-difference create a delay-free loop needing an ad-hoc unit delay; constant-Q / decoupled-cutoff properties to preserve. | Why we must *not* silently drop a unit delay in the resonance path; the cutoff/Q decoupling to verify in tests. |
| **D'Angelo & Välimäki**, ICASSP-13 / TASLP-14 | Thermal-voltage (`V_T`) scaling, SPICE-validated, an explicit delay-free nonlinear solve. | The `tanh` argument scaling (`V_T ≈ 25 mV` thermal voltage) and a future white-box upgrade path. |
| **Pirkle**, *Moog Ladder (Biquad style)* / Addendum A11 | A hard output **peak-limiter** yields a pure self-oscillation sinusoid **without** oversampling. | The mechanism that lets us ship a clean self-oscillation at base rate (matches our no-per-voice-OS rule). |

## Thesis

The Moog identity is a **4-pole 24 dB/oct lowpass that thins the bass as resonance rises and self-oscillates to a near-sine at full resonance** — a fundamentally different voice from the Huggett SVF. We add it as a self-contained library entry that reuses the project's primitives and conventions, **without** breaking the heap-free, per-voice, stereo, 256-voice constraints, and **without** introducing a per-voice oversampling object (forbidden by L7/Q12).

## Anti-aliasing stance (locked — inherits Q12 + the Huggett session)

Follow `HuggettFilter`'s current stance exactly:
- **Plain per-stage `tanh` at base rate now.** No per-voice `juce::dsp::Oversampling` object — forbidden by the L7 in-place/heap-free hot-swap rule and resolved in **Q12** ([engine-questions.md](../architecture/engine-questions.md)).
- **No ADAA on the ladder stages.** The Huggett session *removed* ADAA (measured no better than plain `tanh` across k2000's drive range — see `OverdriveDiagnosticTests`) and removed the level-dependent `g_eff` droop. The Moog matches: plain `tanh`, no ADAA, no droop.
- **Self-oscillation cleaned by a Pirkle-style output peak-limiter**, not by oversampling — a bounded soft ceiling on the LP output so the limit cycle settles to a near-sine at base rate. This is the base-rate substitute for OS at high resonance.
- **HQ oversampling is a v5.1-tier concern**, applied uniformly by the hand-rolled inline 2/8/32× tiers (Light/Normal/Heavy/Full), *not* a per-voice OS object inside this model. The ladder core is written so a future tier wraps it transparently.

The `OverdriveDiagnosticTests` harness (FFT inharmonic-energy score) is the instrument that will decide whether base-rate + limiter is acceptable, or whether the Moog is the model that finally forces the v5.1 OS tier. **Open question O1.**

## DSP architecture

### Topology

Four cascaded one-pole TPT lowpass stages with a global resonance feedback path:

```
        ┌──────────────────────────  k·r ·────────────────────────┐  (global feedback)
        ▼                                                          │
 x ──▶ (−) ──▶ [stage1] ──▶ [stage2] ──▶ [stage3] ──▶ [stage4] ──┬─┴──▶ peak-limiter ──▶ y (LP24)
                                                                  └─ taps: y1..y4 for slope/pole-mixing
```

Continuous-time form (per the dossier, `S(·) = tanh`):
```
y1' = g·( S(x − r·y4) − S(y1) )
y2' = g·( S(y1)        − S(y2) )
y3' = g·( S(y2)        − S(y3) )
y4' = g·( S(y3)        − S(y4) )
```
where `g = tan(pi·fc/fs)` (prewarped, same as `NlSvfCell`), `r ∈ [0,4]` is the feedback amount, and `y4` is the 24 dB LP output. `r → 4` is the self-oscillation threshold (four 1-pole stages → 180° + the feedback inversion = 360°, Barkhausen).

### Stage cell

Each stage is a one-pole TPT lowpass (the same prewarped trapezoidal integrator the SVF uses, reduced to one pole):
```
v   = (in − s) · G          // G = g / (1 + g),  s = integrator state
out = v + s
s   = out + v               // = 2·out − s_old  (trapezoidal update)
```
`tanh` is applied to the **stage inputs** per Huovilainen (the transistor-pair compression), not as an outer box.

### Solve approach — **one-step (instantaneously-linearized) ZDF**, not iterative

**Decision: one-step.** Rationale, consistent with the project's [ZDF solve table](../architecture/nonlinear-filter-modeling.md#zdf-solve-strategy-cost-bounded):

1. **Cost.** Full Newton on a 4-stage nonlinear ladder is ~15–40 transcendentals/sample — ~10–25× the linear cost — and at **256 voices × stereo** that is the dominant block cost. The register **rejected** full Newton for Huggett for exactly this reason; the Moog (4 stages vs Huggett's 1–2) makes it worse.
2. **The closed-form linear ladder solve is cheap and exact.** For the *linear* ZDF ladder, the delay-free loop `x − r·y4` has a one-line algebraic solution (Zavalishin ch. 4): with `G = g/(1+g)` and `G4 = G⁴`, solve `y4 = (G4·u) / (1 + r·G4)` where `u` is the pre-feedback chain drive, then back-substitute the stage states. **Linear path is linear by construction** (mirrors the spine commit `b5ee8a2` ethos: gate nonlinearity on drive/resonance; the zero-drive/zero-res path is bit-exact linear and keeps existing regression tests green).
3. **Nonlinearity as a per-sample delta, reusing the `fbExtra` idiom.** Like `NlSvfCell::step`, compute the `tanh` correction from the *previous* sample's stage outputs and inject it as a delta on the stage inputs, then run the linear closed-form solve. This is the project's established "single-evaluation, one-sample-delayed nonlinear feedback" technique: ~4 `tanh`/sample (one per stage), **no iteration**, self-limiting (effective loop gain `r·S'(A)` falls as amplitude `A` grows → the limit cycle settles instead of diverging), and **O(x²)→0 at low level** so the model collapses to the validated linear ladder when quiet. The aggressive one-sample-delay character is musically *desirable* for a ladder ("spit"), same as the Huggett resonance loop.
4. **Self-oscillation stays bounded** by the combination of the self-limiting `tanh` delta and the Pirkle output limiter; we do **not** rely on iteration for stability.

(A single-Newton-step variant is reserved for a future HQ tier only — out of scope here, parallels the Huggett "HQ tier only" row.)

### Resonance, tuning & gain compensation

- **Resonance taper.** Map common-core `resonance ∈ [0,1]` to `r ∈ [0,4]` with a taper calibrated so musically useful resonance occupies most of the knob and `r≈4` (self-osc) sits at the top — analogous to `NlSvfCell`'s `Q = 0.5 + res²·49.5`. **CALIB.**
- **Cutoff tuning compensation.** The one-sample-delayed feedback shifts the effective resonant frequency; apply Huovilainen-style compensation (a small `fc`-dependent correction to `g` or `r`) so self-oscillation pitch tracks `fc`. Verified by the pitch-tracking test. **CALIB.**
- **Bass-thinning is a feature, not a bug.** The inverted feedback cancels low frequencies as `r` rises — the defining Moog trait. Default = *do not* fully compensate (authentic Minimoog "thins"). Optionally expose an "auto-gain" / bass-compensation amount as a model param (**O2**) — default off/minimal. Do **not** silently scale input by `(1+res)`; that erases the character.
- **Output peak-limiter.** A bounded soft ceiling (monotonic, e.g. the same Padé-`tanh` family used in `NlSvfCell`) on the LP output to clean self-oscillation per Pirkle. Reuse a shared primitive if possible.

### Modes & slope mapping (how the Moog meets the spine's LP/BP/HP + 12/24 controls)

The Moog is **classically a 4-pole LP**. The spine exposes a `Mode` (LP/BP/HP, from `layer.filter.type` → `svfType`) and a `Slope` (12/24 dB, from `spine.slope`). Decision — **pole-mixing**, the standard, source-grounded way a ladder offers more than 24 dB LP:

- **Slope (12 vs 24 dB):** tap a different ladder node. `Slope::db24` → `y4` (4-pole, 24 dB). `Slope::db12` → `y2` (2-pole, 12 dB). (6/18 dB taps `y1`/`y3` exist but are not exposed by the spine's 2-position control.) This honors the spine `Slope` control natively — no faked second cascade.
- **Mode (LP/BP/HP):** a real transistor ladder has no native HP/BP; these are synthesized by **pole-mixing** (weighted sums of taps `y1..y4`, the classic Oberheim Xpander trick — e.g. BP ≈ `y2 − y4` scaled, HP ≈ `x − combination of taps`). Decision for v5.3: **expose LP fully; map BP/HP to the nearest pole-mix approximation** and document that the Moog's BP/HP are character approximations, not the SVF's exact responses. If the pole-mix BP/HP prove unconvincing, **constrain the Moog to LP-only** and have it ignore the Mode control (the model is free to interpret the common spine controls). **Open question O3** — user to choose "approximate BP/HP via pole-mixing" vs "LP-only, ignore Mode."

`Mode`/`Slope` are passed via **model-specific setters** (`setMode`/`setSlope`), exactly as Huggett does — these are **not** on the `FilterModel` base interface; `Layer::updateParameters` calls them through a typed pointer (see "Wiring", and note the hot-swap typed-pointer hazard, Q17).

### `separation` — a Huggett concept; the Moog **ignores it**

`spine.separation` (octave offset between the Huggett's two SVF cells) has **no analog** in a single 4-pole ladder. The Moog's `setSeparation(...)` is a **no-op** (accept-and-ignore), mirroring how the base interface leaves mode/slope/separation as model-specific. The parameter remains visible/automatable (it is a common spine param) but is inert for this model — documented in the param table and asserted by a test. (If a future dual-ladder variant wants it, that is a *new* library entry, not a mutation of this one — append-only discipline.)

## Class design — `MoogLadder : public FilterModel`

Mirror `HuggettFilter` exactly in shape. Header `src/dsp/spine/MoogLadder.h`:

```cpp
#pragma once
#include "FilterModel.h"
#include <array>

// Moog transistor-ladder: 4 one-pole TPT stages + global resonance feedback
// (one-step ZDF, delta-injected per-stage tanh) + output peak-limiter. setCommon's
// `drive` is the input drive into the ladder. See docs/specs/2026-06-20-moog-ladder-design.md.
class MoogLadder : public FilterModel {
public:
    enum class Mode  { LP, BP, HP };   // BP/HP via pole-mixing (or LP-only per O3)
    enum class Slope { db12, db24 };   // db12 -> 2-pole tap (y2); db24 -> 4-pole (y4)

    struct VoiceState : public FilterModel::State {
        std::array<float,4> s {{0,0,0,0}};      // per-stage integrator state, L
        std::array<float,4> sR{{0,0,0,0}};      // ... R  (stereo as 2 lanes)
        std::array<float,4> yPrev {{0,0,0,0}};  // last stage outputs, L (for delta-injected tanh)
        std::array<float,4> yPrevR{{0,0,0,0}};  // ... R
    };

    void prepare(double sampleRate) noexcept override { sampleRate_ = sampleRate; }
    State* makeState() const override;            // new VoiceState
    void reset(State& s) const noexcept override; // zero all state

    void setCommon(float cutoffHz, float resonance, float drive) noexcept override;
    void setMode(Mode m) noexcept   { mode_ = m; }
    void setSlope(Slope s) noexcept { slope_ = s; }
    void setSeparation(float) noexcept { /* no-op: no analog in a single ladder */ }

    void processStereo(State& s, float* left, float* right, int numSamples) const noexcept override;

private:
    // recompute g/G/feedback coefficients per block from cutoff/res; CALIB tapers here
    double sampleRate_ = 44100.0;
    float  cutoffHz_ = 1000.0f, resonance_ = 0.0f, drive_ = 0.0f;
    float  g_ = 0, G_ = 0, r_ = 0;          // prewarped g, one-pole G=g/(1+g), feedback r in [0,4]
    Mode   mode_  = Mode::LP;
    Slope  slope_ = Slope::db24;
    // input drive shaper (plain tanh, reuse AsymSaturator) + output peak-limiter primitive
};
```

`processStereo` shape (branch-free on a `nonlinear` flag, mirroring Huggett):
- per block: recompute `g_`, `G_`, `r_` (cutoff/res tuning + taper); set the input drive shaper.
- per sample, per channel (L/R as two lanes sharing all coefficients): apply input drive → compute the `tanh` feedback/stage deltas from `yPrev` → run the closed-form one-step ladder solve → store `yPrev` → tap by `Slope`/`Mode` → output peak-limiter.
- `drive_ == 0 && resonance_` low → the linear closed-form path runs with no `tanh` (bit-exact linear; keeps the linearity test green).

**Stereo:** L/R are two lanes sharing all coefficients (the `NlSvfCell` pattern), independent state arrays. No SIMD required for v5.3; structure leaves room for it (Q11).

**Denormals:** `ScopedNoDenormals` is set upstream in the voice render; additionally flush the longest-decaying state (the feedback/`yPrev`) the way the nonlinear-modeling doc prescribes.

**DC blocker:** the ladder feedback `tanh` is *symmetric* (no fixed bias), so it does not inject DC the way the Huggett's asymmetric shapers do. A DC blocker is therefore **not** required by default; add one only if calibration shows residual offset from the (optional) asymmetric input drive. **CALIB / O4.**

## Param bank + common core

The Moog consumes the **common core** (always front-panel + mod-targetable) and adds a small namespaced bank `spine.moog.*` (the [ADR-0008](../decisions/0008-algorithm-selection-and-param-namespace.md) idiom, exactly like `spine.huggett.postDrive`):

| Param | Source | Range / default | Notes |
|---|---|---|---|
| cutoff | common (`layer.filter.cutoff` → `svfCutoffHz`) | shared | prewarped to `g` |
| resonance | common (`layer.filter.resonance` → `svfResonance`) | shared | tapered to `r∈[0,4]` |
| drive | common (`spine.drive`) | shared | input drive into the ladder |
| output | common (`spine.output`) | shared | post-model trim (unchanged) |
| mode | common (`layer.filter.type` → `svfType`) | LP/BP/HP | BP/HP pole-mixed (O3) |
| slope | common (`spine.slope`) | 12/24 dB | tap `y2` / `y4` |
| separation | common (`spine.separation`) | — | **ignored** (no-op) |
| `spine.moog.bassComp` | **new bank** | `0..1`, default 0 | optional bass-thinning compensation (O2); 0 = authentic Minimoog thinning |

**Param surface decision:** keep the new bank **minimal** — one `bassComp` knob at most. Resonance taper, tuning compensation, and the peak-limiter ceiling are **CALIB constants in the code**, not user params (matching how Huggett keeps its biases/droop fixed). If calibration wants a "Minimoog vs corrected" character switch, that is `bassComp` (O2). Adding params later is additive (append to `ParamSnapshot` + `Parameters` layout + the consumer) and preset-safe.

**ParamSnapshot / Parameters additions** (if `bassComp` ships):
- `src/params/ParamSnapshot.h`: add `float moogBassComp = 0.0f;` in the spine section (additive — preset-safe).
- `src/params/Parameters.{h,cpp}`: add `spineMoogBassComp` id `= p + "spine.moog.bassComp"`, a `FloatParam` in the per-layer block, and read it in `snapshot()`. **Append at the end of the existing layout block** — do not reorder existing params (APVTS id stability).

## Library registration (append-only, stable ID)

`src/dsp/spine/FilterModelLibrary.cpp` — **append** one entry; never reorder:
```cpp
const Entry kEntries[] = {
    { "huggett", "Huggett", []{ return std::make_unique<HuggettFilter>(); } },
    { "moog",    "Moog",    []{ return std::make_unique<MoogLadder>();   } },   // <-- v5.3, index 1
};
```
- ID `"moog"` is the stable serialized id; index `1` is its serialized position — **never** insert before it.
- `algoNamesSpine()` automatically picks up "Moog" → it appears in the `spine.filterModel` combo (`PluginEditor`) and the `ChoiceParam` choices with no further edits.
- `FilterModelLibraryTests` gains: `expect(FilterModelLibrary::id(1) == "moog")` and a `create(1)` usability check.

## Wiring (`Layer.cpp`) — the typed-pointer + hot-swap hazard

`Layer` currently holds a `HuggettFilter* huggett_` typed view (`dynamic_cast` after each rebuild) and calls `setMode/setSlope/setSeparation/setPostDrive` on it. The Moog needs the same treatment:
- Add a `MoogLadder* moog_ = nullptr;` typed view; `dynamic_cast` it alongside `huggett_` whenever `spineModel_` is rebuilt (in `prepare` and on `spineModel` id change in `updateParameters`).
- In `updateParameters`, when `moog_` is non-null: call `moog_->setCommon(svfCutoffHz, svfResonance, spineDrive)`, `setMode`, `setSlope` (and `setSeparation` is a harmless no-op, can be called unconditionally for symmetry).
- **Hot-swap hazard (register Q17, currently 🔴):** today the library has one entry, so the model-type never actually changes at runtime and the dangling-state path is *latent*. **Adding the Moog makes a runtime model-type change reachable for the first time.** Per Q17, `Layer::updateParameters` rebuilds `spineModel_` on id change, which would dangle every `Voice`'s `SpineFilterSlot::state_` (allocated for the *prior* model type). **This spec does NOT implement the click-free crossfade hot-swap** — that is the Q17/Q18/Q19 work item. For v5.3, either:
  - **(a)** ship the Moog selectable but require the host/voice-manager to re-prepare per-voice state on a model-type change (re-`makeState()` for the new type) — acceptable if it clicks on switch, OR
  - **(b)** gate v5.3 ship on the Q17 crossfade hot-swap landing first.
  **Open question O5 (high priority):** which — accept a click on switch (a) now, or block on Q17 (b)? The dossier roadmap implies the hot-swap is its own work; recommend **(a)** with a tracked follow-up, so the Moog is not blocked on the full crossfade machine, but the per-voice `SpineFilterSlot` state **must** be re-made for the new type to avoid a dangling-pointer crash (this is the non-negotiable part of Q17).

## Performance (256 voices × stereo)

- **Hot-loop cost.** One-step solve = the linear closed-form ladder (a handful of mults/adds) + 4 `tanh`/sample when nonlinear (drive or resonance engaged), ×2 lanes. ~4 transcendentals/sample/voice vs Huggett's ~1–2. This is the headline cost and the reason iterative Newton is rejected.
- **Fast `tanh`.** Use the project's monotonic, bounded Padé approximation (the `NlSvfCell` `padTanh` family) for the stage saturators — monotonicity matters for self-oscillation pitch stability (a non-monotonic approx breeds spurious limit cycles). Reuse, don't duplicate.
- **Linear fast-path.** `drive==0 && low res` → no `tanh` calls at all (linear ladder is linear by construction), so an undriven Moog costs barely more than a linear filter — the same ethos as the Huggett `nonlinear` gate (spine commit `b5ee8a2`).
- **Profiling gate (Q11).** Measure block cost at 256 voices with the Moog active and driven; if it exceeds the per-voice budget, the fallbacks are: single-`tanh` simplified model (one nonlinearity instead of four — Välimäki/Huovilainen), or voices-as-SIMD-lanes. **Do not** prematurely SIMD; gate it on the profile.

## Test plan (existing harness patterns)

New file `tests/MoogLadderTests.cpp` (a `juce::UnitTest`, registered in `tests/CMakeLists.txt` alongside `HuggettNonlinearTests.cpp`; add `../src/dsp/spine/MoogLadder.cpp` to the test sources). Pattern after `HuggettNonlinearTests.cpp` (pass/fail) and `OverdriveDiagnosticTests.cpp` (FFT diagnostics). Targets:

1. **Library registration** (extend `FilterModelLibraryTests`): `id(1)=="moog"`, `create(1)` non-null and usable.
2. **Linearity at low res/zero drive** — `MoogLadder` at `drive=0, res=0` matches a hand-rolled reference linear ladder (or a known LP magnitude at a couple of frequencies) to tight tolerance; proves the linear-by-construction path. (Mirrors Huggett's "zero-drive path is bit-for-bit linear" test.)
3. **4-pole slope** — feed band-limited noise / swept sines; FFT and check the LP rolloff is ~**24 dB/oct** above `fc` for `Slope::db24` and ~**12 dB/oct** for `db12` (tap `y2`). (Uses the `OverdriveDiagnosticTests` FFT helpers.)
4. **Resonance behavior** — as `resonance` rises, the magnitude peak at `fc` grows; verify monotonic peak growth and the **bass-thinning** trait (low-frequency magnitude drops as res rises, with `bassComp=0`).
5. **Self-oscillation onset + pitch tracking** — at max resonance, kick with an impulse then run silence; assert (a) sustained non-decaying oscillation (onset), (b) its frequency ≈ `fc` within a tolerance (e.g. ±3%) across several `fc` values (pitch tracking — this is what the tuning-compensation CALIB targets), (c) the waveform is near-sinusoidal (low THD with the peak-limiter on). (Mirrors `NlSvfCell` "self-oscillation is bounded".)
6. **Boundedness / finiteness** — at max resonance + max drive + loud input, output is finite (no NaN/Inf) and bounded (peak below a ceiling) over a long run — the limit-cycle-settles guarantee. (Mirrors the Huggett bounded-drive test.)
7. **`separation` is inert** — two runs with `separation = 0` and `= 2` octaves produce identical output (the no-op assertion).
8. **Diagnostic (not a gate):** run the `OverdriveDiagnosticTests` inharmonic-energy score on a driven self-oscillating Moog and print it, to feed the O1 decision (base-rate-acceptable vs needs the v5.1 OS tier).

## Build sub-sequence (tasks)

1. **Linear ladder core** — `MoogLadder.{h,cpp}`: four TPT one-pole stages + the closed-form linear ZDF feedback solve; tap `y2`/`y4` for slope; `setCommon/setMode/setSlope/setSeparation(no-op)`. Test 2 (linearity) + test 3 (slope) green. No nonlinearity yet.
2. **Library entry** — append `"moog"` to `FilterModelLibrary.cpp`; extend `FilterModelLibraryTests` (test 1). Confirm it appears in the spine combo.
3. **Wiring** — `Layer.cpp`: add `moog_` typed view + `dynamic_cast` + per-block setter calls. **Address the Q17 dangling-state hazard** per O5 (re-make per-voice `SpineFilterSlot` state on model-type change at minimum).
4. **Nonlinearity** — delta-injected per-stage `tanh` (one-step solve) reusing the `padTanh` family; resonance taper + Huovilainen tuning compensation (CALIB). Tests 4, 5 (onset + pitch tracking).
5. **Self-oscillation polish** — output peak-limiter (Pirkle); near-sine self-osc at base rate. Tests 5c, 6 (bounded/finite) + the test-8 diagnostic.
6. **Optional bass-comp param** — if shipping `spine.moog.bassComp`: `ParamSnapshot` + `Parameters` + snapshot read + `Layer` plumb (all additive). Test 4 covers `bassComp=0` thinning.
7. **Calibration pass** — A/B against Minimoog/Diva-Ladder demos; pin resonance taper, tuning comp, limiter ceiling, optional bass-comp curve. (CALIB constants only.)
8. **Version surface** — bump CMake `VERSION` + the `PluginEditor` panel label on ship (memory: version surface goes stale otherwise). Update `phases.md` v5.3 row → Implemented, register **L7** lineup, and this spec's Status.

## Open questions (for the user)

- **O1 (AA):** Is base-rate plain-`tanh` + the Pirkle output limiter acceptable for the Moog's self-oscillation/overdrive, or is the Moog the model that should *force* the deferred v5.1 OS tier? The test-8 diagnostic will quantify it, but the threshold is a taste call.
- **O2 (bass-comp):** Ship a `spine.moog.bassComp` knob (Minimoog-thins ↔ corrected), or hard-code authentic thinning and add the knob only if asked? Recommendation: ship the knob, default 0 (authentic).
- **O3 (modes):** For the Moog's `Mode`, approximate **BP/HP via pole-mixing** (more capable, less exact), or constrain the Moog to **LP-only** and ignore the Mode control (more authentic to a real ladder)? Recommendation: LP-only first (authentic, ships faster), pole-mix BP/HP as a fast-follow if wanted.
- **O4 (DC blocker):** Symmetric ladder `tanh` shouldn't need one; confirm we can skip the DC blocker for the Moog (re-add only if an optional asymmetric input drive is introduced).
- **O5 (hot-swap, high priority):** Adding the Moog makes a runtime model-type change reachable for the first time and exposes the **Q17 dangling per-voice-state** hazard. Accept a click on model-switch now (with the mandatory per-voice state re-make to avoid a crash), or **block v5.3 on the Q17 crossfade hot-swap landing first**? Recommendation: do not block — ship with re-made state + a tracked Q17 follow-up.

## Decisions (resolved 2026-06-20, user review)

- **O5 (hot-swap)** — **block Moog on the Q17 click-free crossfade**; the equal-power crossfade + per-voice `State` heap→in-place migration is built BEFORE Moog (see roadmap build sequence).
- **O1 (anti-aliasing)** — Moog rides the **full v5.1 HQ oversampling tiers**, which land before Moog; not a one-off focused OS.
- **O3 (modes)** — **pole-mix BP/HP** (expose the Mode control), not LP-only.
- **O2 (bass-comp)** — ship the **`spine.moog.bassComp`** knob, default 0 = authentic Minimoog thinning.
- **O4 (DC blocker)** — **include it** (safety insurance against any drive/limiter asymmetry).
- **Solve:** keep the one-step `fbExtra` delta technique (no per-sample Newton), per the spec.
