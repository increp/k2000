#include <juce_core/juce_core.h>
#include "../src/dsp/spine/AsymSaturator.h"
#include "../src/dsp/spine/DcBlocker.h"
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

        beginTest("DcBlocker removes a constant offset, keeps audio");
        {
            DcBlocker dc; dc.prepare(48000.0); dc.reset();
            std::vector<float> out;
            for (int i = 0; i < 8192; ++i) {
                float x = 0.5f + std::sin(2.0 * juce::MathConstants<double>::pi * 200.0 * i / 48000.0);
                out.push_back(dc.process(x, 0));
            }
            double tail = 0; for (int i = 6000; i < 8192; ++i) tail += out[(size_t) i];
            expect(std::abs(tail / 2192.0) < 0.02, "DC removed from tail");
            std::vector<float> ac(out.begin() + 6000, out.end());
            expect(rms(ac) > 0.5f, "audio preserved: " + juce::String(rms(ac)));
        }

        beginTest("DcBlocker keeps L/R state independent");
        {
            DcBlocker dc; dc.prepare(48000.0); dc.reset();
            // Feed ch0 a +0.5 DC offset and ch1 a -0.5 DC offset for many samples.
            float lastL = 0.0f, lastR = 0.0f;
            for (int i = 0; i < 8192; ++i) {
                lastL = dc.process(+0.5f, 0);
                lastR = dc.process(-0.5f, 1);
            }
            // Each channel independently converges toward removing its own DC.
            expect(std::abs(lastL) < 0.05f, "L DC removed: " + juce::String(lastL));
            expect(std::abs(lastR) < 0.05f, "R DC removed: " + juce::String(lastR));
            // If state were shared, the opposite-sign inputs would cross-contaminate
            // and at least one would NOT converge near zero.
        }
    }
};
static HuggettNonlinearTests huggettNonlinearTestsInstance;
