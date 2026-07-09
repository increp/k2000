# VCO Section Pre-Work: Drive/Mix Knob Removal + Moog Bass Retirement — Design

**Version:** 5.28 (artifact) · **Date:** 2026-07-09 · **Status:** Approved in brainstorm; build follows

---

## 1. Problem

Two small, unrelated cleanups requested as prerequisites before the 3-VCO Blend rework (`2026-07-09-three-vco-blend-design.md`):

1. The Source section's Drive/Mix knobs are visual clutter ahead of a section rebuild — hide them without losing the underlying waveshaper.
2. The Moog "Bass" sub-oscillator (filter-section "played-note bass voice", `MoogLadder.h:30-35`) is being retired outright — a deliberate reversal of the earlier "opt-in sub-osc bass, not auto-makeup" call (see project memory on the Moog filter consolidation), superseded by the coming real 3rd VCO.

## 2. Scope

**A. Drive/Mix knob visibility (keep the code):**
- Remove `shaperDrive_` and `shaperMix_` from `sourceSection_`'s `addAndMakeVisible`/layout calls in `PluginEditor.cpp`.
- Leave `id.shaperDrive`/`id.shaperMix` params, the `Waveshaper` DSP block, and their `binder_.bind(...)` calls untouched — the knobs stop being drawn/reachable, everything they control keeps running at its current default.
- `LabeledKnob` member declarations in `PluginEditor.h` stay (dead-but-present, per "don't delete the code, we'll look at it later") — revisit when the Source section rebuild (Spec 2) lands, since that rebuild touches the same section layout anyway.

**B. Moog Bass full removal:**
- Params: `spineMoogBassAmount`, `spineMoogBassWave`, `spineMoogBassOctave` — remove from `LayerIds`, `buildIds()`, the `layout.add(...)` calls, and the snapshot read in `Parameters.cpp`.
- DSP: `MoogLadder::setBass()`, `bassAmount_`/`bassWave_`/`bassOctave_` members, and the `vs.l.setBass(...)`/`vs.r.setBass(...)` call site (the "Task 6" comment block in `MoogLadder.h`/`.cpp`) — remove entirely.
- GUI: `moogBass_`, `moogWave_`, `moogOctave_` and their labels — remove from `PluginEditor.h`/`.cpp` (combo item lists, binder calls, layout cells, model-visibility toggling).
- No replacement mechanism — the ladder's inherent bass loss at high resonance goes uncompensated until real-hardware fingerprinting (SP-D) settles what, if anything, authentic voicing calls for.
- **Touches existing tests directly** (confirmed via grep, not guessed): `MoogLadderTests.cpp`, `FilterUnderTestTests.cpp`, `ParamSnapshotTests.cpp`, `RunnerLevelTests.cpp`, `RenderFingerprintTests.cpp`, and the `golden/fingerprints/baseline.csv` golden. The golden diff needs the usual `BERNIE_UPDATE_GOLDEN=1` regen plus an explicit justification in the commit message (removed-feature diff, not a DSP regression) per the standing golden-update rule.

## 3. Non-goals

- No change to `CALIB_LIM_CEIL` or any other Moog saturation/voicing constant — this is strictly the sub-osc bass-voice feature, unrelated to the ladder's internal limiter.
- No change to Huggett (confirmed via grep: no bass-related code exists there).

## 4. Verification

Suite build + run (`cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests`) — the five touched test files must compile clean with every Moog-bass-specific case removed or updated, not left dangling; golden diff reviewed and justified in the commit. Live GUI check in the running Standalone: Source section shows no Drive/Mix knobs; Filter section (Moog model selected) shows no Bass/Wave/Octave controls; both waveshaper and ladder filter otherwise behave unchanged.
