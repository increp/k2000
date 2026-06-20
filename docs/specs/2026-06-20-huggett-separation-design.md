# v5.2 — Real Separation / dual-filter (Huggett flagship completion)

Design spec for fixing and completing the Huggett **Separation** behaviour: two always-active 2-pole sections with independently offset cutoffs that work in **both** 12 and 24 dB, plus the Summit series/parallel **dual-filter routings**.

Version: 5.01
Status: Draft (design only — no code)
Date: 2026-06-20
Phase: v5.2 (see [roadmap](../roadmap/phases.md))

---

## 1. Purpose & problem statement

Separation is documented as **the** Huggett signature — "two distinct resonances" from two linked 2-pole sections offset in cutoff (OSCar `Separation`; Summit's nine dual routings). See [huggett-filter.md](../architecture/huggett-filter.md) and [huggett-filter-dossier.md](../architecture/huggett-filter-dossier.md) §31/§41/§99.

**Today it is broken.** In [`HuggettFilter.cpp`](../../src/dsp/spine/HuggettFilter.cpp):

- Cell `b` is offset (`cutB = cutoff * 2^separationOct`) but **only runs `if (slope_ == Slope::db24)`**.
- Cell `a` always runs at the *unoffset* cutoff.

Consequences, confirmed by measurement this session (the separation block of [`OverdriveDiagnosticTests`](../../tests/OverdriveDiagnosticTests.cpp)):

- **12 dB:** only cell `a` runs → Separation is **completely dead** (no audible or measurable effect).
- **24 dB:** cells `a` (at cutoff) and `b` (at offset cutoff) are in series → Separation merely **shifts the composite corner**; it never produces two distinct resonant peaks because `a` sits at the fixed cutoff and the pair is always a serial low-pass-of-a-low-pass.

This is structurally wrong against the dossier: separation should offset **both** sections symmetrically (or at minimum drive a real two-peak structure), should be **slope-independent in existence**, and the mode/slope choice should select **how the two sections combine**, not **whether the second section exists**.

This spec defines the corrected dual-section architecture, the routing matrix, the parameter model, preset migration, the test plan, and the calibration/build sequence.

---

## 2. What the dossier actually says (load-bearing constraints)

Pinned from the research, so the design is grounded rather than invented:

1. **The core is two 12 dB/oct sections, always.** OSCar: "actually two filters, each with 12 dB/octave cut-off slope," whose cutoffs move together but can be offset by Separation (huggett-filter.md §"Historical", dossier §31/§68/§71). Summit: "one state-variable OTA filter per voice" with 12/24 dB and dual-filter combinations; the 24 dB mode is **two 12 dB stages in series** (dossier §40).
2. **Slope = how the two sections relate, not how many exist.** In LP/HP: zero separation + series = 24 dB; the single-section / parallel arrangement = 12 dB. In BP: "one filter acts as low-pass and the other as high-pass, and separation controls the band width" (huggett-filter.md §"Historical", dossier §31).
3. **Separation yields two distinct resonances, especially in band-pass mode** (huggett-filter.md §"Historical", dossier §31).
4. **The nine Summit dual routings, verbatim** (huggett-filter.md line 21, dossier §41):
   - **Series ("→"/"to"):** `LP→HP`, `LP→BP`, `HP→BP` — core1 feeds core2.
   - **Parallel ("+"):** `LP+HP`, `LP+BP`, `HP+BP`, `LP+LP`, `BP+BP`, `HP+HP` — outputs summed.
5. **Implementation recipe (dossier §99):** "Run two independent 2-pole cores with independently offset cutoffs (Separation = the offset/ratio between the two cutoffs) and provide the nine series/parallel routings. Series routings feed core1 → core2; parallel routings sum the two outputs."
6. **Keep ZDF/TPT.** "If modulation produces zipper noise or instability at high resonance, you have not implemented true ZDF" (dossier §137). Our [`NlSvfCell`](../../src/dsp/spine/NlSvfCell.h) is already TPT (Cytomic), so this is satisfied as long as we keep using it.

---

## 3. Key design decision — how 12 vs 24 dB coexists with two always-active sections

**Decision (D1): the two `NlSvfCell` sections are ALWAYS active. The slope switch and the routing choice both resolve to a `(modeA, modeB, combine)` triple — they never gate a section on/off.** Separation is the cutoff offset applied to the two sections in **every** mode and slope.

We split cutoff symmetrically around the common cutoff so the offset is balanced and the "two distinct resonances" straddle the corner rather than one peak sitting on it:

```
cutA = cutoff * 2^(-separationOct / 2)
cutB = cutoff * 2^(+separationOct / 2)
```

(At `separation = 0`, `cutA == cutB == cutoff`, so all single + dual modes collapse to the documented un-separated behaviour and old presets are unchanged — see §6.) The half-octave-each split is a calibration choice; the alternative "fix A at cutoff, offset B by full separation" is rejected because it cannot produce a symmetric two-peak structure and is exactly today's bug shape.

The single-mode (LP/BP/HP) slope choice maps as follows:

| Mode | Slope | modeA | modeB | combine | Result |
|---|---|---|---|---|---|
| LP | 12 dB | LP @ cutA | LP @ cutB | **parallel sum** | two 1-pole-pair LP peaks; at sep=0 → a single 12 dB LP (one section's worth of slope), see D1a |
| LP | 24 dB | LP @ cutA | LP @ cutB | **series** | sep=0 → classic 24 dB LP; sep>0 → broadened/double-kneed LP, two resonances |
| HP | 12/24 | HP | HP | parallel / series | symmetric to LP |
| BP | any | LP @ cutB (high cut) | HP @ cutA (low cut) | **series** | classic SVF band-pass-by-construction; **separation = bandwidth** (the OSCar BP law), two distinct edge resonances |

**D1a — resolving "12 dB from two 2-pole sections."** A literal parallel sum of two 2-pole LPs at the same cutoff is **not** a 12 dB filter (it is 2× a 12 dB filter — still 12 dB/oct asymptotically, with a +6 dB gain bump and a doubled resonant peak). That is acceptable and arguably *more* Huggett (it is exactly the "two distinct resonances" behaviour at non-zero separation). To keep `separation = 0, 12 dB` bit-identical to today's single-section response (preset stability, §6), we special-case it:

> **At `separation == 0` AND slope `12 dB`, run a single section** (`b` muted, `a` at `cutoff`), reproducing today's exact 12 dB output. For `separation != 0` in 12 dB, both sections run in parallel and the second peak appears. This is the minimal special-case that satisfies both "separation works in 12 dB" and "old presets unchanged."

This is the crux: **slope no longer decides whether section `b` exists; it decides series-vs-parallel combination, and a single narrow special-case (`sep==0 && 12 dB`) preserves the legacy single-pole-pair response.**

---

## 4. Dual-section architecture

### 4.1 Section combination primitive

Both `NlSvfCell`s already produce any tap (LP/HP/BP/Notch). We add a per-mode `(tapA, tapB, combine)` resolution and a small combiner:

- **Series:** `y = cellB.process(cellA.process(x, tapA), tapB)`. Per-sample, in place, preserving the existing pre/res-sat/post nonlinear ordering.
- **Parallel:** `yA = cellA.process(x, tapA); yB = cellB.process(x, tapB); y = 0.5*(yA + yB)`. The `0.5` keeps unity-ish gain for `LP+LP`-type sums; calibrated per routing (CALIB) — some Summit parallel routings (`LP+HP`) are naturally complementary and may want full sum. Pin during §8 calibration.

Both sections see the **same input** in parallel; in series, `b` sees `a`'s output. Resonance feedback stays *inside* each `NlSvfCell` (the self-limiting saturator is per-section), so "two distinct resonances" emerge from two independent resonant loops — exactly the dossier's intent.

### 4.2 Process loop shape (replacing the current body)

Per block (cheap, recomputed when params change), set `cutA`/`cutB`, resonance, res-sat on both `a` and `b`. Per sample: pre-drive shaper → section combine (series or parallel, per the resolved triple) → post-drive shaper → DC blocker (when nonlinear). The nonlinear-path gating (`preOn || postOn || res>0`) and the plain-tanh pre/post `AsymSaturator`s are unchanged from the current remediated implementation — **this spec touches only the linear section topology + combine, not the drive stages.**

### 4.3 Where this lives in the interface

`setMode/setSlope/setSeparation/setPostDrive` are **not** on the abstract [`FilterModel`](../../src/dsp/spine/FilterModel.h) — they are Huggett-bank setters reached in [`Layer.cpp`](../../src/Layer.cpp) via `dynamic_cast<HuggettFilter*>`. v5.2 stays inside that pattern: we **add `setRouting(Routing)`** to `HuggettFilter` only, set from the new routing param after the same dynamic_cast. The `FilterModel` interface is **unchanged** (no churn to other future models / the slot / the hot-swap path).

---

## 5. The routing matrix and v5.2 scope

`HuggettFilter::Mode` today is `{ LP, BP, HP }` (sourced from `svfType`). We introduce a richer **Routing** enum that supersedes Mode for the Huggett bank while keeping the three legacy values as the first three entries (stable indices — see §6):

```
enum class Routing {           // index — STABLE, append-only
    LP = 0, BP = 1, HP = 2,    // legacy single modes (slope decides 12/24 + series/parallel per §3)
    // --- Summit dual routings (new in v5.2) ---
    SeriesLPHP = 3,  SeriesLPBP = 4,  SeriesHPBP = 5,   // LP→HP, LP→BP, HP→BP
    ParLPHP    = 6,  ParLPBP    = 7,  ParHPBP    = 8,   // LP+HP, LP+BP, HP+BP
    ParLPLP    = 9,  ParBPBP    = 10, ParHPHP    = 11   // LP+LP, BP+BP, HP+HP
};
```

Each dual routing resolves to `(tapA, tapB, combine)` with `cutA`/`cutB` from the symmetric separation split. Slope is **ignored** for the explicit dual routings (the routing already names both sections' poles); slope only applies to the three single modes.

### v5.2 ship scope vs stretch

- **Ship (v5.2):** the three single modes correct in **both** 12 and 24 dB with working separation, **plus all six parallel routings and all three series routings** (the full nine). Rationale: once the `(tapA, tapB, combine)` machinery and symmetric split exist, the nine routings are pure table entries — the cost is in the combiner + UI param + calibration, which we pay once.
- **Stretch / deferred:** the dossier's distinct **"OSCar mode"** (one-sided positive separation, more aggressive pre-filter drive law, stronger resonance splitting — huggett-filter.md §11/§163/§182). That is a *voicing variant*, not new topology; defer to a later Huggett point release. v5.2 implements the **Summit** separation/routing law (bipolar separation centred at 0), which the existing `-2..+2 oct` param already matches.

---

## 6. Parameter model & preset migration

Constraints: presets must stay stable; params are **additive with stable IDs**; the spine bank is per-Layer.

### 6.1 New param

Add **one** per-layer choice param, additive:

- ID: `layerN.spine.huggett.routing` (namespaced under the Huggett bank, mirroring `spine.huggett.postDrive`).
- Type: `AudioParameterChoice`, 12 entries, default **0** (= `LP`).
- Display names: `"LP", "BP", "HP", "LP→HP", "LP→BP", "HP→BP", "LP+HP", "LP+BP", "HP+BP", "LP+LP", "BP+BP", "HP+HP"` (decode the arrow via `util::u8()` per the [UTF-8 boundary rule](../architecture/) — `→` is `\xE2\x86\x92`).
- Snapshot: add `int huggettRouting = 0;` to [`ParamSnapshot`](../../src/params/ParamSnapshot.h); read in `snapshot()`; add `id.spineHuggettRouting` to `LayerIds`/`buildIds`.

### 6.2 Relationship to the existing `filter.type` / `spine.slope`

- The legacy `svfType` (`filter.type`) currently drives the Huggett Mode (jlimited 0–2). **Keep it as the source for routings 0–2** (back-compat), but route the new `huggett.routing` param as an **override when > 2**. Decision **D2:** routing param value 0 = "follow legacy LP/BP/HP from `filter.type`"? **No** — cleaner: the new param fully owns Huggett mode selection; on migration we **seed `huggett.routing` from the old `filter.type`** (LP/HP/BP → 0/2/1 mapping; note `filter.type` order is LP,HP,BP,Notch while Routing is LP,BP,HP, so map indices, Notch→LP). After migration, `huggett.routing` is the single source of truth; `filter.type` keeps driving the *optional palette* SVF block only.
- `spine.slope` is unchanged and continues to apply to routings 0–2 only.

### 6.3 Migration step

Additive + a one-time value seed (the AlgorithmLibrary/[ADR-0008](../decisions/0008-algorithm-selection-and-param-namespace.md) idiom):

1. New `huggett.routing` choice defaults to `0` → a freshly loaded **old preset** (no stored value) reads `LP`, and with `separation` already stored, the corrected engine reproduces:
   - old `12 dB`: single section at cutoff (D1a special-case) — **bit-stable** vs today.
   - old `24 dB`: series LP→LP — same composite as today at sep=0; at sep≠0 the response changes (today it was the broken corner-shift; the *fix* is the intended behaviour, documented as an acceptable sonic change for the broken case).
2. On preset load where `huggett.routing` is absent but `filter.type` is present, seed `huggett.routing` from `filter.type` (index remap above) so a preset that had selected HP/BP keeps its mode. Keep this in the existing cumulative-migration path; bump the preset/state version and document in the migration changelog.

**Stable-ID guarantee:** Routing enum is append-only; the three legacy values keep indices 0/1/2. No existing ID is renamed or reordered.

---

## 7. "Two distinct resonances" & interaction with resonance / saturator

- Each section has its own resonance loop and its own self-limiting `satRes` (the `fbExtra` correction in [`NlSvfCell`](../../src/dsp/spine/NlSvfCell.h)). Two sections at offset cutoffs with the same `resonance_` therefore self-oscillate at **two different pitches** when resonance is high — directly the documented OSCar/Summit behaviour.
- **Resonance is shared** (one `resonance_` from the common core) — both sections get the same Q. This matches the hardware (one resonance knob, two linked sections). Per-section resonance is explicitly **out of scope** (no such hardware control).
- **BP mode** is where two distinct resonances are most audible: `HP @ cutA` (low edge) in series with `LP @ cutB` (high edge); separation widens the band and the two edge resonances pull apart. The diagnostic must show **two peaks** as separation increases (§8 T3).
- **Saturator stability under two loops:** parallel routings sum two *independently* self-limited outputs → bounded. Series routings cascade them; the upstream section's saturator limits what reaches the downstream loop → still bounded. No new stability mechanism needed, but the test plan asserts finiteness + no runaway at max resonance × max separation across both slopes and all routings (§8 T5).

---

## 8. Test plan (extends `OverdriveDiagnosticTests`)

Build on the existing FFT/inharmonic-energy + steady-state magnitude-response harness ([`tests/OverdriveDiagnosticTests.cpp`](../../tests/OverdriveDiagnosticTests.cpp)). These move from "print + assert finite" toward **pass/fail gates** (the harness's stated trajectory in [phases.md](../roadmap/phases.md) §"Testing harness").

- **T1 — Separation is alive in BOTH slopes.** Extend the existing "separation: LP response vs sep (db12 vs db24)" block. **Gate:** the magnitude response at a fixed probe frequency must differ by > X dB between `sep=0` and `sep=2` in **12 dB** (today: 0 dB → currently FAILS) and in **24 dB**. Asserts the dead-in-12 dB bug is fixed.
- **T2 — sep=0 stability vs legacy.** Assert `sep=0` responses are unchanged from a captured golden (12 dB single-section; 24 dB series) within tolerance → preset stability.
- **T3 — Two distinct resonant peaks.** In BP and in `ParLPLP`/dual modes at high resonance, sweep the response and **count spectral peaks**; assert **≥ 2 distinct maxima** whose separation in Hz grows monotonically with the `separation` param. (New peak-finder helper on the magnitude vector.)
- **T4 — Routing responses.** For each of the nine routings, assert the qualitative shape: series LP→HP / parallel LP+HP give band-pass-like vs complementary responses; `HP+HP` rejects lows; etc. Table-driven, magnitude-response based.
- **T5 — Stability / no NaN / no zipper.** At `res=0.999`, `sep=±2`, all routings, both slopes, block-128 vs per-sample: assert `allFinite`, bounded peak, and block-vs-sample max-diff below threshold (no per-block coefficient click — reuses the existing block-vs-sample metric).
- **T6 — Inharmonic-energy regression.** The existing post-drive aliasing metric must not regress for the single modes (topology change must not add inharmonic energy at sep=0).

---

## 9. Calibration (gray-box, against the user's Summit later)

Consistent with the v5 gray-box posture (literature defaults now; hardware-pin later — phases.md follow-up #4). All tunables marked `// CALIB`:

- The symmetric-split exponent (half-octave each — D1) and whether BP uses symmetric split or a one-sided width law.
- Parallel-sum normalisation per routing (`0.5` vs complementary full-sum for `LP+HP`).
- Confirm `separation` range stays `-2..+2 oct`; the dossier notes OSCar separation reaches ~4 octaves, so a wider range is a possible later CALIB (param range change → additive, but re-tune).
- **Hardware step (deferred, needs the user's Summit):** capture (a) self-osc pitch of each section vs note, (b) resonance sweeps at 12 vs 24 dB per mode, (c) Separation sweeps in BP and a dual routing, and match peak spacing + bandwidth law. This is the same capture set the dossier prescribes (§143) and the existing CALIB follow-up.

---

## 10. Build sub-sequence (tasks)

1. **Section combiner + symmetric split** in `HuggettFilter` — `cutA`/`cutB` split, `(tapA, tapB, combine)` resolution, series/parallel combine in the process loop. Keep single-mode 12/24 behaviour (D1, D1a special-case). No new param yet; drive `Routing` from existing `svfType` for modes 0–2. (Unit-testable immediately via T1/T2/T5.)
2. **`Routing` enum + `setRouting()`** on `HuggettFilter`; resolve all nine routings to triples. Wire from a temporary hard-coded value to validate T3/T4 before the param exists.
3. **Param + snapshot + migration** — add `spine.huggett.routing` choice (12 entries, UTF-8 names), `ParamSnapshot.huggettRouting`, `LayerIds`, `snapshot()` read, the `filter.type → routing` seed migration, version bump. Wire in [`Layer.cpp`](../../src/Layer.cpp) after the existing `dynamic_cast<HuggettFilter*>`.
4. **UI** — add the routing combo to the filter section in `PluginEditor` (widened combo per the v5.0 UI-cosmetics fix); bind via `binder_`. The legacy LP/HP/BP `filter.type` combo's role for the spine is superseded (it stays for the palette SVF block).
5. **Tests** — land T1–T6 as gates in `OverdriveDiagnosticTests` (peak-finder helper, routing table). Run the existing diagnostic to confirm the dead-in-12 dB measurement now shows effect.
6. **Calibration pass** — pin the gray-box `// CALIB` constants from literature; flag the hardware capture as a follow-up.
7. **Docs** — note the resolution in [huggett-filter.md](../architecture/huggett-filter.md)/dossier (separation now real in both slopes; nine routings shipped), tick the v5.2 row in [phases.md](../roadmap/phases.md), register/resolve the relevant engine question(s).

Build with bounded parallelism (`-j4`).

---

## 11. Open questions (for the user)

- **OQ1 (slope semantics in 12 dB).** §3/D1a special-cases `sep==0 && 12 dB` to a single section for exact preset stability, but `sep!=0 && 12 dB` runs two sections in parallel (a +6 dB-ish doubled peak). Is that acceptable, or do you want 12 dB to *always* run a single section and route separation only into the dual/BP routings? (The dossier supports the dual-section reading, but it's a voicing call.)
- **OQ2 (parallel normalisation).** Sum-and-halve vs complementary full-sum for `LP+HP`-type routings — pin now, or leave for the Summit calibration session?
- **OQ3 (filter.type vs routing param).** OK to make the new `spine.huggett.routing` the single source of truth for the spine mode (seeding it once from `filter.type`), leaving `filter.type` to the optional palette SVF only? Or keep `filter.type` driving LP/BP/HP and add the routing param only for the six+three dual entries?
- **OQ4 (OSCar mode).** Defer the distinct one-sided "OSCar mode" voicing (separate, more aggressive drive + resonance-split law) to a later point release, shipping only the bipolar Summit law in v5.2 — agreed?
- **OQ5 (separation range).** Keep `-2..+2 oct`, or widen toward the OSCar's documented ~4 octaves now (additive param-range change, needs re-calibration)?

## Decisions (resolved 2026-06-20, user review)

- **OQ1 (12 dB parallel level)** — keep the authentic **+6 dB overlap bump** (two sections in parallel); trim overall spine level globally if it's too hot.
- **OQ2 (LP+HP combine)** — **sum-and-halve** for level consistency.
- **OQ3 (mode control)** — **`spine.huggett.routing` is the single source of truth** for spine mode; seed it once from `filter.type`.
- **OQ4 (OSCar one-sided voicing)** — **deferred** to a later stretch; v5.2 ships the bipolar Summit law + the nine routings.
- **OQ5 (separation range)** — **widen to ±4 oct** (additive range change; re-calibrate).
- The `→` routing display names must pass through `util::u8()` at the JUCE boundary (UTF-8 rule).
