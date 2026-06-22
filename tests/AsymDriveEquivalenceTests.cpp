#include <juce_core/juce_core.h>
#include "../src/dsp/spine/cmajor/AsymDriveAdapter.h"
#include "../src/dsp/spine/AsymSaturator.h"
#include <cmath>
#include <vector>

struct AsymDriveEquivalenceTests : public juce::UnitTest {
    AsymDriveEquivalenceTests() : juce::UnitTest("AsymDriveEquivalence") {}
    static constexpr double kSR = 48000.0;

    void runTest() override {
        beginTest("Cmajor AsymDrive matches AsymSaturator");
        const float drives[] = { 0.0f, 0.25f, 0.5f, 1.0f };
        const float bias = 0.25f, maxDb = 30.0f;
        const int n = 8192;
        double worst = 0.0;
        for (float d : drives) {
            std::vector<float> a((size_t)n), b((size_t)n);
            for (int i = 0; i < n; ++i)
                a[(size_t)i] = b[(size_t)i] = 0.6f * (float) std::sin(2.0*juce::MathConstants<double>::pi*220.0*i/kSR);

            AsymDriveAdapter ad; ad.prepare(kSR); ad.reset(); ad.setParams(d, bias, maxDb);
            ad.process(a.data(), n);

            AsymSaturator s; s.setDrive(d, bias, maxDb);
            for (int i = 0; i < n; ++i) b[(size_t)i] = s.process(b[(size_t)i]);

            double m = 0.0;
            for (int i = 0; i < n; ++i) m = std::max(m, (double) std::abs(a[(size_t)i] - b[(size_t)i]));
            worst = std::max(worst, m);
            expect(std::isfinite(m), "finite");
            expect(m < 1.0e-3, "per-sample within 1e-3 at drive " + juce::String(d,2)
                   + ": max err " + juce::String(m, 7));
        }
        logMessage("AsymDrive worst per-sample error: " + juce::String(worst, 8));
        // If a drive level exceeds 1e-3 purely from tanh/pow intrinsic differences (output still
        // sane, error tiny+smooth), that is the documented harmonic-fallback case: replace the
        // per-sample assert with a harmonic-amplitude comparison (first ~8 harmonics within ~0.5 dB)
        // and record the tanh/pow last-bit behavior in the report (primary ADR evidence).
    }
};
static AsymDriveEquivalenceTests asymDriveEquivalenceTestsInstance;
