#include <juce_core/juce_core.h>
#include "../src/Voice.h"
#include "../src/Layer.h"
#include "../src/dsp/ParamSnapshot.h"
#include <vector>
#include <cmath>

class VoiceTest : public juce::UnitTest {
public:
    VoiceTest() : juce::UnitTest("Voice") {}

    static constexpr double SR = 48000.0;
    static constexpr int BLOCK = 256;

    void runTest() override {
        ParamSnapshot s;
        s.oscWaveform = 3;  // sine for clean test signal
        s.svfCutoffHz = 20000.0f; s.svfResonance = 0.0f;
        s.wsDrive = 0.0f; s.wsMix = 0.0f;
        s.ampAttackS = 0.001f; s.ampDecayS = 0.01f;
        s.ampSustain = 1.0f; s.ampReleaseS = 0.01f;

        // The Voice walks a Layer: the Layer owns the snapshot and blocks.
        Layer layer;
        layer.prepare(SR, BLOCK);
        layer.updateParameters(s);

        Voice v;
        v.setLayer(&layer);
        v.prepare(SR, BLOCK);

        beginTest("idle voice renders nothing");
        std::vector<float> outL(BLOCK, 0.0f), outR(BLOCK, 0.0f);
        v.render(outL.data(), outR.data(), BLOCK);
        for (float x : outL) expectWithinAbsoluteError(x, 0.0f, 1e-6f);

        beginTest("noteOn produces non-zero output");
        v.noteOn(69, 1.0f);  // A4
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        // Render two blocks to let envelope ramp past attack.
        v.render(outL.data(), outR.data(), BLOCK);
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        v.render(outL.data(), outR.data(), BLOCK);
        double sumAbs = 0;
        for (float x : outL) sumAbs += std::abs(x);
        expect(sumAbs > 1.0, "voice should produce audible output after noteOn");

        beginTest("noteOn renders finite, audible output (bind path)");
        {
            Layer regLayer; regLayer.prepare(48000.0, 512);
            Voice regVoice; regVoice.setLayer(&regLayer); regVoice.prepare(48000.0, 512);
            regVoice.noteOn(60, 1.0f);
            std::vector<float> lBuf(512, 0.0f), rBuf(512, 0.0f);
            regVoice.render(lBuf.data(), rBuf.data(), 512);
            float regPeak = 0.0f;
            for (int i = 0; i < 512; ++i) {
                expect(std::isfinite(lBuf[i]), "non-finite sample after noteOn via bind");
                regPeak = std::max(regPeak, std::abs(lBuf[i]));
            }
            expect(regPeak > 0.0f, "voice produced silence after noteOn");
        }

        beginTest("noteOff eventually silences voice");
        v.noteOff();
        for (int i = 0; i < 200; ++i) {
            std::fill(outL.begin(), outL.end(), 0.0f);
            std::fill(outR.begin(), outR.end(), 0.0f);
            v.render(outL.data(), outR.data(), BLOCK);
            if (!v.isActive()) break;
        }
        expect(!v.isActive(), "voice should become inactive after release completes");
    }
};

static VoiceTest voiceTestInstance;
