#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include "Program.h"
#include "VoiceManager.h"
#include "params/Parameters.h"
#include "dsp/SafetyLimiter.h"

class K2000AudioProcessor : public juce::AudioProcessor {
public:
    K2000AudioProcessor();
    ~K2000AudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer,
                      juce::MidiBuffer& midiMessages) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "k2000"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 5.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& apvts() { return apvts_; }

    bool  isLimiterEnabled() const { return limiterEnabled_; }
    void  setLimiterEnabled(bool on) { limiterEnabled_ = on; }
    float gainReductionDb() const { return gainReductionDb_.load(std::memory_order_relaxed); }

private:
    juce::AudioProcessorValueTreeState apvts_;
    Program program_;
    VoiceManager voiceManager_;
    std::vector<float> scratchL_, scratchR_;
    SafetyLimiter      limiter_;
    bool               limiterEnabled_ = true;   // protected: NOT an APVTS param; defaults ON
    std::atomic<float> gainReductionDb_{ 0.0f };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(K2000AudioProcessor)
};
