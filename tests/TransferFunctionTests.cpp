#include <juce_core/juce_core.h>
#include "testdsp/TransferFunction.h"
#include "testdsp/SignalGen.h"
#include <cmath>

struct TransferFunctionTests : public juce::UnitTest {
    TransferFunctionTests() : juce::UnitTest("TransferFunction") {}
    void runTest() override {
        const double sr = 96000.0;

        beginTest("unit impulse -> 0 dB flat, ~0 phase");
        {
            auto ir = testdsp::SignalGen::impulse(1.0f, 4096);
            std::vector<double> freqs { 100.0, 1000.0, 10000.0 };
            auto r = testdsp::TransferFunction::fromImpulse(ir, sr, freqs);
            for (double m : r.magDb) expectWithinAbsoluteError(m, 0.0, 1.0e-3);
        }

        beginTest("pure delay -> group delay equals the delay");
        {
            const int D = 24;                          // 24-sample delay
            std::vector<float> ir(4096, 0.0f); ir[(size_t) D] = 1.0f;
            std::vector<double> freqs { 500.0, 1000.0, 2000.0, 4000.0 };
            auto r = testdsp::TransferFunction::fromImpulse(ir, sr, freqs);
            const double expected = (double) D / sr;   // seconds
            for (double g : r.groupDelaySec) expectWithinAbsoluteError(g, expected, 1.0e-6);
        }
    }
};
static TransferFunctionTests transferFunctionTestsInstance;
