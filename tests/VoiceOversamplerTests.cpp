#include <juce_core/juce_core.h>
#include <vector>
#include <cmath>
#include "../src/dsp/VoiceOversampler.h"

class VoiceOversamplerTests : public juce::UnitTest {
public:
    VoiceOversamplerTests() : juce::UnitTest("VoiceOversampler") {}

    void runTest() override {
        const int N = 1024;

        beginTest("factor 1 is identity (up then down)");
        {
            VoiceOversampler os; os.prepare(N); os.setFactor(1);
            std::vector<float> in(N), up(N), dL(N), dR(N);
            for (int i = 0; i < N; ++i) in[i] = std::sin(0.05f * i);
            os.processMonoUp(in.data(), N, up.data());
            os.processStereoDown(up.data(), up.data(), N, dL.data(), dR.data());
            for (int i = 0; i < N; ++i) expectWithinAbsoluteError(dL[i], in[i], 1e-5f);
        }

        beginTest("round-trip latency matches the table (impulse, 2x/4x/8x)");
        {
            for (int f : { 2, 4, 8 }) {
                VoiceOversampler os; os.prepare(N); os.setFactor(f);
                std::vector<float> in(N, 0.0f), up((size_t) N*f), dL(N), dR(N);
                in[0] = 1.0f;
                os.processMonoUp(in.data(), N, up.data());
                os.processStereoDown(up.data(), up.data(), N, dL.data(), dR.data());
                int peak = 0; float pm = 0;
                for (int i = 0; i < N; ++i) if (std::abs(dL[i]) > pm) { pm = std::abs(dL[i]); peak = i; }
                expectEquals(peak, VoiceOversampler::latencyBaseSamples(f),
                             "latency for " + juce::String(f) + "x");
            }
        }
    }
};
static VoiceOversamplerTests voiceOversamplerTestsInstance;
