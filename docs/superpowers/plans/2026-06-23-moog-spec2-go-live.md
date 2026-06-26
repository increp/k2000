# Moog Spec 2 — Go-Live + Filter-Section Consolidation — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Moog a live, playable filter model (mode + slope-in-all-modes + sub-osc bass voice, wired through params/`Layer`/`Voice`/UI), and consolidate the spine filter section (remove dead Type + `SVFFilter`, add Huggett Notch, rename Routing → "Filter Routing").

**Architecture:** Follow the existing spine patterns — shared model config on `Layer` (+ cached per-model pointers), per-voice state in `SpineFilterSlot`, active model passed per-call. Moog's params are namespaced `spine.moog.*`; dispatch uses an explicit `moog_` block beside the `huggett_` one (no generic abstraction — YAGNI for two models). The per-voice sub-osc fundamental rides a new `FilterModel::setVoiceContext` hook threaded through `processStereo`.

**Tech Stack:** C++17, JUCE 8.0.4, Cmajor (dev-time Docker codegen), CMake, JUCE `UnitTest`.

**Design spec:** `docs/superpowers/specs/2026-06-23-moog-spec2-go-live-design.md` (v5.08).

## Global Constraints

- **No preset/patch backward-compat** (`[[feedback-no-preset-backcompat]]`): no migration code, no "presets load unchanged" tests; remove dead params outright; choose defaults on merit.
- **Build/test:** `cmake --build build --target k2000_tests -j4` (ALWAYS `-j4`; bare `-j` OOMs). Run `./build/tests/k2000_tests`; green = final line `Summary: N tests, 0 failed` — grep stdout.
- **Cmajor codegen (Task 1 only):** after the `.cmajor` edit, regenerate with `sg docker -c "tools/cmajor/cmaj-codegen.sh src/dsp/spine/cmajor/MoogLadder.cmajorpatch src/dsp/spine/cmajor/generated/MoogLadder.h"` (input is the **`.cmajorpatch`**, not the `.cmajor`). Commit the `.cmajor` + regenerated `.h` together. cmaj never runs in CI.
- **Pristine output:** `-Wshadow`/`-Wfloat-equal` are defects (`std::fpclassify(v)==FP_ZERO` for zero tests); pre-existing `-Wsign-conversion` deferred.
- **Never** loosen an assertion or change a `// CALIB` to force green; tune the DSP.
- **RT-safety:** nothing allocates/locks on the audio path; `Layer`/`Voice`/slot stay heap-free after `prepare`.
- **UTF-8 boundary:** display strings with non-ASCII go through `util::u8()` (`[[feedback-utf8-centralized]]`); the param/UI strings here are ASCII.
- **Branch:** `feat/moog-spec2` (off `main`; design committed).
- **Key existing interfaces** (read before touching): `MoogLadder.h` (`Mode{LP=0,BP=1,HP=2}`, `Slope{db12,db24}`, `setMode/setSlope/setBass(amount,wave,octave)/setFundamental(State&,hz)`, `VoiceState{l,r,fundamentalHz}`); `HuggettFilter.h` (`Routing` 12-value enum, `resolve()`); `FilterModel.h`; `Layer.{h,cpp}`; `Voice.cpp` (`hz` at line 72, `processStereo` call line 90); `SpineFilterSlot.{h,cpp}`; `ParamSnapshot.h`; `Parameters.cpp`; `PluginEditor.cpp`; `Algorithm.h` (`BlockTypeId`); `NlSvfCell.h` (`Tap{LP,HP,BP,Notch}`).

---

### Task 1: Moog DSP — 12/24 dB in all modes (BP4 + HP2)

Add the two missing pole-mix variants so slope is meaningful in every mode. Cmajor edit + regen + behavioral tests.

**Files:**
- Modify: `src/dsp/spine/cmajor/MoogLadder.cmajor` (+ regenerate `generated/MoogLadder.h`)
- Test: `tests/MoogLadderTests.cpp`

**Interfaces:** no C++ API change (mode/slope already wired via `setMode`/`setSlope` → `addEvent_mode`/`addEvent_slope`). Internal: the `tapOut` selector becomes a `(modeSel, slopeSel)` switch.

- [ ] **Step 1: Add the failing tests** — append to `tests/MoogLadderTests.cpp` `runTest()`:

```cpp
beginTest("slope is meaningful in BP and HP (BP24 steeper than BP12; HP12 gentler than HP24)");
{
    const double sr = 48000.0; MoogLadder m; m.prepare(sr);
    std::unique_ptr<FilterModel::State> st(m.makeState());
    m.setCommon(1000.0f, 0.3f, 0.0f);
    auto skirtDb = [&](MoogLadder::Mode mode, MoogLadder::Slope sl, double probe) {
        m.setMode(mode); m.setSlope(sl);
        return 20.0 * std::log10(std::max(1e-6, mag(m, *st, 1000.0, probe)));   // mag() = existing helper
    };
    // BP: compare skirt steepness two octaves below centre (250 Hz) — BP4 rejects more than BP2
    const double bp12 = skirtDb(MoogLadder::Mode::BP, MoogLadder::Slope::db12, 250.0);
    const double bp24 = skirtDb(MoogLadder::Mode::BP, MoogLadder::Slope::db24, 250.0);
    expect(bp24 < bp12 - 4.0, "BP24 not steeper than BP12 @250Hz: " + juce::String(bp24,1) + " vs " + juce::String(bp12,1));
    // HP: one octave below cutoff (500 Hz) — HP2 passes more low than HP4
    const double hp12 = skirtDb(MoogLadder::Mode::HP, MoogLadder::Slope::db12, 500.0);
    const double hp24 = skirtDb(MoogLadder::Mode::HP, MoogLadder::Slope::db24, 500.0);
    expect(hp12 > hp24 + 4.0, "HP12 not gentler than HP24 @500Hz: " + juce::String(hp12,1) + " vs " + juce::String(hp24,1));
}
```

(If your `MoogLadderTests` no longer has a private `mag()` after the harness branch, this branch is off `main` which predates that — `mag()` is present. Confirm by grep.)

- [ ] **Step 2: Run — expect FAIL** (BP/HP ignore slope today).

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | grep -E "MoogLadder|Summary"`
Expected: the new sub-test fails (BP24==BP12, HP12==HP24).

- [ ] **Step 3: Edit `MoogLadder.cmajor`** — replace the single BP/HP tap expressions with slope-keyed variants. In BOTH the nonlinear and linear branches, the `tapOut` ternary's BP and HP arms become slope switches. Add CALIB weight sets near the existing `CALIB_BP_*`:

```
    // BP: 12 dB = BP2 (existing 4(y2-y3)); 24 dB = BP4 (4-pole bandpass pole-mix).
    let CALIB_BP4_W1 =  0.0f;  let CALIB_BP4_W2 =  8.0f;
    let CALIB_BP4_W3 = -16.0f; let CALIB_BP4_W4 =  8.0f;   // CALIB: 8(y2 - 2y3 + y4) — tune levels
    // HP: 24 dB = HP4 (existing binomial); 12 dB = HP2 (2-pole, y0 - 2y1 + y2).
    let CALIB_HP2_SCALE = 1.0f;
```

and the tap selection (shown for the NL branch; mirror verbatim in the linear branch, using that branch's `y0`/`y1..y4`):

```
                let bp = (slopeSel == 0)
                    ? (CALIB_BP_W1*y1 + CALIB_BP_W2*y2 + CALIB_BP_W3*y3 + CALIB_BP_W4*y4)        // BP2
                    : (CALIB_BP4_W1*y1 + CALIB_BP4_W2*y2 + CALIB_BP4_W3*y3 + CALIB_BP4_W4*y4);   // BP4
                let hp = (slopeSel == 0)
                    ? (CALIB_HP2_SCALE * (y0 - 2.0f*y1 + y2))                                     // HP2
                    : (CALIB_HP_SCALE  * (y0 - 4.0f*y1 + 6.0f*y2 - 4.0f*y3 + y4));                // HP4
                let tapOut = (modeSel == 1) ? bp
                           : (modeSel == 2) ? hp
                           : ((slopeSel == 0) ? y2 : y4);                                          // LP
```

- [ ] **Step 4: Regenerate + build + iterate the CALIB** until green.

Run: `sg docker -c "tools/cmajor/cmaj-codegen.sh src/dsp/spine/cmajor/MoogLadder.cmajorpatch src/dsp/spine/cmajor/generated/MoogLadder.h"` then `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | grep -E "MoogLadder|Summary"`.
Expected: PASS, `0 failed`, and the existing Moog tests (linearity, self-osc, modes, bass) still green (linear path bit-stable). Tune BP4/HP2 CALIB weights (not the test bounds) so the variants attenuate as asserted and sit at a sane passband level.

- [ ] **Step 5: Commit** (with regenerated header)

```bash
git add src/dsp/spine/cmajor/MoogLadder.cmajor src/dsp/spine/cmajor/generated/MoogLadder.h tests/MoogLadderTests.cpp
git commit -m "feat(moog): 12/24 dB in all modes (BP4 + HP2 pole-mix, slope-keyed)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: Register Moog in `FilterModelLibrary`

Make Moog the second registered model so it appears in the Filter selector and the `Layer` builds it.

**Files:**
- Modify: `src/dsp/spine/FilterModelLibrary.cpp`
- Test: `tests/FilterModelLibraryTests.cpp`

**Interfaces:** Consumes `MoogLadder` (Task-1 unchanged API). Produces: `FilterModelLibrary::count()==2`, `names()` contains "Moog", `create(1)` yields a `MoogLadder`.

- [ ] **Step 1: Add the failing test** — append to `tests/FilterModelLibraryTests.cpp`:

```cpp
beginTest("Moog is registered as the second model");
{
    expect(FilterModelLibrary::count() == 2, "expected 2 models, got " + juce::String((int) FilterModelLibrary::count()));
    expect(FilterModelLibrary::names().contains("Moog"), "names() missing Moog");
    expect(FilterModelLibrary::id(1) == "moog", "id(1) != moog");
    auto m = FilterModelLibrary::create(1);
    expect(dynamic_cast<MoogLadder*>(m.get()) != nullptr, "create(1) is not a MoogLadder");
}
```

Add `#include "../src/dsp/spine/MoogLadder.h"` to the test if absent.

- [ ] **Step 2: Run — expect FAIL** (`count()==1`).

- [ ] **Step 3: Register Moog** — in `FilterModelLibrary.cpp`: add `#include "MoogLadder.h"`, the Q18 asserts, and the table entry:

```cpp
#include "MoogLadder.h"
static_assert(sizeof(MoogLadder::VoiceState)  <= kMaxSpineStateBytes,
              "MoogLadder::VoiceState exceeds kMaxSpineStateBytes — bump it (Q18) or slim the model");
static_assert(alignof(MoogLadder::VoiceState) <= kSpineStateAlign,
              "MoogLadder::VoiceState over-aligned for the spine slot");
// ... in kEntries, after the huggett entry:
    { "moog", "Moog", []() -> std::unique_ptr<FilterModel> { return std::make_unique<MoogLadder>(); } },
```

Add `MoogLadder.cpp` + `cmajor/MoogLadderAdapter.cpp` to the `FilterModelLibrary`'s consumers in the **non-test** build if needed: check `CMakeLists.txt` (plugin target) — the library now references `MoogLadder`, so its `.cpp` + the adapter `.cpp` must be in the plugin sources. (The test target `tests/CMakeLists.txt` already lists them from Spec 1.)

- [ ] **Step 4: Run — expect PASS.** `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | grep -E "FilterModelLibrary|Summary"`.

- [ ] **Step 5: Commit**

```bash
git add src/dsp/spine/FilterModelLibrary.cpp tests/FilterModelLibraryTests.cpp CMakeLists.txt
git commit -m "feat(spine): register Moog as the 2nd filter model (Q18-guarded)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 3: Moog params + `ParamSnapshot`

Add the four Moog-namespaced params and snapshot fields. (Type removal is Task 7.)

**Files:**
- Modify: `src/params/Parameters.cpp` (id struct, layout, snapshot builder), the param-id header it populates (`Parameters.h` or wherever `id.*` is declared)
- Modify: `src/params/ParamSnapshot.h`
- Test: `tests/ParamSnapshotTests.cpp`

**Interfaces:** Produces `ParamSnapshot` fields `moogMode` (int 0..2), `moogBassAmount` (float 0..1), `moogBassWave` (int 0..2), `moogBassOctave` (int 0..2); param IDs `spine.moog.mode`, `spine.moog.bassAmount`, `spine.moog.bassWave`, `spine.moog.bassOctave`.

- [ ] **Step 1: Add `ParamSnapshot` fields** — in `src/params/ParamSnapshot.h`, after `huggettRouting`:

```cpp
    // Moog bank (spine.moog.*)
    int   moogMode       = 0;   // 0=LP 1=BP 2=HP
    float moogBassAmount = 0.0f;
    int   moogBassWave   = 0;   // 0=Sine 1=Triangle 2=Saw
    int   moogBassOctave = 0;   // 0=unison 1=-1oct 2=-2oct
```

- [ ] **Step 2: Add the failing test** — append to `tests/ParamSnapshotTests.cpp` a case asserting the Moog params exist and default correctly:

```cpp
beginTest("Moog bank params exist with correct defaults");
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout = params::createLayout();   // existing factory
    auto apvts = makeApvts(layout);   // existing test helper; else construct as other cases do
    expect(apvts.getParameter(params::ids().spineMoogMode)       != nullptr, "spine.moog.mode missing");
    expect(apvts.getParameter(params::ids().spineMoogBassAmount) != nullptr, "spine.moog.bassAmount missing");
    expect(apvts.getParameter(params::ids().spineMoogBassWave)   != nullptr, "spine.moog.bassWave missing");
    expect(apvts.getParameter(params::ids().spineMoogBassOctave) != nullptr, "spine.moog.bassOctave missing");
    auto s = params::snapshot(apvts);
    expect(s.moogMode == 0 && std::fpclassify(s.moogBassAmount) == FP_ZERO, "moog defaults wrong");
}
```

(Mirror the exact helper names this file already uses for building the APVTS + snapshot — read the top of `ParamSnapshotTests.cpp`.)

- [ ] **Step 2b: Run — expect FAIL** (ids/params don't exist).

- [ ] **Step 3: Add the param IDs + layout + snapshot read** — in `Parameters.cpp`:
  - Id struct (mirror `id.spineHuggettRouting = p + "spine.huggett.routing";`):
    ```cpp
    id.spineMoogMode       = p + "spine.moog.mode";
    id.spineMoogBassAmount = p + "spine.moog.bassAmount";
    id.spineMoogBassWave   = p + "spine.moog.bassWave";
    id.spineMoogBassOctave = p + "spine.moog.bassOctave";
    ```
    (and declare those members in the id struct header next to `spineHuggettRouting`.)
  - Layout (mirror the existing `ChoiceParam`/`FloatParam` adds; the Huggett routing add is the template at `Parameters.cpp:183`):
    ```cpp
    layout.add(std::make_unique<ChoiceParam>(juce::ParameterID{id.spineMoogMode, 1},
        "Moog Mode " + juce::String(i), juce::StringArray{"LP","BP","HP"}, 0));
    layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.spineMoogBassAmount, 1},
        "Moog Bass " + juce::String(i), juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    layout.add(std::make_unique<ChoiceParam>(juce::ParameterID{id.spineMoogBassWave, 1},
        "Moog Bass Wave " + juce::String(i), juce::StringArray{"Sine","Triangle","Saw"}, 0));
    layout.add(std::make_unique<ChoiceParam>(juce::ParameterID{id.spineMoogBassOctave, 1},
        "Moog Bass Octave " + juce::String(i), juce::StringArray{"0","-1 oct","-2 oct"}, 0));
    ```
    (Match the per-layer loop/index pattern the existing spine params use — they are created per layer index `i`.)
  - Snapshot builder (mirror `s.huggettRouting = (int) raw(apvts, id.spineHuggettRouting);`):
    ```cpp
    s.moogMode       = (int)   raw(apvts, id.spineMoogMode);
    s.moogBassAmount =         raw(apvts, id.spineMoogBassAmount);
    s.moogBassWave   = (int)   raw(apvts, id.spineMoogBassWave);
    s.moogBassOctave = (int)   raw(apvts, id.spineMoogBassOctave);
    ```

- [ ] **Step 4: Run — expect PASS.** Build + run; the new case passes, existing ParamSnapshot tests green.

- [ ] **Step 5: Commit**

```bash
git add src/params/Parameters.cpp src/params/Parameters.h src/params/ParamSnapshot.h tests/ParamSnapshotTests.cpp
git commit -m "feat(spine): spine.moog.* params (mode + bass voice) + snapshot fields

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 4: `Layer` Moog dispatch

Route the Moog params to the Moog instance via an explicit `moog_` block.

**Files:**
- Modify: `src/Layer.h` (add `MoogLadder* moog_`), `src/Layer.cpp` (cache it + dispatch block)
- Test: `tests/LayerTests.cpp`

**Interfaces:** Consumes `ParamSnapshot.moog*` (Task 3), `MoogLadder` setters. Produces: selecting `spineModel==1` and setting Moog params drives the Moog instance.

- [ ] **Step 1: Add the failing test** — append to `tests/LayerTests.cpp`:

```cpp
beginTest("Layer routes Moog params to the Moog instance and processes through it");
{
    Layer layer; layer.prepare(48000.0, 64);
    ParamSnapshot s;                       // defaults
    s.spineModel = 1;                      // Moog
    s.svfCutoffHz = 1200.0f; s.svfResonance = 0.4f; s.moogMode = 0; s.spineSlope = 1;
    layer.updateParameters(s);
    expect(layer.spineModel() != nullptr, "no spine model");
    // the active model is Moog (index 1)
    expect(dynamic_cast<const MoogLadder*>(layer.spineModel()) != nullptr, "active model is not Moog");
}
```

Add `#include "dsp/spine/MoogLadder.h"` to the test.

- [ ] **Step 2: Run — expect FAIL** (compile: `moog_` not present / active model wrong).

- [ ] **Step 3: Implement** — `Layer.h`: add `MoogLadder* moog_ = nullptr;` beside `huggett_` (+ `#include "dsp/spine/MoogLadder.h"`). `Layer.cpp` `prepare`, after `huggett_ = dynamic_cast<...>`:

```cpp
    moog_ = nullptr;
    for (auto& m : models_) if (auto* mg = dynamic_cast<MoogLadder*>(m.get())) moog_ = mg;
```

`Layer.cpp` `updateParameters`, after the `if (huggett_) { ... }` block:

```cpp
    if (moog_) {
        moog_->setMode (static_cast<MoogLadder::Mode>(s.moogMode));
        moog_->setSlope(s.spineSlope == 0 ? MoogLadder::Slope::db12 : MoogLadder::Slope::db24);
        moog_->setBass (s.moogBassAmount, s.moogBassWave, s.moogBassOctave);
        moog_->setSeparation(s.spineSeparationOct);   // no-op, for symmetry
    }
```

(`setCommon` already fans out to all models — leave it. The `svfType` fallback stays until Task 7.)

- [ ] **Step 4: Run — expect PASS.** `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | grep -E "Layer|Summary"`.

- [ ] **Step 5: Commit**

```bash
git add src/Layer.h src/Layer.cpp tests/LayerTests.cpp
git commit -m "feat(spine): Layer dispatches Moog bank params to the Moog instance

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 5: `setVoiceContext` base hook + played-note plumbing

Thread the per-voice played-note fundamental from `Voice` to Moog's sub-osc.

**Files:**
- Modify: `src/dsp/spine/FilterModel.h` (base hook), `src/dsp/spine/MoogLadder.h` (override), `src/dsp/spine/SpineFilterSlot.{h,cpp}` (`processStereo` gains `fundamentalHz`), `src/Voice.cpp` (pass `hz`)
- Test: `tests/MoogLadderTests.cpp` (sub-osc tracks the played note via the hook)

**Interfaces:** Produces `virtual void FilterModel::setVoiceContext(State&, float fundamentalHz) const noexcept {}`; `MoogLadder::setVoiceContext` overrides → `setFundamental`. `SpineFilterSlot::processStereo(... , float fundamentalHz, float* L, float* R, int n)`.

- [ ] **Step 1: Add the failing test** — append to `tests/MoogLadderTests.cpp`:

```cpp
beginTest("setVoiceContext drives the sub-osc fundamental (base no-op; Moog override)");
{
    const double sr = 48000.0; MoogLadder m; m.prepare(sr);
    std::unique_ptr<FilterModel::State> st(m.makeState());
    m.setCommon(1500.0f, 0.0f, 0.0f); m.setBass(0.8f, /*sine*/0, /*oct*/0);
    m.reset(*st);
    m.setVoiceContext(*st, 110.0f);                 // polymorphic hook
    std::vector<float> l(16384,0.0f), r(16384,0.0f);  // silent input
    m.processStereo(*st, l.data(), r.data(), (int) l.size());
    double e = 0; for (float v : l) e += double(v)*v;
    expect(e > 1.0, "setVoiceContext did not drive the sub-osc");
    // base default is a no-op: a FilterModel& whose dynamic type ignores it must not crash/alter
    FilterModel& base = m; base.setVoiceContext(*st, 220.0f);   // compiles + safe
}
```

- [ ] **Step 2: Run — expect FAIL** (`setVoiceContext` undefined on the base).

- [ ] **Step 3: Implement the hook + override.**
  - `FilterModel.h`, after `setCommon`:
    ```cpp
    // Per-voice played-note context (e.g. Moog's sub-osc fundamental). Default: ignore.
    virtual void setVoiceContext(State&, float /*fundamentalHz*/) const noexcept {}
    ```
  - `MoogLadder.h`, after `setFundamental`:
    ```cpp
    void setVoiceContext(State& s, float fundamentalHz) const noexcept override { setFundamental(s, fundamentalHz); }
    ```

- [ ] **Step 4: Run — expect PASS** for the Step-1 test.

- [ ] **Step 5: Thread `fundamentalHz` through the slot.**
  - `SpineFilterSlot.h`: change the `processStereo` decl to add `float fundamentalHz` before `float* left`:
    ```cpp
    void processStereo(const HuggettHpStage* hp, bool hpEnabled,
                       const FilterModel* current, float fadeMs, float fundamentalHz,
                       float* left, float* right, int numSamples) noexcept;
    ```
  - `SpineFilterSlot.cpp`: in `processStereo`, before each `model_[*]->processStereo(...)` call (the steady path ~line 74 and the fade paths ~82-83), apply the context to that state:
    ```cpp
    if (state_[active_]) model_[active_]->setVoiceContext(*state_[active_], fundamentalHz);
    // ... and for the fade's other state:
    if (state_[other]) model_[other]->setVoiceContext(*state_[other], fundamentalHz);
    ```
    (Apply to whichever states are processed in that branch, mirroring the existing `model_[x]->processStereo` calls.)
  - `Voice.cpp` `render`: pass `hz` (already computed line 72) into the call (line ~90):
    ```cpp
    spine_.processStereo(layer_->hpStage(), s.hpEnable != 0,
                         layer_->spineModel(), s.spineModelFadeMs, hz, tmpL, tmpR, numSamples);
    ```

- [ ] **Step 6: Build + run full suite green** (the `SpineFilterSlot` signature change touches `Voice.cpp` and any slot tests — update call sites).

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | tail -3`. Expected `0 failed`.

- [ ] **Step 7: Commit**

```bash
git add src/dsp/spine/FilterModel.h src/dsp/spine/MoogLadder.h src/dsp/spine/SpineFilterSlot.h src/dsp/spine/SpineFilterSlot.cpp src/Voice.cpp tests/MoogLadderTests.cpp
git commit -m "feat(spine): setVoiceContext hook + played-note plumbing for Moog sub-osc

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 6: Huggett Notch (routing after HP)

Add a Notch routing to Huggett so the single-mode list reads LP/BP/HP/Notch. `NlSvfCell` already has a `Notch` tap.

**Files:**
- Modify: `src/dsp/spine/HuggettFilter.h` (Routing enum), `src/dsp/spine/HuggettFilter.cpp` (`resolve()`), `src/params/Parameters.cpp` (routing item list)
- Test: `tests/HuggettNonlinearTests.cpp` (Notch attenuates the centre band)

**Interfaces:** `HuggettFilter::Routing::Notch` inserted at index 3 (series/parallel shift +1 — preset-compat dropped). `resolve()` maps Notch → single, `tapA = NlSvfCell::Notch`.

- [ ] **Step 1: Add the failing test** — append to `tests/HuggettNonlinearTests.cpp`:

```cpp
beginTest("Huggett Notch attenuates the band around cutoff vs LP passband");
{
    const double sr = 48000.0; HuggettFilter h; h.prepare(sr);
    std::unique_ptr<FilterModel::State> st(h.makeState());
    h.setCommon(1000.0f, 0.2f, 0.0f); h.setSlope(HuggettFilter::Slope::db12);
    auto magAt = [&](HuggettFilter::Routing r, double probe) {
        h.setRouting(r); h.reset(*st);
        return hmag(h, *st, probe);   // existing magnitude helper in this file; mirror its name
    };
    const double notchCentre = magAt(HuggettFilter::Routing::Notch, 1000.0);
    const double lpLow       = magAt(HuggettFilter::Routing::LP,    100.0);
    expect(notchCentre < lpLow * 0.5, "Notch did not attenuate the centre band: "
           + juce::String(notchCentre,4) + " vs " + juce::String(lpLow,4));
}
```

(Use the magnitude helper this test file already defines; read the file header.)

- [ ] **Step 2: Run — expect FAIL** (`Routing::Notch` undefined).

- [ ] **Step 3: Implement.**
  - `HuggettFilter.h` Routing enum — insert `Notch = 3` after `HP`, shift the rest:
    ```cpp
    enum class Routing {
        LP = 0, BP = 1, HP = 2, Notch = 3,            // single modes (slope decides 12/24)
        SeriesLPHP = 4, SeriesLPBP = 5, SeriesHPBP = 6,
        ParLPHP    = 7, ParLPBP    = 8, ParHPBP    = 9,
        ParLPLP    = 10, ParBPBP   = 11, ParHPHP   = 12
    };
    ```
  - `HuggettFilter.cpp` `resolve()` — add a `Notch` case to the single-mode mapping: `single=true, tapA = (int) NlSvfCell::Notch` (mirror how LP/BP/HP set `tapA`). Verify the series/parallel cases still match their (now-renumbered) enum values — they switch on the enum, so they follow automatically; just confirm no hard-coded `3..11` integer literals remain (replace any with the enum names).
  - `Parameters.cpp` routing item list — insert `"Notch"` after `"HP"` in the `spineHuggettRouting` `ChoiceParam` StringArray (the list at ~line 184) so indices line up with the enum.

- [ ] **Step 4: Run — expect PASS** for the Notch test; existing Huggett routing tests still green (the renumber is internal + consistent).

- [ ] **Step 5: Commit**

```bash
git add src/dsp/spine/HuggettFilter.h src/dsp/spine/HuggettFilter.cpp src/params/Parameters.cpp tests/HuggettNonlinearTests.cpp
git commit -m "feat(spine): Huggett Notch routing (after HP) via NlSvfCell notch tap

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 7: Remove the dead "Type" control

Delete `filter.type` / `svfType` and its legacy Huggett-routing fallback entirely (no migration).

**Files:**
- Modify: `src/params/Parameters.cpp` (+ id header), `src/params/ParamSnapshot.h`, `src/Layer.cpp` (fallback), `src/PluginEditor.cpp` (combo + binding + layout), `src/PluginEditor.h` (member)
- Test: `tests/ParamSnapshotTests.cpp` / build-green

**Interfaces:** removes `id.filterType`, `ParamSnapshot::svfType`, the `filterType_` combo, and `Layer`'s `switch(s.svfType)`.

- [ ] **Step 1: Remove the param** — delete `id.filterType = p + "filter.type";`, its member, and the `filterType` `ChoiceParam` add (the `{"LP","HP","BP","Notch"}` layout entry ~`Parameters.cpp:107`). Delete the `s.svfType = (int) raw(apvts, id.filterType);` snapshot read (~line 205). Delete `int svfType` from `ParamSnapshot.h`.

- [ ] **Step 2: Remove the `Layer` fallback** — in `Layer.cpp updateParameters`, replace the `if (huggett_)` routing block's fallback:
    ```cpp
    if (huggett_) {
        huggett_->setRouting(static_cast<HuggettFilter::Routing>(s.huggettRouting));
        huggett_->setSlope(s.spineSlope == 0 ? HuggettFilter::Slope::db12 : HuggettFilter::Slope::db24);
        huggett_->setSeparation(s.spineSeparationOct);
        huggett_->setPostDrive(s.huggettPostDrive);
    }
    ```
    (the `int routingIdx = ...; switch(s.svfType)...` lines are gone.)

- [ ] **Step 3: Remove the UI combo** — in `PluginEditor.cpp`: delete `filterTypeLbl_`/`filterType_` setup (~lines 65-69), the `binder_.bind(filterType_, ids.filterType)` (~line 181), and the `{ &filterTypeLbl_, &filterType_ }` layout cell (~line 314). Remove the `filterTypeLbl_`/`filterType_` members from `PluginEditor.h`.

- [ ] **Step 4: Build green** — `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | tail -3`. Expected `0 failed`, no dangling references to `svfType`/`filterType`. Grep to confirm: `grep -rn "svfType\|filterType\|filter.type" src/ | grep -v "// "` returns nothing.

- [ ] **Step 5: Commit**

```bash
git add src/params/Parameters.cpp src/params/Parameters.h src/params/ParamSnapshot.h src/Layer.cpp src/PluginEditor.cpp src/PluginEditor.h tests/ParamSnapshotTests.cpp
git commit -m "refactor(spine): remove dead Type control + svfType legacy routing fallback

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 8: Remove the dead `SVFFilter` block

Delete the retired palette block (already off the per-voice graph since v5).

**Files:**
- Delete: `src/dsp/blocks/SVFFilter.{h,cpp}`, `tests/SVFFilterTests.cpp`
- Modify: `src/Layer.cpp` (construction + include), `src/dsp/Algorithm.h` (`BlockTypeId`), `CMakeLists.txt` + `tests/CMakeLists.txt` (sources), any `#include "dsp/blocks/SVFFilter.h"`

**Interfaces:** removes `BlockTypeId::SvfFilter` usage. Keep `BlockTypeId::Waveshaper` working.

- [ ] **Step 1: Confirm it's truly dead** — `grep -rn "SvfFilter\|SVFFilter" src/ tests/ | grep -v "SVFFilterTests"`. Expected refs: the `BlockTypeId::SvfFilter` enum + `Layer.cpp` construction + the include + maybe `AlgorithmLibrary` (a comment). Confirm **no `Algorithm` in `AlgorithmLibrary.cpp` lists `BlockTypeId::SvfFilter` in `blockTypePerSlot`** — if one does, STOP and escalate (it's not dead).

- [ ] **Step 2: Remove construction + class** — in `Layer.cpp`, delete `palette_[(int) BlockTypeId::SvfFilter] = std::make_unique<SVFFilter>();` and `#include "dsp/blocks/SVFFilter.h"`. Delete `src/dsp/blocks/SVFFilter.{h,cpp}` and `tests/SVFFilterTests.cpp`. Remove their entries from `CMakeLists.txt` + `tests/CMakeLists.txt`.

- [ ] **Step 3: Handle the enum** — in `Algorithm.h`, renumber to close the gap (no preset compat):
    ```cpp
    enum class BlockTypeId : int { None = 0, Waveshaper = 1 };
    inline constexpr std::size_t kNumBlockTypes = 2;
    ```
    Then build and fix any breakage: every `BlockTypeId::SvfFilter` reference must be gone (Task-7/8 removed them); algorithm definitions that used index-2 Waveshaper now use index-1 — since they reference `BlockTypeId::Waveshaper` by name, they follow automatically. The `palette_` array shrinks to `kNumBlockTypes`.

- [ ] **Step 4: Build green** — `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | tail -3`. Expected `0 failed`. (If `AlgorithmTests`/`AlgorithmNameTests` referenced SvfFilter, update them.)

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "refactor: remove retired SVFFilter palette block (off the graph since v5)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 9: Filter-section UI — Moog controls, show/hide, rename

Surface Moog (Mode + bass), swap the model-specific block on selection, rename Routing → "Filter Routing".

**Files:**
- Modify: `src/PluginEditor.h` (new members), `src/PluginEditor.cpp` (controls, binding, `onChange` show/hide, `resized` layout, label rename, Notch already in the routing param items from Task 6)
- Test: manual via Windows CI build (UI has no unit test); a `ParamBinderTests` smoke if a binding id is asserted there.

**Interfaces:** Consumes the Task-3 Moog param ids + the Task-6 routing items. No new non-UI interface.

- [ ] **Step 1: Add Moog controls** — in `PluginEditor.h`, add members beside the spine ones: `juce::Label moogModeLbl_, moogWaveLbl_, moogOctaveLbl_; juce::ComboBox moogMode_, moogWave_, moogOctave_; LabeledKnob moogBass_;` (mirror the existing `LabeledKnob` usage for spine knobs). In `PluginEditor.cpp` setup, after the spine controls:
    ```cpp
    moogModeLbl_.setText("Mode", juce::dontSendNotification);
    moogMode_.addItemList(juce::StringArray{ "LP", "BP", "HP" }, 1);
    filterSection_.addAndMakeVisible(moogModeLbl_); filterSection_.addAndMakeVisible(moogMode_);
    moogWaveLbl_.setText("Wave", juce::dontSendNotification);
    moogWave_.addItemList(juce::StringArray{ "Sine", "Triangle", "Saw" }, 1);
    filterSection_.addAndMakeVisible(moogWaveLbl_); filterSection_.addAndMakeVisible(moogWave_);
    moogOctaveLbl_.setText("Octave", juce::dontSendNotification);
    moogOctave_.addItemList(juce::StringArray{ "0", "-1 oct", "-2 oct" }, 1);
    filterSection_.addAndMakeVisible(moogOctaveLbl_); filterSection_.addAndMakeVisible(moogOctave_);
    // moogBass_ (LabeledKnob) added + captioned "Bass" like the other spine knobs
    ```

- [ ] **Step 2: Bind them** — in the `binder_.bind(...)` block:
    ```cpp
    binder_.bind(moogMode_,   ids.spineMoogMode);
    binder_.bind(moogWave_,   ids.spineMoogBassWave);
    binder_.bind(moogOctave_, ids.spineMoogBassOctave);
    binder_.bind(moogBass_.slider(), ids.spineMoogBassAmount);   // mirror how other LabeledKnobs bind their slider
    ```

- [ ] **Step 3: Rename Routing → "Filter Routing"** — change `spineRoutingLbl_.setText("Routing", ...)` to `setText("Filter Routing", ...)` (~line 84). (The Notch item is already in the routing combo's list from Task 6's param-item change if the combo populates from the param; if the combo hard-codes its `addItemList`, add "Notch" after "HP" there too — line ~86.)

- [ ] **Step 4: Per-model show/hide** — add a private `void updateModelVisibility();` method to the editor class (the one declared in `PluginEditor.h` — use its actual name) and call it from `spineModel_.onChange` and at the end of `resized()`:
    ```cpp
    void /*EditorClass*/::updateModelVisibility() {
        const bool moog = (spineModel_.getSelectedItemIndex() == 1);
        // Moog-only:
        for (auto* c : { (juce::Component*)&moogModeLbl_, &moogMode_, &moogWaveLbl_, &moogWave_,
                         &moogOctaveLbl_, &moogOctave_, &moogBass_ }) c->setVisible(moog);
        // Huggett-only:
        for (auto* c : { (juce::Component*)&spineRoutingLbl_, &spineRouting_,
                         &postDriveLbl_, &postDrive_, &separationLbl_, &separation_ }) c->setVisible(!moog);
    }
    ```
    Wire `spineModel_.onChange = [this]{ updateModelVisibility(); resized(); };` and call `updateModelVisibility()` once at the end of setup. (Use the actual member names for Huggett's post-drive/separation controls — read the editor.)

- [ ] **Step 5: Layout** — in `resized()`, lay out the Moog controls in the same model-specific cell region the Huggett controls use (`layoutCells(fc, ...)` ~line 317), so the hidden group occupies the same area. Since visibility is toggled, both groups can be laid into the same rect; only the visible one shows.

- [ ] **Step 6: Build green + visual check** — `cmake --build build --target k2000_tests -j4` (UI compiles; tests still `0 failed`). Then trigger Windows CI for the real surface: `gh workflow run build.yml --ref feat/moog-spec2`.

- [ ] **Step 7: Commit**

```bash
git add src/PluginEditor.h src/PluginEditor.cpp
git commit -m "feat(ui): Moog filter controls + per-model show/hide; Routing -> Filter Routing

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 10: Huggett↔Moog hot-swap test + version bump + roadmap

Verify the live crossfade works with two real models, bump the version surface, update the roadmap.

**Files:**
- Modify: `tests/ModelHotSwapTests.cpp`; `CMakeLists.txt` (project `VERSION`); `src/PluginEditor.cpp` (panel version label if not already derived from `JucePlugin_VersionString`); `tools/roadmap-dashboard/roadmap.json`
- Test: `tests/ModelHotSwapTests.cpp`

- [ ] **Step 1: Add the failing test** — append to `tests/ModelHotSwapTests.cpp` a case that builds a `Layer`, plays a tone, switches `spineModel` 0→1 with a fade, and asserts the output stays finite + bounded across the swap (mirror the existing v5.1 hot-swap case's structure, now asserting Huggett→Moog specifically):

```cpp
beginTest("live crossfade Huggett <-> Moog stays finite and bounded");
{
    Layer layer; layer.prepare(48000.0, 64);
    Voice v; v.setLayer(&layer); v.prepare(48000.0, 64);
    ParamSnapshot s; s.spineModel = 0; s.svfCutoffHz = 800.0f; s.svfResonance = 0.5f; s.spineModelFadeMs = 25.0f;
    layer.updateParameters(s); v.noteOn(60, 1.0f);
    std::vector<float> l(512), r(512); bool finite = true; float peak = 0;
    auto blockRun = [&]{ std::fill(l.begin(),l.end(),0.0f); std::fill(r.begin(),r.end(),0.0f);
        v.render(l.data(), r.data(), 512);
        for (float x : l) { finite = finite && std::isfinite(x); peak = std::max(peak, std::abs(x)); } };
    blockRun();
    s.spineModel = 1; layer.updateParameters(s);            // switch to Moog -> fade
    for (int b = 0; b < 8; ++b) blockRun();
    expect(finite, "non-finite across Huggett->Moog crossfade");
    expect(peak < 4.0f, "crossfade output blew up: " + juce::String(peak));
}
```

(Match the test file's existing `Voice`/`Layer` setup helpers + `setLayer` API.)

- [ ] **Step 2: Run — expect PASS** (the v5.1 fade machinery already handles this; this is a regression guard now that Moog is a real 2nd model). If it FAILS, the slot's `setVoiceContext`/fade interaction is the suspect — fix the slot, not the test.

- [ ] **Step 3: Version bump** — in `CMakeLists.txt`, bump the project `VERSION` (e.g. the minor for a feature release). Confirm the panel label derives from `JucePlugin_VersionString` (`[[release-version-surface]]`); if it's a hard-coded string in `PluginEditor.cpp`, change it to read `JucePlugin_VersionString`.

- [ ] **Step 4: Roadmap** — in `tools/roadmap-dashboard/roadmap.json`, mark `v5.4` Moog shipped (2nd live filter model; hot-swap now live with two models). Edit on THIS branch (`[[feedback-roadmap-working-tree]]`).

- [ ] **Step 5: Full suite green + commit**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | tail -3`. Expected `0 failed`.

```bash
git add tests/ModelHotSwapTests.cpp CMakeLists.txt src/PluginEditor.cpp tools/roadmap-dashboard/roadmap.json
git commit -m "test(spine): Huggett<->Moog hot-swap guard; bump version + roadmap (Moog live)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Notes for the implementer

- **Tasks 1 and 6 carry behavioral oracles** (DSP); the rest are integration wiring whose tests assert routing/registration/plumbing. When a DSP test fails, fix the Cmajor/CALIB (Task 1) or `resolve()` (Task 6) — never the bound.
- **`-j4` always.** Bare `-j` OOMs the JUCE build.
- **Cmajor regen only in Task 1.** Tasks 2–10 are pure C++ — no Docker.
- **No preset compat anywhere** — remove dead params/blocks outright; no migration, no compat tests.
- **Read the adjacent code before mirroring a pattern** — exact helper/member names (the `id` struct fields, `ParamBinder::bind` overloads, the editor's Huggett post-drive/separation member names, `LabeledKnob::slider()`) come from the existing files; this plan cites locations, not every identifier.
- **UAT after Task 9/10:** trigger Windows CI (`gh workflow run build.yml --ref feat/moog-spec2`), load in Ableton, and audition Moog (mode, slope-in-all-modes, the bass voice) + the Huggett↔Moog morph — the first time Moog is playable.
- **Order note:** Tasks 1–6 each leave the suite green and are independently shippable; Tasks 7–8 (deletions) and 9 (UI) depend on 2–6 being in. 10 finalizes.
