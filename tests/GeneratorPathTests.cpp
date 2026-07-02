#include <juce_core/juce_core.h>
#include "characterization/ReferenceDevices.h"
#include "characterization/CharacterizationRunner.h"
#include "testdsp/AWeighting.h"
#include <cmath>

// The Generator/Trigger path end-to-end: a calibrated tone of KNOWN absolute
// level, measured through the abstract DeviceUnderTest base by the runner's
// Trigger driver. Peak reads the set dBFS; RMS sits 3.01 dB under it; the
// A-weighted figure at ~1 kHz matches flat (A(1 kHz) = 0 dB).

struct GeneratorPathTests : public juce::UnitTest {
    GeneratorPathTests() : juce::UnitTest("GeneratorPath") {}

    void runTest() override {
        using namespace chz;

        beginTest("CalibratedToneRef declares Generator / Trigger");
        CalibratedToneRef tone;
        DeviceUnderTest& dut = tone;
        expect(dut.kind() == DeviceKind::Generator);
        expect(dut.excitation() == Excitation::Trigger);
        expectEquals(dut.name(), juce::String("ref_tone"));

        beginTest("runner Trigger driver recovers the calibrated level");
        tone.setToneDbfs(-18.0);
        Grid g;
        g.cutoffs   = { 1000.0 };       // tone frequency (Generator convention)
        g.hostRates = { 48000.0 };
        auto outDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("chz_gen_path_test");
        outDir.deleteRecursively();
        outDir.createDirectory();
        auto s = CharacterizationRunner::run(dut, g, outDir);

        expect(s.count("ref_tone/gen/peak_dbfs") == 1, "peak key present");
        expect(s.count("ref_tone/gen/rms_dbfs")  == 1, "rms key present");
        expect(s.count("ref_tone/gen/rms_dbfsA") == 1, "rms(A) key present");
        expectWithinAbsoluteError(s.at("ref_tone/gen/peak_dbfs"), -18.0,   0.05);
        expectWithinAbsoluteError(s.at("ref_tone/gen/rms_dbfs"),  -21.01,  0.05);
        // ~1 kHz: A(f) ~ 0, so the weighted figure matches flat within the lens tol.
        expectWithinAbsoluteError(s.at("ref_tone/gen/rms_dbfsA"),
                                  s.at("ref_tone/gen/rms_dbfs"), 0.3);

        expect(outDir.getChildFile("emission.csv").existsAsFile(), "emission.csv written");
        outDir.deleteRecursively();
    }
};

static GeneratorPathTests generatorPathTestsInstance;
