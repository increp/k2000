#include <juce_core/juce_core.h>
#include "testdsp/AWeighting.h"
#include "testdsp/SignalGen.h"
#include "testdsp/Level.h"
#include <cmath>
#include <vector>

// The A-weighting lens must match the published IEC 61672 curve (table anchors)
// and, applied to a single bin-aligned tone, must shift the flat RMS by exactly
// the curve value at that frequency (single-bin Parseval identity).

struct AWeightingTests : public juce::UnitTest {
    AWeightingTests() : juce::UnitTest("AWeighting") {}

    void runTest() override {
        using testdsp::AWeighting;

        beginTest("curve matches IEC 61672 table anchors");
        expectWithinAbsoluteError(AWeighting::aWeightDb(1000.0),     0.0, 0.1);
        expectWithinAbsoluteError(AWeighting::aWeightDb(100.0),    -19.1, 0.3);
        expectWithinAbsoluteError(AWeighting::aWeightDb(10000.0),   -2.5, 0.3);

        beginTest("single bin-aligned tone: weighted RMS = flat RMS + A(f)");
        const double sr = 48000.0;
        const int    N  = 1 << 14;
        // ~1 kHz (bin 341) and ~100 Hz (bin 34) — both bin-aligned, leak-free.
        for (int bin : { 341, 34 }) {
            const double f = (double) bin * sr / N;
            auto tone = testdsp::SignalGen::binAlignedSine(0.25f, bin, N);
            const double flat = testdsp::Level::rmsDbfs(tone);
            const double wtd  = AWeighting::aWeightedRmsDbfs(tone, sr);
            expectWithinAbsoluteError(wtd, flat + AWeighting::aWeightDb(f), 0.1);
        }

        beginTest("white noise: A-weighted RMS is below flat RMS");
        auto noise = testdsp::SignalGen::whiteNoise(0.1f, 1 << 14, 4242u);
        expect(AWeighting::aWeightedRmsDbfs(noise, 48000.0)
                 < testdsp::Level::rmsDbfs(noise),
               "A-curve must attenuate broadband noise overall");

        beginTest("empty input / f<=0 return the -300 sentinel");
        std::vector<float> empty;
        expectWithinAbsoluteError(AWeighting::aWeightedRmsDbfs(empty, 48000.0), -300.0, 1.0e-9);
        expectWithinAbsoluteError(AWeighting::aWeightDb(0.0),                   -300.0, 1.0e-9);
    }
};

static AWeightingTests aWeightingTestsInstance;
