#include "HuggettFilter.h"
#include <cmath>

FilterModel::State* HuggettFilter::makeState() const {
    auto* vs = new VoiceState();
    vs->a.prepare(sampleRate_);
    vs->b.prepare(sampleRate_);
    return vs;
}

void HuggettFilter::reset(State& s) const noexcept {
    auto& vs = static_cast<VoiceState&>(s);
    vs.a.reset();
    vs.b.reset();
}

void HuggettFilter::setCommon(float cutoffHz, float resonance, float /*drive*/) noexcept {
    cutoffHz_  = cutoffHz;
    resonance_ = resonance;
}

void HuggettFilter::processStereo(State& s, float* left, float* right, int n) const noexcept {
    auto& vs = static_cast<VoiceState&>(s);
    const int tap = tapForMode(mode_);
    const float cutB = cutoffHz_ * std::pow(2.0f, separationOct_);
    vs.a.setCutoff(cutoffHz_); vs.a.setResonance(resonance_);
    vs.b.setCutoff(cutB);      vs.b.setResonance(resonance_);

    for (int i = 0; i < n; ++i) {
        float l = left[i], r = right[i];
        vs.a.process(l, r, tap);
        if (slope_ == Slope::db24)
            vs.b.process(l, r, tap);
        left[i] = l; right[i] = r;
    }
}
