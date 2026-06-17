#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "../src/VoiceManager.h"
#include "../src/Program.h"
#include <vector>
#include <cmath>

class VoiceManagerTest : public juce::UnitTest {
public:
    VoiceManagerTest() : juce::UnitTest("VoiceManager") {}

    static constexpr double SR = 48000.0;
    static constexpr int BLOCK = 256;

    void runTest() override {
        ParamSnapshot s;
        s.oscWaveform = 3;
        s.svfCutoffHz = 20000.0f;
        s.wsMix = 0.0f;
        s.ampAttackS = 0.001f; s.ampDecayS = 0.01f;
        s.ampSustain = 1.0f; s.ampReleaseS = 0.05f;

        // Voices read parameters from their bound Layer's snapshot.
        Program prog; prog.prepare(SR, BLOCK);
        prog.slot(0).layer.updateParameters(s);
        prog.slot(0).routing = LayerRouting{true, 0, 127, 1, 127, 0};
        VoiceManager vm; vm.setProgram(&prog); vm.prepare(SR, BLOCK);

        beginTest("no MIDI input → silent output");
        std::vector<float> outL(BLOCK, 0.0f), outR(BLOCK, 0.0f);
        juce::MidiBuffer midi;
        vm.renderBlock(outL.data(), outR.data(), BLOCK, midi);
        double sumAbs = 0;
        for (float v : outL) sumAbs += std::abs(v);
        expectWithinAbsoluteError(float(sumAbs), 0.0f, 1e-6f);

        beginTest("noteOn produces non-silent output");
        midi.clear();
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 0);
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        vm.renderBlock(outL.data(), outR.data(), BLOCK, midi);
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        midi.clear();
        vm.renderBlock(outL.data(), outR.data(), BLOCK, midi);
        sumAbs = 0;
        for (float v : outL) sumAbs += std::abs(v);
        expect(sumAbs > 1.0, "should produce audible output");

        beginTest("noteOff releases voice");
        midi.clear();
        midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        vm.renderBlock(outL.data(), outR.data(), BLOCK, midi);
        // Run for long enough to drain release
        for (int b = 0; b < 200; ++b) {
            std::fill(outL.begin(), outL.end(), 0.0f);
            std::fill(outR.begin(), outR.end(), 0.0f);
            midi.clear();
            vm.renderBlock(outL.data(), outR.data(), BLOCK, midi);
        }
        sumAbs = 0;
        for (float v : outL) sumAbs += std::abs(v);
        expectLessThan(float(sumAbs), 1e-3f);

        beginTest("more notes than polyphony triggers voice stealing");
        midi.clear();
        for (int i = 0; i < VoiceManager::kNumVoices + 2; ++i)
            midi.addEvent(juce::MidiMessage::noteOn(1, 60 + i, (juce::uint8) 100), 0);
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        vm.renderBlock(outL.data(), outR.data(), BLOCK, midi);
        // Just verify nothing crashed and we have a finite, bounded output.
        for (float v : outL) expect(std::isfinite(v));
    }
};

static VoiceManagerTest voiceManagerTestInstance;
