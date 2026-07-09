# VCO Section Pre-Work: Drive/Mix + Moog Bass Knob Removal Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove the Drive/Mix waveshaper knobs and the Moog Bass sub-osc controls (Wave/Octave/Bass) from the visible panel, without touching any of the params, DSP, or bindings they drive.

**Architecture:** Pure GUI-layer change confined entirely to `src/PluginEditor.cpp`. JUCE only renders a `Component` that has been added as a child of a visible parent (via `addAndMakeVisible`/`addChildComponent`) — so removing just the `addAndMakeVisible(...)` call for a control hides it completely while leaving the control object, its APVTS binding, and everything downstream fully intact and still responding to automation/host state. The only other edits needed are trimming the now-dead cells out of two layout helper calls (`updateModelVisibility()`'s visibility array and one `layoutCells(...)` row in `resized()`) so the remaining, still-visible controls reclaim the freed space instead of leaving a blank gap.

**Tech Stack:** JUCE (vendored `third_party/JUCE`), C++17, CMake.

## Global Constraints

- Build with `-j4` always — bare `-j` OOMs the JUCE compile (0-byte object, confusing link error).
- Do not touch any param (`Parameters.h`/`.cpp`), `ParamSnapshot`, DSP (`Waveshaper`, `MoogLadder`, Cmajor sources), or `binder_.bind(...)` call — every removal in this plan is `addAndMakeVisible`-level only, per the approved spec (`docs/superpowers/specs/2026-07-09-vco-prework-removals-design.md`).
- GUI changes are verified live in the running Standalone build, not just by compiling or running the unit suite — per this repo's standing rule that UI changes need eyes-on confirmation.
- Suite must stay green (`cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests`) with **zero test file changes** — nothing at the param/DSP layer moves in this plan, so if a test needs editing, that's a signal something touched the wrong layer.

---

## Task 1: Hide Drive/Mix (Source section) and Moog Wave/Octave/Bass (Filter section) knobs

**Files:**
- Modify: `src/PluginEditor.cpp` (five edits, detailed below — `buildStaticControls()`, `updateModelVisibility()`, `resized()`)

**Interfaces:**
- Consumes: nothing new — every symbol touched (`shaperDrive_`, `shaperMix_`, `moogWave_`, `moogWaveLbl_`, `moogOctave_`, `moogOctaveLbl_`, `moogBass_`, `moogMode_`, `moogModeLbl_`, `algo_`, `algoLbl_`, `layoutCells`) already exists in `PluginEditor.h`/`.cpp` today.
- Produces: nothing new — no new symbols, no new params, no new files. Downstream code (bindLayer, DSP) is unaffected and needs no changes.

- [ ] **Step 1: Confirm the current (before) state builds and shows all five controls**

```bash
cmake --build build --target k2000_Standalone -j4
```

Run the Standalone binary (`build/k2000_artefacts/Release/Standalone/Bernie` — confirmed via `find build -iname "*Standalone*"` against this repo's actual Release build layout; `build/` is Release by this repo's convention) and confirm, before making any change: the Source section shows a "Drive" knob and a "Mix" knob; with the Filter section's model dropdown set to Moog, the Filter section shows Mode, Wave, Octave, and Bass controls. This is the baseline the rest of this task removes.

- [ ] **Step 2: Edit 1 — stop adding Drive/Mix as visible children of the Source section**

In `src/PluginEditor.cpp`, inside `buildStaticControls()`, find:

```cpp
    algo_.addItemList(params::algoNames(), 1);
    addToSource(algoLbl_); addToSource(algo_);
    addToSource(shaperDrive_); addToSource(shaperMix_);
```

Replace with:

```cpp
    algo_.addItemList(params::algoNames(), 1);
    addToSource(algoLbl_); addToSource(algo_);
```

- [ ] **Step 3: Edit 2 — give Algo the full row width instead of leaving Drive/Mix's blank cells**

This edit is in `resized()`, not `buildStaticControls()`. Find:

```cpp
        layoutCells(sc,  { { &algoLbl_, &algo_ }, { nullptr, &shaperDrive_ }, { nullptr, &shaperMix_ } });
```

Replace with:

```cpp
        layoutCells(sc,  { { &algoLbl_, &algo_ } });
```

- [ ] **Step 4: Edit 3 — stop adding Moog Wave/Octave/Bass as visible children of the Filter section**

In `buildStaticControls()`, find:

```cpp
    moogWaveLbl_.setText("Wave", juce::dontSendNotification);
    moogWaveLbl_.setJustificationType(juce::Justification::centred);
    moogWave_.addItemList(juce::StringArray{ "Sine", "Triangle", "Saw" }, 1);
    filterSection_.addAndMakeVisible(moogWaveLbl_);
    filterSection_.addAndMakeVisible(moogWave_);
    moogOctaveLbl_.setText("Octave", juce::dontSendNotification);
    moogOctaveLbl_.setJustificationType(juce::Justification::centred);
    moogOctave_.addItemList(juce::StringArray{ "0", "-1 oct", "-2 oct" }, 1);
    filterSection_.addAndMakeVisible(moogOctaveLbl_);
    filterSection_.addAndMakeVisible(moogOctave_);
    filterSection_.addAndMakeVisible(moogBass_);
```

Replace with:

```cpp
    moogWaveLbl_.setText("Wave", juce::dontSendNotification);
    moogWaveLbl_.setJustificationType(juce::Justification::centred);
    moogWave_.addItemList(juce::StringArray{ "Sine", "Triangle", "Saw" }, 1);
    moogOctaveLbl_.setText("Octave", juce::dontSendNotification);
    moogOctaveLbl_.setJustificationType(juce::Justification::centred);
    moogOctave_.addItemList(juce::StringArray{ "0", "-1 oct", "-2 oct" }, 1);
```

(The `setText`/`addItemList` setup calls are harmless to keep on an unparented component and cost nothing — only the five `filterSection_.addAndMakeVisible(...)` calls actually control visibility, and all five are gone.)

- [ ] **Step 5: Edit 4 — trim the dead controls out of `updateModelVisibility()`'s array**

Find:

```cpp
    juce::Component* moogControls[] = { &moogModeLbl_,  &moogMode_,
                                        &moogWaveLbl_,  &moogWave_,
                                        &moogOctaveLbl_, &moogOctave_,
                                        &moogBass_ };
```

Replace with:

```cpp
    juce::Component* moogControls[] = { &moogModeLbl_, &moogMode_ };
```

- [ ] **Step 6: Edit 5 — give Mode the full row width in the Moog-only layout row**

In `resized()`, find:

```cpp
            layoutCells(fc,  { { &moogModeLbl_,    &moogMode_ },
                                { &moogWaveLbl_,    &moogWave_ },
                                { &moogOctaveLbl_,  &moogOctave_ },
                                { nullptr,          &moogBass_ } });
```

Replace with:

```cpp
            layoutCells(fc,  { { &moogModeLbl_, &moogMode_ } });
```

- [ ] **Step 7: Build**

```bash
cmake --build build --target k2000_Standalone k2000_tests -j4
```

Expected: clean build, no errors, no warnings about unused variables (the five controls are still used — just never added to a parent — so no new dead-code warnings should appear).

- [ ] **Step 8: Run the full suite — expect green with no changes needed**

```bash
./build/tests/k2000_tests | tee build/last-test-run.log | tail -1
```

Expected: same pass count as before this change (this plan does not add, remove, or modify any test). If anything fails, stop — a failure here means something outside the five edits above was touched, which contradicts the plan's scope.

- [ ] **Step 9: Live-verify in the Standalone (the actual proof for a GUI change)**

Run the Standalone binary again:
- Source section: no Drive knob, no Mix knob visible. Algo dropdown occupies the full width of its row (no blank gap where Drive/Mix used to be).
- Filter section, model set to Moog: only Mode is visible (no Wave/Octave/Bass), and Mode occupies the full width of that row.
- Filter section, model set to Huggett: unchanged from before (Huggett's routing/separation/post-drive controls were never touched).
- Turn the (now invisible) Drive/Mix and Moog Bass automation on from a host or via APVTS state load, if convenient, to confirm the underlying params still respond — not required, but confirms "keep the code" held.

- [ ] **Step 10: Commit**

```bash
git add src/PluginEditor.cpp
git commit -m "$(cat <<'EOF'
feat(gui): hide Drive/Mix and Moog Wave/Octave/Bass knobs

Removes the addAndMakeVisible() calls for the Source section's Drive/Mix
waveshaper knobs and the Filter section's Moog Wave/Octave/Bass sub-osc
controls. Params, DSP, and APVTS bindings are untouched -- this is a
pure visibility change ahead of the Source section rebuild
(docs/superpowers/specs/2026-07-09-three-vco-blend-design.md).

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

## Plan Self-Review

**Spec coverage:** Spec §2A (Drive/Mix knob visibility) → Steps 2-3. Spec §2B (Moog Bass knob visibility) → Steps 4-6. Spec §4 verification (suite green + live GUI check) → Steps 8-9. Spec's "leave everything else untouched" constraint → Global Constraints + Step 8's zero-test-changes expectation. No spec requirement without a corresponding step.

**Placeholder scan:** No TBD/TODO/"add appropriate handling" language — every step shows exact before/after code or an exact command with an expected result.

**Type/name consistency:** All five edits reference symbols exactly as they appear in the current `src/PluginEditor.cpp` (confirmed by reading the full file before writing this plan, not from memory) — `addToSource`, `layoutCells`, `filterSection_`, `moogControls[]`, and every control/label name match their existing declarations in `PluginEditor.h` verbatim.
