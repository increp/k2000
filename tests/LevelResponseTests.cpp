#include <juce_core/juce_core.h>
#include "testdsp/LevelResponse.h"
#include "characterization/ReferenceDevices.h"
#include <cmath>
#include <vector>

// Multi-level excitation proven against CubicNonlinearity closed forms.
// c3 = -0.3 (compressive): fundamental gain(A) = 20log10(1 - 0.225 A^2).
//   - small signal (-40 dBFS): gain ~ 0 dB
//   - A = 0.5 (-6.02 dBFS): gain = 20log10(0.94375) = -0.503 dB
//   - 1 dB knee: 1 - 0.225 A^2 = 10^(-1/20) -> A = 0.6952 -> -3.16 dBFS input
// c3 = +0.5 (expansive): output peak A + 0.5 A^3 = 1.0 at A = 0.7715 -> clip
//   headroom = 20log10(0.7715) = -2.25 dBFS input.

struct LevelResponseTests : public juce::UnitTest {
    LevelResponseTests() : juce::UnitTest("LevelResponse") {}

    void runTest() override {
        using testdsp::LevelResponse;

        std::vector<double> amps;
        for (int d = -40; d <= -1; ++d) amps.push_back((double) d);

        beginTest("gain-vs-level matches the cubic's closed form");
        chz::CubicNonlinearity comp;
        comp.c3 = -0.3f;
        auto pts = LevelResponse::measure(comp, 1000.0, 48000.0, amps);
        expectEquals((int) pts.size(), (int) amps.size());
        expect(std::abs(pts.front().gainDb) < 0.05, "small-signal gain ~ 0 dB");
        // Point at -6 dBFS (index 34: -40 + 34 = -6).
        expectWithinAbsoluteError(pts[(size_t) 34].gainDb, -0.503, 0.2);

        beginTest("THD-vs-level matches trueThdDb at the driven point");
        expectWithinAbsoluteError(pts[(size_t) 34].thdDb,
                                  comp.trueThdDb((float) std::pow(10.0, -6.0 / 20.0)), 0.5);

        beginTest("1 dB compression knee at the closed-form input level");
        expectWithinAbsoluteError(LevelResponse::kneeInDbfs(pts, 1.0), -3.16, 0.5);

        beginTest("knee sentinel when the device never compresses");
        chz::CubicNonlinearity linear;   // c3 = 0 -> pure unity
        auto lpts = LevelResponse::measure(linear, 1000.0, 48000.0, amps);
        expectWithinAbsoluteError(LevelResponse::kneeInDbfs(lpts, 1.0), -300.0, 1.0e-9);

        beginTest("headroom-to-clip at the closed-form input level");
        chz::CubicNonlinearity exp5;
        exp5.c3 = 0.5f;
        std::vector<double> hamps;
        for (int d = -20; d <= 0; ++d) hamps.push_back((double) d);
        auto hpts = LevelResponse::measure(exp5, 1000.0, 48000.0, hamps);
        expectWithinAbsoluteError(LevelResponse::headroomToClipInDbfs(hpts, 0.0), -2.25, 0.5);

        beginTest("headroom sentinel when the ceiling is never reached");
        expectWithinAbsoluteError(LevelResponse::headroomToClipInDbfs(lpts, 0.0), -300.0, 1.0e-9);
    }
};

static LevelResponseTests levelResponseTestsInstance;
