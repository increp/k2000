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
    vs.dc.reset();
}

void HuggettFilter::setCommon(float cutoffHz, float resonance, float drive) noexcept {
    cutoffHz_  = cutoffHz;
    resonance_ = resonance;
    preDrive_  = drive;
    preSat_.setDrive(drive, kPreBias, kPreDriveDb);
}

void HuggettFilter::processStereo(State& s, float* left, float* right, int n) const noexcept {
    auto& vs = static_cast<VoiceState&>(s);
    const int tap = tapForMode(mode_);
    const float cutB = cutoffHz_ * std::pow(2.0f, separationOct_);

    const bool preOn     = preDrive_  > 0.0f;
    const bool postOn    = postDrive_ > 0.0f;
    const bool nonlinear = preOn || postOn || (resonance_ > 0.0f);

    vs.a.setCutoff(cutoffHz_); vs.a.setResonance(resonance_); vs.a.setResSat(resonance_);
    vs.b.setCutoff(cutB);      vs.b.setResonance(resonance_); vs.b.setResSat(resonance_);

    for (int i = 0; i < n; ++i) {
        float l = left[i], r = right[i];
        if (preOn)  { l = preSat_.process(l);  r = preSat_.process(r); }
        vs.a.process(l, r, tap);
        if (slope_ == Slope::db24) vs.b.process(l, r, tap);
        if (postOn) { l = postSat_.process(l); r = postSat_.process(r); }
        if (nonlinear) { l = vs.dc.process(l, 0); r = vs.dc.process(r, 1); }
        left[i] = l; right[i] = r;
    }
}
