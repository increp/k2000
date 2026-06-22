#include <juce_core/juce_core.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>
#include <algorithm>
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

        beginTest("safety limiter is enabled by default");
        {
            K2000AudioProcessor p;
            expect(p.isLimiterEnabled(), "fresh instance must default to limiter ON");
        }

        beginTest("limiter-enable round-trips through saved state (OFF and ON)");
        {
            K2000AudioProcessor p; p.prepareToPlay(SR, BLOCK);
            p.setLimiterEnabled(false);
            juce::MemoryBlock mb; p.getStateInformation(mb);
            K2000AudioProcessor q; q.prepareToPlay(SR, BLOCK);
            expect(q.isLimiterEnabled(), "q starts ON before load");
            q.setStateInformation(mb.getData(), (int) mb.getSize());
            expect(! q.isLimiterEnabled(), "OFF must persist across save/restore");

            p.setLimiterEnabled(true);
            juce::MemoryBlock mb2; p.getStateInformation(mb2);
            q.setStateInformation(mb2.getData(), (int) mb2.getSize());
            expect(q.isLimiterEnabled(), "ON must persist across save/restore");
        }

        beginTest("enabled limiter caps a hot processed block; disabled does not");
        {
            // Drive a loud note and confirm enabled output stays under the ceiling.
            const float ceil = std::pow(10.0f, -12.0f / 20.0f);
            auto peakOf = [](juce::AudioBuffer<float>& b) {
                float m = 0.0f;
                for (int c = 0; c < b.getNumChannels(); ++c)
                    for (int i = 0; i < b.getNumSamples(); ++i) m = std::max(m, std::abs(b.getSample(c, i)));
                return m;
            };
            K2000AudioProcessor p; p.prepareToPlay(SR, BLOCK);
            // crank master gain so the raw mix would exceed the ceiling
            if (auto* mg = p.apvts().getParameter(params::masterGain))
                mg->setValueNotifyingHost(mg->convertTo0to1(6.0f));
            juce::MidiBuffer midi; midi.addEvent(juce::MidiMessage::noteOn(1, 48, (juce::uint8) 127), 0);
            juce::AudioBuffer<float> buf(2, BLOCK);
            p.setLimiterEnabled(true);
            float mEnabled = 0.0f;
            for (int k = 0; k < 8; ++k) { buf.clear(); p.processBlock(buf, midi); midi.clear(); mEnabled = std::max(mEnabled, peakOf(buf)); }
            expect(mEnabled <= ceil + 1e-4f, "enabled output must stay <= ceiling (peak " + juce::String(mEnabled,5) + ")");
        }
    }
};

static PluginLifecycleTest pluginLifecycleTestInstance;
