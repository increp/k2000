#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include "../src/PluginProcessor.h"
#include "../src/params/Parameters.h"
#include "../src/params/ParamSnapshot.h"

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
        expect(s.oscWaveform == 0);
        expect(s.svfType == 0);

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
            expect(s.hpEnable == 0, "HP disabled by default");
            expectWithinAbsoluteError(s.huggettPostDrive, 0.0f, 1.0e-6f);
            expect(s.hpSlope == 0, "HP slope defaults 12 dB");
            expectWithinAbsoluteError(s.hpCutoffHz, 20.0f, 1e-3f);
            expectWithinAbsoluteError(s.hpResonance, 0.0f, 1e-6f);
            expectWithinAbsoluteError(s.hpDrive, 0.0f, 1e-6f);
        }
    }
};

static ParamSnapshotTest paramSnapshotTestInstance;
