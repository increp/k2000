#include <juce_core/juce_core.h>
#include "../src/dsp/spine/HuggettFilter.h"
#include <cmath>
#include <memory>

// Regression for "drive (pre/post) jumps abruptly off 0 instead of fading in".
// A gradual overdrive must be ~imperceptible just above 0: the steady-state level
// a hair off zero must barely differ from bypassed (drive==0). The old code engaged
// the shaper at a fixed character (bias 0.15 + ~+1.8 dB makeup) the instant drive>0.
struct HuggettDriveOnsetTests : public juce::UnitTest {
    HuggettDriveOnsetTests() : juce::UnitTest("HuggettDriveOnset") {}
    static constexpr double kSR = 48000.0;

    // Steady-state passband output RMS (dB) of an LP with the given pre/post drive.
    static double rmsDb(float preDrive, float postDrive) {
        HuggettFilter h; h.prepare(kSR);
        h.setMode(HuggettFilter::Mode::LP); h.setSlope(HuggettFilter::Slope::db24);
        h.setSeparation(0.0f);
        h.setCommon(1000.0f, 0.0f, preDrive);   // 3rd arg = pre (input) drive
        h.setPostDrive(postDrive);
        std::unique_ptr<FilterModel::State> st(h.makeState()); h.reset(*st);
        const int warm = 8192, meas = 8192;
        double outSq = 0.0;
        for (int i = 0; i < warm + meas; ++i) {
            const float x = 0.3f * (float) std::sin(2.0 * juce::MathConstants<double>::pi * 200.0 * i / kSR);
            float l = x, r = x; h.processStereo(*st, &l, &r, 1);
            if (i >= warm) outSq += double(l) * l;
        }
        return 20.0 * std::log10(std::max(1e-7, std::sqrt(outSq / meas)));
    }

    void runTest() override {
        const double bypassed = rmsDb(0.0f, 0.0f);

        beginTest("post-drive fades in: a hair off 0 barely differs from bypassed");
        const double postEps = rmsDb(0.0f, 0.02f);
        expect(std::abs(postEps - bypassed) < 0.5,
               "post-drive onset jump " + juce::String(postEps - bypassed, 2) + " dB (should be ~0)");

        beginTest("pre-drive fades in: a hair off 0 barely differs from bypassed");
        const double preEps = rmsDb(0.02f, 0.0f);
        expect(std::abs(preEps - bypassed) < 0.5,
               "pre-drive onset jump " + juce::String(preEps - bypassed, 2) + " dB (should be ~0)");
    }
};
static HuggettDriveOnsetTests huggettDriveOnsetTestsInstance;
