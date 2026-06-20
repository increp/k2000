#include <juce_core/juce_core.h>
#include "testdsp/SignalGen.h"
#include "testdsp/Spectrum.h"

class TestDspSelfTests : public juce::UnitTest {
public:
    TestDspSelfTests() : juce::UnitTest("TestDspSelf") {}
    void runTest() override {
        beginTest("bin-aligned tone is a single FFT bin");
        const int N = 1 << 14, bin = 75;
        auto x = testdsp::SignalGen::binAlignedSine(1.0f, bin, N);
        auto mag = testdsp::Spectrum::magnitude(x);
        // fundamental bin dominates; neighbours are ~0 (numerical floor).
        const float fund = mag[(size_t) bin];
        float other = 0.0f;
        for (int b = 2; b < (int) mag.size(); ++b) if (b != bin) other = std::max(other, mag[(size_t) b]);
        expect(fund > 1.0f, "fundamental present: " + juce::String(fund));
        expect(other < fund * 1.0e-4f, "no leakage: other=" + juce::String(other));

        beginTest("rms of unit sine is ~0.707");
        expectWithinAbsoluteError(testdsp::Spectrum::rms(x), 0.70710677f, 1.0e-3f);

        beginTest("allFinite catches NaN");
        std::vector<float> bad { 0.0f, std::nanf(""), 1.0f };
        expect(! testdsp::Spectrum::allFinite(bad));
    }
};
static TestDspSelfTests testDspSelfTestsInstance;
