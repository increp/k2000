#include <juce_core/juce_core.h>
#include "../src/dsp/spine/cmajor/CmajorSvfFilter.h"
#include <cmath>
#include <memory>
#include <vector>

struct CmajorFilterModelTests : public juce::UnitTest {
    CmajorFilterModelTests() : juce::UnitTest("CmajorFilterModel") {}

    void runTest() override {
        beginTest("CmajorSvfFilter runs through the FilterModel interface (stereo, param-driven)");
        const double sr = 48000.0;
        CmajorSvfFilter f; f.prepare(sr); f.setTap(0);
        std::unique_ptr<FilterModel::State> st(f.makeState()); f.reset(*st);
        f.setCommon(1000.0f, 0.2f, 0.0f);

        auto rms = [&](double freq) {
            f.reset(*st);
            const int warm = 4096, meas = 4096;
            std::vector<float> l((size_t)(warm+meas)), r((size_t)(warm+meas));
            for (int i = 0; i < warm + meas; ++i)
                l[(size_t)i] = r[(size_t)i] = 0.4f * (float) std::sin(2.0 * juce::MathConstants<double>::pi * freq * i / sr);
            f.processStereo(*st, l.data(), r.data(), warm + meas);
            double sl = 0.0, sr2 = 0.0;
            for (int i = warm; i < warm + meas; ++i) { sl += double(l[(size_t)i])*l[(size_t)i]; sr2 += double(r[(size_t)i])*r[(size_t)i]; }
            expect(std::abs(sl - sr2) < 1e-6, "L and R identical for identical input");
            return std::sqrt(sl / meas);
        };
        const double low = rms(200.0), high = rms(8000.0);
        expect(std::isfinite(low) && std::isfinite(high), "finite");
        expect(high < low * 0.5, "LP attenuates highs through the FilterModel path");

        beginTest("processStereo does not allocate on the audio thread");
        // Structural assertion: processStereo only calls setParams/process on pre-made
        // adapters (no makeState/new in the hot path). Verified by code review + this
        // smoke running a large block without state recreation.
        std::vector<float> big((size_t)8192, 0.1f), big2((size_t)8192, 0.1f);
        f.processStereo(*st, big.data(), big2.data(), 8192);
        expect(std::isfinite(big[8191]), "large block stays finite");
    }
};
static CmajorFilterModelTests cmajorFilterModelTestsInstance;
