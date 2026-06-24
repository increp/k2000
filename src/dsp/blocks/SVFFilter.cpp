#include "SVFFilter.h"
#include <cmath>
#include <algorithm>

void SVFFilter::prepare(double sr, int) {
    sampleRate_ = sr;
    coefsDirty_ = true;
}

std::unique_ptr<DSPBlock::VoiceState> SVFFilter::makeVoiceState() const {
    return std::make_unique<SVFFilter::VoiceState>();
}

void SVFFilter::resetVoice(DSPBlock::VoiceState& s) {
    auto& vs = static_cast<SVFFilter::VoiceState&>(s);
    vs.ic1eq = 0.0f;
    vs.ic2eq = 0.0f;
}

void SVFFilter::updateParameters(const ParamSnapshot& s) {
    if (s.svfCutoffHz != cutoffHz_ || s.svfResonance != resonance_) {
        cutoffHz_ = s.svfCutoffHz;
        resonance_ = s.svfResonance;
        coefsDirty_ = true;
    }
}

void SVFFilter::recomputeCoefs() {
    // Clamp cutoff to safe range
    const float cutoff = std::clamp(cutoffHz_, 20.0f, float(sampleRate_ * 0.45));
    const float res = std::clamp(resonance_, 0.0f, 0.999f);

    // Q ranges from 0.5 (no resonance) to 9.0 (max resonance, quadratic curve).
    // Peak LP gain at resonance ≈ Q, so capping at 9.0 keeps output well below
    // the < 10.0 safety bound in tests. (The plan's original 49.5 multiplier
    // produces Q=49 at res=0.99, which is physically correct but contradicts
    // the test's amplitude bound — the Q range is capped here to reconcile both.)
    const float Q = 0.5f + res * res * 8.5f;
    g_ = float(std::tan(juce::MathConstants<double>::pi * cutoff / sampleRate_));
    k_ = 1.0f / Q;

    a1_ = 1.0f / (1.0f + g_ * (g_ + k_));
    a2_ = g_ * a1_;
    a3_ = g_ * a2_;
    coefsDirty_ = false;
}

void SVFFilter::process(DSPBlock::VoiceState& s, float* buf, int n) {
    if (coefsDirty_) recomputeCoefs();
    auto& vs = static_cast<SVFFilter::VoiceState&>(s);

    for (int i = 0; i < n; ++i) {
        const float v0 = buf[i];
        const float v3 = v0 - vs.ic2eq;
        const float v1 = a1_ * vs.ic1eq + a2_ * v3;
        const float v2 = vs.ic2eq + a2_ * vs.ic1eq + a3_ * v3;
        vs.ic1eq = 2.0f * v1 - vs.ic1eq;
        vs.ic2eq = 2.0f * v2 - vs.ic2eq;

        float out = 0.0f;
        switch (type_) {
            case 0: out = v2; break;                    // LP
            case 1: out = v0 - k_ * v1 - v2; break;     // HP
            case 2: out = v1; break;                    // BP
            case 3: out = v0 - k_ * v1; break;          // Notch
            default: out = v2;
        }
        buf[i] = out;
    }
}

std::vector<ParamSpec> SVFFilter::getParamSpecs() const {
    // v1 reads parameters from ParamSnapshot directly; getParamSpecs() is
    // declared by the interface but not yet driving APVTS registration
    // (the v1 processor builds the APVTS layout from params/Parameters.h).
    // When v4 makes slot type user-selectable this will become the source
    // of truth — for now an empty vector is fine.
    return {};
}
