# SOTA Filter-Characterization Harness — Design

**Version:** 5.07 (artifact; distinct from plugin SemVer)
**Date:** 2026-06-23
**Status:** Approved (brainstorm) — pending spec review
**Roadmap item:** extends/supersedes `v5-filter-sweep-standard` (the 7000-point LOG dual-sweep standard) with a full SOTA, model-agnostic characterization framework.
**Applies to:** Moog (primary, `[[followup-moog-filter-consolidation]]`), Huggett (back-test), and **all future filter models**.
**Sequencing:** spec + plan are authored now, then **shelved** — the immediate next implementation effort is **Moog Spec 2**. This harness is built afterward (or whenever chosen). See §15.

---

## 1. Purpose & premise

Build a **reusable, model-agnostic filter-characterization framework** that reflects state-of-the-art audio-industry measurement practice. It validates every filter model — Moog now, all future models, plus a **Huggett back-test** — across four measurement batteries, and forms the automated leg of a **three-stage validation gate**:

1. **Internal** — automated SOTA measurement (this harness).
2. **UAT** — the user's ear, by playing the real VST3 in Ableton (Windows CI build). *No tooling in this harness* — UAT is out of scope here.
3. **Golden** — comparison against data captured from an **Arturia Mini V** hardware/software reference.

The harness is **numbers-only** (no audio/WAV rendering — UAT is the listening stage and uses the real plugin). It is the **instrument** that, when oversampling later lands (§8), will validate and tune the OS tiers — it measures OS, it does not implement it.

This supersedes the prior `v5-filter-sweep-standard` (7000-point LOG dual-sweep): the swept-sine method (§7) makes point-count a free resolution knob rather than a measurement count, and the scope grows from "response + self-osc pitch" to all four batteries (§6).

## 2. Goals / non-goals

**Goals**
- One trusted, self-validated measurement library reused by every model (kills the duplicated `mag()`/`magR()` in `MoogLadderTests`).
- SOTA methodology (exponential-sine-sweep / Farina core, §7).
- Full characterization: magnitude, phase/group delay, resonance/self-oscillation, distortion/aliasing (§6).
- Three distinct "golden" tiers — spec gates, self-golden baseline, Arturia golden — kept separate (§9).
- OS-ready data model from day one, base-rate only exercised until OS ships (§8).
- A fast always-on CI subset + an opt-in heavy run, the latter fronted by a `/characterize-filter` skill (§11–12).
- Back-tested on Huggett to prove model-agnosticism and re-validate the shipping filter.

**Non-goals (this effort)**
- Oversampling **implementation** (separate roadmap item; harness is merely OS-ready).
- Actual Arturia data **capture** and tolerance **calibration** (deferred; the comparison layer is built but dormant until data exists).
- Any WAV-rendering / listening tooling (UAT = play the VST).
- Any Moog Spec 2 wiring (registration / params / UI).

## 3. Architecture — five independently-testable layers

```
L0  testdsp measurement library  (pure DSP; knows nothing about our models)
      Sweep.h            ESS gen, inverse filter, deconvolution -> impulse response
      TransferFunction.h IR -> complex H(f): magnitude/phase/group-delay + derived metrics
      Harmonics.h        Farina harmonic separation -> THD(f); stepped THD + aliasing-energy
      Response.h/Spectrum.h (exist)  self-osc pitch (peakFreqHz), RMS magnitude, FFT
        \__ validated against synthetic closed-form filters (L0 self-tests, §10)

L1  FilterUnderTest  (one uniform socket to excite ANY filter)
      wraps a FilterModel (stereo) or a mono adapter
      setOperatingPoint(mode, cutoff, resonance, drive, slope, osFactor, osMode, hostSR)
      reset(); process(float* mono, int n)
        \__ REPLACES the duplicated mag()/magR() in MoogLadderTests

L2  Characterization runner  (per model)
      sweeps the operating-point grid, runs the four batteries via L0
      -> structured results + CSV/report artifacts (the model "fingerprint")

L3  Two consumers of L2:
      (a) Fast regression gates    -> stay in default k2000_tests (small grid)
      (b) Opt-in heavy target      -> k2000_filter_characterization (full grid + golden)
```

Each layer has one responsibility and a well-defined interface; each is testable in isolation. L0 is reused unchanged by both L3 consumers.

## 4. The model-agnostic contract (L1)

`FilterUnderTest` decouples measurement from each model's specific API so the framework works for current and future models without per-model measurement code:

- Construction binds a concrete model (e.g. `MoogLadder`, `HuggettFilter`) behind the socket.
- `setOperatingPoint(mode, cutoffHz, resonance, drive, slope, osFactor, osMode, hostSampleRate)` — the full operating point. Models ignore axes they don't support (e.g. a model with no Notch mode rejects/clamps it; today every model takes `osFactor=1, osMode=live`).
- `reset()`, `process(float* mono, int n)` — mono excitation/capture (characterization is mono; our stereo models run two identical lanes, so the left lane is measured).
- The socket generalizes the existing `testdsp::Response` adapter contract (`prepare`/`reset`/`process`) to also accept a stereo `FilterModel`.

**Huggett back-test (early — Phase ③):** running the identical grid + batteries through Huggett proves the socket is genuinely model-agnostic and re-validates the shipping filter against the new standard. It runs as soon as the runner can drive one model — *before* the guardrail / golden / skill layers — so a Moog-shaped abstraction is caught early, not after four layers are built on it.

## 5. The operating-point model

A single struct defines one measurement point, shared by L1, L2, and the artifact column schema. Every axis is present from day one; models ignore unsupported axes, and today's grid pins the OS axes to base:

| Axis | Values (today → future) | Notes |
|------|--------------------------|-------|
| `mode` | LP12, LP24, BP, HP, Notch* | *where the model supports it |
| `cutoffHz` | log-spaced grid | per battery |
| `resonance` | 0 → just-below-osc, and max | B2 sweeps it |
| `drive` | 0 → 1 | B3 sweeps it |
| `slope` | db12 / db24 | LP tap (model-specific) |
| `osFactor` | **1** → {2, 4, 8, 16, 32} | §8; base only today |
| `osMode` | **live** → {live, render} | §8 |
| `hostSampleRate` | **48 k** → {44.1, 48, 88.2, 96, 192 k} | §8 |

The same struct is the column header of every artifact row (§9), so a model fingerprint, a self-golden baseline, and an Arturia capture are all column-aligned.

## 6. The four measurement batteries

All driven through L1, measured by L0. Each battery emits its metrics into the artifacts (§9), carrying the full operating point per row.

**B1 — Linear frequency response** *(per mode: LP12 / LP24 / BP / HP / Notch-where-applicable)*
- Excitation: low-amplitude ESS, `res=0, drive=0` → small-signal transfer function.
- Metrics: `|H(f)|` dB curve at N log points (N = resolution knob); **−3 dB corner vs set cutoff** = cutoff accuracy (Hz / cents); **slope dB/oct** one octave above corner (continuous 12 vs 24 dB verification); passband ripple; stopband floor; mode-shape sanity (BP centered, HP rejects lows, Notch depth at center).

**B2 — Resonance + self-oscillation**
- Resonance **peak gain & Q** vs the resonance knob (0 → just-below-osc), read from H(f) at each setting.
- Self-oscillation **onset threshold** (resonance value where the IR stops decaying).
- Self-oscillation **pitch tracking** vs cutoff: `res=max`, impulse kick → FFT-peak pitch over ~50–200 log-spaced cutoffs → error reported in **cents** (and %). Starting standard: **±3 % (≈ ±50 cents) ≤ 4 kHz**, report-only above. This tolerance is **provisional** — the proven Moog default — and is finalized against the first dense measured pitch curve (Phase ②), locked when the spec-gates are set (Phase ④); tightened only on evidence, never frozen by guesswork now.
- Limit-cycle amplitude + crest factor (near-sine check).

**B3 — Nonlinear / distortion**
- **THD(f)** & per-harmonic (2nd/3rd/…) levels via Farina harmonic separation from a driven ESS.
- **THD vs input level & drive**: stepped tone, sweep amplitude + drive knob → dirt-growth curve.
- **Aliasing / spectral purity**: tone at `f₀` whose harmonics fold near/above Nyquist → energy at **inharmonic** bins = aliased fold-back metric vs `f₀`, level, **and osFactor** (§8). The OS-tier decision instrument.
- Idle noise floor.

**B4 — Phase / group delay**
- From the same small-signal ESS: unwrapped `phase(f)` and **group delay = −dφ/dω** per mode.

**Small-signal caveat:** B1/B4 are linear-system concepts, measured in the near-linear regime (low level, res below self-osc). Farina pushes residual nonlinearity into separable harmonic pre-echoes, keeping the fundamental transfer function clean.

## 7. Measurement methodology — exponential sine sweep (Farina)

**Core engine = logarithmic exponential sine sweep (ESS).** One sweep `f0→f1` (10 Hz → 25 kHz) per operating point; deconvolve with the inverse sweep → impulse response; FFT → the **complete complex transfer function** (magnitude, phase, group delay) in a single pass. Farina's property: harmonic distortion separates into distinct pre-echoes of the same deconvolution → **THD-vs-frequency from the one sweep**.

**Stepped sub-measurements** where a continuous sweep does not apply: self-oscillation pitch vs cutoff (discrete cutoffs), aliasing inharmonic-energy vs tone/level/osFactor, resonance Q vs resonance setting (an ESS per setting).

**"70,000 points" reconciliation:** the ESS transfer function is continuous, so it can be evaluated at 7 k, 70 k, or 700 k log-spaced frequencies from one sweep — denser is a *sampling* choice, not 70 k settled measurements. This honours the resolution intent and is strictly more robust and faster than discrete stepping. The default report resolution is a config constant.

## 8. Oversampling as a designed-in axis (OS-ready, base-only)

Oversampling is a **separate future roadmap item**, not built here. The harness is the instrument that will validate/tune it, so the operating-point model is **OS-ready from day one** and widened with **zero rework** when OS ships:

- **`osFactor` ∈ {1 (base), 2, 4, 8, 16, 32}** — internal oversampling factor.
- **`osMode` ∈ {live, render}** — two OS configurations to characterize (live: real-time, lower factor / lighter filters / low latency; render: offline, higher factor / steeper / possibly linear-phase).
- **`hostSampleRate` ∈ {44.1, 48, 88.2, 96, 192 k}** — made explicit (render commonly runs higher host rates); today hardcoded 48 k.

Today the grid is pinned to `osFactor=1, osMode=live, hostSR=48k`. These are struct fields + artifact columns now; **the measurement code, L1 socket, and artifact format do not change** when OS lands.

**The aliasing metric (B3) is the OS-tier decision instrument:** it produces an **aliasing-vs-osFactor curve per model** (and per osMode), the evidence on which live and render tiers are chosen. All four batteries run per `(osFactor, osMode, hostSR)` once OS exists, because the OS up/down-sampling filters also color magnitude (passband droop near Nyquist), phase, and group delay.

## 9. Artifacts + the three golden tiers

**Artifacts (the fingerprint).** Per model, L2 writes:
- `response.csv` (`|H(f)|` dB, phase, group delay), `resonance.csv` (Q/peak vs res, self-osc onset, pitch-vs-cutoff), `distortion.csv` (THD-vs-f, THD-vs-level, aliasing-vs-f₀/level/osFactor), `summary.json` (headline numbers + spec pass/fail).
- **Every row carries the operating point** as columns: `model, mode, osFactor, osMode, hostSR, cutoffHz, resonance, drive, probeHz, …metric…`. Our fingerprint and the Arturia capture share **one column schema** (column-aligned comparison; future OS rows slot in unchanged).
- Full dense CSVs → gitignored `build/characterization/<model>/` (ephemeral, regenerated). A **compact `baseline.json`** (headline metrics + coarsely-sampled curves) **is committed** per model under `tests/golden/<model>/baseline.json`.

**Three "golden" tiers — kept distinct (the robustness backbone):**
1. **Spec gates** — assert vs analytic/spec'd truth (24 dB/oct, ±3 % self-osc, bounded output, aliasing < threshold). Always-on, fast subset in `k2000_tests`.
2. **Self-golden baseline** — assert the fingerprint hasn't drifted from the committed `baseline.json` (catches unintended DSP / codegen / refactor regressions). **Needs no Arturia data — works today.** A coarse subset runs in `k2000_tests` (CI); the full fingerprint diff runs in the heavy target.
3. **Arturia golden** — compare vs the hardware capture. Opt-in, dormant until captured.

**Arturia comparison philosophy.** The Mini V is a *different* filter — naive absolute-dB point matching is brittle and wrong. Compare on **characteristic features within calibratable tolerance bands**: corner (cents), slope (dB/oct), resonance peak/Q, self-osc tracking (%), and **gain-normalized curve shape** (align passbands, then compare `|H(f)|` inside a dB envelope). All tolerances live in **one CALIB config block per metric** — start loose, tightened in the deferred calibration pass. The golden capture format (`tests/golden/<model>/arturia/{response,selfosc,thd}.csv`) extends the existing README; the documented `selfosc.csv` finally gets a harness.

**Harness self-robustness:** malformed CSV rows → **logged warning** (not silent — the prior code review flagged silent skips); missing files → clear dormant skip; NaN/inf and degenerate-measurement guards (e.g. "self-osc never started") flagged, never silently passed.

## 10. Validating the ruler (L0 self-tests)

Trust the ruler before the readings. The `testdsp` primitives are proven against synthetic filters with known analytic answers (extending `tests/TestDspSelfTests.cpp`), in the **fast default suite**:

- **1-pole RC** → known −6 dB/oct, corner, phase.
- **RBJ biquad** (LP/BP/HP/notch) → analytic `|H(f)|`, slope, corner, phase, group delay within tight tolerance.
- **Pure delay line** → measured group delay equals the delay exactly (validates −dφ/dω).
- **Memoryless tanh/polynomial shaper at known level** → analytically-predictable harmonics → Farina THD extraction recovers them.
- **Intentional aliaser** (naive base-rate op) → aliasing metric fires; **clean signal** → reports ≈ 0.
- **Pure sine at known f** → `peakFreqHz` recovers f.

A harness bug therefore cannot masquerade as a model verdict.

## 11. Build targets + CI policy

- **`k2000_tests`** (existing, fast, **always-on in CI**, `-j4`): L0 self-tests + **spec-gate regression subset** (per-model assertions on a small grid) + a **coarse self-golden baseline** check. Catches drift cheaply on every build; stays green and fast.
- **`k2000_filter_characterization`** (NEW, **opt-in, separate executable, not in default `add_test`/CI**): the heavy runner — full grid × four batteries × all models → dense artifacts, full self-golden diff, Arturia compare. **Self-sufficient:** emits machine artifacts *and* prints a human-readable summary *and* returns a meaningful exit code (so CI and a plain developer never need the skill). Invoked `./build/tests/k2000_filter_characterization [--model moog|huggett|all]`.
- Both share `tests/testdsp/` + the L1 driver + L2 runner. CI gates run a tiny grid; the heavy target runs the full grid.

**CI policy:** the dense characterization is **deliberate, not automatic** — run locally or via a manual `workflow_dispatch` job, matching the project's "smoke is a manual Windows-CI act" stance. Every-push CI stays fast.

## 12. The `/characterize-filter` skill

A thin **orchestration + interpretation** layer over the self-sufficient executable — **never reimplements DSP**:
- Invocation: `/characterize-filter <model|all>`.
- Steps: build `k2000_filter_characterization` (`-j4`) → run for the requested model(s) → read `summary.json`/CSVs → present a **concise readable report** (per-battery headline metrics, spec-gate pass/fail, self-golden drift vs baseline, Arturia verdict or "no data — dormant").
- On detected self-golden drift it **surfaces what changed and offers to update `baseline.json`** — never auto-updates (drift may be a regression).
- **Boundary:** the executable stands alone (CI + bare-CLI use it directly); the skill adds only the interactive layer (in-context interpretation, the baseline-update offer, cross-run comparison, follow-up Q&A). If the skill vanished, nothing would break.
- Lives at `.claude/skills/characterize-filter/SKILL.md`, committed with the harness.

## 13. Scope boundaries

**IN:** `testdsp` L0 extensions (`Sweep`, `TransferFunction`, `Harmonics`) + self-tests; L1 model-agnostic driver (replaces duplicated `mag()`/`magR()`); L2 runner + dense artifacts + committed `baseline.json`; spec-gate regression subset + coarse self-golden in `k2000_tests`; opt-in `k2000_filter_characterization` target (self-sufficient); self-golden + Arturia-compare layers (Arturia dormant until data); **all four batteries** incl. phase/group-delay; **OS axes designed into the data model, base-only exercised**; applied to **Moog + Huggett back-test**; the `/characterize-filter` skill.

**OUT (deferred / separate efforts):** OS *implementation*; Arturia data *capture* + tolerance *calibration*; WAV-rendering / UAT tooling; any Moog Spec 2 wiring.

## 14. Phasing (the plan will split into these phases; each is independently shippable/pausable)

1. **L0 measurement library + self-tests** — `Sweep`, `TransferFunction`, `Harmonics`; validated against synthetic filters (the ruler). The OS axes exist in the operating-point struct from here.
2. **L1 driver + L2 runner + artifacts + the opt-in target** — applied to Moog; dense fingerprint emitted; self-sufficient summary + exit code.
3. **Huggett back-test (model-agnosticism gate)** — point the runner at the existing `HuggettFilter` as soon as it can drive one model, *before* building more layers, to surface any Moog-shaped assumptions in the L1/L2 abstraction early. Emits Huggett's fingerprint.
4. **Spec-gate regression subset + self-golden baselines** — wired into `k2000_tests` (CI); commit **both** `baseline.json` (Moog + Huggett).
5. **Arturia-compare layer (dormant) + golden-format extension** — ingestion, feature-based comparison, CALIB tolerance block, `selfosc.csv` importer; skip-when-absent with logged warnings.
6. **The `/characterize-filter` skill** — the interactive front door over the self-sufficient binary.

Because each phase is independently shippable, we can build them piecemeal around the Moog-Spec-2 work (§15).

## 15. Sequencing

The spec + plan are produced now and **shelved**. The immediate next implementation effort is **Moog Spec 2** (`[[followup-moog-filter-consolidation]]`). This harness is implemented afterward (or whenever chosen). Nothing here blocks Moog Spec 2; conversely the Huggett back-test and Moog characterization can run the moment the harness exists, independent of Spec 2.

## 16. Success criteria

- L0 self-tests recover the known analytic answers of all synthetic reference filters within tight tolerance (the ruler is trusted).
- Moog and Huggett each produce a complete four-battery fingerprint + committed `baseline.json`; the duplicated `mag()`/`magR()` helpers are gone.
- `k2000_tests` stays fast and green with the spec-gate subset + coarse self-golden; the dense run is opt-in only.
- The Arturia-compare layer ingests the documented CSV format, compares feature-wise within the CALIB band, and is cleanly dormant (with logged skips) until data exists.
- `/characterize-filter moog` builds, runs, and returns a readable summary; the underlying binary is fully usable without the skill.
- The operating-point model carries `osFactor`/`osMode`/`hostSR` and the artifact schema has the columns, exercised at base today — provably zero-rework to widen when OS lands.

## 17. Open questions (resolve during planning / calibration)

- Exact ESS parameters (sweep duration, fade windows, FFT size) per host SR — pin in Phase 1 against the L0 self-tests.
- Default report resolution (the N in "N log points") — a config constant; 70 k is cheap given §7.
- Self-oscillation pitch tolerance — start ±3 % (≈ ±50 cents) ≤ 4 kHz, report-only above (provisional, proven Moog default); reported in cents, finalized against the first dense pitch curve (Phase 2), not frozen now.
- Self-golden tolerance bands (how much fingerprint drift is "drift") — set in Phase 4, distinct from the Arturia CALIB band; must allow small floating-point variance (not an exact match).
- The precise operating-point grid (which cutoffs/resonances/drives/levels) per battery — pin in Phase 2 to balance coverage vs runtime.
