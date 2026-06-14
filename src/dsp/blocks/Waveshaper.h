#pragma once
#include "../DSPBlock.h"

class Waveshaper : public DSPBlock {
public:
    // Stateless block — VoiceState is empty.
    struct VoiceState : public DSPBlock::VoiceState { };

    void prepare(double sampleRate, int maxBlockSize) override;
    std::unique_ptr<DSPBlock::VoiceState> makeVoiceState() const override;
    void resetVoice(DSPBlock::VoiceState& state) override;
    void process(DSPBlock::VoiceState& state, float* buffer, int numSamples) override;
    juce::String getTypeId() const override { return "waveshaper"; }
    std::vector<ParamSpec> getParamSpecs() const override { return {}; }
    void updateParameters(const ParamSnapshot& snapshot) override;

private:
    float drive_ = 0.0f;
    float mix_   = 1.0f;
};
