# Moog Ladder DSP in Cmajor (Spec 1 of 2) — Design

**Version:** 5.06 (artifact; distinct from plugin SemVer)
**Date:** 2026-06-22
**Status:** Approved (brainstorm) — pending spec review
**Roadmap item:** `v5.4` — Moog ladder (2nd filter model); `meta.nextStep` ("Moog as the FIRST production Cmajor filter block").
**Supersedes (DSP approach):** the hand-C++ implementation in `docs/specs/2026-06-20-moog-ladder-design.md` — that spec's **DSP math is reused**, but the Moog is now authored in **Cmajor** (ADR-0012), targets the **v5.1 in-place `FilterModel` interface**, ships **base-rate** (the OS tiers it assumed never landed), and gains a **synthesized bass voice**.
**Scope:** this spec is **Spec 1 — the Moog DSP component, built and tested in isolation (test target only)**. Registration in `FilterModelLibrary`, per-model param dispatch, the Type→"Filter Routing" + Notch consolidation, and UI are **Spec 2** (`[[followup-moog-filter-consolidation]]`).

---

## 1. Purpose & premise

The spine is an append-only, stable-ID `FilterModel` library (ADR-0011, register L7). Huggett is entry 0; Moog will be entry 1. Per the roadmap and ADR-0012, Moog is the **first production DSP block authored in Cmajor** (write `.cmajor` → `cmaj generate --target=cpp` → commit the generated C++ → wrap in a thin C++ adapter presenting `FilterModel`). This avoids the C++→Cmajor double-build that ADR-0012 exists to prevent, and it is the migration step that makes the v5.1 click-free model hot-swap *live* (the production library finally gets a second model).

Spec 1 delivers the **DSP component** — a Cmajor transistor-ladder LP with a synthesized bass voice, wrapped in an **in-place** adapter, **validated in isolation in the test target**. It does not touch the shipping plugin, the library registry, params, or UI (all Spec 2). This mirrors the Cmajor spike's `CmajorSvfFilter` (test-target-only) → production-promotion path.

A transistor ladder is legally clear (Moog US 3,475,623 expired 1986; see `docs/architecture/filter-dossiers-sem-moog.md`).

---

## 2. Decisions (resolved in brainstorm, 2026-06-22)

- **Authoring → Cmajor, one fused processor.** A single `MoogLadder.cmajor` processor holds the whole ladder + feedback + per-stage `tanh` + bass voice + limiter (ADR-0012: author fused per-voice processors, not micro-nodes — fused ≈ 1.3× C++, micro-nodes ≈ 1.95×). Generated to committed C++; the toolchain (Docker `cmaj-codegen.sh`, `[[reference-cmaj-toolchain]]`) is dev-time-only, never in CI or the shipping binary.
- **Anti-aliasing → base-rate (no per-voice OS object).** Plain per-stage `tanh` at base rate; a Pirkle-style output peak-limiter cleans self-oscillation to a near-sine. No `juce::dsp::Oversampling` per voice (L7/Q12). The HQ OS tiers the old spec assumed are a **separate, future** effort; the `OverdriveDiagnostic` FFT score quantifies whether base-rate is acceptable (non-gating readout).
- **State → in-place (production requirement), with a small codegen block size.** The adapter embeds the generated processor state **by value** and `constructState` placement-news it into `SpineFilterSlot`'s in-place buffer — no heap (the v5.1 invariant; the ADR-0012 heap→in-place follow-up). The spike's heap `unique_ptr` handle is **not** acceptable for production. The generated class bakes its I/O scratch as `Array<float, maxFramesPerBlock>` (measured: `sizeof(NlSvfDrive)=4240 B` at the default 512, ~4 KB of which is scratch); so the Moog is generated with a **small `maxFramesPerBlock` (~32–64)** to shrink that scratch ~16× (per-voice slot ~1–2.6 KB instead of ~17 KB). The adapter chunks the audio block to the generated cap — perf is **block-size-flat** (Spike II addendum), so ~no cost. **Embedding by value is already proven** (the existing `NlSvfDriveLeanAdapter::Impl` holds `Generated dsp;` inline; only the pimpl `unique_ptr` is dropped).
- **Validation → behavioral + Arturia golden-data, NOT a C++ twin.** There is no hand-C++ Moog to diff against and we will not build one (that is the double-build we are avoiding). Correctness = analytical/behavioral targets + an **Arturia Mini V golden-data calibration gate** (user-captured measurements, matched within tolerance — not a bit-reproduction of Arturia's proprietary algorithm).
- **Bass voice → played-note sub-oscillator, selectable waveform.** A per-voice sub-osc *inside* the Moog at the **played-note fundamental** (octave selector 0/−1/−2), waveform **sine/triangle/saw** (band-limited), summed **clean post-ladder**, amount `0..1` (**0 = authentic Minimoog thinning**), phase **resets on note-on**. This replaces the old gain-only `bassComp` (O2).
- **Note-pitch is per-voice, so it lives in the per-voice State.** `FilterModel` is pitch-blind and the model instance is shared across voices, so the fundamental cannot be a normal shared setter. **Spec 1:** `MoogLadder` exposes a model-specific `setFundamental(State&, float hz)` that writes the Hz into that voice's `VoiceState` (feeding the Cmajor `fundamental` input); tests drive it directly (the seam). **Spec 2** adds the polymorphic plumbing: an optional `virtual void setVoiceContext(State&, float fundamentalHz) const noexcept {}` on the base (default no-op, only `MoogLadder` overrides → forwards to `setFundamental`), called per-voice by `Voice`/`SpineFilterSlot`.
- **Carried from the old spec (unchanged):** one-step ZDF + delta-injected `tanh` (no per-sample Newton), resonance taper, Huovilainen tuning compensation, taps `y2`/`y4`, pole-mix BP/HP (O3), DC blocker (O4), `separation` is a no-op.

---

## 3. Architecture & files (test target only, this spec)

```
MoogLadder.cmajor  ──(cmaj generate, Docker)──▶  generated/MoogLadder.h  (committed)
                                                        │
                                          MoogLadderAdapter (in-place lean wrapper)
                                                        │
                                          MoogLadder : public FilterModel
                                                        │
                                          tests/MoogLadderTests.cpp  (+ golden/ data)
```

- Create: `src/dsp/spine/cmajor/MoogLadder.cmajor`, `MoogLadder.cmajorpatch`
- Create: `src/dsp/spine/cmajor/generated/MoogLadder.h` (committed codegen output)
- Create: `src/dsp/spine/cmajor/MoogLadderAdapter.{h,cpp}` (in-place wrapper, one per mono lane)
- Create: `src/dsp/spine/MoogLadder.{h,cpp}` (`FilterModel` subclass; stereo = two adapters)
- Create: `tests/MoogLadderTests.cpp`; `tests/golden/` Arturia capture(s)
- Modify: `tests/CMakeLists.txt` (add the new sources + test, like the existing cmajor blocks)
- Modify: `src/dsp/spine/SpineState.h` (bump `kMaxSpineStateBytes` if the Moog state exceeds 512 — measured)

(`FilterModel.h` is **not** touched in Spec 1 — the base `setVoiceContext` hook is a Spec 2 change; Spec 1's `MoogLadder::setFundamental(State&, hz)` is a model-specific method, not a base override.)

The generated render model (from ADR-0012): a self-contained struct, block-oriented (`setInputFrames` → `addEvent_*`/`setValue` params → `advance(n)` → `copyOutputFrames`), `maxFramesPerBlock = 512`. The adapter chunks larger blocks and presents a stable `prepare/reset/setParams/setFundamental/process` surface so no generated specifics leak past the one wrapper.

---

## 4. Cmajor processor — `MoogLadder.cmajor`

One fused mono processor (stereo = two instances sharing block-set coefficients).

**Endpoints:**
- `input stream float audioIn;` / `output stream float audioOut;`
- `input value float cutoffHz, resonance, drive;`           // common core
- `input value int mode;`        // 0=LP 1=BP 2=HP (pole-mix)
- `input value int slope;`       // 0=12 (tap y2) 1=24 (tap y4)
- `input value float fundamentalHz;`     // played note (for the bass voice)
- `input value float bassAmount;`        // 0..1 (0 = off / authentic thinning)
- `input value int   bassWave;`          // 0=sine 1=tri 2=saw
- `input value int   bassOctave;`        // 0, -1, -2
- `input event void  noteReset;`         // resets sub-osc phase (note-on)

**Per-block:** recompute prewarped `g`, one-pole `G=g/(1+g)`, feedback `r` from the resonance taper, the Huovilainen tuning correction, and the input-drive gain.

**Per-sample (the fused body):**
1. input drive shaper (plain `tanh`, gated on drive>0);
2. delta-injected per-stage `tanh` correction from the previous sample's stage outputs (`fbExtra`);
3. one-step closed-form linear ZDF ladder solve (`x − r·y4`); back-substitute stage states;
4. tap by `slope` (`y2`/`y4`) and `mode` (LP, or pole-mix BP/HP);
5. **bass voice:** advance the sub-osc phase at `fundamentalHz·2^bassOctave`, generate the `bassWave` sample (sine direct; tri/saw band-limited via PolyBLEP), scale by `bassAmount`, **add to the ladder output**;
6. output peak-limiter (Pirkle, bounded soft ceiling) on the **summed** signal;
7. DC blocker.

**State (per processor instance):** 4 stage integrator states + 4 `yPrev` (delta feedback) + sub-osc phase (+ any limiter/DC state). Linear-by-construction: `drive==0` and low `res` → no `tanh` calls (the linear ladder is exact), keeping the linearity gate green.

> **Bass-voice routing note:** the sub-osc is summed *after* the ladder and limiter-clamped *with* it, so it restores the thinned low end without re-entering the resonance loop. `bassAmount==0` must be **bit-identical** to the ladder-only path (the sub-osc contributes exactly 0).

---

## 5. In-place adapter & Q18 sizing

`MoogLadderAdapter` (one per mono lane) holds the generated processor **by value** (inline), exposing `prepare(sr)`, `reset()`, `setParams(cutoff,res,drive,mode,slope)`, `setBass(amount,wave,octave)`, `setFundamental(hz)`, `noteReset()`, `process(float* buf, int n)`. `MoogLadder::VoiceState` holds **two** adapters (L/R) inline. `constructState(void*)` placement-news the `VoiceState`; nothing allocates after `prepare`.

**Small-block codegen (decided 2026-06-22).** The generated I/O scratch is `Array<float, maxFramesPerBlock>×2`; at the default 512 that is ~4 KB/mono (measured `sizeof(NlSvfDrive)=4240 B`). Generate the Moog with `maxFramesPerBlock ≈ 32–64` so the embedded scratch is ~256–512 B/mono and the stereo dual-buffer slot stays ~1–2.6 KB instead of ~17 KB. The adapter's `process(buf,n)` chunks `n` to the generated cap (already the `NlSvfDriveLeanAdapter` pattern); perf is block-size-flat (Spike II) so chunking is free. **Task 1 confirms `cmaj` can set the codegen block size** (a generate flag or manifest setting); if it cannot, fall back to default-512 + the larger cap below, and note it.

**Q18:** measure `sizeof(MoogLadder::VoiceState)` after the small-block codegen; if it exceeds `kMaxSpineStateBytes = 512`, **bump the constant** (the per-model `static_assert` enforces it — the governance moment, not a silent shrink). Record the measured size + the new cap in the plan.

---

## 6. Test plan

New `tests/MoogLadderTests.cpp` (a `juce::UnitTest`, registered in `tests/CMakeLists.txt`). The Moog is driven directly (test seam sets `fundamentalHz`, params); no library/Layer/UI involvement (that is Spec 2).

**Layer 1 — behavioral/analytical gates:**
1. **Linear path** — `drive=0, res=0`: magnitude matches an analytical 4-pole LP at several frequencies to tight tolerance (proves linear-by-construction).
2. **Slope** — FFT rolloff ≈ **24 dB/oct** (`slope=24`, tap `y4`) and ≈ **12 dB/oct** (`slope=12`, tap `y2`) above `fc`.
3. **Resonance** — peak at `fc` grows monotonically with `resonance`; **bass-thinning** (low-freq magnitude drops as `res` rises) with `bassAmount=0`.
4. **Self-oscillation** — at max `res`: sustained non-decaying oscillation (onset), frequency ≈ `fc` within **±3%** across several `fc` (pitch-tracking — the tuning-comp CALIB target), near-sine THD (limiter on).
5. **Boundedness** — max res + max drive + loud input stays finite and below the limiter ceiling over a long run.
6. **`separation` inert** — `setSeparation(0)` vs `(2)` produce identical output (no-op assertion).

**Layer 2 — bass-voice gates:**
7. **Sub present at pitch** — `bassAmount>0`, known `fundamentalHz`: spectral energy at the expected sub frequency; `bassOctave=-1/-2` shifts it by the expected octave(s).
8. **Waveform signature** — sine = ~single partial; triangle/saw show the expected (band-limited) harmonic series; alias energy above Nyquist-image bands stays bounded (PolyBLEP check).
9. **Amount=0 is a no-op** — `bassAmount=0` output is **bit-identical** to the ladder-only build.
10. **Phase reset** — `noteReset()` restarts the sub-osc phase (deterministic attack).

**Layer 3 — Arturia golden-data calibration gate:**
11. User captures **Arturia Mini V** filter response — magnitude/phase over a cutoff×resonance grid, self-osc pitch vs `fc`, and a driven harmonic snapshot — exported into `tests/golden/`. The test asserts the Moog matches **within tolerance** (bands per metric; tolerances are CALIB, set during the calibration pass). This is the "sounds like a Minimoog" gate; tolerances start loose and tighten as the taper/tuning/limiter CALIBs are pinned.

**Diagnostic (non-gating):** the `OverdriveDiagnostic` inharmonic-energy score on a driven self-oscillating Moog, printed to inform any future OS-tier decision.

---

## 7. Scope boundary (Spec 1 vs Spec 2)

**In Spec 1:** the Cmajor processor + generated C++ + in-place adapter + `MoogLadder : FilterModel` + the test-target build + all tests + Arturia golden harness + the `kMaxSpineStateBytes` bump if needed. (The base `FilterModel::setVoiceContext` hook is **Spec 2** — see §2/§6 and the Deferred list below; Spec 1 exposes only the model-specific `setFundamental(State&, hz)`, not a base override, and does not touch `FilterModel.h`.)

**Deferred to Spec 2:** registering `"moog"` in `FilterModelLibrary` (promote to plugin sources); per-model param-bank dispatch in `Layer`; **adding the base `FilterModel::setVoiceContext` hook** + the played-note plumbing through `Voice`/`SpineFilterSlot`; the bass-voice params (`spine.moog.bassAmount/bassWave/bassOctave`) + the common-core consolidation (remove Type, rename Routing→"Filter Routing", add Notch to Huggett); UI; version-surface bump; roadmap/register update.

---

## 8. Risks & open items

- **Codegen block size + inline embedding (primary risk, task 1).** Embedding by value is already proven (the existing lean adapter holds `Generated dsp;` inline). The open unknown is whether `cmaj` lets us set a small `maxFramesPerBlock` at codegen to shrink the baked I/O scratch (~4 KB → ~0.5 KB). **Task 1 proves both** — generate a trivial Moog-shaped processor at a small block size, embed it by value, `constructState` into a slot buffer, and measure `sizeof` — before any DSP work. Fallback if the block size is not settable: accept default-512 and bump `kMaxSpineStateBytes` to ~8.5 KB (≈1.1 MB total voice state — acceptable, just inelegant), documented in the plan.
- **PolyBLEP at low fundamentals.** A bass saw/tri has many in-band harmonics; PolyBLEP tames the discontinuity alias but very low notes are cheap to keep clean. Sine needs none. Calibrate alias tolerance in test 8.
- **Arturia capture is user-supplied + collaborative.** I build the golden harness; the measurements come from you. Until captured, the gate runs with placeholder tolerances on the behavioral metrics; the Arturia comparison lands during the calibration pass.
- **`bassAmount==0` bit-identity** must hold across the fused processor (the sub-osc path must contribute exactly 0, not an epsilon) — explicitly tested (test 9).
- **CALIB constants** (resonance taper, Huovilainen tuning correction, limiter ceiling, bass-voice levels, Arturia tolerances) are pinned in a calibration pass, never changed to force a test green.
