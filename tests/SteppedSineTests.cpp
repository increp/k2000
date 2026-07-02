#include <juce_core/juce_core.h>
#include "testdsp/SteppedSine.h"
#include "testdsp/ProcessAdapter.h"
#include "../src/dsp/spine/NlSvfCell.h"
#include <cmath>

struct SteppedSineTests : public juce::UnitTest {
    SteppedSineTests() : juce::UnitTest("SteppedSine") {}
    void runTest() override {
        const double sr = 96000.0;

        beginTest("identity passthrough: 0 dB, ~0 phase across the grid");
        {
            struct Passthrough { void reset() {} void process(float*, int) {} };
            Passthrough p;
            std::vector<double> freqs { 100.0, 1000.0, 10000.0 };
            auto r = testdsp::SteppedSine::transfer(p, freqs, sr, 0.1f);
            expect(r.magDb.size() == freqs.size(), "one result per frequency");
            for (double m : r.magDb) expectWithinAbsoluteError(m, 0.0, 0.05);
            for (double ph : r.phaseRad) expectWithinAbsoluteError(ph, 0.0, 0.01);
        }

        beginTest("matches Cytomic LP analytic at fc=1000 (CellAdapter, res=0)");
        {
            const double fc = 1000.0, k = 2.0;   // res=0 -> Q=0.5 -> k=2.0 (see TestDspSelfTests)
            testdsp::CellAdapter ca; ca.cutoff = (float) fc; ca.res = 0.0f; ca.tap = NlSvfCell::LP;
            ca.prepare(sr);
            auto analyticDb = [&](double f) { const double u = f / fc, u2 = u * u;
                return -10.0 * std::log10((1.0 - u2) * (1.0 - u2) + k * k * u2); };
            std::vector<double> freqs { 100.0, 300.0, 700.0, 1000.0 };
            auto r = testdsp::SteppedSine::transfer(ca, freqs, sr, 0.05f);
            for (size_t i = 0; i < freqs.size(); ++i)
                expectWithinAbsoluteError(r.magDb[i], analyticDb(freqs[i]), 0.5);
        }
    }
};
static SteppedSineTests steppedSineTestsInstance;
