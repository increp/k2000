# Independent Consultant Review — PR #7 (Filter-Validation Harness, Sub-Project #1)

**Reviewer:** Independent Opus consultant (outside second opinion)
**Date:** 2026-06-30
**Subject:** `feat/filter-validation-internal` → `main`, PR #7 (OPEN). ~28 commits, +4684/−1 across 33 files, Windows MSVC CI green. Internally reviewed "ready to merge, zero Critical."
**Charter:** Fit-for-purpose? SOTA-conformant? Where are the gaps? Go/no-go for sub-project #2?
**Method:** Read the PR, spec, plan, build ledger, the L0/L1/L2 source, the committed goldens, and the operator manual; traced the full measurement chain; inspected the actual Huggett/Moog DSP; ran both models `--quick`. Review-only — no harness code was modified.

---

## 1. Bottom line up front

1. **The harness is a well-built instrument for the wrong axis.** It measures *frequency-response shape* (relative magnitude, corner, slope, self-osc pitch, aliasing-vs-oversampling) to a high standard. The user's actual complaint — **Huggett screechy/slams the limiter, Moog tame/barely there — is a LEVEL problem, and the harness has no level axis.** Verdict: **not fit-for-purpose for the problem that motivated it**, despite being a clean relative-response/aliasing instrument. *(Measured this session: at an identical setting with both drives off, Huggett's resonant peak runs **57–68 dB hotter** than Moog's — a disparity that is present in `response.csv` and absent from every summary, gate, and golden. §2.6.)*

2. **The brief's own hypothesis is slightly wrong, and the truth is worse.** The absolute small-signal gain *is* computed (both engines return output÷input) and *is* sitting in `response.csv`. The failure is downstream: **nothing summarizes it, nothing compares it across models, the manual mislabels it, and the one operating point that reproduces the user's complaint (high resonance) is measured only for its normalized shape — its level is discarded.**

3. **The level metrics were specified, then silently dropped.** Spec §6 explicitly called for resonance **peak gain & Q**, self-osc **onset threshold**, **limit-cycle amplitude + crest factor** (B2), and **THD-vs-input-level + idle noise floor** (B3). The plan quietly reduced them; the implementation delivered none of them; no review caught it because every downstream artifact describes only what was built. This is a **spec→implementation drift**, not a considered scoping decision.

4. **The disparity is structurally real, it's resonance-driven, and the harness runs the hot operating point yet still can't read its level.** With the drive knobs off (their default — `spine.drive`/`spine.huggett.postDrive` both default 0.0, `Parameters.cpp:162,182`), Huggett's level comes from its **resonance**: `NlSvfCell` runs `Q = 0.5 + res²·49.5`, i.e. **Q up to ~50**, an uncompensated SVF peak on a full-level passband. `MoogLadder` does the opposite — it *loses* passband (`1/(1+r)`, ≈ −13 dB) as resonance rises. Same knob, opposite level trajectory — that's hot-vs-tame. The harness *does* exercise this (the grid includes `res = 0.9`), but B1 keeps only corner/slope/agreement and B2 keeps only pitch, so **the tall resonant peak is measured and discarded.** The optional pre/post-drive knobs (default off) add grit and more level when engaged — a secondary axis the harness also can't see.

5. **Sub-project #2: conditional GO, with one hard precondition.** The MIDI→Summit→capture rig is a good idea and the schema is a reasonable launchpad — **but an absolute-level axis must be added to #1 first.** Without it, an expensive hardware capture can only answer "do the shapes match," never "is my in-the-box Huggett as hot/screechy as my real Summit" — which is the authenticity question that motivates #2.

---

## 2. The level/gain finding (the headline)

### 2.1 What the user hears vs what the harness reports

The four batteries emit, into `summary.csv`, exactly these scalar keys (verified in `CharacterizationRunner::run`):

| Key | Quantity | Nature |
|---|---|---|
| `corner_hz` | −3 dB point | frequency |
| `slope_db_oct` | transition slope | **relative** (a difference) |
| `method_delta_db` | stepped-vs-ESS agreement | **relative** (a difference) |
| `selfosc_cents_err` | self-osc pitch error | pitch |
| `thd_db` | harmonic ÷ fundamental | **relative ratio** |
| `alias_db@os{1,2,4,8}` | inharmonic ÷ fundamental | **relative ratio** |

Every key is a frequency, a pitch, or a normalized ratio. **There is no key for passband gain, peak/resonant gain, output level, headroom, or any inter-model level reference.** A filter that screams into the limiter and one that is inaudible can produce identical "passing" fingerprints. The empirical `--quick` run confirms it — both models' `summary.csv` contains only the keys above (committed goldens already show this: `corner_hz`, `method_delta_db`, `selfosc_cents_err`, `slope_db_oct` and nothing else).

### 2.2 The precise mechanism (correcting the brief)

The handoff hypothesized that `magDb` is "normalized to passband" and so level is destroyed at the source. **That is not quite what the code does, and the real story is more damning:**

- `SteppedSine` (`SteppedSine.h:47`) computes `magLin = |output| / amp` — output amplitude ÷ **input** amplitude. That is the **true absolute gain**, not a passband-normalized number.
- `EssResponse` divides system-sweep by identity-sweep → `IR_sys/IR_ref = System(f)` exactly — also true absolute gain in a unity frame.

So the absolute small-signal gain **is** present in `response.csv`'s `magDb` column for every probe. The harness throws it away **four times over**:

1. **No summary metric extracts it.** `summary.csv` keeps only corner/slope/delta/cents/thd/alias (§2.1).
2. **No cross-model comparison exists.** Each model's fingerprint floats independently; nothing puts Huggett and Moog on a common gain ruler.
3. **The manual mislabels it.** `interpreting-results.md` calls `magDb` "dB relative to passband." It isn't — it's absolute gain. (At res = 0 the LP passband sits at ≈ 0 dB so the two coincide numerically, which is presumably how the mislabel slipped through; at high resonance or for Moog's bass-loss they diverge and the label is simply wrong.)
4. **It's small-signal only.** Both engines excite at amp = 0.05 (−26 dBFS). This *does* capture the linear resonant peak — Huggett's tall Q~50 peak sits in `response.csv` at `res = 0.9`, collected and then discarded (the cheapest fix, §6). What small-signal *cannot* see is the amplitude-dependent behavior: how the resonance-loop saturator compresses that peak at real playing levels, the self-oscillation limit-cycle amplitude, and anything with the drive knobs engaged. Both — the latent linear peak and the large-signal level — are absent from every summary and gate.

And the one place an amplitude is actually measured — B2's self-oscillation ring — it is computed (`Spectrum::maxAbs(capture)`) **solely as a 1e-4 detection guard and then discarded** (`CharacterizationRunner.cpp:299`). The self-oscillation *level*, the thing that pins the limiter, is measured and thrown on the floor.

### 2.3 The smoking gun in the DSP

The disparity isn't mysterious — it's in the filter headers:

- **Huggett:** `kPreDriveDb = 30.0f`, `kPostDriveDb = 24.0f` (both CALIB) set how hard the signal is driven into two asymmetric **tanh saturators** (`AsymSaturator`, pre- and post-filter). These are *overdrive* stages, **not clean gain** — the tanh bounds each stage's output near ±1, so the dB figure is the drive-into-the-clipper (and the small-signal gain: a −26 dBFS tone is boosted ~+30 dB into near-clipping by the pre-stage alone at full drive). The raw *level* that pins the limiter comes mostly from the **resonance** — `NlSvfCell` runs `Q = 0.5 + res²·49.5`, i.e. **Q up to ~50** (self-oscillation), a very tall resonant peak. So the high-Q resonance supplies the loudness (enough to clip on its own), and the drives — when turned up — add harmonic grit/screech on top. `MoogLadder` has neither a pre/post-drive stage nor makeup gain.
- **Moog:** a clean Cmajor transistor ladder, tanh inside the patch, **no make-up gain**, plus the authentic bass-loss `1/(1+r)` (≈ −13 dB at high resonance). Structurally quiet.

Now the indictment — and a correction to a tempting wrong turn. The pre/post-drives are **optional knobs, default off** (`spine.drive`/`spine.huggett.postDrive`, both default 0.0 — `Parameters.cpp:162,182`), so they are **not** the baseline cause of "hot." In normal use Huggett is hot because of its **resonance** — the Q~50 SVF peak on a full-level passband. The codebase corroborates this: the sibling OTA HP stage had its resonance **capped to 0.15** because it "self-oscillates too hot across its full range" (`Parameters.cpp:172-174`). The team already knows the resonance path, not the drive, is the hot one.

So the indictment is *sharper* than "the harness skips the hot path": **the harness runs the hot path and deletes the reading.** The grid includes `res = 0.9`, so B1 measures Huggett right on its resonant peak — then keeps only the normalized corner/slope/agreement; B2 kicks it into self-oscillation and keeps only the **pitch**, discarding the amplitude it already computed. The one operating point that reproduces the user's complaint is measured, and its level is thrown away at the summary layer.

(Secondary blind spot: `FilterUnderTest` only calls `setCommon`, so if the user *does* turn the drives up, that large-signal path is untested too — post-drive is never engaged, and the quick grid pins pre-drive to 0.)

### 2.4 Spec → plan → implementation drift

This is the fairness check, and it strengthens the finding rather than softening it. The team *knew* to measure level — they specified it:

- **Spec §6, B2:** "Resonance **peak gain & Q**… Self-oscillation **onset threshold**… **Limit-cycle amplitude + crest factor**."
- **Spec §6, B3:** "**THD vs input level & drive** … dirt-growth curve … **Idle noise floor**."

What survived:

- **Plan** (line 1130) reduced B2 to self-osc pitch + "peak gain/Q **from B1** at high resonance," and dropped onset threshold and limit-cycle amplitude/crest **without comment**. B3's amplitude sweep and noise floor vanished (the plan's `Harmonics.h` is a single-tone THD helper).
- **Implementation** delivered **even less than the reduced plan**: B2 is pitch-only; even the promised "peak gain/Q from B1 at high resonance" is never extracted into any metric. The runner *does* run B1 across the resonance grid, so the resonant peak is **latent in `response.csv` at res = 0.9** — but no summary key, gate, or doc ever reads it.
- **The build ledger** (`progress.md`, Task 8b) records B2 as "self-osc (max-res, energy-guarded peakFreqHz, selfosc_cents_err)" and B3 as "distortion/aliasing" — with **no note that peak gain, limit-cycle amplitude, THD-vs-level, or noise floor were cut.**

So the level axis wasn't weighed and rejected; it **evaporated** across three documents, and the "ready to merge" review validated built-vs-correct, never built-vs-spec. That's exactly the blind spot an outside review exists to catch.

### 2.5 Fit-for-purpose verdict

**No — not for the user's stated problem.** As a relative-response / aliasing / self-osc-pitch instrument it is genuinely good (§9). As the tool to answer "why is Huggett dangerous and Moog inaudible, and are they gain-matched," it is blind by construction. The good news, per §2.2: the cheapest fixes are *surfacing data the harness already collects* — the small-signal resonant peak is already in `response.csv`, and the self-osc amplitude is already computed and discarded.

### 2.6 Empirical confirmation (both models, `--quick`, this session)

I ran both models. Two facts, straight from the artifacts:

**(1) Neither `summary.csv` contains a single level key.** Both emit only `corner_hz`, `slope_db_oct`, `method_delta_db`, `selfosc_cents_err`, `thd_db`, `alias_db@os{1,2,4,8}`.

**(2) The hot/quiet disparity is enormous — and it is sitting unread in `response.csv`.** LP24, `res = 0.9`, **`drive = 0` (both drive knobs off)**, small-signal stepped-sine, os1:

| Cutoff | Huggett peak | Moog peak | **Disparity** | Huggett passband | Moog passband |
|---|---|---|---|---|---|
| 250 Hz | **+72.3 dB** | +4.7 dB | **67.6 dB** | −0.7 dB | −13.3 dB |
| 1 kHz | **+70.2 dB** | +4.8 dB | **65.4 dB** | −0.8 dB | −13.4 dB |
| 4 kHz | **+62.1 dB** | +4.8 dB | **57.3 dB** | −0.8 dB | −13.4 dB |

At an identical setting, all drive off, **Huggett's resonant peak is 57–68 dB hotter than Moog's** — and Moog additionally *loses ~13 dB of passband* (the authentic ladder bass-loss `1/(1+r)`, measured dead-on). That is exactly "Huggett screechy / Moog barely there," quantified. Every number is in `response.csv`; **not one reaches `summary.csv`, a gate, or a golden.** The harness measured the precise thing the user is complaining about, then deleted it on the way to the report.

*(Caveat: +72 dB is the **small-signal linear** peak; at real playing levels the resonance-loop saturator compresses it — which is exactly why the large-signal level battery is also needed, §6. And note a separate question this raises for deeper review: is a +72 dB uncompensated small-signal peak **authentic** to the Summit, or is `Q = 0.5 + res²·49.5` too aggressive? The sibling HP stage was already capped for this reason.)*

---

## 3. SOTA gap analysis — what an Arturia-grade suite would also measure

"What would Arturia / u-he / Cytomic / NI do to validate a VA filter?" Mapped against this harness:

| SOTA measurement | Here? | Notes |
|---|---|---|
| Relative magnitude response, dual-method cross-check | ✅ strong | Genuinely above-average; the ESS-vs-stepped agreement gate is real rigor. |
| Cutoff tracking / −3 dB corner vs setpoint | ✅ | `corner_hz` per cutoff — tracking is recoverable. Good. |
| Slope / order verification | ✅ (transition-band) | Honest about transition vs asymptotic. |
| Aliasing vs oversampling factor | ✅ strong | 0 → −117 dB across os1→os8 is a real deliverable and SOTA-aware. |
| Self-oscillation **pitch** tracking | ✅ | Cents error, authentic warping documented. |
| **Absolute gain staging / passband gain / headroom** | ❌ | The core gap. §2. |
| **Resonance peak gain & Q vs res** | ❌ (latent only) | In `response.csv`, never surfaced. Spec'd, dropped. |
| **Self-osc amplitude / limit-cycle level & crest** | ❌ (computed, discarded) | The thing that hits the limiter. |
| **Large-signal / drive-dependent gain** (gain vs input level) | ❌ | Nonlinear filters must be characterized at several levels, not just small-signal. |
| **Two-tone intermodulation (IMD)** | ❌ | Single-tone THD only; IMD is the standard large-signal companion. |
| **Idle noise floor** | ❌ | Spec'd, dropped. Matters for analog modeling. |
| **Parameter-modulation artifacts** (zipper/clicks on cutoff/res sweeps; ZDF stability under fast mod) | ❌ | All measurements are at static operating points. Real plugin-quality dimension. |
| **Stability / NaN / denormal robustness at extremes** | ❌ | Not probed. |
| **CPU / cost per OS tier** | ❌ | The aliasing-vs-OS curve argues *which* tier earns its keep on quality; cost is the other half. |
| **Null-test vs a reference model** | ❌ | More a #2 technique, but the gold standard for "is it authentic." |
| Phase / group delay | ⚠️ broken | B4 descriptive-only (Q20); known, documented. |

The pattern is consistent: **everything the harness measures well is about frequency-domain shape and spectral purity; everything it omits is about level, large-signal behavior, and dynamics.** That is precisely the half the user is complaining about.

---

## 4. The five design decisions (§5) — crisp second opinion

1. **ESS reference calibration** (`EssResponse.h`) — **Endorse.** `IR_sys/IR_ref = System(f)` is correct and non-circular; it preserves true gain (which is why §2.2's absolute level is actually present). Sound.
2. **Method-agreement scoped in-band (40 dB)** — **Endorse with a caveat.** Excluding the noise-limited deep stopband is principled, and the −60 dB-scatter evidence justifies it. The caveat: 40 dB is a reasonable default but is asserted, not derived; for #2's hardware comparison the stopband-rejection depth *is* a feature you may want to compare, so don't let the in-band convenience harden into "we don't measure the stopband."
3. **Aliasing isolation probe** (fixed hot condition) — **Endorse.** Honest, well-labeled in its own columns, and the right instrument for an OS-tier verdict. One of the best parts of the harness.
4. **Measured −3 dB corner + transition slope** (gate asserts `slope ≤ −3`) — **Endorse.** Correctly distinguishes transition-band from asymptotic; the 0.44×fc honesty is a credit.
5. **Tiny always-on gate grid (~5 s)** — **Endorse as a regression net.** It catches drift cheaply. But note it is a *shape* drift net; it would not catch a level regression even if a level metric existed and changed, because the grid runs at `drive = 0`.

No disagreement on correctness with the internal review. The disagreement is about **what was left unmeasured**, not whether what was measured is right.

---

## 5. Is the dual-method ruler worth its cost?

**Honest answer: it's elegant, and it's over-invested relative to the gaps.** The team built *two* independent ways to measure the *same relative quantity* (magnitude shape) and *zero* ways to measure the quantity that hurts (level). The dual-method cross-check is the intellectual centerpiece of the PR — and it produced the B4 phase liability as a side effect (writing the ESS phase test is what surfaced Q20).

I would not rip it out — cross-validation is how you earn trust in a ruler, and the stepped-sine alone could hide a systematic error. But if the question is "where should the next engineering hour go," it is **not** into tightening a 0.6 dB agreement on shape; it is into the first absolute-level metric. The ruler is sharp; it is pointed at the wrong workpiece.

---

## 6. Ranked gaps + effort sizing

**P0 — required to answer the user's question and to make #2 meaningful**

| # | Addition | Effort | Notes |
|---|---|---|---|
| 1 | **Surface what's already collected**: passband gain + resonant peak gain (read `response.csv` at high res), fix the "relative to passband" mislabel | **S** (hours) | Cheapest win; partially answers the question from existing data. |
| 2 | **Self-osc limit-cycle amplitude + crest factor** in B2 | **S** (hours) | Amplitude is already computed and discarded — just record it. |
| 3 | **Absolute output-level / headroom battery**: peak & RMS output vs input level **and drive**, with Huggett's pre+post-drive engaged | **M** (1–2 d) | Requires `FilterUnderTest` to expose `setPostDrive`; multi-level excitation. This is the metric that catches "hot/quiet." |
| 4 | **Inter-model gain reference**: a common reference operating point reported per model | **S** once #3 exists | Makes "Huggett vs Moog at the same setting" a number. |

**P1 — large-signal truth**

| 5 | **Drive-dependent gain curve** (gain vs input amplitude, the compression knee) | **M** (1–2 d) |
| 6 | **Two-tone IMD** alongside single-tone THD | **M** |

**P2 — Arturia-grade rounding-out**

| 7 | Idle **noise floor** | **S** |
| 8 | **Modulation/zipper/ZDF-stability** under dynamic cutoff/res sweeps | **M–L** |
| 9 | **Stability/NaN/denormal** stress at extremes | **S–M** |

**P3 — supporting**

| 10 | **B4 time-align fix** (Q20: align IR to its peak before the transfer function) | **S–M** | See §7. |
| 11 | **CPU/cost per OS tier** | **S** |
| 12 | **Null-test methodology** vs a reference | **M** | Primarily a #2 technique. |

S = hours, M = 1–2 days, L = several days. P0 #1+#2 alone (a half-day) would already let the harness *say something* about the disparity using data it already has.

---

## 7. Sub-project #2 go/no-go

**Verdict: conditional GO.** The MIDI→Summit→capture rig is the right backbone for the authenticity question, and the column-aligned schema is a reasonable foundation. But there are two rulings to make explicit, plus a schema caveat.

**(a) Absolute-level axis — HARD PRECONDITION. Build it in #1 before any hardware capture.**
The whole point of #2 is "is my in-the-box Huggett true to my real Summit." The audible worry — *screechy vs not, hot vs tame* — is a **level + large-signal** question. The capture rig will record the Summit's **absolute** output. If the in-the-box fingerprint has no absolute-level column to compare against, the expensive capture answers only "do the normalized shapes line up," not the question the user actually has. **#1 must grow P0 items #1–#4 first**, and you should prove they catch the *known* Huggett-hot/Moog-quiet disparity in-the-box before you spend a single hour capturing hardware. Otherwise #2 inherits #1's blindness — exactly the failure mode the brief worried about.

**(b) B4 phase / group delay — STRONG SHOULD, not a hard blocker.**
For a first, level-and-magnitude-focused #2 pass, B4 can stay deferred (it's non-gating and the level question dominates). **But** hardware capture is slow and expensive, and you do not want to re-capture the Summit later just to add phase. The Q20 fix is bounded and known (time-align the deconvolved IR to its peak before the transfer function). **Recommendation: fix B4 before the capture campaign** as cheap insurance, so the captured data can support a phase/GD comparison without a re-run. If you're certain #2 is magnitude+level only, forever, you may skip it — but that's a bet against your own curiosity.

**Schema caveat (resolve before #2 starts).** The operating-point schema is **param-space** (cutoff/res/drive), not **MIDI/capture-space**. A hardware rig needs columns the current schema lacks: MIDI note + velocity, the mapped Summit **CC values**, the Summit's own filter **drive/level** setting, and an **absolute capture-level reference** (dBFS/dBu) so in-box and hardware sit on one ruler. There's also an unanswered **methodology** question #1 doesn't address: how do you extract a transfer function from a real Summit — play its oscillators, or feed an external sweep through its filter if it has an audio-in/filter-FM path? None of this is a #1 defect (it's correctly deferred), but it should be on the table before #2 starts, and it reinforces that the shared schema needs the absolute-level + excitation-provenance extension from ruling (a).

---

## 8. What's genuinely good (credit where due)

This is not a teardown of the engineering — the engineering is solid; the *scope* missed the user. Real strengths:

- **The dual-method cross-check is real rigor** — ESS trusted only because it agrees with a trivially-correct stepped-sine. Most shops ship one engine and hope.
- **Aliasing-vs-oversampling is a true, SOTA-aware deliverable** (0 → −117 dB across tiers), and the isolation-probe honesty (separate, labeled columns) is exemplary.
- **The model-agnostic socket genuinely works** — Huggett drives through it (incl. Notch) with zero runner changes; future models slot in.
- **The honesty of the artifacts** — corner = 0.44×fc documented as authentic not a bug; B4's limitation disclosed rather than hidden; CSV columns labeled by measurement source. (The one lapse is the `magDb` "relative to passband" mislabel, §2.2.)
- **The always-on gate** is a sensible, bounded regression net.

The fix is additive, not corrective: keep all of this, and add the level axis it was specified to have.

---

## 9. Recommended next moves (in order)

1. **Half-day:** P0 #1 + #2 — surface passband/resonant peak gain from existing `response.csv`, record the self-osc amplitude/crest already computed, fix the mislabel. Re-run both models and **look at whether Huggett's resonant peak and Moog's passband now show the disparity.** This is the cheapest possible test of "can the harness see it at all."
2. **1–2 days:** P0 #3 + #4 — the absolute output-level/headroom battery with Huggett's drive + post-drive engaged, plus an inter-model gain reference. This is the metric that makes "hot vs tame" a number.
3. **Gate it:** validate that the new level battery reproduces the known Huggett-hot/Moog-quiet disparity in-the-box. Only then start #2's hardware capture.
4. **Before the Summit capture campaign:** extend the schema (MIDI/CC/level-reference columns), fix B4 (Q20), and settle the Summit excitation methodology.

The harness earned its keep on shape. Point the same rigor at level, and it will earn its keep on the problem the user actually has.

---

*Appendix — key source references:* `tests/testdsp/SteppedSine.h:47` (absolute gain), `tests/testdsp/EssResponse.h:48-52` (reference calibration), `tests/characterization/CharacterizationRunner.cpp:299` (self-osc amplitude discarded), `:533/:570-572/:598-602` (summary keys), `tests/characterization/FilterUnderTest.cpp:21` (only `setCommon`), `src/dsp/spine/HuggettFilter.h:49-52` (+30/+24 dB drive CALIB), spec `docs/superpowers/specs/2026-06-29-filter-validation-internal-design.md` §6 (level metrics specified), plan `docs/superpowers/plans/2026-06-29-filter-validation-internal.md:1130` (reduced), `docs/filter-validation/interpreting-results.md` (magDb mislabel).
