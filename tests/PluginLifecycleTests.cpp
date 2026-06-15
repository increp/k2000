#include <juce_core/juce_core.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../src/PluginProcessor.h"

class PluginLifecycleTest : public juce::UnitTest {
public:
    PluginLifecycleTest() : juce::UnitTest("PluginLifecycle") {}

    static constexpr double SR = 48000.0;
    static constexpr int BLOCK = 512;

    void runTest() override {
        beginTest("construct -> prepare -> silent processBlock");
        {
            K2000AudioProcessor p;
            p.prepareToPlay(SR, BLOCK);
            juce::AudioBuffer<float> buf(2, BLOCK);
            juce::MidiBuffer midi;
            p.processBlock(buf, midi);
            // No MIDI -> output should be silence
            double sumAbs = 0;
            for (int c = 0; c < buf.getNumChannels(); ++c)
                for (int i = 0; i < buf.getNumSamples(); ++i)
                    sumAbs += std::abs(buf.getSample(c, i));
            expectWithinAbsoluteError((float) sumAbs, 0.0f, 1e-6f);
        }

        beginTest("noteOn through processBlock produces audible output");
        {
            K2000AudioProcessor p;
            p.prepareToPlay(SR, BLOCK);
            juce::AudioBuffer<float> buf(2, BLOCK);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 69, (juce::uint8) 100), 0);
            p.processBlock(buf, midi);
            // Render a few more blocks to let envelope past attack
            for (int i = 0; i < 4; ++i) {
                midi.clear();
                buf.clear();
                p.processBlock(buf, midi);
            }
            double sumAbs = 0;
            for (int c = 0; c < buf.getNumChannels(); ++c)
                for (int i = 0; i < buf.getNumSamples(); ++i)
                    sumAbs += std::abs(buf.getSample(c, i));
            expect(sumAbs > 1.0, "should produce audible output");
        }

        beginTest("state save and restore round-trips a non-default parameter");
        {
            K2000AudioProcessor p;
            p.prepareToPlay(SR, BLOCK);
            if (auto* cutoff = p.apvts().getParameter(params::layerIds(0).filterCutoff))
                cutoff->setValueNotifyingHost(cutoff->convertTo0to1(3200.0f));

            juce::MemoryBlock mb;
            p.getStateInformation(mb);

            K2000AudioProcessor q;
            q.prepareToPlay(SR, BLOCK);
            q.setStateInformation(mb.getData(), (int) mb.getSize());
            float restored = *q.apvts().getRawParameterValue(params::layerIds(0).filterCutoff);
            expectWithinAbsoluteError(restored, 3200.0f, 5.0f);
        }
    }
};

static PluginLifecycleTest pluginLifecycleTestInstance;
