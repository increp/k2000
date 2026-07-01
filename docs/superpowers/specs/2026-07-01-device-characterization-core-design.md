# Device-Characterization Framework — The Core (SP-A) — Design

**Version:** 5.10 (artifact; distinct from plugin SemVer 5.9.0)
**Date:** 2026-07-01
**Status:** Approved (brainstorm) — pending spec review
**Relates to:** Generalizes PR #7 (`feat/filter-validation-internal`, the filter-validation harness) per the L3 analysis [`ROADMAP-PR7-filter-validation-L3.md`](../../../ROADMAP-PR7-filter-validation-L3.md). Extends spec [`2026-06-29-filter-validation-internal-design.md`](2026-06-29-filter-validation-internal-design.md).
**Applies to:** All devices under test — filters (Huggett, Moog, and future Yamaha/Oberheim/…), oscillators (future), and real-hardware captures (Summit, Arturia; via SP-D). Device-agnostic by construction.

---

## 0. Where this sits — the framework decomposition

The user asked for a **general device-characterization framework** that can vouch for *all* current and future filters and oscillators, is **extensible** to new models cheaply, and is **trustworthy** — with the explicit north star that the in-the-box DSP be **authentic to the real hardware** and **musical/pleasing**. That scope is too large for one spec, so it is decomposed into four sub-projects, each with its own spec → plan → build cycle:

- **SP-A — The Core (THIS SPEC).** A device-agnostic measurement instrument: the ruler, the `DeviceUnderTest` contract, the recording infrastructure (schema/summary/gate/golden), and the **trust layer**. The keystone every other piece rides on.
- **SP-B — Filter profile.** The level + large-signal filter batteries and the extensibility proof (a reference filter). Answers the live Huggett-hot / Moog-tame problem in-box. Depends on SP-A.
- **SP-C — Oscillator profile.** Oscillator batteries (spectral purity, aliasing-vs-pitch, waveform accuracy, sync/PWM/sub-osc, level). Depends on SP-A.
- **SP-D — Hardware bridge (sub-project #2).** MIDI→device→capture, absolute calibration, the Summit excitation-method spike, null-test, grey-box fit to real Summit/Arturia. Depends on SP-A (+ a profile).

**Build order:** SP-A first (B/C/D all depend on it) → SP-B (live pain + Summit-authenticity goal) → SP-C → SP-D. B and C are independent and may reorder/parallelize.

**Prior context this inherits (from PR #7 + the L3 analysis):** the existing harness measures frequency-response *shape* well but has **no absolute-level axis**; it summarizes both nonlinear filters only at their linear operating point; the level metrics were spec'd-then-dropped; Huggett's resonance is effectively ungated. SP-A fixes the categorical gap (adds the level axis and generalizes), without changing any shipping DSP — per the standing decision to **hold all DSP-voicing changes for hardware** (SP-D).

---

## 1. Purpose & premise

Build **one trustworthy, device-agnostic measurement instrument** — the Core — on top of the existing dual-method ruler, such that:

1. Any device (filter, oscillator, future model, hardware capture) plugs in through a single `DeviceUnderTest` contract, and adding one requires **zero runner/schema/gate changes**.
2. The instrument measures **absolute level** — the axis the L3 analysis identified as the missing half — alongside the shape/aliasing/self-osc it already reads.
3. The instrument's **accuracy is proven against known answers** before it is trusted on real DSP, and its trust claims never outrun their evidence (math-proven now; hardware-vouched when the rig exists).
4. Physical measurements are primary; a small set of **published perceptual weightings** add musical relevance as an additional, labeled lens — never replacing the physics.

The premise is Approach #2 (agreed in brainstorming): **extract a clean device-agnostic Core and port the existing filters onto it**, reusing the proven L0 ruler rather than rebuilding or bolting oscillator support onto filter-shaped code.

---

## 2. Goals / Non-goals

**Goals (SP-A)**
- A `DeviceUnderTest` contract that filters, oscillators, and (later) hardware captures satisfy, distinguished by an **excitation mode** (input-sweep / trigger / midi-capture).
- Extend the L0 ruler with **absolute-level/gain extraction**, the **two perceptual lenses**, and **capture-calibration** math — all as pure signal helpers.
- A **device-agnostic runner + schema/summary/gate/golden** that is level-aware and hardware-reference-ready.
- The **trust layer**: synthetic known-answer references (CI, seconds) + physical-reference-standard calibration capability (synthetically validated) + a written hardware-correlation acceptance criterion.
- **Port the existing Huggett/Moog filter batteries onto the Core** so nothing regresses.
- Level-regression **gates** (closing the "Huggett resonance ungated" hole) + synthetic-recovery gates, always-on in `k2000_tests`.
- Test-driven throughout: the synthetic references *are* the tests.

**Non-goals (deferred to later sub-projects)**
- The new large-signal filter batteries and the actual reading of Huggett's +72 dB → **SP-B** (they fall out of SP-A cheaply the moment SP-B's Phase 0 runs).
- Oscillator batteries → **SP-C**.
- The real MIDI→Summit→capture rig, the Summit **excitation-method spike**, and satisfying the hardware half of the acceptance gate → **SP-D**.
- Any change to shipping filter/oscillator **DSP voicing** → held for hardware (SP-D-guided).
- B4 phase/group-delay time-align fix (Q20) → tracked, scheduled before the SP-D capture campaign, not SP-A.

---

## 3. Architecture — three tiers + the `DeviceUnderTest` contract

**L0 — the ruler (device-agnostic signal analysis).** The existing `tests/testdsp/*`, kept and extended. It runs on the adapter contract `void reset(); void process(float* buf, int n)` — which an oscillator satisfies (it overwrites the buffer with generated output) as readily as a filter (it transforms the buffer). SP-A *adds* pure helpers here: absolute-level/gain extraction, audibility-weighted aliasing, A-weighting, and capture-calibration (loopback + physical-reference math). No device knowledge — math over signals.

**L1 — the device socket (`DeviceUnderTest`).** Generalizes `FilterUnderTest`. Wraps any DSP unit behind the L0 adapter plus a capability descriptor:

```cpp
struct DeviceUnderTest {
    // L0 adapter — any ruler can measure it
    void  reset();
    void  process(float* buf, int n);

    // capability descriptor
    DeviceKind  kind()       const;   // TransferFunction | Generator | Captured
    Excitation  excitation() const;   // InputSweep | Trigger | MidiCapture
    bool        supports(Mode m);
    void        setOperatingPoint(const OperatingPoint& op);   // generalized param superset
    void        setVoiceContext(/* played-note context */);    // e.g. fundamental Hz
};
```

The one thing that differs across device types is **how they are excited**: a filter is fed a signal; an oscillator is triggered and its emission recorded; hardware is sent MIDI and captured. The device declares its `Excitation`; the runner selects the matching **excitation driver**. Everything downstream (metric extraction, recording) is identical.

**L2 — runner & infrastructure (device-agnostic) + batteries (device-specific).** The runner iterates operating points, invokes the right excitation driver, runs the device's declared batteries, and records results in the standard schema — knowing nothing about filters vs oscillators. A **battery** is `(name, how-to-excite, what-to-measure) → metrics`. Filter batteries live in SP-B, oscillator batteries in SP-C. The Core ships one **reference battery** that exercises the synthetic references and self-proves the instrument end-to-end.

**Extensibility guarantee:** a new filter or oscillator is added by writing one `DeviceUnderTest` adapter + declaring its batteries. The runner, schema, summary, gate, and golden machinery are untouched.

---

## 4. The absolute-level axis + perceptual lenses

### 4.1 Absolute-level axis

Everything anchors to **full-scale digital: 0 dBFS = ±1.0 peak**, so filters, oscillators, and hardware share one ruler. Metrics, split by cost:

*Cheap — surface data the ruler already computes (the L3 "Phase 0"):*
- **Passband gain** and **resonant-peak gain** per operating point — `max|H|` and the passband anchor at *each resonance* (not only corner/slope at res=0).
- **Self-oscillation limit-cycle amplitude + crest factor** — the B2 amplitude currently computed as an energy guard and discarded.
- **Idle noise floor** (absolute dBFS).
- **Inter-device gain reference** — same operating point, two devices, Δ in dB.

*Large-signal — requires multi-level excitation (the L3 "Phase 1"):*
- **Gain-vs-input-level** — the compression knee, where nonlinear self-limiting engages.
- **THD-vs-level-and-drive** — harmonics measured where the device is actually driven.
- **Headroom-to-clip** — input margin before the output pins/limits.

*Hardware hook:* the dBFS convention plus a documented **calibration tone** (nominal −18 dBFS) so in-box dBFS and captured dBFS/dBu align on one ruler. Present in the schema from day one.

### 4.2 Perceptual lenses (physics-first + published only)

- **Audibility-weighted aliasing** — alongside raw `alias_dB`, split aliased energy **below vs above the fundamental** (below = exposed/dissonant, weighted higher), optionally bark/A-weighted.
- **A-weighted noise floor** — idle noise weighted by the standard A-curve, reported beside the flat dBFS figure.

**Rule that protects trust:** every perceptual number is an *additional, labeled* lens printed **next to** its raw physical number, never replacing it. The weightings are fixed, published curves — musical relevance without opinion.

**Boundary:** the Core delivers these as *capabilities* (multi-level excitation driver, the extractors as L0 helpers, the schema columns) and proves them against the **synthetic references** (known peak gain, THD, noise). Applying the full set to the real Huggett/Moog is SP-B.

---

## 5. The trust layer

Three concentric rings, cheapest/fastest first.

### 5.1 Synthetic known-answer references (CI, seconds)

The Core ships reference devices whose answers are computable exactly; the ruler must recover them within tolerance:

| Reference | Validates | Known answer |
|---|---|---|
| Analytic biquad (2-pole) | magnitude, corner, Q, **peak gain**, phase | closed-form transfer function |
| Known static nonlinearity (waveshaper) | THD, harmonic levels | analytic harmonic amplitudes |
| Analytic oscillator (bandlimited waveform) | spectral purity, harmonic series | defined harmonic set |
| Engineered aliasing case | `alias_dB` | predicted foldback frequency/level |
| Calibrated tone / known noise | absolute dBFS level, noise floor, A-weighting | set amplitude / spectrum |

These do triple duty: ground-truth trust anchor, the Core's end-to-end self-proof, and the **extensibility proof** (the analytic biquad is the first non-Huggett/Moog device through the `DeviceUnderTest` contract).

### 5.2 Physical reference standard (capability built now)

Built as machinery, validated on synthetic stand-ins now; exercised on real hardware in SP-D:
- **Loopback calibration** — interface out→in with no device, to characterize the capture chain's coloration, latency, and level (the ESS reference-calibration principle already in the code).
- **Physical-reference math + schema** — so a passive RC/RLC filter whose response is computable from its component values can later be measured *through that chain* and checked against its known answer.
- Proven now by feeding the calibration path synthetic "captured" signals through known chains and asserting recovery.

This is the "calibration weight": real hardware with a known answer. It breaks the chicken-and-egg (you cannot calibrate a ruler against an unknown) and makes any later Summit disagreement attributable to the **model**, not the ruler.

### 5.3 Hardware-correlation acceptance gate (defined now, satisfied in SP-D)

Written, wired acceptance criterion: the framework is trusted when, through the calibrated chain, (a) the physical reference is recovered within tolerance, **and** (b) in-box devices correlate with their hardware counterparts (Summit↔Huggett, Arturia↔Moog) within a stated bound — or a disagreement is *attributable to the model* because the physical reference already vouches for the ruler. **Prerequisites (explicit):** the §4 level axis + a solved Summit excitation method (the SP-D spike).

### 5.4 Tolerances (evidence-based defaults; become gate thresholds)

| Check | Tolerance | Basis |
|---|---|---|
| Dual-method agreement (in-band) | ≤ 0.6 dB | already achieved on real models |
| Synthetic corner recovery | ≤ 2 % | resolution of the log probe grid |
| Synthetic peak-gain / level recovery | ≤ 0.5 dB / ≤ 0.1 dB | lock-in accuracy |
| Synthetic THD recovery | ≤ 1–2 dB | FFT/window bound |
| Self-osc pitch | ± 3 % ≤ 4 kHz, report above | existing v5 standard |
| Physical-reference recovery | ≤ ~1 dB (finalize on first capture) | real-world chain |

---

## 6. Recording — schema / summary / gate / golden

### 6.1 Schema (device-agnostic, level-aware, hardware-ready)

Today's CSVs are filter-shaped (`cutoffHz,resonance,drive`). Generalize to:
- A stable **core provenance** set: `device, deviceKind, mode, os, hostSR`.
- A **device-typed parameter block** (key=value), so filters (cutoff/res/drive), oscillators (pitch/wave/pwm/sync), and hardware (MIDI note/vel/CC/capture-level) record provenance with **zero schema churn**.
- **Level columns**: absolute dBFS, passband & peak gain, self-osc amplitude+crest, gain-vs-level, THD-vs-level, headroom, noise floor (flat + A-weighted).
- **Perceptual columns**: audibility-weighted aliasing (below/above split) — beside the raw number.
- **Hardware-reference columns**: capture-level dBFS/dBu, calibration-tone anchor, excitation provenance — present (possibly empty) from day one.
- Fix the `magDb` "relative to passband" → **"absolute gain (dBFS ref)"** mislabel.

### 6.2 Summary / gate / golden

- **Summary** headline keys *added* for the level + perceptual metrics at their *relevant* operating point (e.g. peak gain at high resonance — closing the `baseRes=0` blind spot). Existing shape keys (corner/slope/method_delta) keep their current operating point, so the port is **non-regressing**; the level keys are purely additive.
- **Always-on gate** grows past shape: **level-regression gates** (peak gain, inter-device Δ, noise floor vs golden) so a hotter/quieter change is *caught*; plus the §5.1 synthetic-recovery gates.
- **Golden self-baselines** extended to the new metrics; `BERNIE_UPDATE_GOLDEN` regeneration workflow unchanged.

---

## 7. Test-driven build & CI

Trust is enforced by the build, not asserted in a doc: **the synthetic references *are* the tests.** Every metric lands test-first — write "analytic biquad → recovered peak gain within 0.5 dB," watch it fail, implement the extractor, watch it pass. The Core cannot land unless the ruler provably reads known answers.

- Fast synthetic net + a tiny device gate stay in `k2000_tests` (always-on CI).
- The heavy opt-in runner — `k2000_device_characterization` (generalized from `k2000_filter_characterization`) — stays out of CI.
- Bounded parallelism (`-j4`) per project convention.
- Windows CI remains the trusted smoke target for the plugin itself; the Core is numbers-only (no audio rendering).

---

## 8. Deliverable boundary — what SP-A ships

**Ships in SP-A:**
- L0 extensions (level, perceptual, calibration math).
- The `DeviceUnderTest` contract + InputSweep and Trigger excitation drivers (MidiCapture stubbed for SP-D).
- Device-agnostic runner + generalized schema/summary/gate/golden.
- The synthetic reference devices + their always-on trust gates.
- Physical-reference calibration capability (loopback + math + schema), synthetically validated.
- The written hardware-correlation acceptance criterion (§5.3).
- The existing Huggett/Moog filter batteries **ported onto the Core** — same numbers, nothing regresses.

**Explicitly deferred:** new large-signal filter batteries + reading Huggett's +72 dB (SP-B) · oscillator batteries (SP-C) · the hardware rig, Summit excitation-method spike, and satisfying the acceptance gate's hardware half (SP-D) · any DSP-voicing change (held for hardware) · B4 time-align (pre-SP-D).

---

## 9. Trust-model principle

**No trust claim outruns its evidence.** The instrument reports its own validation state: "proven on known math" the day the Core lands (synthetic net green), "capture-chain validated" when the physical reference is run, and "hardware-vouched" only once the real Summit/Arturia have actually vouched for it. The ruler being trustworthy is a *prerequisite* for the authenticity question, not an answer to it — accuracy (does it measure right?) and authenticity (is the filter true to hardware?) are kept distinct throughout.

---

## 10. Open questions / deferred decisions

- **Exact audibility-weighting formula** for aliasing (simple below/above-fundamental split vs. a fuller masking/loudness model) — start simple/defensible; refine if it proves too coarse.
- **Physical-reference component values** (RC vs RLC, corner placement) — chosen when the reference is built for SP-D; the Core only needs the math + schema.
- **Generalized `OperatingPoint` encoding** — fixed superset struct vs. tagged property bag vs. key=value; decided in the plan, biased toward the simplest thing that records osc + hardware provenance without churn.
- **Namespace/target naming** — whether `chz` broadens or a new device-char namespace is introduced during the port; decided in the plan following existing patterns.
- **Summit excitation methodology** — the top SP-D risk (no public schematic; how to drive a known signal through *only* the Summit's filter). Not an SP-A defect; flagged so it is on the table before SP-D.

---

## Appendix — key existing references

- L0 ruler: `tests/testdsp/{SteppedSine,EssResponse,MethodAgreement,Harmonics,Metrics,Spectrum,SignalGen,Response,Reference,Sweep,TransferFunction}.h`; self-tests `tests/{TestDspSelfTests,SteppedSineTests,SweepTests}.cpp`.
- L1/L2: `tests/characterization/{FilterUnderTest,CharacterizationRunner}.{h,cpp}`, `characterize_main.cpp`; gate `tests/CharacterizationGateTests.cpp`; goldens `tests/golden/{moog,huggett}/baseline.csv`.
- Devices: `src/dsp/spine/{HuggettFilter,MoogLadder,NlSvfCell,AsymSaturator}.{h,cpp}`, `src/dsp/spine/cmajor/MoogLadder.cmajor`; oscillators `src/dsp/Oscillator.{h,cpp}`, `src/dsp/spine/cmajor/WtOsc.cmajor`, `src/dsp/blocks/Waveshaper.{h,cpp}`.
- Prior spec/plan/manual: `docs/superpowers/specs/2026-06-29-filter-validation-internal-design.md`; `docs/superpowers/plans/2026-06-29-filter-validation-internal.md`; `docs/filter-validation/`.
- L3 analysis: `ROADMAP-PR7-filter-validation-L3.md` (repo root).
