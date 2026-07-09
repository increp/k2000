# VCO Section Pre-Work: Drive/Mix + Moog Bass Knob Removal — Design

**Version:** 5.30 (artifact, revised from 5.28) · **Date:** 2026-07-09 · **Status:** Approved in brainstorm; scope narrowed during plan-writing (§2B), build follows

---

## 1. Problem

Two small, unrelated cleanups requested as prerequisites before the 3-VCO Blend rework (`2026-07-09-three-vco-blend-design.md`):

1. The Source section's Drive/Mix knobs are visual clutter ahead of a section rebuild — hide them without losing the underlying waveshaper.
2. The Moog "Bass" sub-oscillator (filter-section "played-note bass voice", `MoogLadder.h:30-35`) is being retired from the panel — hide its GUI controls the same way as (1), leaving params/DSP untouched. **Scope narrowed during planning**: the DSP turns out to run through the Cmajor codegen pipeline (`MoogLadder.cmajor` → `tools/cmajor/cmaj-codegen.sh` → the generated `generated/MoogLadder.h`), which would need the Docker/jammy toolchain to touch safely — too much friction/risk for pre-work. Knob-only removal is a deliberate, informed scope cut, not an oversight; full retirement (params + DSP + Cmajor regen) can happen later, e.g. alongside real hardware-informed voicing work (SP-D).

## 2. Scope

**A. Drive/Mix knob visibility (keep the code):**
- Remove `shaperDrive_` and `shaperMix_` from `sourceSection_`'s `addAndMakeVisible`/layout calls in `PluginEditor.cpp`.
- Leave `id.shaperDrive`/`id.shaperMix` params, the `Waveshaper` DSP block, and their `binder_.bind(...)` calls untouched — the knobs stop being drawn/reachable, everything they control keeps running at its current default.
- `LabeledKnob` member declarations in `PluginEditor.h` stay (dead-but-present, per "don't delete the code, we'll look at it later") — revisit when the Source section rebuild (Spec 2) lands, since that rebuild touches the same section layout anyway.

**B. Moog Bass knob visibility (keep the code — same treatment as A):**
- Remove `moogWave_`, `moogOctave_`, `moogBass_` (and their labels `moogWaveLbl_`, `moogOctaveLbl_`) from `filterSection_`'s `addAndMakeVisible` calls in `PluginEditor.cpp`. `moogMode_`/`moogModeLbl_` stay — that's the main filter mode selector, unrelated to bass.
- Trim the now-dead cells out of `updateModelVisibility()`'s `moogControls[]` array and the Moog row's `layoutCells(...)` call in `resized()` (both currently list Mode/Wave/Octave/Bass together) so Mode gets the full row width instead of leaving 3/4 of it blank when the Moog model is selected.
- Leave `id.spineMoogBassAmount/Wave/Octave` params, `ParamSnapshot`'s `moogBassAmount/Wave/Octave` fields, `MoogLadder::setBass()`, the Cmajor DSP, and every `binder_.bind(...)` call for these controls completely untouched — same "stops being drawn/reachable, everything it controls keeps running at its current default" contract as (A).
- `moogBass_`, `moogWave_`, `moogOctave_`, and their labels in `PluginEditor.h` stay too (dead-but-present, same rationale as (A)'s Drive/Mix knobs) — revisit alongside the full param+DSP+Cmajor retirement whenever that happens (see §1's scope-narrowing note), not as part of the Source section rebuild (this pass is Filter-section, not Source-section).
- No replacement mechanism — the ladder's inherent bass loss at high resonance goes uncompensated until real-hardware fingerprinting (SP-D) settles what, if anything, authentic voicing calls for.
- **Does not touch any test file or golden** — confirmed via grep, the only references outside GUI/param/DSP plumbing are in `MoogLadderTests.cpp`, `FilterUnderTestTests.cpp`, `ParamSnapshotTests.cpp`, `RunnerLevelTests.cpp`, `RenderFingerprintTests.cpp`, and `golden/fingerprints/baseline.csv` — all exercise the param/DSP layer, which this pass doesn't change. No golden regen needed.

## 3. Non-goals

- No change to `CALIB_LIM_CEIL` or any other Moog saturation/voicing constant — this is strictly the sub-osc bass-voice feature, unrelated to the ladder's internal limiter.
- No change to Huggett (confirmed via grep: no bass-related code exists there).

## 4. Verification

Suite build + run (`cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests`) — expect it green with zero test changes (nothing at the param/DSP layer moved). Live GUI check in the running Standalone: Source section shows no Drive/Mix knobs; Filter section (Moog model selected) shows only Mode, no Wave/Octave/Bass controls, and Mode fills the row rather than leaving dead space; both waveshaper and ladder filter otherwise behave unchanged.
