#include <juce_core/juce_core.h>
#include "../src/dsp/spine/HuggettFilter.h"
#include <cmath>
#include <memory>

// Regression for the "Separation clicks when going through 0" bug. A click is an
// abrupt output discontinuity; here it shows as a passband LEVEL jump between
// sep=0 and a hair off zero. With a continuous (click-free) Separation the
// passband level must be essentially unchanged across that boundary.
struct HuggettSeparationClickTests : public juce::UnitTest {
    HuggettSeparationClickTests() : juce::UnitTest("HuggettSeparationClick") {}
    static constexpr double kSR = 48000.0;

    // Steady-state passband RMS (linear) of an LP/HP at probe f with given mode/slope/sep.
    static double passRms(HuggettFilter::Mode mode, HuggettFilter::Slope slope, float sep, double f) {
        HuggettFilter h; h.prepare(kSR);
        h.setMode(mode); h.setSlope(slope); h.setSeparation(sep);
        h.setCommon(1000.0f, 0.0f, 0.0f); h.setPostDrive(0.0f);
        std::unique_ptr<FilterModel::State> st(h.makeState()); h.reset(*st);
        const int warm = 8192, meas = 8192;
        double outSq = 0.0;
        for (int i = 0; i < warm + meas; ++i) {
            const float x = 0.3f * (float) std::sin(2.0 * juce::MathConstants<double>::pi * f * i / kSR);
            float l = x, r = x; h.processStereo(*st, &l, &r, 1);
            if (i >= warm) outSq += double(l) * l;
        }
        return std::sqrt(outSq / meas);
    }

    void runTest() override {
        beginTest("LP/HP separation is continuous (click-free) across sep=0 in both slopes");
        struct Case { HuggettFilter::Mode mode; double f; const char* name; };
        // Probe deep in each filter's passband (LP: below cutoff; HP: above cutoff),
        // where a single->parallel-sum topology switch shows as a level jump.
        const Case cases[] = { { HuggettFilter::Mode::LP, 200.0,  "LP" },
                               { HuggettFilter::Mode::HP, 6000.0, "HP" } };
        for (auto slope : { HuggettFilter::Slope::db12, HuggettFilter::Slope::db24 }) {
            const char* sname = (slope == HuggettFilter::Slope::db12) ? "12dB" : "24dB";
            for (auto c : cases) {
                const double atZero = passRms(c.mode, slope, 0.0f,  c.f);
                const double atEps  = passRms(c.mode, slope, 0.02f, c.f);
                const double jumpDb = 20.0 * std::log10(std::max(1e-7, atEps) / std::max(1e-7, atZero));
                expect(std::abs(jumpDb) < 1.0,
                       juce::String(c.name) + " " + sname + ": passband jumps "
                       + juce::String(jumpDb, 2) + " dB across sep 0->0.02 (a jump = click)");
            }
        }
    }
};
static HuggettSeparationClickTests huggettSeparationClickTestsInstance;
