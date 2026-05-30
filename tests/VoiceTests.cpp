#include <juce_core/juce_core.h>
#include "../src/Voice.h"
#include "../src/params/ParamSnapshot.h"
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
        s.svfType = 0; s.svfCutoffHz = 20000.0f; s.svfResonance = 0.0f;
        s.wsDrive = 0.0f; s.wsMix = 0.0f;
        s.ampAttackS = 0.001f; s.ampDecayS = 0.01f;
        s.ampSustain = 1.0f; s.ampReleaseS = 0.01f;

        Voice v;
        v.prepare(SR, BLOCK);

        beginTest("idle voice renders nothing");
        std::vector<float> out(BLOCK, 0.0f);
        v.render(out.data(), BLOCK, s);
        for (float x : out) expectWithinAbsoluteError(x, 0.0f, 1e-6f);

        beginTest("noteOn produces non-zero output");
        v.noteOn(69, 1.0f);  // A4
        std::fill(out.begin(), out.end(), 0.0f);
        // Render two blocks to let envelope ramp past attack.
        v.render(out.data(), BLOCK, s);
        std::fill(out.begin(), out.end(), 0.0f);
        v.render(out.data(), BLOCK, s);
        double sumAbs = 0;
        for (float x : out) sumAbs += std::abs(x);
        expect(sumAbs > 1.0, "voice should produce audible output after noteOn");

        beginTest("noteOff eventually silences voice");
        v.noteOff();
        for (int i = 0; i < 200; ++i) {
            std::fill(out.begin(), out.end(), 0.0f);
            v.render(out.data(), BLOCK, s);
            if (!v.isActive()) break;
        }
        expect(!v.isActive(), "voice should become inactive after release completes");
    }
};

static VoiceTest voiceTestInstance;
