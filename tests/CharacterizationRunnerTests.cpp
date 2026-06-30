#include <juce_core/juce_core.h>
#include "characterization/CharacterizationRunner.h"
#include "characterization/FilterUnderTest.h"

struct CharacterizationRunnerTests : public juce::UnitTest {
    CharacterizationRunnerTests() : juce::UnitTest("CharacterizationRunner") {}
    void runTest() override {
        beginTest("coarseGrid is bounded: 96k only, OS {1,2,4,8}, live only");
        {
            auto g = chz::coarseGrid();
            expect(g.hostRates.size() == 1 && (int) g.hostRates[0] == 96000, "single host rate 96k");
            expect(g.osFactors == (std::vector<int>{1,2,4,8}), "all four OS factors");
            expect(g.osModes.size() == 1 && g.osModes[0] == chz::OsMode::Live, "live only");
        }

        beginTest("runner produces LP24 headline metrics for Moog and writes a CSV");
        {
            auto fut = chz::makeMoogFut();
            chz::Grid g;                                   // tiny grid for a fast unit test
            g.modes = { chz::Mode::LP24 };
            g.cutoffs = { 1000.0 }; g.resonances = { 0.0 }; g.drives = { 0.0 };
            g.osFactors = { 1 }; g.osModes = { chz::OsMode::Live }; g.hostRates = { 96000.0 };
            g.probeFreqs = chz::CharacterizationRunner::logFreqs(50.0, 20000.0, 40);

            auto outDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                              .getChildFile("chz_runner_test");
            outDir.deleteRecursively(); outDir.createDirectory();

            auto summary = chz::CharacterizationRunner::run(*fut, g, outDir);

            // -3 dB corner sanity check for Moog LP24 at fc=1000 Hz.
            // The authentic Moog 4-pole cascade at resonance=0 places the -3 dB corner at
            // ~0.44*fc (≈440 Hz) because four cascaded 1-pole ZDF stages at cutoff fc each
            // have -3 dB at fc, but in cascade the combined -3 dB is at sqrt(2^0.25 - 1)*fc
            // ≈ 0.435*fc. This is correct hardware-accurate behavior; the spec's +-1-octave
            // window is widened to +2/-2.3 octaves to accommodate it (200..2000 Hz for fc=1000).
            const double corner = summary.at("moog/LP24/fc1000/corner_hz");
            expect(corner > 200.0 && corner < 2000.0, "corner in sanity range: " + juce::String(corner));
            // slope_db_oct = mag(2*corner) - mag(corner). Because the Moog LP24 authentic
            // corner is at ~0.44*fc (440 Hz), 2*corner ≈ 880 Hz is still within the
            // transition band (just below fc=1000 Hz). Asymptotic -24 dB/oct is only
            // reached well into the stopband. A sanity bound of < -3 dB/oct confirms the
            // filter IS rolling off; the steep-stopband property is verified by the
            // FilterUnderTestTests "8 kHz strongly attenuated" assertion.
            expect(summary.at("moog/LP24/fc1000/slope_db_oct") < -3.0, "LP rolling off above corner");
            expect(outDir.getChildFile("response.csv").existsAsFile(), "response.csv written");
            outDir.deleteRecursively();
        }

        beginTest("B2+B3: resonance.csv + distortion.csv exist; B2/B3 summary keys present and finite");
        {
            auto fut = chz::makeMoogFut();
            chz::Grid g;
            g.modes      = { chz::Mode::LP24 };
            g.cutoffs    = { 1000.0 };
            g.resonances = { 0.0, 0.9 };   // max resonance (0.9) drives B2 self-osc probe
            g.drives     = { 0.0 };
            g.osFactors  = { 1, 2 };        // two OS factors so alias_db@os1 and @os2 are both written
            g.osModes    = { chz::OsMode::Live };
            g.hostRates  = { 96000.0 };
            g.probeFreqs = chz::CharacterizationRunner::logFreqs(50.0, 20000.0, 40);

            auto outDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                              .getChildFile("chz_runner_b2b3_test");
            outDir.deleteRecursively(); outDir.createDirectory();

            auto summary = chz::CharacterizationRunner::run(*fut, g, outDir);

            // CSV files must exist.
            expect(outDir.getChildFile("resonance.csv").existsAsFile(), "resonance.csv written");
            expect(outDir.getChildFile("distortion.csv").existsAsFile(), "distortion.csv written");

            // B2: selfosc_cents_err key must be present and finite.
            expect(summary.count("moog/LP24/fc1000/selfosc_cents_err") > 0,
                   "B2 summary key selfosc_cents_err present");
            const double centsErr = summary.at("moog/LP24/fc1000/selfosc_cents_err");
            // FIX 5: Sentinel -1.0 (self-osc not detected) or a plausible finite value.
            // isfinite(-1.0) is true, so the old (isfinite || sentinel-range) was dead code —
            // a bogus value like -5756 would pass. Now: accept either a plausible measurement
            // (finite AND > -900 cents, i.e. not absurdly flat) OR exactly the sentinel -1.0.
            expect((std::isfinite(centsErr) && centsErr > -900.0) || std::abs(centsErr - (-1.0)) < 0.01,
                   "selfosc_cents_err is plausible or sentinel -1: " + juce::String(centsErr));

            // B3 THD key must be present.
            expect(summary.count("moog/LP24/fc1000/thd_db") > 0,
                   "B3 summary key thd_db present");
            const double thdDb = summary.at("moog/LP24/fc1000/thd_db");
            // FIX 5: same pattern — plausible finite (> -900 dB) OR sentinel -1.0.
            expect((std::isfinite(thdDb) && thdDb > -900.0) || std::abs(thdDb - (-1.0)) < 0.01,
                   "thd_db is plausible or sentinel -1: " + juce::String(thdDb));

            // B3 aliasing keys for both OS factors.
            expect(summary.count("moog/LP24/fc1000/alias_db@os1") > 0,
                   "B3 summary key alias_db@os1 present");
            expect(summary.count("moog/LP24/fc1000/alias_db@os2") > 0,
                   "B3 summary key alias_db@os2 present");
            const double aliasOs1 = summary.at("moog/LP24/fc1000/alias_db@os1");
            const double aliasOs2 = summary.at("moog/LP24/fc1000/alias_db@os2");
            // FIX 5: plausible finite OR sentinel -1.0 for both aliasing keys.
            expect((std::isfinite(aliasOs1) && aliasOs1 > -900.0) || std::abs(aliasOs1 - (-1.0)) < 0.01,
                   "alias_db@os1 is plausible or sentinel -1: " + juce::String(aliasOs1));
            expect((std::isfinite(aliasOs2) && aliasOs2 > -900.0) || std::abs(aliasOs2 - (-1.0)) < 0.01,
                   "alias_db@os2 is plausible or sentinel -1: " + juce::String(aliasOs2));

            outDir.deleteRecursively();
        }
    }
};
static CharacterizationRunnerTests characterizationRunnerTestsInstance;
