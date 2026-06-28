#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include "Program.h"
#include "VoiceManager.h"
#include "params/Parameters.h"
#include "dsp/SafetyLimiter.h"
#include "dsp/VoiceOversampler.h"

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

    bool  isLimiterEnabled() const { return limiterEnabled_.load(std::memory_order_relaxed); }
    void  setLimiterEnabled(bool on) { limiterEnabled_.store(on, std::memory_order_relaxed); }
    float gainReductionDb() const { return gainReductionDb_.load(std::memory_order_relaxed); }

    int  realtimeOS() const { return realtimeOS_.load(std::memory_order_relaxed); }
    int  offlineOS()  const { return offlineOS_.load(std::memory_order_relaxed); }
    void setRealtimeOS(int f);   // stores factor and re-prepares via suspendProcessing
    void setOfflineOS(int f);
    void reprepareForOS();
    int  activeOS() const {
        const int rt  = realtimeOS_.load(std::memory_order_relaxed);
        const int off = offlineOS_.load(std::memory_order_relaxed);
        return isNonRealtime() ? (off ? off : rt) : rt;
    }

private:
    juce::AudioProcessorValueTreeState apvts_;
    Program program_;
    VoiceManager voiceManager_;
    std::vector<float> scratchL_, scratchR_;
    SafetyLimiter      limiter_;
    std::atomic<bool>  limiterEnabled_{ true };   // protected: NOT an APVTS param; defaults ON
    std::atomic<float> gainReductionDb_{ 0.0f };
    std::atomic<int>   realtimeOS_{ 1 };   // 1 = Off; protected: NOT an APVTS param
    std::atomic<int>   offlineOS_{ 0 };    // 0 = Same as Realtime; protected: NOT an APVTS param
    double lastSR_            = 0.0;
    int    lastBlock_         = 0;
    bool   lastNonRealtime_   = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(K2000AudioProcessor)
};
