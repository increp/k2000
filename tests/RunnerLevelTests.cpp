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
