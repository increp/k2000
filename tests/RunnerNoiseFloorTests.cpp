#include <juce_core/juce_core.h>
#include "characterization/CharacterizationRunner.h"
#include "characterization/FilterUnderTest.h"
#include <cmath>

// §4.1 "idle noise floor": silence in at the base operating point, absolute
// output level out — flat dBFS beside the A-weighted lens. A clean digital
// filter idles far below -60 dBFS; a change that makes a filter hum at idle
// must trip this.

struct RunnerNoiseFloorTests : public juce::UnitTest {
    RunnerNoiseFloorTests() : juce::UnitTest("RunnerNoiseFloor") {}

    void runTest() override {
        using namespace chz;
        beginTest("summary records noise_floor_dbfs + noise_floor_dbfsA (Moog LP24)");
        auto moog = makeMoogFut();
        Grid g;
        g.modes       = { Mode::LP24 };
        g.cutoffs     = { 1000.0 };
        g.resonances  = { 0.0 };
        g.drives      = { 0.0 };
        g.osFactors   = { 1 };
        g.osModes     = { OsMode::Live };
        g.hostRates   = { 96000.0 };
        g.probeFreqs  = CharacterizationRunner::logFreqs(50.0, 20000.0, 20);

        auto outDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("chz_noise_floor_test");
        outDir.deleteRecursively();
        outDir.createDirectory();
        auto s = CharacterizationRunner::run(*moog, g, outDir);

        expect(s.count("moog/LP24/fc1000/noise_floor_dbfs")  == 1, "flat key present");
        expect(s.count("moog/LP24/fc1000/noise_floor_dbfsA") == 1, "A-weighted key present");
        const double flat = s.at("moog/LP24/fc1000/noise_floor_dbfs");
        const double wtd  = s.at("moog/LP24/fc1000/noise_floor_dbfsA");
        expect(flat > -300.5 && wtd > -300.5, "values are data, not missing");
        expect(flat < -60.0, "a clean digital filter idles below -60 dBFS");
        outDir.deleteRecursively();
    }
};

static RunnerNoiseFloorTests runnerNoiseFloorTestsInstance;
