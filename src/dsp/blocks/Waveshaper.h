#pragma once
#include "../DSPBlock.h"

class Waveshaper : public DSPBlock {
public:
    void prepare(double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process(float* buffer, int numSamples) override;
    juce::String getTypeId() const override { return "waveshaper"; }
    std::vector<ParamSpec> getParamSpecs() const override { return {}; }
    void updateParameters(const ParamSnapshot& snapshot) override;

private:
    float drive_ = 0.0f;
    float mix_   = 1.0f;
};
