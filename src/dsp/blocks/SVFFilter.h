#pragma once
#include "../DSPBlock.h"

class SVFFilter : public DSPBlock {
public:
    void prepare(double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process(float* buffer, int numSamples) override;
    juce::String getTypeId() const override { return "svf_filter"; }
    std::vector<ParamSpec> getParamSpecs() const override;
    void updateParameters(const ParamSnapshot& snapshot) override;

private:
    double sampleRate_ = 44100.0;
    int type_ = 0;  // 0=LP 1=HP 2=BP 3=Notch
    float cutoffHz_ = 1000.0f;
    float resonance_ = 0.0f;

    // Coefficients (recomputed when cutoff/resonance change)
    float g_ = 0, k_ = 0, a1_ = 0, a2_ = 0, a3_ = 0;
    bool coefsDirty_ = true;

    // State
    float ic1eq_ = 0, ic2eq_ = 0;

    void recomputeCoefs();
};
