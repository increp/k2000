#include <juce_core/juce_core.h>
#include "characterization/CharacterizationRunner.h"
#include "characterization/FilterUnderTest.h"
#include "testdsp/Gate.h"
#include "testdsp/GoldenIO.h"

// ---------------------------------------------------------------------------
// CharacterizationGateTests
//
// Fast always-on CI gate for Moog and Huggett filters.
//
// Gate grid: LP24 only, fc=1000 Hz, res={0.0,0.9}, drive=0.0, osFactor=1
// (Live), hostSR=96000. 40 log-spaced probe freqs 50-20 kHz. ~3 ESS
// operating points total, adds ~5 sec to k2000_tests (measured: 76 vs 71 sec).
//
// What the gate asserts per model:
//  1. Spec: LP24 fc1000 slope_db_oct <= -3.0 (rolling off above the corner).
//  2. Spec: LP24 fc1000 method_delta_db <= 1.0 (in-band dual-method agreement).
//  3. Self-golden: corner_hz, slope_db_oct, method_delta_db (all at res=0 base).
//     Moog additionally: selfosc_cents_err (from res=0.9 B2 measurement).
//
// Baseline CSVs: tests/golden/moog/baseline.csv, tests/golden/huggett/baseline.csv.
// Regenerate with: BERNIE_UPDATE_GOLDEN=1 ./build/tests/k2000_tests
// ---------------------------------------------------------------------------

struct CharacterizationGateTests : public juce::UnitTest {
    CharacterizationGateTests() : juce::UnitTest("CharacterizationGate") {}

    void gateModel(const juce::String& modelName, bool includeSelfOsc) {
        // --- Build tiny gate grid (NOT coarseGrid — that is 10-25 min) ---
        chz::Grid g;
        g.modes      = { chz::Mode::LP24 };
        g.cutoffs    = { 1000.0 };
        // res=0.9 so B2 self-osc runs (Moog). Huggett: still include 0.9 so B2
        // runs but we only golden its B1 metrics (skip selfosc golden for Huggett).
        g.resonances = { 0.0, 0.9 };
        g.drives     = { 0.0 };
        // osFactors={1} only — OS-tier coverage already provided by the
        // "aliasing decreases as OS factor rises" test in CharacterizationRunnerTests.
        g.osFactors  = { 1 };
        g.osModes    = { chz::OsMode::Live };
        g.hostRates  = { 96000.0 };
        g.probeFreqs = chz::CharacterizationRunner::logFreqs(50.0, 20000.0, 40);

        auto fut = (modelName == "moog") ? chz::makeMoogFut() : chz::makeHuggettFut();

        auto outDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("chz_gate_" + modelName);
        outDir.deleteRecursively();
        outDir.createDirectory();

        auto s = chz::CharacterizationRunner::run(*fut, g, outDir);

        // --- Spec gate 1: LP24 fc1000 is rolling off above its -3 dB corner ---
        // slope_db_oct = mag(2*corner) - mag(corner); negative means attenuation.
        // Known measured: Moog ~-7 dB/oct, Huggett similar. Threshold -3.0 confirms
        // the filter is rolling off (any steeper than -3 dB/oct passes).
        beginTest(modelName + ": LP24 fc1000 slope_db_oct <= -3.0 (rolling off)");
        testdsp::Gate::check(*this, s.at(modelName + "/LP24/fc1000/slope_db_oct"),
                             -3.0, testdsp::Gate::Dir::Max, modelName + " LP24 slope");

        // --- Spec gate 2: method-agreement < 1 dB in the gate grid ---
        // method_delta_db is the in-band (within 40 dB of passband peak) max|delta|
        // between stepped-sine and ESS. Known measured: Moog ~0.31 dB.
        beginTest(modelName + ": LP24 fc1000 method_delta_db <= 1.0 (in-band dual-method agreement)");
        testdsp::Gate::check(*this, s.at(modelName + "/LP24/fc1000/method_delta_db"),
                             1.0, testdsp::Gate::Dir::Max, modelName + " method delta");

        // --- Self-golden: headline metrics must not drift from committed baselines ---
        beginTest(modelName + ": self-golden (LP24/fc1000 headline metrics)");
        testdsp::GoldenSet gs(modelName + "/baseline");

        // B1 metrics: corner_hz and slope_db_oct. Summary keys are keyed at base
        // resonance (res=0.0), so the grid's min resonance is the source here.
        gs.check(*this, "LP24/fc1000/corner_hz",
                 s.at(modelName + "/LP24/fc1000/corner_hz"),      50.0);
        gs.check(*this, "LP24/fc1000/slope_db_oct",
                 s.at(modelName + "/LP24/fc1000/slope_db_oct"),    2.0);
        gs.check(*this, "LP24/fc1000/method_delta_db",
                 s.at(modelName + "/LP24/fc1000/method_delta_db"), 0.5);

        // B2 self-osc cents error: only for models where it is meaningful.
        // Moog LP24 at res=0.9 self-oscillates; Huggett does not reliably.
        if (includeSelfOsc) {
            gs.check(*this, "LP24/fc1000/selfosc_cents_err",
                     s.at(modelName + "/LP24/fc1000/selfosc_cents_err"), 50.0);
        }

        gs.flush();

        // Clean up temp dir.
        outDir.deleteRecursively();
    }

    void runTest() override {
        gateModel("moog",    /*includeSelfOsc=*/ true);
        gateModel("huggett", /*includeSelfOsc=*/ false);
    }
};
static CharacterizationGateTests characterizationGateTestsInstance;
