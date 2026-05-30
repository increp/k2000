#include <juce_core/juce_core.h>
#include "../src/dsp/blocks/Waveshaper.h"
#include "../src/params/ParamSnapshot.h"
#include <vector>
#include <cmath>

class WaveshaperTest : public juce::UnitTest {
public:
    WaveshaperTest() : juce::UnitTest("Waveshaper") {}

    void runTest() override {
        beginTest("mix=0 passes input unchanged");
        {
            Waveshaper w;
            w.prepare(48000.0, 256);
            ParamSnapshot s; s.wsDrive = 1.0f; s.wsMix = 0.0f;
            w.updateParameters(s);
            std::vector<float> buf{0.0f, 0.5f, -0.5f, 0.9f, -0.9f};
            std::vector<float> expected = buf;
            w.process(buf.data(), int(buf.size()));
            for (size_t i = 0; i < buf.size(); ++i)
                expectWithinAbsoluteError(buf[i], expected[i], 1e-6f);
        }

        beginTest("mix=1 drive=0 passes input nearly unchanged for small signals");
        {
            Waveshaper w;
            w.prepare(48000.0, 256);
            ParamSnapshot s; s.wsDrive = 0.0f; s.wsMix = 1.0f;
            w.updateParameters(s);
            std::vector<float> buf{0.0f, 0.1f, -0.1f};
            std::vector<float> expected = buf;
            w.process(buf.data(), int(buf.size()));
            for (size_t i = 0; i < buf.size(); ++i)
                expectWithinAbsoluteError(buf[i], expected[i], 0.05f);
        }

        beginTest("output is bounded");
        {
            Waveshaper w;
            w.prepare(48000.0, 256);
            ParamSnapshot s; s.wsDrive = 1.0f; s.wsMix = 1.0f;
            w.updateParameters(s);
            std::vector<float> buf(1024);
            for (size_t i = 0; i < buf.size(); ++i) buf[i] = 5.0f;  // extreme
            w.process(buf.data(), int(buf.size()));
            for (float v : buf) expect(std::abs(v) <= 1.0f, "output must be in [-1, 1]");
        }

        beginTest("odd symmetry: shaper(-x) == -shaper(x)");
        {
            Waveshaper w;
            w.prepare(48000.0, 256);
            ParamSnapshot s; s.wsDrive = 0.7f; s.wsMix = 1.0f;
            w.updateParameters(s);
            std::vector<float> pos{0.1f, 0.5f, 0.9f};
            std::vector<float> neg{-0.1f, -0.5f, -0.9f};
            w.process(pos.data(), int(pos.size()));
            w.process(neg.data(), int(neg.size()));
            for (size_t i = 0; i < pos.size(); ++i)
                expectWithinAbsoluteError(pos[i], -neg[i], 1e-6f);
        }
    }
};

static WaveshaperTest waveshaperTestInstance;
