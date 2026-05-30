#include <juce_core/juce_core.h>
#include "../src/dsp/blocks/SVFFilter.h"
#include "../src/params/ParamSnapshot.h"
#include <vector>
#include <cmath>

class SVFFilterTest : public juce::UnitTest {
public:
    SVFFilterTest() : juce::UnitTest("SVFFilter") {}

    static constexpr double SR = 48000.0;
    static constexpr int BLOCK = 1024;

    void runTest() override {
        beginTest("LP with high cutoff passes DC nearly unchanged");
        {
            SVFFilter f;
            f.prepare(SR, BLOCK);
            f.reset();
            ParamSnapshot s;
            s.svfType = 0; s.svfCutoffHz = 20000.0f; s.svfResonance = 0.0f;
            f.updateParameters(s);
            std::vector<float> buf(BLOCK, 1.0f);  // DC = 1
            f.process(buf.data(), BLOCK);
            // After settling, output should be ~1
            expectWithinAbsoluteError(buf.back(), 1.0f, 0.05f);
        }

        beginTest("LP with low cutoff attenuates a 5kHz sine");
        {
            SVFFilter f;
            f.prepare(SR, BLOCK);
            f.reset();
            ParamSnapshot s;
            s.svfType = 0; s.svfCutoffHz = 200.0f; s.svfResonance = 0.0f;
            f.updateParameters(s);

            const int N = 8 * BLOCK;
            std::vector<float> buf(N);
            double phase = 0;
            for (int i = 0; i < N; ++i) {
                buf[i] = float(std::sin(phase));
                phase += 2.0 * juce::MathConstants<double>::pi * 5000.0 / SR;
            }
            // Skip the first block (transient) and process; measure RMS of last half.
            f.process(buf.data(), N);
            double s2 = 0;
            int from = N / 2;
            for (int i = from; i < N; ++i) s2 += double(buf[i]) * buf[i];
            double rmsOut = std::sqrt(s2 / (N - from));
            expectLessThan(rmsOut, 0.1, "5kHz should be heavily attenuated by 200Hz LP");
        }

        beginTest("HP with low cutoff blocks DC");
        {
            SVFFilter f;
            f.prepare(SR, BLOCK);
            f.reset();
            ParamSnapshot s;
            s.svfType = 1; s.svfCutoffHz = 200.0f; s.svfResonance = 0.0f;
            f.updateParameters(s);
            std::vector<float> buf(BLOCK * 8, 1.0f);
            f.process(buf.data(), int(buf.size()));
            // Output near the end should be ~0
            expectWithinAbsoluteError(buf.back(), 0.0f, 0.05f);
        }

        beginTest("output is bounded at high resonance");
        {
            SVFFilter f;
            f.prepare(SR, BLOCK);
            f.reset();
            ParamSnapshot s;
            s.svfType = 0; s.svfCutoffHz = 1000.0f; s.svfResonance = 0.99f;
            f.updateParameters(s);
            std::vector<float> buf(BLOCK);
            for (int i = 0; i < BLOCK; ++i)
                buf[i] = float(std::sin(2.0 * juce::MathConstants<double>::pi * 1000.0 * i / SR));
            f.process(buf.data(), BLOCK);
            for (float v : buf)
                expect(std::abs(v) < 10.0f, "high-resonance output must not blow up");
        }

        beginTest("reset returns state to zero");
        {
            SVFFilter f;
            f.prepare(SR, BLOCK);
            ParamSnapshot s;
            s.svfType = 0; s.svfCutoffHz = 1000.0f; s.svfResonance = 0.5f;
            f.updateParameters(s);
            std::vector<float> buf(BLOCK, 1.0f);
            f.process(buf.data(), BLOCK);
            f.reset();
            std::vector<float> buf2(BLOCK, 0.0f);
            f.process(buf2.data(), BLOCK);
            // With zero input after reset, output should be zero
            for (float v : buf2)
                expectWithinAbsoluteError(v, 0.0f, 1e-6f);
        }
    }
};

static SVFFilterTest svfFilterTestInstance;
