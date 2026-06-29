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

        beginTest("factor 1 render produces finite audible output");
        {
            Layer f1Layer;
            f1Layer.prepare(SR, BLOCK);
            f1Layer.updateParameters(s);
            Voice f1Voice;
            f1Voice.setLayer(&f1Layer);
            f1Voice.prepare(SR, BLOCK, 1);  // new 3-arg signature; factor=1 = identity
            f1Voice.noteOn(69, 1.0f);       // A4
            std::vector<float> f1L(BLOCK, 0.0f), f1R(BLOCK, 0.0f);
            // Two blocks to let envelope past attack
            f1Voice.render(f1L.data(), f1R.data(), BLOCK);
            std::fill(f1L.begin(), f1L.end(), 0.0f);
            std::fill(f1R.begin(), f1R.end(), 0.0f);
            f1Voice.render(f1L.data(), f1R.data(), BLOCK);
            double sumAbsL = 0.0, sumAbsR = 0.0;
            for (int i = 0; i < BLOCK; ++i) {
                expect(std::isfinite(f1L[i]), "non-finite sample at factor 1");
                expect(std::isfinite(f1R[i]), "non-finite sample at factor 1 (R)");
                sumAbsL += std::abs(f1L[i]);
                sumAbsR += std::abs(f1R[i]);
            }
            expect(sumAbsL > 0.0, "factor 1 voice should produce audible output on L");
            expect(sumAbsR > 0.0, "factor 1 voice should produce audible output on R");
        }

        beginTest("factor 4 render produces finite audible output");
        {
            // Spine driven hot: high resonance + moderate spine drive to exercise the
            // oversampled domain meaningfully. Layer prepared at base SR; Voice at 4x.
            ParamSnapshot s4;
            s4.oscWaveform  = 3;     // sine
            s4.svfCutoffHz  = 8000.0f;
            s4.svfResonance = 0.9f;  // high resonance -> self-osc region
            s4.wsDrive      = 0.5f;
            s4.wsMix        = 0.5f;
            s4.spineDrive   = 0.5f;  // some spine drive
            s4.ampAttackS   = 0.001f; s4.ampDecayS = 0.01f;
            s4.ampSustain   = 1.0f;  s4.ampReleaseS = 0.01f;

            Layer f4Layer;
            f4Layer.prepare(SR * 4, BLOCK * 4);
            f4Layer.updateParameters(s4);

            Voice f4Voice;
            f4Voice.setLayer(&f4Layer);
            f4Voice.prepare(SR, BLOCK, 4);

            f4Voice.noteOn(60, 1.0f);  // middle C

            std::vector<float> f4L(BLOCK, 0.0f), f4R(BLOCK, 0.0f);
            // Render enough blocks to clear latency + attack (factor-4 latency ~54 base samples;
            // a few BLOCK=256-sample blocks is more than enough).
            for (int b = 0; b < 5; ++b) {
                std::fill(f4L.begin(), f4L.end(), 0.0f);
                std::fill(f4R.begin(), f4R.end(), 0.0f);
                f4Voice.render(f4L.data(), f4R.data(), BLOCK);
            }

            double sumL = 0.0, sumR = 0.0;
            for (int i = 0; i < BLOCK; ++i) {
                expect(std::isfinite(f4L[i]), "non-finite sample on L at factor 4");
                expect(std::isfinite(f4R[i]), "non-finite sample on R at factor 4");
                sumL += std::abs(f4L[i]);
                sumR += std::abs(f4R[i]);
            }
            expect(sumL > 0.0, "factor 4 voice should produce audible output on L");
            expect(sumR > 0.0, "factor 4 voice should produce audible output on R");
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
