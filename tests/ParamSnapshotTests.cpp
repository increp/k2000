#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include "../src/PluginProcessor.h"
#include "../src/params/Parameters.h"
#include "../src/dsp/ParamSnapshot.h"

class ParamSnapshotTest : public juce::UnitTest {
public:
    ParamSnapshotTest() : juce::UnitTest("ParamSnapshot") {}

    // Minimal AudioProcessor harness — we just need an APVTS attached
    // to something AudioProcessor-like to test snapshot reads.
    struct DummyProc : public juce::AudioProcessor {
        DummyProc() : juce::AudioProcessor(BusesProperties()
            .withOutput("Out", juce::AudioChannelSet::stereo(), true)) {}
        const juce::String getName() const override { return "Dummy"; }
        void prepareToPlay(double, int) override {}
        void releaseResources() override {}
        void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
        void processBlock(juce::AudioBuffer<double>&, juce::MidiBuffer&) override {}
        bool hasEditor() const override { return false; }
        juce::AudioProcessorEditor* createEditor() override { return nullptr; }
        bool acceptsMidi() const override { return true; }
        bool producesMidi() const override { return false; }
        double getTailLengthSeconds() const override { return 0.0; }
        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram(int) override {}
        const juce::String getProgramName(int) override { return ""; }
        void changeProgramName(int, const juce::String&) override {}
        void getStateInformation(juce::MemoryBlock&) override {}
        void setStateInformation(const void*, int) override {}
    };

    void runTest() override {
        DummyProc proc;
        juce::AudioProcessorValueTreeState apvts(
            proc, nullptr, "PARAMS", params::createLayout());

        beginTest("defaults match expected values");
        auto s = params::snapshot(apvts, 0);
        expectWithinAbsoluteError(s.oscCoarse, 0.0f, 1e-6f);
        expectWithinAbsoluteError(s.svfCutoffHz, 1000.0f, 1e-3f);
        expectWithinAbsoluteError(s.svfResonance, 0.2f, 1e-6f);
        expectWithinAbsoluteError(s.ampSustain, 0.8f, 1e-6f);
        expectWithinAbsoluteError(s.masterGainDb, -9.0f, 1e-3f);
        expect(s.oscWaveform == 0);

        beginTest("setting a parameter changes the snapshot");
        if (auto* p = apvts.getParameter(params::layerIds(0).filterCutoff))
            p->setValueNotifyingHost(p->convertTo0to1(2500.0f));
        s = params::snapshot(apvts, 0);
        expectWithinAbsoluteError(s.svfCutoffHz, 2500.0f, 1.0f);

        beginTest("snapshot carries spine model + separation");
        {
            K2000AudioProcessor p;
            auto& apvts = p.apvts();
            const auto& id = params::layerIds(0);
            apvts.getParameter(id.spineSeparation)->setValueNotifyingHost(
                apvts.getParameter(id.spineSeparation)->convertTo0to1(0.5f));
            auto s = params::snapshot(apvts, 0);
            expectWithinAbsoluteError(s.spineSeparationOct, 0.5f, 0.01f);
            expectEquals(s.spineModel, 0);
        }

        beginTest("new v5.0 HP pre-filter + post-drive params default sanely");
        {
            K2000AudioProcessor p;
            auto& apvts = p.apvts();
            auto s = params::snapshot(apvts, 0);
            // New v5.0 HP + post-drive params default sanely
            expectWithinAbsoluteError(s.hpCutoffHz, 0.0f, 1.0e-6f);   // HP off by default (cutoff at 0)
            expectWithinAbsoluteError(s.huggettPostDrive, 0.0f, 1.0e-6f);
            expect(s.hpSlope == 0, "HP slope defaults 12 dB");
            expectWithinAbsoluteError(s.hpResonance, 0.0f, 1e-6f);

            // HP resonance is capped: the knob's max maps to 0.15 (full-range
            // OTA HP self-oscillates too hot), not 1.0.
            apvts.getParameter(params::layerIds(0).spineHpResonance)
                ->setValueNotifyingHost(1.0f);
            s = params::snapshot(apvts, 0);
            expectWithinAbsoluteError(s.hpResonance, 0.15f, 1e-4f);
        }

        beginTest("spine.huggett.routing snapshots and defaults to 0 (LP)");
        {
            s = params::snapshot(apvts, 0);
            expect(s.huggettRouting == 0, "default routing is 0 (LP)");

            if (auto* p = apvts.getParameter(params::layerIds(0).spineHuggettRouting))
                p->setValueNotifyingHost(p->convertTo0to1(7.0f));
            s = params::snapshot(apvts, 0);
            expect(s.huggettRouting == 7, "routing param round-trips to 7 (LP+HP)");
        }

        beginTest("spine.modelFadeMs default reaches the snapshot (25 ms) and round-trips");
        {
            s = params::snapshot(apvts, 0);
            expectWithinAbsoluteError(s.spineModelFadeMs, 25.0f, 1e-4f);
            if (auto* p = apvts.getParameter(params::spineModelFadeMs))
                p->setValueNotifyingHost(p->convertTo0to1(60.0f));
            s = params::snapshot(apvts, 0);
            expectWithinAbsoluteError(s.spineModelFadeMs, 60.0f, 0.1f);
        }

        beginTest("Moog bank params exist with correct defaults");
        {
            const auto& id = params::layerIds(0);
            expect(apvts.getParameter(id.spineMoogMode)       != nullptr, "spine.moog.mode missing");
            expect(apvts.getParameter(id.spineMoogBassAmount) != nullptr, "spine.moog.bassAmount missing");
            expect(apvts.getParameter(id.spineMoogBassWave)   != nullptr, "spine.moog.bassWave missing");
            expect(apvts.getParameter(id.spineMoogBassOctave) != nullptr, "spine.moog.bassOctave missing");
            s = params::snapshot(apvts, 0);
            expect(s.moogMode == 0 && std::fpclassify(s.moogBassAmount) == FP_ZERO, "moog defaults wrong");
        }

        beginTest("VCO1/2/3 + Mixer params exist, default to unison saw with only VCO1 audible");
        {
            const auto& id = params::layerIds(0);
            expect(apvts.getParameter(id.osc1BlendSaw) != nullptr, "osc1.blend.saw missing");
            expect(apvts.getParameter(id.osc2BlendSaw) != nullptr, "osc2.blend.saw missing");
            expect(apvts.getParameter(id.osc3BlendSaw) != nullptr, "osc3.blend.saw missing");
            expect(apvts.getParameter(id.osc1PulseDuty) != nullptr, "osc1.blend.pulseDuty missing");
            expect(apvts.getParameter(id.mixerOsc1Level) != nullptr, "mixer.osc1.level missing");

            s = params::snapshot(apvts, 0);
            for (float coarse : { s.osc1Coarse, s.osc2Coarse, s.osc3Coarse })
                expectWithinAbsoluteError(coarse, 0.0f, 1e-6f);
            for (float fine : { s.osc1Fine, s.osc2Fine, s.osc3Fine })
                expectWithinAbsoluteError(fine, 0.0f, 1e-6f);
            for (float saw : { s.osc1BlendSaw, s.osc2BlendSaw, s.osc3BlendSaw })
                expectWithinAbsoluteError(saw, 1.0f, 1e-6f);
            for (float sine : { s.osc1BlendSine, s.osc2BlendSine, s.osc3BlendSine })
                expectWithinAbsoluteError(sine, 0.0f, 1e-6f);
            for (float duty : { s.osc1PulseDuty, s.osc2PulseDuty, s.osc3PulseDuty })
                expectWithinAbsoluteError(duty, 0.5f, 1e-6f);

            expectWithinAbsoluteError(s.mixerOsc1Level, 1.0f, 1e-6f);
            expectWithinAbsoluteError(s.mixerOsc2Level, 0.0f, 1e-6f);
            expectWithinAbsoluteError(s.mixerOsc3Level, 0.0f, 1e-6f);
        }
    }
};

static ParamSnapshotTest paramSnapshotTestInstance;
