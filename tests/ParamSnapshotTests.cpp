#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
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
        auto s = params::snapshot(apvts);
        expectWithinAbsoluteError(s.oscCoarse, 0.0f, 1e-6f);
        expectWithinAbsoluteError(s.svfCutoffHz, 1000.0f, 1e-3f);
        expectWithinAbsoluteError(s.svfResonance, 0.2f, 1e-6f);
        expectWithinAbsoluteError(s.ampSustain, 0.8f, 1e-6f);
        expect(s.oscWaveform == 0);
        expect(s.svfType == 0);

        beginTest("setting a parameter changes the snapshot");
        if (auto* p = apvts.getParameter(params::id::svfCutoff))
            p->setValueNotifyingHost(p->convertTo0to1(2500.0f));
        s = params::snapshot(apvts);
        expectWithinAbsoluteError(s.svfCutoffHz, 2500.0f, 1.0f);
    }
};

static ParamSnapshotTest paramSnapshotTestInstance;
