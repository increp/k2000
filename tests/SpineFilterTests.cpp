#include <juce_core/juce_core.h>
#include "../src/dsp/spine/TptSvfCell.h"
#include <cmath>

// Measure steady-state magnitude of a cell at a probe frequency.
static float cellMagAt(TptSvfCell& cell, int tap, double sr, double freqHz) {
    cell.reset();
    const int N = 8192;
    float peak = 0.0f;
    for (int i = 0; i < N; ++i) {
        const float x = std::sin(2.0 * juce::MathConstants<double>::pi * freqHz * i / sr);
        float l = x, r = x;
        cell.process(l, r, tap);
        if (i > N / 2) peak = std::max(peak, std::abs(l));
    }
    return peak;  // ~amplitude (input amplitude 1.0)
}

class SpineFilterTests : public juce::UnitTest {
public:
    SpineFilterTests() : juce::UnitTest("SpineFilter") {}
    void runTest() override {
        beginTest("LP cell passes lows, attenuates highs");
        TptSvfCell cell;
        cell.prepare(48000.0);
        cell.setCutoff(1000.0f);
        cell.setResonance(0.0f);
        const float lowMag  = cellMagAt(cell, TptSvfCell::LP, 48000.0, 100.0);
        const float highMag = cellMagAt(cell, TptSvfCell::LP, 48000.0, 10000.0);
        expect(lowMag > 0.7f, "low passes: " + juce::String(lowMag));
        expect(highMag < 0.1f, "high cut: " + juce::String(highMag));
    }
};
static SpineFilterTests spineFilterTestsInstance;
