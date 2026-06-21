#include <juce_core/juce_core.h>
#include "../src/dsp/spine/cmajor/SvfLinearAdapter.h"
#include "../src/dsp/spine/NlSvfCell.h"
#include <cmath>
#include <vector>

struct CmajorSvfEquivalenceTests : public juce::UnitTest {
    CmajorSvfEquivalenceTests() : juce::UnitTest("CmajorSvfEquivalence") {}
    static constexpr double kSR = 48000.0;

    static double adapterDb(double f, float cutoff, float res, int tap) {
        SvfLinearAdapter a; a.prepare(kSR); a.reset(); a.setParams(cutoff, res, tap);
        return measureDb(f, [&](float* buf, int n){ a.process(buf, n); });
    }
    static double nlsvfDb(double f, float cutoff, float res, int tap) {
        NlSvfCell c; c.prepare(kSR); c.reset();
        c.setCutoff(cutoff); c.setResonance(res); c.setResSat(0.0f);  // linear core only
        return measureDb(f, [&](float* buf, int n){
            for (int i = 0; i < n; ++i) { float l = buf[i], r = buf[i]; c.process(l, r, tap); buf[i] = l; }
        });
    }
    template <typename Proc>
    static double measureDb(double f, Proc&& proc) {
        const int warm = 8192, meas = 8192;
        std::vector<float> buf((size_t)(warm + meas));
        for (int i = 0; i < warm + meas; ++i)
            buf[(size_t)i] = 0.3f * (float) std::sin(2.0 * juce::MathConstants<double>::pi * f * i / kSR);
        std::vector<float> in = buf;
        proc(buf.data(), warm + meas);
        double inSq = 0.0, outSq = 0.0;
        for (int i = warm; i < warm + meas; ++i) { inSq += double(in[(size_t)i])*in[(size_t)i]; outSq += double(buf[(size_t)i])*buf[(size_t)i]; }
        return 20.0 * std::log10(std::max(1e-7, std::sqrt(outSq / inSq)));
    }

    void runTest() override {
        beginTest("Cmajor SVF matches NlSvfCell linear core within 0.5 dB");
        const float cutoffs[] = { 250.0f, 1000.0f, 4000.0f };
        const double freqs[]  = { 100, 300, 1000, 3000, 8000 };
        const int taps[] = { SvfLinearAdapter::LP, SvfLinearAdapter::HP, SvfLinearAdapter::BP };
        for (float cut : cutoffs)
            for (int tap : taps)
                for (double f : freqs) {
                    const double a = adapterDb(f, cut, 0.3f, tap);
                    const double n = nlsvfDb(f, cut, 0.3f, tap);
                    expect(std::abs(a - n) < 0.5,
                        "cut " + juce::String(cut,0) + " tap " + juce::String(tap)
                        + " f " + juce::String(f,0) + ": adapter " + juce::String(a,2)
                        + " vs NlSvf " + juce::String(n,2) + " dB");
                }
    }
};
static CmajorSvfEquivalenceTests cmajorSvfEquivalenceTestsInstance;
