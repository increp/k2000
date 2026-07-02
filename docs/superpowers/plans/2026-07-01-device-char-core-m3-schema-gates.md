# Device-Char Core — Milestone 3: Level in Summary + Regression Gates — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the harness's automatic summary and CI regression-gate actually *use* the level axis — record peak & passband gain (at the resonance where the disparity lives), gate them so a hot/quiet regression trips CI (closing the "Huggett resonance is ungated" hole), assert the Huggett-vs-Moog disparity as a golden number, and rename the opt-in runner to `k2000_device_characterization`.

**Architecture:** `runB1OnePoint` already computes the stepped-sine magnitude response; add `testdsp::Level` reductions to it and return them in `B1Result`. `run()` stores `peak_gain_db` + `passband_gain_db` at the **max-resonance** base operating point (a new deterministic summary key, alongside the existing corner/slope which stay at base resonance). The always-on gate goldens the new level keys per model and adds one cross-model disparity assertion. The exe rename is mechanical (CMake + skill + docs). No shipping DSP changes.

**Tech Stack:** C++17, JUCE 8 (`juce::UnitTest`, `testdsp::GoldenSet`), CMake. Namespace `chz`.

## Global Constraints

- **C++17**, JUCE 8, CMake. Namespace `chz`; level reductions from `testdsp::Level` (M2).
- **Bounded build parallelism: `-j4`** — never bare `-j`.
- **This milestone MAY modify the production runner** `CharacterizationRunner` and the gate — but ONLY additively for level metrics. It must NOT change existing metric values (corner/slope/method_delta/selfosc/thd/alias stay byte-for-byte), so existing goldens do not churn. Verify by diffing the committed golden CSVs (only new rows appear).
- **No shipping DSP changes** (voicing held for hardware).
- **Level summary keys:** `peak_gain_db` and `passband_gain_db`, stored at the **max resonance** in the grid, at the base operating point (osFactor=1, `OsMode::Live`, `hostRate=baseHost`). Absolute gain in dB (0 dB = unity), from `testdsp::Level` over the stepped-sine `magDb`.
- **Passband anchor by mode:** LP/LP12 → `Level::Passband::Low` (DC side); HP → `Level::Passband::High`; BP/Notch → `Level::Passband::Low` (approximate; documented).
- **Golden regeneration:** `BERNIE_UPDATE_GOLDEN=1 ./build/tests/k2000_tests` writes `tests/golden/{moog,huggett}/baseline.csv`; commit the updated baselines.
- Dev loop: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests`. Current baseline: `240 tests, 0 failed`.
- Commit messages end with `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`. If a message trips bash quoting, write it to a file and `git commit -F`.

## Milestone sequence (context)

Milestone 3 of SP-A (spec: `docs/superpowers/specs/2026-07-01-device-characterization-core-design.md`, §6 recording, §7 exe). Builds on M2's `testdsp::Level`. **Deliberately deferred (YAGNI):** the full device-typed key=value param-block schema generalization + hardware-reference columns (§6.1) land in **SP-C/SP-D** when oscillators/hardware actually need non-filter columns — M3 keeps the current filter-shaped CSV and only adds level to the summary. Reading the real Huggett large-signal +72 behavior under drive/multi-level excitation is **SP-B**; M3 surfaces only the small-signal resonant peak the ruler already measures.

---

## File Structure

- **Modify** `tests/characterization/CharacterizationRunner.h` — add `peakGainDb`, `passbandGainDb` to the private `B1Result` struct.
- **Modify** `tests/characterization/CharacterizationRunner.cpp` — `#include "../testdsp/Level.h"`; compute the two reductions in `runB1OnePoint`; store them at the max-res base point in `run()`.
- **Create** `tests/RunnerLevelTests.cpp` — a focused test that the runner now emits `peak_gain_db`/`passband_gain_db` and that they match `testdsp::Level` applied to the response.
- **Modify** `tests/CharacterizationGateTests.cpp` — golden the two level keys per model; add one cross-model disparity assertion.
- **Modify** `tests/golden/moog/baseline.csv` and `tests/golden/huggett/baseline.csv` — regenerated with the new keys.
- **Modify** `tests/CMakeLists.txt` — add `RunnerLevelTests.cpp`; rename target `k2000_filter_characterization` → `k2000_device_characterization`.
- **Modify** `.claude/skills/characterize-filter/SKILL.md` and `docs/filter-validation/running.md` — update the exe name.

---

## Task 1: Runner records peak & passband gain at max resonance

**Files:**
- Modify: `tests/characterization/CharacterizationRunner.h:45-49` (B1Result)
- Modify: `tests/characterization/CharacterizationRunner.cpp` (include; runB1OnePoint ~L253-257; run ~L537-578)
- Create: `tests/RunnerLevelTests.cpp`
- Modify: `tests/CMakeLists.txt` (add the test source)

**Interfaces:**
- Consumes: `testdsp::Level::{peakGainDb,passbandGainDb,Passband}` (M2), `chz::makeMoogFut`, `chz::CharacterizationRunner::{run,logFreqs}`, `chz::Grid`.
- Produces: summary keys `"<model>/<mode>/fc<cutoff>/peak_gain_db"` and `".../passband_gain_db"` (absolute dB, at max grid resonance, base operating point).

- [ ] **Step 1: Write the failing test**

Create `tests/RunnerLevelTests.cpp`:

```cpp
#include <juce_core/juce_core.h>
#include "characterization/CharacterizationRunner.h"
#include "characterization/FilterUnderTest.h"
#include "testdsp/Level.h"
#include <cmath>

using namespace chz;

// The runner must SURFACE the absolute level the ruler already measures: peak
// (resonant) gain and passband gain at the grid's max resonance. Moog LP24 at
// res=0.9 loses passband (~-13 dB, authentic ladder bass loss) and peaks only
// mildly — so peak > passband and passband is well below 0 dB. This is the
// smallest assertion that fails if level is not recorded at max resonance.
struct RunnerLevelTests : public juce::UnitTest {
    RunnerLevelTests() : juce::UnitTest("RunnerLevel") {}

    void runTest() override {
        beginTest("summary records peak_gain_db and passband_gain_db (LP24, Moog)");
        auto moog = makeMoogFut();
        Grid g;
        g.modes       = { Mode::LP24 };
        g.cutoffs     = { 1000.0 };
        g.resonances  = { 0.0, 0.9 };     // max res = 0.9 is where level is stored
        g.drives      = { 0.0 };
        g.osFactors   = { 1 };
        g.osModes     = { OsMode::Live };
        g.hostRates   = { 96000.0 };
        g.probeFreqs  = CharacterizationRunner::logFreqs(20.0, 24000.0, 200);

        auto outDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("chz_runner_level_test");
        outDir.deleteRecursively(); outDir.createDirectory();
        auto s = CharacterizationRunner::run(*moog, g, outDir);

        expect(s.count("moog/LP24/fc1000/peak_gain_db") == 1,     "peak_gain_db key present");
        expect(s.count("moog/LP24/fc1000/passband_gain_db") == 1, "passband_gain_db key present");

        const double peak = s.at("moog/LP24/fc1000/peak_gain_db");
        const double pass = s.at("moog/LP24/fc1000/passband_gain_db");
        expect(std::isfinite(peak) && std::isfinite(pass), "level metrics finite");
        // Authentic Moog ladder at res=0.9: passband droops well below unity, peak sits above passband.
        expect(pass < -6.0,   "Moog passband droops at high res (authentic bass loss)");
        expect(peak > pass,   "peak gain exceeds passband gain");
        outDir.deleteRecursively();
    }
};

static RunnerLevelTests runnerLevelTestsInstance;
```

Add `RunnerLevelTests.cpp` to the `k2000_tests` target in `tests/CMakeLists.txt`, immediately after `SyntheticReferenceTests.cpp`:

```cmake
    DeviceUnderTestTests.cpp
    SyntheticReferenceTests.cpp
    RunnerLevelTests.cpp
    characterization/FilterUnderTest.cpp
    characterization/CharacterizationRunner.cpp)
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | grep -E "RunnerLevel|FAIL|Summary:"`
Expected: builds, but the `RunnerLevel` test **FAILS** — `peak_gain_db key present` is false (the runner does not emit level keys yet), so the suite reports failures (`Summary: 240 tests, N failed` with N>0).

- [ ] **Step 3: Compute and store the level metrics**

In `tests/characterization/CharacterizationRunner.cpp`, add the include near the other testdsp includes (after `#include "../testdsp/Harmonics.h"`):

```cpp
#include "../testdsp/Level.h"
```

In `tests/characterization/CharacterizationRunner.h`, extend `B1Result` (currently `cornerHz`, `slopeDbOct`, `methodDeltaDb`):

```cpp
    struct B1Result {
        double cornerHz      = -1.0;
        double slopeDbOct    = -1.0;
        double methodDeltaDb = -1.0;
        double peakGainDb     = -300.0;
        double passbandGainDb = -300.0;
    };
```

In `runB1OnePoint` (`CharacterizationRunner.cpp`), just before `B1Result result;` near the end (the block that currently sets `result.cornerHz/...`), compute the reductions from the stepped-sine response `st.magDb` with a mode-aware passband anchor:

```cpp
    // Absolute-level reductions (M3): peak = resonant peak gain; passband = gain at
    // the mode's passband anchor. st.magDb is absolute gain (output/input) in dB.
    const double peakGainDb = testdsp::Level::peakGainDb(st.magDb);
    const testdsp::Level::Passband anchor =
        (op.mode == Mode::HP) ? testdsp::Level::Passband::High
                              : testdsp::Level::Passband::Low;   // LP/LP12/BP/Notch -> Low (BP/Notch approximate)
    const double passbandGainDb = testdsp::Level::passbandGainDb(st.magDb, anchor);
```

and set them on the result:

```cpp
    B1Result result;
    result.cornerHz      = cornerHz;
    result.slopeDbOct    = slopeDbOct;
    result.methodDeltaDb = methodDeltaDb;
    result.peakGainDb     = peakGainDb;
    result.passbandGainDb = passbandGainDb;
    return result;
```

In `run()` (`CharacterizationRunner.cpp`), inside the `for (double resonance : ...)` nest, ADD a second base-point capture for level at **max** resonance, right after the existing `if (isBase) { ... }` block (which stays untouched at base resonance):

```cpp
                                // M3: level metrics are stored at MAX resonance (where the
                                // resonant peak and the authentic passband droop actually appear),
                                // at the same os=1/Live/baseHost base point for deterministic keys.
                                const bool isLevelBase = (osFactor == 1)
                                                      && (osMode == OsMode::Live)
                                                      && (std::abs(resonance - maxRes) < 1.0e-9)
                                                      && (std::abs(hostRate - baseHost) < 0.5);
                                if (isLevelBase) {
                                    const double pk = std::isfinite(b1r.peakGainDb)     ? b1r.peakGainDb     : -300.0;
                                    const double pb = std::isfinite(b1r.passbandGainDb) ? b1r.passbandGainDb : -300.0;
                                    summary[keyBase + "/peak_gain_db"]     = pk;
                                    summary[keyBase + "/passband_gain_db"] = pb;
                                }
```

(`maxRes` is already computed earlier in `run()`. When the grid has a single resonance, `baseRes == maxRes` and both captures fire at the same point — still correct.)

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | grep -E "RunnerLevel|FAIL|Summary:"`
Expected: `[PASS] RunnerLevel: ...` and `Summary: 241 tests, 0 failed` (240 + 1 new block).

- [ ] **Step 5: Confirm existing metrics did NOT change (no golden churn)**

Run: `git diff --stat tests/golden/` — expected: **no changes** (this task did not touch goldens; existing corner/slope/etc. values are unchanged because the `if (isBase)` block was not modified). If `git diff tests/golden/` shows any change, STOP — an existing metric was altered; revert and investigate.

- [ ] **Step 6: Commit**

```bash
git add tests/characterization/CharacterizationRunner.h tests/characterization/CharacterizationRunner.cpp tests/RunnerLevelTests.cpp tests/CMakeLists.txt
git commit -m "feat(chz): runner records peak_gain_db + passband_gain_db at max resonance" -m "Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 2: Level-regression gate + Huggett-vs-Moog disparity assertion

**Files:**
- Modify: `tests/CharacterizationGateTests.cpp`
- Modify: `tests/golden/moog/baseline.csv`, `tests/golden/huggett/baseline.csv` (regenerated)

**Interfaces:**
- Consumes: the `peak_gain_db` / `passband_gain_db` summary keys from Task 1; `testdsp::GoldenSet`.
- Produces: golden coverage of the two level keys per model; a cross-model assertion that Huggett's resonant peak is far hotter than Moog's.

**Context (current gate):** `CharacterizationGateTests.cpp` has `void gateModel(const juce::String& modelName, bool includeSelfOsc)` run for `"moog"` then `"huggett"` from `runTest()`. It builds a tiny grid (`modes={LP24}, cutoffs={1000}, resonances={0.0,0.9}, os={1}, Live, 96000`), runs the runner, and self-goldens headline metrics via `testdsp::GoldenSet gs(modelName + "/baseline"); gs.check(*this, "LP24/fc1000/<key>", s.at(...), tol); gs.flush();`.

- [ ] **Step 1: Write the failing test**

In `tests/CharacterizationGateTests.cpp`, change `gateModel` to RETURN the measured peak gain (so `runTest` can compare across models), and golden the two new level keys. Change the signature:

```cpp
    double gateModel(const juce::String& modelName, bool includeSelfOsc) {
```

Inside `gateModel`, after the existing `gs.check(...)` calls for corner/slope/method_delta (and before `gs.flush();`), add golden checks for the level keys:

```cpp
        // Level-regression golden (M3): peak & passband gain at max res (0.9). Closes the
        // "resonance is ungated" hole — a change that makes a filter hotter/quieter now trips CI.
        gs.check(*this, "LP24/fc1000/peak_gain_db",
                 s.at(modelName + "/LP24/fc1000/peak_gain_db"),      2.0);
        gs.check(*this, "LP24/fc1000/passband_gain_db",
                 s.at(modelName + "/LP24/fc1000/passband_gain_db"),  1.0);
```

At the end of `gateModel`, after `gs.flush();` and the temp-dir cleanup, return the peak gain:

```cpp
        const double peakGainDb = s.at(modelName + "/LP24/fc1000/peak_gain_db");
        outDir.deleteRecursively();
        return peakGainDb;
```

(Move the existing `outDir.deleteRecursively();` so the return is last, or keep cleanup then return — ensure the temp dir is removed before returning.)

Change `runTest()` to capture both and assert the disparity:

```cpp
    void runTest() override {
        const double moogPeak = gateModel("moog",    /*includeSelfOsc=*/ true);
        const double hugPeak  = gateModel("huggett", /*includeSelfOsc=*/ false);

        beginTest("Huggett resonant peak is far hotter than Moog (known level disparity)");
        // In-box, LP24 res=0.9, drives off: Huggett stacks two high-Q SVF peaks (~+60..+72 dB)
        // while Moog's ladder is internally bounded (~+5 dB). This asserts the disparity is
        // real and gated as a number — it would trip if the two models were ever gain-matched.
        expect(hugPeak - moogPeak > 30.0,
               "Huggett peak should exceed Moog peak by >30 dB at res=0.9");
    }
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | grep -E "CharacterizationGate|FAIL|Summary:"`
Expected: **FAILS** — `GoldenSet` has no committed baseline for `peak_gain_db`/`passband_gain_db`, so `gs.check` reports a missing-golden failure (the `CharacterizationGate` self-golden test fails).

- [ ] **Step 3: Regenerate the goldens**

Run: `BERNIE_UPDATE_GOLDEN=1 ./build/tests/k2000_tests 2>&1 | grep -E "CharacterizationGate|Summary:"`
This writes the new `peak_gain_db` / `passband_gain_db` rows into `tests/golden/moog/baseline.csv` and `tests/golden/huggett/baseline.csv` (existing rows unchanged). Then inspect them:

Run: `git diff tests/golden/`
Expected: ONLY additive rows for `LP24/fc1000/peak_gain_db` and `LP24/fc1000/passband_gain_db` in each model's baseline. Sanity-check the values: Moog `peak_gain_db` ≈ +5 dB, `passband_gain_db` ≈ −13 dB; Huggett `peak_gain_db` ≈ +60..+72 dB. If any pre-existing row changed, STOP and investigate.

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | grep -E "CharacterizationGate|FAIL|Summary:"`
Expected: `[PASS] CharacterizationGate: ...` including the new disparity block; `Summary: 242 tests, 0 failed` (241 + 1 new disparity block).

- [ ] **Step 5: Commit**

```bash
git add tests/CharacterizationGateTests.cpp tests/golden/moog/baseline.csv tests/golden/huggett/baseline.csv
git commit -m "feat(chz): gate level metrics + assert Huggett-vs-Moog peak disparity" -m "Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 3: Rename the opt-in runner to `k2000_device_characterization`

**Files:**
- Modify: `tests/CMakeLists.txt` (target name + the comment header)
- Modify: `.claude/skills/characterize-filter/SKILL.md` (build/run commands)
- Modify: `docs/filter-validation/running.md` (exe name references)

**Interfaces:** none (build-target + docs rename only; the binary's CLI flags are unchanged).

- [ ] **Step 1: Rename the CMake target**

In `tests/CMakeLists.txt`, replace every occurrence of `k2000_filter_characterization` with `k2000_device_characterization` (the `add_executable`, all `target_*` calls, `set_target_properties`, and the comment header at "k2000_filter_characterization — opt-in heavy runner"). There are 6 occurrences.

- [ ] **Step 2: Verify the renamed target builds**

Run: `cmake --build build --target k2000_device_characterization -j4`
Expected: configures (CMake re-runs because CMakeLists changed) and builds cleanly; produces `build/tests/k2000_device_characterization`.

- [ ] **Step 3: Update the skill and docs references**

In `.claude/skills/characterize-filter/SKILL.md`, replace `k2000_filter_characterization` with `k2000_device_characterization` in the build target and run commands (grep the file first: `grep -n k2000_filter_characterization .claude/skills/characterize-filter/SKILL.md`). Do the same in `docs/filter-validation/running.md` (`grep -n k2000_filter_characterization docs/filter-validation/running.md`). Replace every occurrence in both files.

- [ ] **Step 4: Confirm no stale references remain**

Run: `grep -rn "k2000_filter_characterization" tests/ docs/ .claude/ 2>/dev/null`
Expected: **no output** (all references renamed). If any remain (outside `.git`), update them.

- [ ] **Step 5: Verify the unit suite is unaffected**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | grep -E "Summary:"`
Expected: `Summary: 242 tests, 0 failed` (the rename does not touch `k2000_tests`).

- [ ] **Step 6: Commit**

```bash
git add tests/CMakeLists.txt .claude/skills/characterize-filter/SKILL.md docs/filter-validation/running.md
git commit -m "refactor(chz): rename opt-in runner to k2000_device_characterization" -m "Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Self-Review

**1. Spec coverage (M3 scope).** §6.2 "summary headline keys added for level metrics at their relevant operating point (peak gain at high resonance — closing the baseRes=0 blind spot)" → Task 1 (stored at `maxRes`). §6.2 "level-regression gates so a hotter/quieter change is caught" + "inter-device Δ" → Task 2 (goldens + disparity assertion). §7 "k2000_device_characterization (generalized from k2000_filter_characterization)" → Task 3. Correctly deferred: device-typed param-block schema generalization + hardware-reference columns → SP-C/SP-D (stated in Milestone sequence); large-signal/drive reading → SP-B.

**2. Placeholder scan.** No TBD/TODO. Each code step shows exact edits; each run step has a command + expected result, including exact test counts (240 → 241 → 242) and the golden-diff sanity values. The "revert and investigate" branches are guard rails, not placeholders.

**3. Type consistency.** `B1Result.peakGainDb`/`passbandGainDb` (Task 1 header) are set in `runB1OnePoint` and read in `run()` under `isLevelBase`, producing keys `peak_gain_db`/`passband_gain_db` — the same strings asserted in `RunnerLevelTests` (Task 1) and goldened in the gate (Task 2). `testdsp::Level::{peakGainDb,passbandGainDb,Passband::{Low,High}}` match the M2 header. `gateModel` returning `double` is consistent between its definition and the two calls in `runTest()`.

---

## Execution Handoff

Two execution options:
1. **Subagent-Driven** — fresh subagent per task + spec/quality review each (as M2). Task 2 touches goldens, so its reviewer should confirm only additive golden rows.
2. **Inline Execution** — execute in this session with checkpoints.
