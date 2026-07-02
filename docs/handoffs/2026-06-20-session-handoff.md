# Bernie (k2000) — Session Handoff (2026-06-20)

Handoff for a fresh session with zero memory of the prior conversation. Concrete file paths and symbol names throughout. **Supersedes the previous (2026-06-19, pre-v5.0-remediation) handoff.** Everything below is on `main` at **`95f1844`** (local == `origin/main`), unless noted.

---

## 1. What we're building and why

**Bernie** is a JUCE 8.0.4 VST3 synthesizer (C++17, CMake) at `/home/increp/dev/k2000`. Repo codename stays `k2000`; the shipping product name is now **Bernie**, and its built-in effects section is **Ricky** (see roadmap). Not modeled on the Kurzweil K2000.

**North star (ADR-0010):** a **K2061/K2088-class VAST engine** (a user-wired per-voice serial/parallel DSP graph for *generation*) **bracketed by a constant Novation Summit analog voice** — the always-present *shaping* spine: a **selectable analog filter model** + drive → VCA, plus the modulation system. Anchor references are **Summit + K2061/K2088 only** (the Kurzweil K2000 is fully dropped from the reference KB).

**This session** did three things: (a) **remediated the v5.0 nonlinear spine** (it had crackles/artifacts found in UAT), (b) **built a reusable DSP test harness** that catches such defects objectively, and (c) **groomed the roadmap + wrote v5 design specs** (separation, Moog) and resolved their open questions.

---

## 2. Current state

**Git:** `main` @ `95f1844`, pushed. **`./build/tests/k2000_tests` → "Summary: 138 tests, 0 failed", build pristine (no `-Wshadow`/`-Wsign-conversion`/`-Wmisleading-indentation`/`-Wfloat-equal`).** Plugin `CMakeLists.txt` is still `VERSION 5.1.0` — **NOT bumped this session** (no plugin release; the spine remediation is unreleased on `main`). Windows CI not re-run after the early UAT artifact (the cosmetics-only run `27861164332` passed; later runs were cancelled per user).

### Working / shipped to `main` this session

**A. v5.0 nonlinear-spine remediation** (commit `0263d1c`; harness that proved it `3ec1f8a`):
- `src/dsp/spine/AsymSaturator.h` — **now a memoryless plain-tanh shaper.** Single method `float process(float x) const` returns `comp_ * std::tanh(gain_ * x + bias_)`. **ADAA removed** (no `State`, no `logcosh`, no per-channel memory). `setDrive(drive01, biasFixed, maxDriveDb)` unchanged.
- `src/dsp/spine/NlSvfCell.h` — **clean linear TPT core + resonance saturator only.** `recompute()` sets `g_ = tan(pi*cutoff/sr)`, `k_ = 1/Q` (`Q = 0.5 + res²·49.5`), `a1_/a2_/a3_` directly. **Droop removed** (no `env_`, no `droopActive_`, no `setDroopActive`, no `gmScale`). **Coefficient slewing removed** (no `updateBlock`, no target/slew fields). Kept: `satRes()` (unit-slope-normalized asymmetric Padé tanh) resonance feedback via the `fbExtra` delta in `step()`.
- `src/dsp/spine/HuggettFilter.{h,cpp}` — `VoiceState` dropped `AsymSaturator::State pre, post`. `processStereo` calls `preSat_.process(l)` / `postSat_.process(l)` (memoryless), removed all droop/updateBlock calls. `nonlinear` flag still gates the DC blocker.
- `src/dsp/spine/HuggettHpStage.{h,cpp}` — **HP drive removed entirely.** `setParams(cutoffHz, resonance, Slope)` (no `drive01`); `State` dropped `AsymSaturator::State pre`; `processStereo` is clean HP-only (DC blocker gated on `resonance_ > 0`). Constants `kHpBias`/`kHpDriveDb` deleted.
- Params: `src/params/Parameters.cpp` — master gain default **−3.5 → −9.0 dB** (`project(...)` line 185-ish); HP resonance `NormalisableRange{0,1}` → **`{0, 0.15}`**; **removed** `spineHpDrive` id/layout/snapshot. `src/params/Parameters.h` — `spineHpDrive` removed from `LayerIds`. `src/params/ParamSnapshot.h` — `hpDrive` field removed.
- `src/Layer.cpp` — `hpStage_.setParams(...)` now 3-arg.
- UI: `src/gui/SummitLookAndFeel.{h,cpp}` — overrode `drawComboBox`/`getComboBoxFont`/`positionComboBoxText` (18 px arrow zone, 13 px font) so panel combos stop truncating. `src/PluginEditor.{h,cpp}` — master gain is now a **horizontal `juce::Slider`** (`masterGain_` + `masterGainLbl_`) not a `LabeledKnob`; HP-enable toggle widened; **removed `hpDrive_` knob**; HP band is now HP Cut / HP Reso / HP Slope.
- Tests updated: `tests/HuggettNonlinearTests.cpp` (sat.process 1-arg, droop test removed), `tests/HpPreFilterTests.cpp` (setParams 3-arg), `tests/LayerTests.cpp`, `tests/MultiLayerTests.cpp`, `tests/ParamSnapshotTests.cpp` (gain −9, HP-reso cap 0.15, hpDrive removed).

**B. DSP test harness** (commits `9727e65`..`84241c7`, built subagent-driven from `docs/superpowers/plans/2026-06-20-test-harness.md`):
- `tests/testdsp/` (header-only, framework-agnostic): `SignalGen.h`, `Spectrum.h` (FFT magnitude/RMS), `Metrics.h` (M1–M3/M6/M8/M14 + `stereoCorrelation`), `Reference.h` (**`OversampledReference` NSR comparator** — Kaiser-sinc `firLowpass`+`decimate`+`noiseToSignalDb`, the SOTA bandlimited-reference aliasing method), `ProcessAdapter.h` (`ShaperAdapter`/`CellAdapter`/`ModelAdapter`), `Gate.h` (`Gate::check(ut, measured, threshold, Dir, label)`), `Response.h` (`magDb` steady-state response + zero-crossing pitch), `GoldenIO.h` (CSV load/save + `GoldenSet` update-or-assert).
- `tests/fixtures/`: `SpineShaperHarnessTests.cpp`, `SpineHuggettHarnessTests.cpp`, `SpineNlSvfHarnessTests.cpp`, `SpineDcBlockerHarnessTests.cpp`, `SpineHpStageHarnessTests.cpp`.
- `tests/TestDspSelfTests.cpp` (validates the library against analytic signals), `tests/golden/shaper_thd.csv` (M6 THD+N golden).
- `tests/CMakeLists.txt` — added fixtures + `tests/TestDspSelfTests.cpp` + `target_include_directories(... ${CMAKE_CURRENT_SOURCE_DIR})` + `BERNIE_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden"` compile def.
- `tests/OverdriveDiagnosticTests.cpp` — the **original print-only diagnostic harness (the "seed")**, KEPT as the verbose human-inspection entry (per user decision). It still asserts `AsymSaturator == plain tanh` to 1e-6.
- **Metric catalog M1–M15** (finiteness, boundedness, inharmonic/NSR aliasing, THD+N, click, block-vs-sample, zipper, freq-response-vs-analytic, Q-peak, self-osc pitch, low-level-linear, DC, denormal). Gates are `Gate::check` so failures print measured vs threshold. The runner (`tests/TestMain.cpp`) exits non-zero on failure → CI gate. **Proven**: a deliberate 5% cutoff nudge to `NlSvfCell::recompute` made 11 gates fail (exit 1); reverted → green.

### In progress / specced but NOT built
- **v5 design specs (ready to implement):** `docs/specs/2026-06-20-huggett-separation-design.md` (v5.2) and `docs/specs/2026-06-20-moog-ladder-design.md` (v5.3). Each ends with a **"Decisions (resolved 2026-06-20)"** section. `docs/specs/2026-06-20-test-harness-design.md` is the (now-built) harness spec.
- **Agreed v5 build sequence (in `docs/roadmap/phases.md` "v5 build sequence"):** `harness ✅ → Q17 hot-swap → Separator (v5.2) → v5.1 HQ OS tiers → Moog (v5.3) → SEM (v5.4)`. Note build order ≠ release numbers.

### Untouched / known backlog the harness surfaced
- **Cutoff-automation zipper (~−15 dB):** `NlSvfCell` has **no cutoff smoothing** (the v5 "per-parameter smoothing" sub-step is unbuilt; slewing was removed with the droop). The `SpineNlSvfHarnessTests` M9 zipper test is a **regression anchor at −12 dB**, not a "zipper-free" assertion. Fixing it = re-introduce proper coefficient/param smoothing (NOT the level-dependent droop).
- **Separation is broken** (the v5.2 work): in `HuggettFilter::processStereo`, cell `b` (the only offset section) runs only `if (slope_ == Slope::db24)`, so Separation is dead in 12 dB and merely shifts the corner in 24 dB. The dossiers call separation THE Huggett signature → the v5.2 spec fixes it.
- **~92 pre-existing `-Wsign-conversion` warnings** in OLDER code (`src/Layer.h`, `src/VoiceManager.cpp`, `src/Voice.cpp`) — not new; the spine + harness code is clean.
- Stale branches: `fix/huggett-separation` and `feat/moog-filter` were cut off the **old** main (`973a2ea`) and are behind — `git branch -f <branch> main` before building on them. `feat/test-harness` and `feat/v5.0-uat-remediation` are merged.

---

## 3. Key decisions and reasoning

- **Drop ADAA → plain tanh.** The harness's oversampled-reference NSR measured 1st-order ADAA as **24–60 dB WORSE** (more aliasing) than plain tanh across Bernie's drive range. ADAA was anti-aliasing *math*, not tone — removing it is a pure cleanup (the tanh curve + bias is unchanged). Oversampling deferred to v5.1.
- **Remove the `g_eff` "darken when loud" droop.** It was an **uncalibrated gray-box guess**, not a documented Huggett behavior (the grounded OTA nonlinearity is the resonance-loop tanh, which we KEPT). It caused clicks. The dossier-grounded character (asymmetric pre/post drive + self-limiting resonance) is intact.
- **Remove HP drive** (user). HP is now a clean HP-only filter; drive shaping lives only in the main filter (which keeps pre + post drive — "two drives, left and right of cutoff").
- **Master gain −9 dB default; HP resonance capped 0.15** (user: too loud; HP OTA self-oscillated too hot). HP reso `NormalisableRange{0,0.15}` so the knob's full throw == the old 15%.
- **Build Q17 hot-swap BEFORE Moog.** Adding a 2nd filter model makes a runtime model-**type** switch reachable for the first time, exposing the deferred dangling-per-voice-`State` hazard (Q17–Q19). User chose to build the click-free crossfade first, not ship Moog with a switch-click.
- **Moog rides the full v5.1 OS tiers** (so v5.1 lands before Moog); **pole-mix BP/HP**; `spine.moog.bassComp` knob (authentic default); keep a DC blocker. One-step `fbExtra` delta solve (no per-sample Newton).
- **Separation (v5.2) design:** two **always-active** 2-pole sections, symmetric split `cutA=cutoff·2^(−sep/2)`, `cutB=·2^(+sep/2)`; a `Routing` enum (3 single + 9 Summit dual routings) where slope decides *combination* (24 = series, 12 = parallel), never section existence; `spine.huggett.routing` is the single mode source (seed from `filter.type`); separation widened to **±4 oct**; keep the authentic +6 dB parallel overlap bump; LP+HP sum-and-halve; OSCar one-sided voicing deferred. (Route display names need `util::u8()` at the JUCE boundary.)
- **Harness decisions:** keep JUCE `UnitTest` + `juce_dsp::FFT`, extract metrics into the portable `testdsp/` lib; oversampled-reference NSR is the authoritative aliasing gate; **self-osc pitch gate ±0.75%** (widened from ±0.5% — see Failure #3); CSV goldens; stereo metrics from the start; thresholds anchored to measured baselines.
- **Roadmap (groomed in `docs/roadmap/phases.md`):** product = Bernie + Ricky; **v8 = Ricky** (Advanced-button multi-FX after the amp, some blocks shared into VAST DSP); **v9 = Summit A/B/Split/Layer multipart**; **v7 oscillator = per-waveform mini-mixer** (saw/square/tri/sine each with its own level slider + a wavetable slot → morphed composite waveform); GUI is incremental (no deferred "real GUI"); **removed Intonation & Tuning**; **demo + license unlock** at v11+; **commercial-VST comparison rig at v14**; **gate re-evaluation vs real synths** (nearer-term, feeds calibration); **Cmajor evaluation** registered (ADR-worthy: evaluate at the next DSP boundary; pilot one filter; verify JUCE integration; mind the 256-voice per-voice model).

---

## 4. Approaches we TRIED and that FAILED (do NOT re-attempt)

1. **"Fix the ADAA numerics" (double-precision antiderivative in `AsymSaturator`).** FAILED. The hypothesis was catastrophic float32 cancellation in `(G−G1)/dx`. Double precision left the inharmonic energy **bit-identical at drive=1 (−50.88 dB)** and unchanged elsewhere — the grit is **structural to 1st-order ADAA**, not numerical. The harness proved plain tanh is far cleaner. → Dropped ADAA entirely. Do not try to "rescue" ADAA; if heavy-drive aliasing matters, that's the v5.1 oversampling tiers.
2. **"Keep + smooth the droop" (per-block coefficient slewing in `NlSvfCell`).** FAILED. The droop artifact is the **amplitude-follower `env_` rippling at 2× the signal frequency** (modulating cutoff at audio rate → sidebands), NOT the per-block coefficient step. Slewing coefficients (block-vs-sample maxDiff went 0.082 → 0.082, inharmonic −27 → −27, unchanged) could not fix the env modulation. → Removed the droop entirely.
3. **Self-osc pitch gate at ±0.5%.** FAILED as a global gate. The gray-box `NlSvfCell` self-oscillates within ±0.5% everywhere EXCEPT **fc=5 kHz at 44.1/48 kHz**, where it's flat by ~0.6% (~10 cents) — inherent to the one-sample-delayed nonlinear resonance feedback at high fc/fs. User widened the gate to **±0.75%**. Don't re-tighten without a DSP pitch-correction.
4. **Subagent-dispatched Task 7.** The implementer subagent hit a **monthly spend limit** (`You've hit your monthly spend limit`). Task 7 (golden anchoring + CI confirmation) was done **inline by the controller** instead. If subagents fail on spend, do the work inline.
5. **Trusting a reviewer's hand-derived "true" BP analytic.** The Task-6 reviewer computed `−6.56 dB` for the M10 BP analytic and flagged a possible silent failure; it was a **different BP normalization** than the SVF tap convention `analyticBPdB` encodes. The suite was 0-failed (the gate is `err ≤ 0.5 dB` via `expect`), so it was a non-issue. Verify "gate is failing" claims against the actual suite exit, not a hand-calc.
6. **(Carried) bare `cmake --build build -j`** OOMs the JUCE compile → **always `-j4`**. Per-voice `juce::dsp::Oversampling` is forbidden at 256 voices (register Q12).

---

## 5. Files touched / created (this session)

**Spine DSP (remediated):** `src/dsp/spine/AsymSaturator.h` (→ memoryless tanh), `NlSvfCell.h` (droop+slew removed), `HuggettFilter.{h,cpp}` (sat calls 1-arg, droop calls removed, VoiceState slimmed), `HuggettHpStage.{h,cpp}` (HP drive removed).
**Params/UI:** `src/params/Parameters.{h,cpp}`, `src/params/ParamSnapshot.h` (gain −9, HP reso 0.15, spineHpDrive removed), `src/Layer.cpp` (setParams 3-arg), `src/PluginEditor.{h,cpp}` (master gain horizontal slider, HP toggle widened, hpDrive_ removed), `src/gui/SummitLookAndFeel.{h,cpp}` (compact combo rendering).
**Tests (created):** `tests/testdsp/{SignalGen,Spectrum,Metrics,Reference,ProcessAdapter,Gate,Response,GoldenIO}.h`, `tests/fixtures/Spine{Shaper,Huggett,NlSvf,DcBlocker,HpStage}HarnessTests.cpp`, `tests/TestDspSelfTests.cpp`, `tests/OverdriveDiagnosticTests.cpp` (seed), `tests/golden/shaper_thd.csv`. **(modified)** `tests/CMakeLists.txt`, `tests/{HuggettNonlinear,HpPreFilter,Layer,MultiLayer,ParamSnapshot}Tests.cpp`.
**Docs (created):** `docs/specs/2026-06-20-{test-harness,huggett-separation,moog-ladder}-design.md`, `docs/superpowers/plans/2026-06-20-test-harness.md`. **(modified)** `docs/roadmap/phases.md` (heavy groom), `docs/roadmap/README.md` (stale "after v1").
**SDD scratch (gitignored):** `.superpowers/sdd/progress.md` is the subagent-driven ledger (per-task commit ranges + every Minor finding deferred to the final review). Read it for granular detail.

Commit map: `902dec7`/`c7c0fbd`/`933d0b9` (roadmap follow-ups, UI cosmetics, param defaults) · `0263d1c`+`3ec1f8a` (spine remediation + diagnostic seed) · `7a45d12` (roadmap groom) · `973a2ea` (3 v5 specs + resolved decisions) · `3d4bdf6`..`84241c7` (harness plan + 7 tasks + fixes) · `eeadf9b`/`a2374f8`/`95f1844` (roadmap: gate-reeval/VST-rig, osc mini-mixer/v14, README).

---

## 6. The precise next step

The user **paused after merging the harness and grooming the roadmap.** No single mandated task; the queued threads are the user's pick:

1. **Q17 click-free crossfade hot-swap** — the next item in the agreed build sequence and a **prerequisite for Moog**. Scope: migrate per-voice `FilterModel::State` from heap (`makeState()` returns `State*`, owned in `src/dsp/spine/SpineFilterSlot.{h,cpp}` as `state_`/`hpState_`) to **in-place value storage**, and add an **equal-power crossfade** on model change. Resolves register Q17–Q19. No spec yet — would brainstorm → spec → subagent-driven build (like the harness).
2. **Separator (v5.2)** — spec is ready (`docs/specs/2026-06-20-huggett-separation-design.md`); it's all-Huggett so it does NOT need Q17, but the agreed order is `Q17 → Separator`. Ready to plan → build.
3. **Cmajor evaluation** — ADR + spike (pilot one filter model in Cmajor, verify the current JUCE integration, mind 256-voice per-voice model).
4. **Commercial-VST comparison rig (v14)** — far-future; needs its own brainstorm → spec.

**Recommendation:** the most shovel-ready DSP value is **#2 (Separator)** — fixes the broken, dossier-critical Huggett signature with a written spec in hand. If following the strict agreed order, **#1 (Q17)** comes first.

**Build / verify:**
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target k2000_tests -j4      # ALWAYS -j4, never bare -j
./build/tests/k2000_tests                          # expect "Summary: 138 tests, 0 failed"
cmake --build build --target k2000_VST3 k2000_Standalone -j4
```
Regenerate a golden after an intentional voicing change: `BERNIE_UPDATE_GOLDEN=1 ./build/tests/k2000_tests`.

**Process note:** the harness was executed via the superpowers **subagent-driven-development** flow (fresh implementer + task reviewer per task, model chosen per task: haiku for transcription, sonnet for integration/review; controller fixed trivial findings inline). The ledger at `.superpowers/sdd/progress.md` has the per-task record. Deferred Minor findings for a later cleanup pass: M3 `inharmonicDb` comment says "below the fundamental" (it's all inharmonic bins); `Response.h` includes `juce_dsp` redundantly; M15 denormal test (res=0.3) doesn't stress the subnormal-accumulation regime; consider hardening shaper M4 onto the live `AsymSaturator` (equivalence is gated in the seed).
