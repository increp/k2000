#include <juce_core/juce_core.h>
#include "../src/dsp/spine/TptSvfCell.h"
#include "../src/dsp/spine/HuggettFilter.h"
#include "../src/dsp/spine/SpineFilterSlot.h"
#include <cmath>
#include <memory>

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

        beginTest("Huggett 24 dB LP attenuates an octave above cutoff more than 12 dB");
        {
            HuggettFilter h;
            h.prepare(48000.0);
            std::unique_ptr<FilterModel::State> st(h.makeState());
            h.setMode(HuggettFilter::Mode::LP);
            h.setCommon(1000.0f, 0.0f, 0.0f);
            h.setSeparation(0.0f);

            auto magAtSlope = [&](HuggettFilter::Slope slope) {
                h.setSlope(slope);
                h.reset(*st);
                const int N = 8192; float peak = 0.0f;
                for (int i = 0; i < N; ++i) {
                    float x = std::sin(2.0 * juce::MathConstants<double>::pi * 2000.0 * i / 48000.0);
                    float l = x, r = x;
                    h.processStereo(*st, &l, &r, 1);
                    if (i > N / 2) peak = std::max(peak, std::abs(l));
                }
                return peak;
            };
            const float m12 = magAtSlope(HuggettFilter::Slope::db12);
            const float m24 = magAtSlope(HuggettFilter::Slope::db24);
            expect(m24 < m12, "24 dB steeper: 12=" + juce::String(m12) + " 24=" + juce::String(m24));
        }

        beginTest("SpineFilterSlot filters using the active model");
        {
            HuggettFilter h; h.prepare(48000.0); h.setMode(HuggettFilter::Mode::LP);
            h.setSlope(HuggettFilter::Slope::db24); h.setCommon(500.0f, 0.0f, 0.0f);
            SpineFilterSlot slot;
            slot.prepare(48000.0, 512, &h, nullptr);
            const int N = 8192; float peak = 0.0f;
            for (int i = 0; i < N; ++i) {
                float x = std::sin(2.0 * juce::MathConstants<double>::pi * 8000.0 * i / 48000.0);
                float l = x, r = x;
                slot.processStereo(nullptr, false, &h, &l, &r, 1);
                if (i > N / 2) peak = std::max(peak, std::abs(l));
            }
            expect(peak < 0.1f, "high freq cut by spine: " + juce::String(peak));
        }
    }
};
static SpineFilterTests spineFilterTestsInstance;
