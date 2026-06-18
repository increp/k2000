#include "HuggettFilter.h"
#include <cmath>

FilterModel::State* HuggettFilter::makeState() const {
    auto* vs = new VoiceState();
    vs->a.prepare(sampleRate_);
    vs->b.prepare(sampleRate_);
    vs->dc.prepare(sampleRate_);
    return vs;
}

void HuggettFilter::reset(State& s) const noexcept {
    auto& vs = static_cast<VoiceState&>(s);
    vs.a.reset(); vs.b.reset();
    vs.pre.reset(); vs.post.reset();
    vs.dc.reset();
}

void HuggettFilter::setCommon(float cutoffHz, float resonance, float drive) noexcept {
    cutoffHz_  = cutoffHz;
    resonance_ = resonance;
    preDrive_  = drive;
}

void HuggettFilter::processStereo(State& s, float* left, float* right, int n) const noexcept {
    auto& vs = static_cast<VoiceState&>(s);
    const int tap = tapForMode(mode_);
    const float cutB = cutoffHz_ * std::pow(2.0f, separationOct_);

    vs.a.setCutoff(cutoffHz_); vs.a.setResonance(resonance_); vs.a.setResSat(resonance_);
    vs.b.setCutoff(cutB);      vs.b.setResonance(resonance_); vs.b.setResSat(resonance_);

    AsymSaturator pre, post;
    pre.setDrive(preDrive_,  kPreBias,  kPreDriveDb);
    post.setDrive(postDrive_, kPostBias, kPostDriveDb);
    const bool preOn  = pre.engaged();
    const bool postOn = post.engaged();

    for (int i = 0; i < n; ++i) {
        float l = left[i], r = right[i];
        if (preOn) { l = pre.process(l, 0, vs.pre); r = pre.process(r, 1, vs.pre); }
        vs.a.process(l, r, tap);
        if (slope_ == Slope::db24) vs.b.process(l, r, tap);
        if (postOn) { l = post.process(l, 0, vs.post); r = post.process(r, 1, vs.post); }
        l = vs.dc.process(l, 0); r = vs.dc.process(r, 1);
        left[i] = l; right[i] = r;
    }
}
