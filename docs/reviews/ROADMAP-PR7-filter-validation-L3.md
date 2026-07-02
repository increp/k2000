# Third-Level Analysis & Roadmap — PR #7 (Filter-Validation Harness)

**Analyst:** L3 (DSP / analog+digital circuits / sound design)
**Date:** 2026-07-01
**Version:** 1.0
**Subject:** `feat/filter-validation-internal` → `main` (PR #7, OPEN). Evaluation of the PR code **and** the L2 consultant report ([`REVIEW-PR7-filter-validation.md`](REVIEW-PR7-filter-validation.md)).
**Method:** Re-derived every L2 claim from source + the fresh `build/characterization/**` artifacts (both models, `--quick`). Read the measurement chain (`SteppedSine`, `EssResponse`, `CharacterizationRunner`, `FilterUnderTest`), both DSP models (`NlSvfCell`, `HuggettFilter`, `MoogLadder.cmajor`, `AsymSaturator`), `Parameters.cpp`, the gate, the spec §6, and the plan. North star: **authenticity to the real hardware**, and **safe/musical to play**.

---

## 0. Decision record (2026-07-01)

The user reviewed this analysis and chose the **authenticity-purist path**:
- **Scope:** analysis-only for now — no harness or DSP changes yet.
- **Huggett voicing:** defer the re-voice-vs-ceiling call until the **real Summit Huggett filter is fingerprinted**. Hardware is the authority, not textbook/ear.
- **Trust model:** **hold all DSP changes for hardware** — the Summit/Arturia capture is the sole authenticity target.

**Consequence — this reorders the roadmap and *raises* the priority of the in-box level work:** if the hardware comparison is the sole authority *and* it is done shape-only, the Summit will "match" the in-box model and teach nothing about the screechy/tame (level) problem. So the level + large-signal axis becomes a **hard prerequisite to capture**, not an optional P0. Critical path under this decision:

> **Phase 0 → Phase 1 (harness gains a level/large-signal axis) → settle Summit excitation method → Phase 3 (capture + grey-box fit) → Phase 2 (DSP fix, now hardware-guided).**

Phase 2 moves to *after* capture by choice; Phase 0–1 stay first because the capture is meaningless without them. Two things to hold onto:

1. **The +72 dB is now a falsifiable prediction.** The real Summit should *not* show a +72 dB small-signal resonant peak. The capture confirms (fix needed) or refutes (model faithful) it — the disagreement gets settled by measurement, exactly as the north star intends.
2. **Safety tension (flagged once, not relitigated).** "Very dangerous to use / hits the limiter immediately" is a *present* problem and the hardware campaign is the longest phase. The ceiling-only stopgap (the declined hybrid) remains available without prejudging authenticity, should the danger become a usability blocker before capture.

---

## 1. Verdict up front

**I CONCUR with L2's core finding, SHARPEN it in two ways, ADD one finding L2 missed, and am MORE GENEROUS than L2 on one point.**

- **CONCUR (fully reproduced, to the decimal):** The harness measures frequency-response *shape* to a high standard and has **no level axis**. It is **not fit-for-purpose for the user's actual problem** (Huggett screechy/limiter-slamming, Moog tame). The level metrics were **specified in the spec, reduced in the plan without comment, and shipped as none** — a spec→implementation drift, not a considered scoping call. Every empirical number in L2's headline table matches the artifacts exactly (§2).

- **SHARPEN #1 — the root cause is bigger than "no level axis":** The harness **characterizes the *linearization* of two deliberately-nonlinear filters.** Every battery is anchored at the linear/small-signal operating point: excitation at −26 dBFS, `drive=0`, and the summary's B1 metrics computed only at `baseRes = 0`. Level is the loudest casualty, but harmonic character, large-signal resonance bound, and self-oscillation amplitude are the *same* casualty of the *same* root cause. This reframes the fix from "add a number" to "add a large-signal excitation regime." (§4)

- **SHARPEN #2 — the +72 dB is a DSP finding, not just a measurement gap:** The Huggett/Moog level disparity is caused by a specific, identifiable, and probably-**inauthentic** DSP structure, in direct contrast to the Moog's internally-bounded ladder. This is co-headline with L2's harness gap — arguably the deeper finding, because fixing the harness to *see* +72 dB does not make the filter authentic. (§5)

- **ADD — a finding L2 missed:** L2 credited the aliasing-vs-oversampling metric as "exemplary, 0→−117 dB, one of the best parts." That is the **Moog** result. For **Huggett** — the nonlinear filter that actually needs oversampling — the same metric is **positive and non-monotonic** (os1 +13.5 dB → os8 +20.0 dB, LP24). The OS story is unproven-to-broken for the model that matters. (§6)

- **MORE GENEROUS — the dual-method ESS engine is under-used, not over-built:** L2 calls it "over-invested gold-plating." I disagree. Farina ESS is exactly the instrument you need for large-signal THD-vs-level **and** for the #2 hardware capture. The team built a genuinely good ruler and then read only ~40% of it (the linear magnitude). The fix *leverages* that investment. (§7)

**Bottom line:** L2 is right that the harness can't answer the user's question, and right about the P0 direction. But the situation is both **more diagnosable** (we can name the DSP cause today) and **more fixable in-box before touching hardware** than L2's "add level axis → then do #2" framing implies. The single most valuable next move is to close a **measure → fix → re-measure** loop on the resonance-bounding asymmetry *in the box*, using the harness the team already built plus a small large-signal extension.

---

## 2. What I verified (evidence)

Every claim below is re-derived from source or the fresh artifacts, not taken from L2.

### 2.1 The empirical disparity — reproduced exactly

LP24, os1, `drive=0`, stepped-sine, from `build/characterization/*/response.csv`:

| Op point | Huggett peak | Moog peak | Disparity | Huggett passband | Moog passband |
|---|---|---|---|---|---|
| res 0.0 (any fc) | ~0 dB (flat) | ~0 dB | ~0 | ~0 dB | −0.85 dB |
| res 0.9, fc250 | **+72.26 dB** | +4.75 dB | **67.5 dB** | −0.67 dB | −13.34 dB |
| res 0.9, fc1000 | **+70.23 dB** | +4.80 dB | **65.4 dB** | −0.83 dB | −13.40 dB |
| res 0.9, fc4000 | **+62.14 dB** | +4.84 dB | **57.3 dB** | −0.84 dB | −13.40 dB |

Matches L2's table to the decimal. None of these numbers reaches `summary.csv`, a gate, or a golden.

### 2.2 The harness is blind by construction (code, not inference)

- `magLin = √(re²+im²)/amp` — output÷**input**, **true absolute gain** ([`SteppedSine.h:47`](tests/testdsp/SteppedSine.h#L47)). The absolute level **is** computed and **is** in `response.csv`. L2's correction of the original "normalized to passband" hypothesis is correct; the manual's [`interpreting-results.md:28`](docs/filter-validation/interpreting-results.md#L28) "dB relative to passband" is a **mislabel**.
- Summary stores B1 metrics **only at `baseRes = min(resonances) = 0.0`** ([`CharacterizationRunner.cpp:479`, `:562-573`](tests/characterization/CharacterizationRunner.cpp#L562)). The res=0.9 pass runs and lands in `response.csv`, but its corner/slope/**peak** is never summarized. No level key exists at any resonance.
- Self-oscillation **amplitude** is computed (`Spectrum::maxAbs`) and used **only** as a `<1e-4` energy guard, then discarded ([`CharacterizationRunner.cpp:299`](tests/characterization/CharacterizationRunner.cpp#L299)); `resonance.csv` emits pitch only.
- THD is measured at `baseRes=0, baseDrive=0` ([`CharacterizationRunner.cpp:590-591`](tests/characterization/CharacterizationRunner.cpp#L590)) → Huggett THD reads **−94 to −133 dB** (cleaner than Moog). The "dirty" filter's harmonics are measured only where it is linear.
- `FilterUnderTest` calls **only `setCommon`** ([`FilterUnderTest.cpp:21`](tests/characterization/FilterUnderTest.cpp#L21)); Huggett's `setPostDrive` is **never invoked** — the post-drive path is wholly untested.

### 2.3 The spec→plan→implementation drift — confirmed verbatim

- **Spec §6 specified the level metrics and the sweeps:** "Resonance **peak gain & Q** vs the resonance knob" ([spec:132](docs/superpowers/specs/2026-06-29-filter-validation-internal-design.md#L132)); "Self-oscillation **onset threshold**" (:133); "**Limit-cycle amplitude + crest factor**" (:135); "**Idle noise floor**" (:141); "THD-vs-level" (:160); resonance and drive are **swept** axes (:105-106).
- **Plan** kept only a prose "peak gain/Q from B1 at high resonance," dropped onset/limit-cycle/crest/THD-vs-level/noise-floor, and its headline key list contains **no level key**.
- **Implementation** shipped none; and `coarseGrid` pins `drives={0.0}`, B3 runs at `baseDrive=0` — so **even the full grid would not surface level**, because the *summarization* has no level extraction at any grid density. "Just run the full grid" is not a workaround.

### 2.4 The gate cannot catch a level regression

The always-on gate asserts only `slope ≤ −3` and `method_delta ≤ 1.0`, plus self-golden on corner/slope/delta **at res=0** ([`CharacterizationGateTests.cpp:58-87`](tests/CharacterizationGateTests.cpp#L58)). Huggett's self-osc golden is **skipped** (`includeSelfOsc=false`, [:97](tests/CharacterizationGateTests.cpp#L97)). Huggett's resonance behavior is therefore **ungated**: a change that made it hotter would pass CI green.

---

## 3. The reframe: it measures the linearization of a nonlinear filter

L2 frames it as "shape vs level." The sharper statement is that **every summary metric is anchored where the filter is linear**:

| Battery | Anchored at | What that hides |
|---|---|---|
| B1 magnitude (summary) | res=0, −26 dBFS | the entire resonant-peak height (the +72 dB) |
| B2 self-osc | pitch only, amplitude discarded | the limit-cycle **level** that pins the limiter |
| B3 THD | drive=0, res=0 | the harmonic character of a filter defined by its dirt |
| B3 aliasing | a fixed torture probe (see §6) | the real per-tier OS benefit for the nonlinear path |

For two filters whose *entire identity* is nonlinear (Huggett's drive+resonance saturators; Moog's per-stage transistor tanh), measuring only the small-signal linearization is measuring the one regime where the models are least themselves. **Level is the loudest symptom of a categorical gap: the harness has no large-signal regime at all.**

---

## 4. The DSP finding — the resonance-bounding asymmetry (the deeper root cause)

The +72 dB is not mysterious and not merely "unmeasured." It is structural:

**Huggett (`HuggettFilter` + `NlSvfCell`):**
- LP24 resolves to **two identical `NlSvfCell`s in series**, both LP tap ([`HuggettFilter.cpp:29`, `:81-83`](tests/characterization/../../src/dsp/spine/HuggettFilter.cpp#L29)).
- Each cell: `Q = 0.5 + res²·49.5` → **Q ≈ 41 at res=0.9**, ~50 near max ([`NlSvfCell.h:71`](src/dsp/spine/NlSvfCell.h#L71)).
- A single 2-pole SVF resonant peak ≈ Q (~+32 dB at res=0.9); **two coincident cells in series ≈ +64 dB** (measured +72 at fc250 — a touch higher because the exact LP peak exceeds Q and the two peaks multiply exactly).
- The only in-loop bound is the per-cell resonance saturator, and it is **deliberately designed to vanish at low level** (O(x²) correction, [`NlSvfCell.h:52-55`](src/dsp/spine/NlSvfCell.h#L52)). At −26 dBFS the cells are **essentially linear** → the +72 dB is the bare, uncompensated cascaded-SVF peak. There is **no output ceiling in the filter** — bounding is delegated entirely to the external `SafetyLimiter`.

**Moog (`MoogLadder.cmajor`) — the authentic contrast:**
- Per-stage tanh saturation (one per pole — authentic transistor-ladder topology).
- An **explicit output soft-limiter**: `limOut = CALIB_LIM_CEIL·padTanh(tapOut/CALIB_LIM_CEIL)` — normal-level signals pass, the resonance is **level-bounded inside the model**.
- Resonance taper `res∈[0,1] → r∈[0, 5.5]`, self-osc at r=4, with a **defined limit-cycle amplitude (~0.3 at res=1.0)**; and the authentic passband droop (the measured −13.4 dB).

**So the audible complaint is a DSP asymmetry, not a measurement gap:**
- **Huggett** stacks two uncompensated high-Q resonances with **no internal ceiling** → explosive small-signal peak, bounded only downstream → *screechy, dangerous, hits the limiter immediately.*
- **Moog** bounds resonance internally and drops its passband authentically → *tame, barely there.*

The repo already knows this path runs hot: the sibling OTA **HP stage was capped to res 0.15** because it "self-oscillates too hot across its full range" ([`Parameters.cpp:172-177`](src/params/Parameters.cpp#L172)). That is a band-aid on exactly this structure.

**Authenticity judgment:** A **+72 dB (×4000) small-signal linear resonant peak is not physically authentic** to any real analog filter — a real SVF/ladder is bounded by integrator/transistor saturation and finite op-amp gain; it self-oscillates into a *bounded* tone rather than an ever-taller linear peak. The honest caveat: proving the *audible* severity requires large-signal measurement (does the saturator tame +72 to something musical, or does it still scream?). But the user's ear already reported the answer, and the small-signal peak + the physical implausibility + the HP-cap precedent make a strong case that **Huggett's 24 dB resonance is mis-structured/over-hot, and the fix belongs in the DSP** — with the harness rebuilt to measure it.

---

## 5. The aliasing finding L2 missed

L2 §8 credits "aliasing drops 0→−117 dB os1→os8" as "a true, SOTA-aware deliverable… one of the best parts." From `summary.csv` / `distortion.csv`:

| Model, LP24 | os1 | os2 | os4 | os8 |
|---|---|---|---|---|
| **Moog** `alias_db` | 0.0 | −29.0 | −80.5 | **−117.6** |
| **Huggett** `alias_db` | **+13.5** | +2.5 | +8.5 | **+20.0** |

The clean monotonic curve L2 praised is **Moog only**. Huggett's is **positive and non-monotonic — os8 is worse than os2.** The isolation probe drives a **33.6 kHz tone at `drive=1.0` into a 38.4 kHz cutoff** ([`CharacterizationRunner.cpp:390-404`](tests/characterization/CharacterizationRunner.cpp#L390)); Huggett's pre-drive tanh (gain ×31.6) turns that into a harmonic storm that folds unpredictably. Two readings, both actionable:
1. **Probe pathology:** nobody plays a 33.6 kHz tone at full drive — the metric is not musically meaningful for the nonlinear path (same "measures an unreal regime" theme).
2. **Possible real regression:** the oversampler *is* in the measured path ([`FilterUnderTest.cpp:37-53`](tests/characterization/FilterUnderTest.cpp#L37)), so this may be a genuine oversampling issue for the nonlinear filter — which resonates with the already-deferred "filter regression after oversampling" item.

Either way, L2's blanket credit does not hold for the model that most needs oversampling.

---

## 6. Where I'm more generous than L2 — the ESS is under-used

L2 §5 says the team "over-invested in the ruler." I read it the other way:
- The ESS calibration is correct and non-circular (`IR_sys/IR_ref = System(f)`, [`EssResponse.h:16-19`](tests/testdsp/EssResponse.h#L16)) — I independently confirm L2's endorsement.
- Farina ESS **natively separates the linear response from the harmonic distortion in a single measurement, at any excitation level.** The team built this and then read out only the linear magnitude.
- ESS is also the **standard way to measure a real analog filter** for #2 (drive a sweep through the Summit's filter, deconvolve, separate harmonics).

So the ESS is infrastructure for exactly the large-signal and hardware work that's missing. The waste isn't "two rulers for one quantity" — it's **one excellent ruler used at 40%.** The dual-method cross-check earns trust in that ruler and is worth keeping. (I agree with L2 that the *next* engineering hour goes to level, not to tightening a 0.6 dB agreement — but I would not characterize the ESS as regrettable.)

---

## 7. Answers to the seed questions

1. **Worth anything without a level battery?** As a shape/aliasing/self-osc-pitch regression net, yes. For the user's problem, no — but the cheapest fix (§8 Phase 0) surfaces data it *already collects*, so it's hours from saying something real.
2. **Is Q≈50 / +72 dB authentic?** Almost certainly **no** — it's the artifact of two uncompensated cascaded high-Q SVF cells with no internal ceiling (§4). Very likely a filter-DSP voicing issue, the deeper finding.
3. **Minimal correct level metric set?** Resonant-peak gain + passband gain per (model, mode, cutoff, res); self-osc limit-cycle amplitude + crest; gain-vs-input-level (compression knee); THD-vs-level-and-drive; inter-model gain reference; idle noise floor. Schema: add `level`/`inputDbFS` columns + a per-model reference op point (§8).
4. **Dual-method worth its cost?** Yes — but under-used, not over-built (§6). Keep it; extend it to level.
5. **#2 go/no-go?** Ratify conditional-GO + absolute-level-axis-first, and **add**: the level axis's *first* job is in-box diagnosis/fix, not hardware comparison; and the **Summit excitation method** must be settled before capture (§9).
6. **Right path to a Summit-faithful filter (no public schematic)?** Measurement-driven grey-box calibration: build the level+large-signal battery, characterize the real Summit with the same ESS engine, then fit the model's CALIB constants (Q law, output ceiling, saturation scale) to the captured targets. Not white-box (no schematic), not pure ear — **grey-box against measured hardware**, which is the user's stated north star and is what the harness exists to enable.

---

## 8. Roadmap to solution (ordered, with rationale + effort)

Re-sequenced around the §4 insight: **make the harness see it → make it see the large-signal truth → fix the DSP against those numbers → then hardware.** Effort: S = hours, M = 1–2 days, L = several days.

### Phase 0 — Make the harness *see* the disparity (S, ~½ day) — do this first
Surfaces data the harness already collects; it is the go/no-go test that it can see the problem at all.
- Extract **resonant-peak gain** (`max|H|`) and **passband gain** (`H` at the passband anchor) per (model, mode, cutoff, **res**) into `summary.csv` — currently only corner/slope at res=0.
- Record the **self-osc limit-cycle amplitude + crest** already computed-and-discarded at [`CharacterizationRunner.cpp:299`](tests/characterization/CharacterizationRunner.cpp#L299).
- Add an **inter-model gain reference**: same op point, both models, Δ dB.
- Fix the `magDb` "relative to passband" mislabel.
- **Exit criterion:** the harness prints "Huggett +72 vs Moog +4.8, disparity 67 dB" as a first-class number.

### Phase 1 — Make it see the *large-signal* truth (M, 1–2 days)
The audible regime is large-signal; small-signal cannot answer "how hot at playing level."
- **Multi-level excitation** (e.g. −26 / −12 / 0 dBFS): gain-vs-input-level (compression knee), large-signal resonant peak (does the saturator bound +72 to something musical?), self-osc amplitude vs res.
- **Engage the drive path**: wire `setPostDrive` into `FilterUnderTest`; sweep drive → THD-vs-level-and-drive (measure the dirt where it's actually dirty).
- **Reuse the ESS** for harmonic separation at level (linear + harmonics in one shot — §6).
- **Idle noise floor** (spec'd, dropped).
- **Exit criterion:** a defensible number for "how hot does Huggett get at playing level," and Moog's true limit-cycle level.

### Phase 2 — Fix the DSP asymmetry, *measured* (M, 2–4 days incl. calibration iteration)
- **Huggett:** give the cascade an internal bound — an output soft-ceiling analogous to Moog's `CALIB_LIM_CEIL`, and/or rescale the `Q = 0.5 + res²·49.5` law, and/or restructure the 24 dB mode so it isn't two independent Q≈41 peaks. Use Phase 0/1 metrics as the before/after ruler. (The HP-cap-to-0.15 precedent is the tell.)
- **Moog:** decide whether "tame" is authentic-and-fine or needs the limit-cycle amplitude / output scaling nudged (the parked `CALIB_RESCOMP` dial).
- **Gate the level metrics** so regressions can't sail through (Huggett resonance is currently ungated, §2.4).
- **Exit criterion:** in-box level metrics reproduce a *musical* (not dangerous) Huggett and an audible Moog; gates protect them.

### Phase 3 — THEN hardware (#2) (L)
Only after Phase 0–2. Preconditions:
- Extend schema: MIDI note/velocity, mapped Summit CC, Summit filter/level setting, **absolute dBFS/dBu capture reference**, excitation provenance.
- Settle the **Summit excitation methodology** (open question — §9).
- Fix **B4 time-align** (Q20) — cheap insurance before slow captures so phase/GD is available without re-capture.
- **Null-test** methodology, in-box vs hardware.
- Grey-box **fit** the model CALIB constants to captured targets (§7.6).

---

## 9. Sub-project #2 go/no-go

**Conditional GO — I ratify L2's precondition and add two.**
- **(a) Absolute-level + large-signal axis in #1 first — HARD (ratified).** Without it the capture answers only "do the shapes match." **And its first use is in-box** (Phase 0–2): prove the harness reproduces the disparity and drive the DSP fix *before* spending an hour on hardware.
- **(b) Summit excitation methodology — HARD, and under-weighted so far.** There is no public Summit schematic and the shared schema is param-space, not capture-space. Before any capture you must answer: **how do you get a clean transfer function out of the real Summit's filter in isolation?** Options: external sweep via its audio-in / filter-FM path if one exists; or play its oscillators and deconvolve (couples osc + filter); or CV/gate automation. If the Summit can't be cleanly excited at its filter input, the #2 premise needs rethinking. This is the top open question.
- **(c) B4 time-align — STRONG SHOULD before the capture campaign** (cheap; avoids re-capturing for phase). Not a blocker for a magnitude+level first pass.

---

## 10. Open questions for the user (the forks that change what I do next)

1. **My scope now:** stay analysis-only (you drive), or start Phase 0 (surface the metrics the harness already collects), or continue into the Phase 2 Huggett DSP fix?
2. **Huggett intent:** is the screaming Q≈41×2 resonance a *sound you want* (tame it with an internal ceiling/limiter) or a *bug to fix at the source* (rescale the Q law / restructure the 24 dB cascade)? This decides whether Phase 2 is "add a ceiling" or "re-voice the filter."
3. **Trust model:** fix the DSP in-box now against textbook/ear targets, or hold all DSP changes until you can measure against the real Summit (pure-authenticity path)? This is your north-star tension made concrete — and it sets whether Phase 2 precedes or follows Phase 3.
