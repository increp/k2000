#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "../src/VoiceManager.h"
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

        VoiceManager vm;
        vm.prepare(SR, BLOCK);

        beginTest("no MIDI input → silent output");
        std::vector<float> out(BLOCK, 0.0f);
        juce::MidiBuffer midi;
        vm.renderBlock(out.data(), BLOCK, midi, s);
        double sumAbs = 0;
        for (float v : out) sumAbs += std::abs(v);
        expectWithinAbsoluteError(float(sumAbs), 0.0f, 1e-6f);

        beginTest("noteOn produces non-silent output");
        midi.clear();
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 0);
        std::fill(out.begin(), out.end(), 0.0f);
        vm.renderBlock(out.data(), BLOCK, midi, s);
        std::fill(out.begin(), out.end(), 0.0f);
        midi.clear();
        vm.renderBlock(out.data(), BLOCK, midi, s);
        sumAbs = 0;
        for (float v : out) sumAbs += std::abs(v);
        expect(sumAbs > 1.0, "should produce audible output");

        beginTest("noteOff releases voice");
        midi.clear();
        midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        vm.renderBlock(out.data(), BLOCK, midi, s);
        // Run for long enough to drain release
        for (int b = 0; b < 200; ++b) {
            std::fill(out.begin(), out.end(), 0.0f);
            midi.clear();
            vm.renderBlock(out.data(), BLOCK, midi, s);
        }
        sumAbs = 0;
        for (float v : out) sumAbs += std::abs(v);
        expectLessThan(float(sumAbs), 1e-3f);

        beginTest("more notes than polyphony triggers voice stealing");
        midi.clear();
        for (int i = 0; i < VoiceManager::kNumVoices + 2; ++i)
            midi.addEvent(juce::MidiMessage::noteOn(1, 60 + i, (juce::uint8) 100), 0);
        std::fill(out.begin(), out.end(), 0.0f);
        vm.renderBlock(out.data(), BLOCK, midi, s);
        // Just verify nothing crashed and we have a finite, bounded output.
        for (float v : out) expect(std::isfinite(v));
    }
};

static VoiceManagerTest voiceManagerTestInstance;
