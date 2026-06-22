#include <juce_core/juce_core.h>
#include "../src/dsp/spine/cmajor/NlSvfAdapter.h"
#include "../src/dsp/spine/NlSvfCell.h"
#include <cmath>
#include <vector>

struct NlSvfEquivalenceTests : public juce::UnitTest {
    NlSvfEquivalenceTests() : juce::UnitTest("NlSvfEquivalence") {}
    static constexpr double kSR = 48000.0;

    // Per-sample max-abs error between the Cmajor adapter and NlSvfCell for one config.
    static double maxErr(double f, float amp, float cutoff, float resAndSat, int tap) {
        const int n = 8192;
        std::vector<float> a((size_t)n), b((size_t)n);
        for (int i = 0; i < n; ++i)
            a[(size_t)i] = b[(size_t)i] = amp * (float) std::sin(2.0*juce::MathConstants<double>::pi*f*i/kSR);

        NlSvfAdapter ad; ad.prepare(kSR); ad.reset();
        ad.setParams(cutoff, resAndSat, resAndSat, tap);
        ad.process(a.data(), n);

        NlSvfCell c; c.prepare(kSR); c.reset();
        c.setCutoff(cutoff); c.setResonance(resAndSat); c.setResSat(resAndSat);
        for (int i = 0; i < n; ++i) { float l = b[(size_t)i], r = l; c.process(l, r, tap); b[(size_t)i] = l; }

        double m = 0.0;
        for (int i = 0; i < n; ++i) m = std::max(m, (double) std::abs(a[(size_t)i] - b[(size_t)i]));
        return m;
    }

    void runTest() override {
        beginTest("Cmajor NlSvf matches NlSvfCell (resonance saturator on), per-sample");
        const float cutoffs[] = { 250.0f, 1000.0f, 4000.0f };
        const float rs[]      = { 0.3f, 0.7f, 0.95f };   // resonance == resSat (engages the nonlinearity)
        const float amps[]    = { 0.1f, 0.5f, 0.9f };    // level-dependent: sweep amplitude
        const int   taps[]    = { NlSvfAdapter::LP, NlSvfAdapter::HP, NlSvfAdapter::BP };
        double worst = 0.0;
        for (float cut : cutoffs) for (float r : rs) for (float amp : amps) for (int tap : taps) {
            const double m = maxErr(1000.0, amp, cut, r, tap);
            worst = std::max(worst, m);
            expect(std::isfinite(m), "finite output");
            // Padé saturator is pure arithmetic; only tan() per-recompute is transcendental.
            // If this bound proves flaky from tan()/FMA drift through the loop, switch to a
            // harmonic-amplitude comparison (see spec section 4 step 1) and record the change.
            expect(m < 2.0e-3, "per-sample within 2e-3 (cut " + juce::String(cut,0)
                   + " res/sat " + juce::String(r,2) + " amp " + juce::String(amp,2)
                   + " tap " + juce::String(tap) + "): max err " + juce::String(m, 7));
        }
        logMessage("NlSvf worst per-sample error: " + juce::String(worst, 8));
    }
};
static NlSvfEquivalenceTests nlSvfEquivalenceTestsInstance;
