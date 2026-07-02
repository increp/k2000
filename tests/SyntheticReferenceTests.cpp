#include <juce_core/juce_core.h>
#include "testdsp/Level.h"
#include "testdsp/SignalGen.h"
#include <vector>
#include <cmath>
#include "characterization/ReferenceDevices.h"
#include "characterization/CharacterizationRunner.h"   // logFreqs
#include "testdsp/SteppedSine.h"
#include "testdsp/Harmonics.h"

// M2 trust gates: the ruler + level extractors must recover known answers.

struct SyntheticReferenceTests : public juce::UnitTest {
    SyntheticReferenceTests() : juce::UnitTest("SyntheticReference") {}

    void runTest() override {
        using namespace chz;

        beginTest("ruler recovers an analytic biquad's exact response (<= 0.1 dB in-band)");
        AnalyticBiquad bq;
        OperatingPoint op;
        op.cutoffHz       = 1000.0;
        op.resonance      = 0.4737;      // -> Q ~= 5.0 (0.5 + 0.4737*9.5)
        op.hostSampleRate = 48000.0;
        op.osFactor       = 1;
        bq.setOperatingPoint(op);

        auto probes = CharacterizationRunner::logFreqs(50.0, 20000.0, 100);
        auto r = testdsp::SteppedSine::transfer(bq, probes, 48000.0, 0.5f);

        double worst = 0.0, truePeak = -300.0;
        for (size_t i = 0; i < probes.size(); ++i) {
            const double truth = bq.trueMagDb(probes[i]);
            worst    = std::max(worst, std::abs(r.magDb[i] - truth));
            truePeak = std::max(truePeak, truth);
        }
        expect(worst < 0.1, "measured vs analytic |H| within 0.1 dB at every probe");

        beginTest("peak-gain extractor matches the analytic peak (<= 0.2 dB)");
        const double measPeak = testdsp::Level::peakGainDb(r.magDb);
        expectWithinAbsoluteError(measPeak, truePeak, 0.2);

        beginTest("THD ruler recovers a cubic's closed-form THD (<= 0.5 dB)");
        CubicNonlinearity cubic;
        cubic.c3 = 0.5f;                 // y = x + 0.5*x^3 -> pure 3rd harmonic
        const double measThd = testdsp::Harmonics::thdDb(cubic, 1000.0, 48000.0, 0.5f);
        const double trueThd = cubic.trueThdDb(0.5f);   // ~ -30.88 dB
        expectWithinAbsoluteError(measThd, trueThd, 0.5);
    }
};

struct LevelExtractorTests : public juce::UnitTest {
    LevelExtractorTests() : juce::UnitTest("LevelExtractors") {}

    void runTest() override {
        using testdsp::Level;

        beginTest("peak / passband gain reductions over a response");
        std::vector<double> magDb { -3.0, 0.0, 6.0, 0.0, -12.0 };
        expectWithinAbsoluteError(Level::peakGainDb(magDb), 6.0, 1.0e-9);
        expectWithinAbsoluteError(Level::passbandGainDb(magDb, Level::Passband::Low),  -3.0,  1.0e-9);
        expectWithinAbsoluteError(Level::passbandGainDb(magDb, Level::Passband::High), -12.0, 1.0e-9);

        beginTest("dBFS on a DC signal is exact (0.5 -> -6.0206 dBFS, crest 0)");
        auto dcBuf = testdsp::SignalGen::dc(0.5f, 1024);
        expectWithinAbsoluteError(Level::peakDbfs(dcBuf), -6.0206, 0.01);
        expectWithinAbsoluteError(Level::rmsDbfs(dcBuf),  -6.0206, 0.01);
        expectWithinAbsoluteError(Level::crestFactorDb(dcBuf), 0.0, 0.01);

        beginTest("crest factor of a sine is ~3.01 dB (sqrt(2))");
        auto sineBuf = testdsp::SignalGen::sine(0.5f, 1000.0, 48000.0, 4096);
        expectWithinAbsoluteError(Level::crestFactorDb(sineBuf), 3.0103, 0.2);

        beginTest("noise-floor RMS recovered (uniform amp 0.1 -> amp/sqrt(3))");
        auto noise = testdsp::SignalGen::whiteNoise(0.1f, 16384, 1234u);
        const double expectedDbfs = 20.0 * std::log10(0.1 / std::sqrt(3.0));  // ~ -24.77
        expectWithinAbsoluteError(testdsp::Level::rmsDbfs(noise), expectedDbfs, 0.5);

        beginTest("empty input returns the -300 dB no-data sentinel (not silence/DC)");
        std::vector<double> emptyResp;
        std::vector<float>  emptyBuf;
        expectWithinAbsoluteError(Level::peakGainDb(emptyResp),   -300.0, 1.0e-9);
        expectWithinAbsoluteError(Level::peakDbfs(emptyBuf),      -300.0, 1.0e-9);
        expectWithinAbsoluteError(Level::rmsDbfs(emptyBuf),       -300.0, 1.0e-9);
        expectWithinAbsoluteError(Level::crestFactorDb(emptyBuf), -300.0, 1.0e-9);
    }
};

static SyntheticReferenceTests syntheticReferenceTestsInstance;
static LevelExtractorTests levelExtractorTestsInstance;
