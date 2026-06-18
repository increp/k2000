#include <juce_core/juce_core.h>
#include "../src/dsp/spine/AsymSaturator.h"
#include <cmath>

class HuggettNonlinearTests : public juce::UnitTest {
public:
    HuggettNonlinearTests() : juce::UnitTest("HuggettNonlinear") {}

    // RMS of a buffer.
    static float rms(const std::vector<float>& v) {
        double s = 0; for (float x : v) s += double(x) * x;
        return (float) std::sqrt(s / v.size());
    }

    void runTest() override {
        beginTest("AsymSaturator: disengaged at zero drive, engaged when driven");
        {
            AsymSaturator sat;
            sat.setDrive(0.0f, 0.0f, 30.0f);
            expect(!sat.engaged(), "zero drive is a no-op");
            sat.setDrive(0.5f, 0.18f, 30.0f);
            expect(sat.engaged(), "driven stage engages");
        }

        beginTest("AsymSaturator: adds even harmonics (asymmetry) and is bounded");
        {
            AsymSaturator sat; sat.setDrive(1.0f, 0.25f, 30.0f);
            AsymSaturator::State st; st.reset();
            const double sr = 48000.0, f = 220.0;
            float peak = 0.0f; double dcAcc = 0.0; int N = 4096;
            for (int i = 0; i < N; ++i) {
                float x = 0.7f * std::sin(2.0 * juce::MathConstants<double>::pi * f * i / sr);
                float y = sat.process(x, 0, st);
                peak = std::max(peak, std::abs(y));
                if (i > N / 2) dcAcc += y;            // asymmetric shaper -> nonzero DC
            }
            expect(peak < 2.0f, "output bounded: " + juce::String(peak));
            expect(std::abs(dcAcc) > 1.0e-3, "asymmetry produces DC offset");
        }
    }
};
static HuggettNonlinearTests huggettNonlinearTestsInstance;
