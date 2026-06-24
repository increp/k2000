#pragma once
#include "../DSPBlock.h"

class SVFFilter : public DSPBlock {
public:
    // Per-voice integrator state (was held inline in v1).
    struct VoiceState : public DSPBlock::VoiceState {
        float ic1eq = 0.0f;
        float ic2eq = 0.0f;
    };

    void prepare(double sampleRate, int maxBlockSize) override;
    std::unique_ptr<DSPBlock::VoiceState> makeVoiceState() const override;
    void resetVoice(DSPBlock::VoiceState& state) override;
    void process(DSPBlock::VoiceState& state, float* buffer, int numSamples) override;
    juce::String getTypeId() const override { return "svf_filter"; }
    std::vector<ParamSpec> getParamSpecs() const override;
    void updateParameters(const ParamSnapshot& snapshot) override;
    // Sets the filter mode directly (0=LP 1=HP 2=BP 3=Notch). Used by tests
    // since the filter.type APVTS param was removed in Spec 2.
    void setType(int t) { type_ = t; }

private:
    double sampleRate_ = 44100.0;
    int type_ = 0;  // 0=LP 1=HP 2=BP 3=Notch
    float cutoffHz_ = 1000.0f;
    float resonance_ = 0.0f;

    // Coefficients (recomputed when cutoff/resonance change)
    float g_ = 0, k_ = 0, a1_ = 0, a2_ = 0, a3_ = 0;
    bool coefsDirty_ = true;

    void recomputeCoefs();
};
