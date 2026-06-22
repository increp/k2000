#include <juce_core/juce_core.h>
#include "../src/dsp/spine/cmajor/SvfLinearAdapter.h"
#include <cmath>
#include <vector>

struct CmajorSvfAdapterTests : public juce::UnitTest {
    CmajorSvfAdapterTests() : juce::UnitTest("CmajorSvfAdapter") {}

    static double rmsAt(double freq, float cutoff, int tap) {
        const double sr = 48000.0;
        SvfLinearAdapter a; a.prepare(sr); a.reset();
        a.setParams(cutoff, 0.0f, tap);
        const int warm = 4096, meas = 4096;
        std::vector<float> buf((size_t)(warm + meas));
        for (int i = 0; i < warm + meas; ++i)
            buf[(size_t)i] = 0.5f * (float) std::sin(2.0 * juce::MathConstants<double>::pi * freq * i / sr);
        a.process(buf.data(), warm + meas);
        double sum = 0.0; for (int i = warm; i < warm + meas; ++i) sum += double(buf[(size_t)i]) * buf[(size_t)i];
        return std::sqrt(sum / meas);
    }

    void runTest() override {
        beginTest("adapter LP passes lows, attenuates highs");
        const double low  = rmsAt(200.0,  1000.0f, SvfLinearAdapter::LP);
        const double high = rmsAt(8000.0, 1000.0f, SvfLinearAdapter::LP);
        expect(std::isfinite(low) && std::isfinite(high), "finite output");
        expect(low > 0.1, "low passes (rms " + juce::String(low, 4) + ")");
        expect(high < low * 0.5, "high attenuated below low (low " + juce::String(low,4)
               + " high " + juce::String(high,4) + ")");
    }
};
static CmajorSvfAdapterTests cmajorSvfAdapterTestsInstance;
