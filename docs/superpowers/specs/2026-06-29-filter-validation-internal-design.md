# Filter-Validation Framework — Internal SOTA Layer (Sub-Project #1) — Design

**Version:** 5.08 (artifact; distinct from plugin SemVer 5.4.0)
**Date:** 2026-06-29
**Status:** Approved (brainstorm) — pending spec review
**Supersedes / refreshes:** `docs/superpowers/specs/2026-06-23-filter-characterization-harness-design.md` (shelved, artifact v5.07), updated for two facts that changed since: **oversampling shipped** (`Halfband2x`, `VoiceOversampler` on `main`, v5.4) and the effort was **decomposed into two sequential sub-projects** — this spec is **#1 (internal only)**.
**Applies to:** Moog (primary), Huggett (back-test), and **all future filter models** (model-agnostic).
**Roadmap item:** extends/supersedes `v5-filter-sweep-standard` (the 7000-point LOG dual-sweep standard).

---

## 0. Where this sits

The broader goal is to gain confidence the filters are **correct**, defined in two layers:

1. **Internal** — prove each filter matches the textbook ideal for its type (analytic, no gear). **← THIS SPEC (sub-project #1).**
2. **External** — compare each filter to the real device it emulates for authenticity (Huggett → the user's **Summit**; Moog → **Arturia Mini V** / a real Moog; future filters → their analogs). **← sub-project #2, depends on #1.**

This spec covers **#1 only**. Everything about capturing or comparing real-device data is explicitly deferred to #2. The #1 data model is built so #2 slots in without rework (shared operating-point column schema).

There is also a separate listening stage — **UAT**: the user plays the real VST3 in Ableton on a Windows CI build ([[feedback-windows-ci-smoke]]). No tooling for UAT lives in this harness; the harness is **numbers-only** (no audio/WAV rendering).

---

## 1. Purpose & premise

Build a **reusable, model-agnostic filter-characterization framework** reflecting state-of-the-art audio-industry measurement practice. It validates every filter model — Moog now, all future models, plus a **Huggett back-test** — across four measurement batteries, using **two independent measurement methods that cross-validate each other**.

The central refresh over the shelved design: the measurement core is now **dual-method**. A trivially-correct **stepped-sine** engine and a state-of-the-art **Farina exponential-sine-sweep (ESS)** engine both derive each filter's transfer function, and the ESS is **gated against** the stepped-sine on the real models. Runtime is explicitly a non-constraint (the user does not care how long the heavy run takes), which is what makes running both affordable.

This supersedes the prior `v5-filter-sweep-standard` (7000-point LOG dual-sweep): point-count becomes a free resolution knob, and scope grows from "response + self-osc pitch" to all four batteries plus cross-method agreement.

## 2. Goals / non-goals

**Goals**
- One trusted, **self-corroborating** measurement library reused by every model (kills the duplicated `mag()`/`magR()` now spread across **3** test files: `MoogLadderTests`, `Halfband2xTests`, `SpineNlSvfHarnessTests`).
- **Dual-method methodology** — stepped-sine (reference) + Farina ESS (SOTA), with an automated **method-agreement gate**.
- Full characterization: magnitude, phase/group delay, resonance/self-oscillation, distortion/aliasing.
- **Live oversampling validation** — the shipped OS tiers (`osFactor ∈ {1,2,4,8}` × `osMode ∈ {live,render}`) are real measurement targets; B3's aliasing metric produces an aliasing-vs-osFactor verdict per model.
- Two internal "golden" tiers (spec gates, self-golden baseline) plus the new method-agreement gate, kept distinct.
- A fast always-on CI subset + an opt-in heavy run, the latter fronted by a `/characterize-filter` skill.
- Back-tested on Huggett to prove model-agnosticism and re-validate the shipping filter.
- A multi-file **operator's manual** under `docs/filter-validation/` (§9).

**Non-goals (this effort)**
- The entire **external real-device comparison** (Arturia/Summit capture, ingestion, CALIB tolerance bands) — that is sub-project #2.
- Oversampling **implementation** — already shipped (v5.4); the harness *measures* it, does not implement it.
- Any WAV-rendering / listening (UAT = play the VST on Windows CI).
- Any new filter-model wiring (registration / params / UI).

## 3. Architecture — five layers, dual-method L0

```
L0  testdsp measurement library  (pure DSP; knows nothing about our models)
      SteppedSine.h       N log tones -> steady-state |H(f)|, phase   [REFERENCE — trivially correct]
      Sweep.h             Farina ESS gen + inverse + deconvolution -> impulse response
      TransferFunction.h  IR -> complex H(f): magnitude / phase / group-delay
      Harmonics.h         Farina harmonic separation -> THD(f); stepped THD + aliasing energy
      MethodAgreement.h   assert |H_ess - H_step| < TOL  per operating point   [THE GATE]
      Spectrum.h / Response.h (exist)  FFT, self-osc peakFreqHz, RMS
        \__ BOTH engines validated against synthetic closed-form filters (L0 self-tests, §8)

L1  FilterUnderTest  (one uniform socket to excite ANY filter)
      wraps a FilterModel (stereo) or a mono adapter
      setOperatingPoint(mode, cutoff, res, drive, slope, osFactor, osMode, hostSR)
      reset(); process(float* mono, int n)
        \__ REPLACES the duplicated mag()/magR() now in 3 test files

L2  Characterization runner  (per model)
      sweeps the operating-point grid, runs BOTH engines + four batteries via L0
      -> structured results + CSV/report artifacts (the model "fingerprint")

L3  Two consumers of L2:
      (a) Fast regression gates    -> stay in default k2000_tests (small grid, base-rate)
      (b) Opt-in heavy target      -> k2000_filter_characterization (full grid, both methods, live OS)
```

Each layer has one responsibility and a well-defined interface; each is testable in isolation. L0 is reused unchanged by both L3 consumers.

**Why the dual-method L0 is stronger than the shelved single-engine version.** Trust flows in two steps:
1. **Analytic self-tests** — both engines recover the closed-form answers of synthetic filters (RC, RBJ biquad, pure delay, tanh shaper) within tolerance (§8). Same discipline as the shelved spec.
2. **Cross-method agreement on real models** — the subtle ESS deconvolution must match the dumb-simple stepped-sine at every B1/B4 operating point (`MethodAgreement.h`). Only then are ESS's unique outputs (group delay, single-pass THD-vs-f) trusted. A deconvolution bug fails the agreement gate before it can masquerade as a model verdict.

Stepped-sine also pays for itself beyond cross-validation: it is the simplest possible thing that retires the `mag()`/`magR()` duplication.

## 4. The model-agnostic contract (L1)

`FilterUnderTest` decouples measurement from each model's specific API so the framework works for current and future models without per-model measurement code:

- Construction binds a concrete model (e.g. `MoogLadder`, `HuggettFilter`) behind the socket.
- `setOperatingPoint(mode, cutoffHz, resonance, drive, slope, osFactor, osMode, hostSampleRate)` — the full operating point. Models ignore axes they don't support (a model with no Notch mode rejects/clamps it).
- `reset()`, `process(float* mono, int n)` — mono excitation/capture (characterization is mono; our stereo models run two identical lanes, so the left lane is measured).
- The socket generalizes the existing `testdsp::Response`/`ProcessAdapter` contract (`prepare`/`reset`/`process`) to also accept a stereo `FilterModel`.

**Huggett back-test (early — Phase 3):** running the identical grid + batteries + both methods through Huggett proves the socket is genuinely model-agnostic and re-validates the shipping filter against the new standard. It runs as soon as the runner can drive one model — *before* the spec-gate / self-golden / skill layers — so a Moog-shaped abstraction is caught early.

## 5. The operating-point model

A single struct defines one measurement point, shared by L1, L2, and the artifact column schema. **OS axes are now live** (the headline refresh delta):

| Axis | Values | Notes |
|------|--------|-------|
| `mode` | LP12, LP24, BP, HP, Notch* | *where the model supports it |
| `cutoffHz` | log-spaced grid | per battery |
| `resonance` | 0 → just-below-osc, and max | B2 sweeps it |
| `drive` | 0 → 1 | B3 sweeps it |
| `slope` | db12 / db24 | LP tap (model-specific) |
| `osFactor` | **1, 2, 4, 8** | ← now live; matches the shipped `VoiceOversampler` cascade |
| `osMode` | **live, render** | ← now live |
| `hostSampleRate` | 44.1 / 48 / 88.2 / 96 / 192 k | render commonly runs higher host rates |
| `method` | stepped / ess | which engine produced the row (B1/B4); single-method batteries omit/flag it |

The same struct is the column header of every artifact row (§7), so a model fingerprint, a self-golden baseline, and (later, in #2) an external capture are all column-aligned.

**OS now live (was base-only in the shelved spec):**
- The heavy run sweeps **all real tiers** — `osFactor ∈ {1,2,4,8}` × `osMode ∈ {live,render}`. Runtime is a non-constraint, so the full grid runs.
- **All four batteries run per `(osFactor, osMode, hostSR)`**, because the OS up/down-sampling filters color magnitude near Nyquist (passband droop), phase, and group delay — not just aliasing.
- **B3's aliasing metric becomes a real deliverable**: the aliasing-vs-osFactor curve per model (and per osMode) — the empirical evidence for whether each shipped tier earns its CPU.
- **The fast CI suite runs at 96 k across all four OS factors** (`osFactor ∈ {1,2,4,8}`, `osMode=live`) — 96 k is the user's production rate, and exercising every shipped OS tier on every commit is the deliberate regression net for the OS work that just landed. The **full** grid (all five host sample rates × both `osMode`s) lives only in the opt-in heavy target.
- Validating `osMode=render` means the harness drives the same Offline→Realtime prepare path the plugin uses (a known one-shot re-prepare on the first realtime block). The harness measures each mode in its own settled state, so this does not distort results.

## 6. The four measurement batteries

All driven through L1, measured by L0. Each battery emits its metrics into the artifacts (§7), carrying the full operating point per row.

**B1 — Linear frequency response** *(per mode: LP12 / LP24 / BP / HP / Notch-where-applicable)* — **both engines**
- Excitation: low-amplitude stepped-sine (N log tones) AND ESS, both at `res=0, drive=0` → small-signal transfer function.
- **Method-agreement gate:** assert `|H_ess(f) − H_step(f)| < TOL` across the band.
- Metrics: `|H(f)|` dB curve at N log points (N = resolution knob); **−3 dB corner vs set cutoff** = cutoff accuracy (Hz / cents); **slope dB/oct** one octave above corner (continuous 12 vs 24 dB verification); passband ripple; stopband floor; mode-shape sanity (BP centered, HP rejects lows, Notch depth at center).

**B2 — Resonance + self-oscillation** *(stepped / impulse — single method)*
- Resonance **peak gain & Q** vs the resonance knob (0 → just-below-osc), read from H(f).
- Self-oscillation **onset threshold** (resonance value where the IR stops decaying).
- Self-oscillation **pitch tracking** vs cutoff: `res=max`, impulse kick → FFT-peak pitch over ~50–200 log-spaced cutoffs → error in **cents** (and %). Standard: **±3 % (≈ ±50 cents) ≤ 4 kHz, report-only above** ([[feedback-filter-sweep-standard]]). Provisional (proven Moog default); finalized against the first dense measured pitch curve (Phase 2), locked when spec-gates are set (Phase 5); tightened only on evidence.
- Limit-cycle amplitude + crest factor (near-sine check).

**B3 — Nonlinear / distortion** *(stepped tones + ESS harmonic separation)*
- **THD(f)** & per-harmonic (2nd/3rd/…) via Farina harmonic separation from a driven ESS.
- **THD vs input level & drive**: stepped tone, sweep amplitude + drive knob → dirt-growth curve.
- **Aliasing / spectral purity**: tone at `f₀` whose harmonics fold near/above Nyquist → energy at **inharmonic** bins = aliased fold-back metric vs `f₀`, level, **and osFactor**. The OS-tier decision instrument.
- Idle noise floor.

**B4 — Phase / group delay** *(ESS primary, stepped cross-check)*
- From the small-signal ESS: unwrapped `phase(f)` and **group delay = −dφ/dω** per mode. Stepped-sine per-tone phase is the agreement reference (covered by B1's comparison).

**Cross-cutting:** the method-agreement gate is not a 5th battery — it is a check layered over B1/B4 (the linear batteries where both engines run). B2/B3 are single-method by nature; their trust comes from the L0 analytic self-tests.

**Small-signal caveat:** B1/B4 are linear-system concepts, measured in the near-linear regime (low level, res below self-osc). Farina pushes residual nonlinearity into separable harmonic pre-echoes, keeping the fundamental transfer function clean.

## 7. Artifacts + the golden tiers

**Correctness gates (kept distinct on purpose):**
1. **Spec gates** — assert vs analytic / spec'd truth (24 dB/oct, ±3 % self-osc ≤ 4 kHz, bounded output, aliasing < threshold). Always-on, fast subset in `k2000_tests`.
2. **Method-agreement gate** *(new)* — assert ESS matches stepped-sine within `TOL` at every B1/B4 operating point. Runs in L0 self-tests (synthetic filters) for CI, and across the full grid on real models in the heavy target.
3. **Self-golden baseline** — assert the committed fingerprint hasn't drifted (catches unintended DSP / codegen / refactor regressions). Needs no external data — works today. Coarse subset in `k2000_tests`; full diff in the heavy target.

**Deferred to sub-project #2:** the external (Arturia/Summit) ingestion, feature-based comparison, and CALIB tolerance bands. The operating-point column schema is shared so #2 slots captured rows in without reworking #1.

**Artifacts (the fingerprint).** Per model, L2 writes to gitignored `build/characterization/<model>/`:
- `response.csv` (`|H(f)|` dB, phase, group delay — **both methods, with the agreement delta column**), `resonance.csv` (Q/peak vs res, self-osc onset, pitch-vs-cutoff), `distortion.csv` (THD-vs-f, THD-vs-level, **aliasing-vs-f₀/level/osFactor**), `summary.json` (headline numbers + every gate's pass/fail).
- **Every row carries the operating point** as columns: `model, mode, osFactor, osMode, hostSR, cutoffHz, resonance, drive, probeHz, method, …metric…`.
- Full dense CSVs → gitignored `build/characterization/<model>/` (ephemeral, regenerated). A **compact `baseline.json`** (headline metrics + coarsely-sampled curves) **is committed** per model under `tests/golden/<model>/baseline.json` (the empty scaffold already exists).

**Harness self-robustness** (a prior code review flagged silent skips): malformed CSV rows → **logged warning** (not silent); missing files → clear skip; NaN/inf and degenerate-measurement guards (e.g. "self-osc never started") flagged, never silently passed.

## 8. Validating the ruler (L0 self-tests)

Trust the ruler before the readings. **Both** `testdsp` engines are proven against synthetic filters with known analytic answers (extending `tests/TestDspSelfTests.cpp`), in the **fast default suite**:

- **1-pole RC** → known −6 dB/oct, corner, phase.
- **RBJ biquad** (LP/BP/HP/notch) → analytic `|H(f)|`, slope, corner, phase, group delay within tight tolerance.
- **Pure delay line** → measured group delay equals the delay exactly (validates −dφ/dω).
- **Memoryless tanh/polynomial shaper at known level** → analytically-predictable harmonics → Farina THD extraction recovers them.
- **Intentional aliaser** (naive base-rate op) → aliasing metric fires; **clean signal** → reports ≈ 0.
- **Pure sine at known f** → `peakFreqHz` recovers f.
- **Method agreement on synthetics** → stepped-sine and ESS agree within `TOL` on the RC and RBJ references (proves the two engines agree before they are trusted on real models).

A harness bug therefore cannot masquerade as a model verdict.

## 9. The operator's manual (documentation deliverable)

A dedicated, multi-file manual under `docs/filter-validation/` ([[feedback-markdown-docs]]), separate from this spec and the implementation plan (those are *why/how-built*; the manual is *how-to-use*).

| File | Covers |
|------|--------|
| `README.md` | What the harness is, the layered definition of "correct," and a 60-second quickstart (build, run, read the summary). The front door. |
| `concepts.md` | The dual-method ruler (stepped-sine + Farina ESS) and *why two*; the three gates (spec / method-agreement / self-golden); the four batteries in plain terms. |
| `running.md` | CLI reference — flags, `--model`, exit codes; always-on CI subset vs opt-in heavy target; the manual `workflow_dispatch` path; the `/characterize-filter` skill and where it adds value over the bare binary. |
| `interpreting-results.md` | How to read `summary.json` and every CSV **column-by-column**; the method-agreement verdict; the aliasing-vs-osFactor curve and what a "good" tier looks like; self-osc tracking in cents; self-golden drift. |
| `operating-points.md` | The axis model, the grid, and the live OS tiers / host sample rates. |
| `extending.md` | **How to add a new filter model** — the L1 `FilterUnderTest` contract, what each axis means for your model, generating and committing its `baseline.json`. The model-agnostic promise, made operational. |
| `troubleshooting.md` | Malformed-CSV warnings, NaN/degenerate guards, "self-osc never started," and the baseline-update workflow (drift expected vs regression). |

**Timing:** the manual is **authored incrementally, one page per implementation phase**, so it documents what actually exists. This spec defines the table of contents now; the implementation plan attaches the matching doc task to each phase, plus a final consolidation pass.

## 10. Build targets + CI policy

- **`k2000_tests`** (existing, **always-on in CI**, `-j4` per [[build-bounded-parallelism]]): L0 self-tests for **both** engines + the **method-agreement gate on synthetics** + a **spec-gate regression subset** (small cutoff/res grid, pinned to **96 k** across **all four OS factors** `{1,2,4,8}`, `osMode=live`) + a **coarse self-golden** check. Catches drift on every build; the grid is deliberately small (one sample rate, live mode, coarse cutoff/res) so it stays green and bounded even while covering every OS tier.
- **`k2000_filter_characterization`** (NEW, **opt-in, separate executable, not in default `add_test`/CI**): the heavy runner — full grid × four batteries × **both methods** × **live OS tiers** (`{1,2,4,8}` × `{live,render}`) × all models → dense artifacts, full method-agreement gate on real models, full self-golden diff. **Self-sufficient:** emits machine artifacts *and* prints a human-readable summary *and* returns a meaningful exit code (so CI and a plain developer never need the skill). Invoked `./build/tests/k2000_filter_characterization [--model moog|huggett|all]`.
- Both share `tests/testdsp/` + the L1 driver + L2 runner. CI gates run a tiny grid; the heavy target runs the full grid.

**CI policy:** the dense characterization is **deliberate, not automatic** — run locally or via a manual `workflow_dispatch` job, matching the project's "smoke is a manual Windows-CI act" stance ([[feedback-windows-ci-smoke]]). Every-push CI stays fast.

## 11. The `/characterize-filter` skill

A thin **orchestration + interpretation** layer over the self-sufficient executable — **never reimplements DSP**:
- Invocation: `/characterize-filter <model|all>`.
- Steps: build `k2000_filter_characterization` (`-j4`) → run for the requested model(s) → read `summary.json`/CSVs → present a **concise readable report** (per-battery headline metrics, **method-agreement verdict**, spec-gate pass/fail, self-golden drift vs baseline, **aliasing-vs-osFactor curve summary**).
- On detected self-golden drift it **surfaces what changed and offers to update `baseline.json`** — never auto-updates (drift may be a regression).
- **Boundary:** the executable stands alone (CI + bare-CLI use it directly); the skill adds only the interactive layer. If the skill vanished, nothing would break.
- Lives at `.claude/skills/characterize-filter/SKILL.md`, committed with the harness.

## 12. Scope boundaries

**IN (sub-project #1, internal only):** `testdsp` L0 dual-method engines (`SteppedSine`, `Sweep`, `TransferFunction`, `Harmonics`, `MethodAgreement`) + self-tests; L1 model-agnostic driver (retires the `mag()`/`magR()` duplication now in 3 files); L2 runner + dense artifacts + committed `baseline.json`; spec-gate subset + method-agreement-on-synthetics + coarse self-golden in `k2000_tests`; opt-in `k2000_filter_characterization` target (self-sufficient; full grid, both methods, **live OS tiers**); **all four batteries** incl. phase/group-delay; the aliasing-vs-osFactor deliverable; applied to **Moog + Huggett back-test**; the `/characterize-filter` skill; the multi-file operator's manual.

**OUT (deferred / separate efforts):** the entire **external Arturia/Summit comparison** (= sub-project #2 — capture, ingestion, CALIB tolerance bands); oversampling *implementation* (already shipped); WAV-rendering / UAT tooling; any new filter-model wiring.

## 13. Phasing (the plan will split into these; each independently shippable/pausable)

1. **L0 dual-method ruler + self-tests** — `SteppedSine`, `Sweep`, `TransferFunction`, `Harmonics`, `MethodAgreement`; both engines validated against synthetic filters + agreement on synthetics. The OS axes exist in the operating-point struct from here. (manual: `concepts.md`)
2. **L1 socket + L2 runner + artifacts + opt-in target** — applied to Moog at **base rate first**; dense fingerprint (both methods) + agreement gate on the real model; self-sufficient summary + exit code. (manual: `interpreting-results.md`, CSV schemas; `operating-points.md`)
3. **Huggett back-test (model-agnosticism gate)** — point the runner at the existing `HuggettFilter` as soon as it drives one model, *before* more layers. Emits Huggett's fingerprint. (manual: `extending.md`)
4. **OS tiers go live** — widen the heavy grid to `{1,2,4,8} × {live,render}`; produce the aliasing-vs-osFactor curve per model. *(New phase — the payoff of OS having shipped.)* (manual: `operating-points.md` OS section)
5. **Spec-gate subset + self-golden baselines into `k2000_tests`** — commit **both** `baseline.json` (Moog + Huggett). (manual: `running.md` gates section, `troubleshooting.md`)
6. **The `/characterize-filter` skill** — interactive front door over the self-sufficient binary. (manual: `running.md` skill section)
7. **Manual consolidation pass** — `README.md` quickstart + cross-links; verify every page matches shipped behavior.

## 14. Success criteria

- Both engines recover the known analytic answers of all synthetic reference filters within tolerance, **and** agree with each other on synthetics and on real models (the ruler is doubly trusted).
- Moog and Huggett each produce a complete four-battery fingerprint + committed `baseline.json`; the duplicated `mag()`/`magR()` helpers are gone.
- `k2000_tests` stays green and bounded with the spec-gate + method-agreement + coarse self-golden subset (96 k, all four OS factors, live, coarse grid); the dense full-sweep run is opt-in only.
- The live OS grid produces a real **aliasing-vs-osFactor verdict** per model (and per osMode).
- `/characterize-filter moog` builds, runs, and returns a readable summary; the underlying binary is fully usable without the skill.
- The operator's manual exists under `docs/filter-validation/`, one accurate page per concern.

## 15. Open questions (resolve during planning)

- Exact ESS parameters (sweep duration, fade windows, FFT size) per host SR — pin in Phase 1 against the L0 self-tests.
- The method-agreement tolerance `TOL` (and whether it varies by frequency region / near self-osc) — pin in Phase 1 on synthetics, confirm on Moog in Phase 2.
- Default report resolution (the N in "N log points") — a config constant.
- Self-oscillation pitch tolerance — start ±3 % (≈ ±50 cents) ≤ 4 kHz, report-only above (provisional); finalized against the first dense pitch curve (Phase 2).
- Self-golden tolerance bands (how much fingerprint drift is "drift") — set in Phase 5; must allow small floating-point variance.
- The precise operating-point grid (which cutoffs/resonances/drives/levels) per battery — pin in Phase 2 to balance coverage vs runtime (runtime is not a hard constraint, but the grid must stay legible).
