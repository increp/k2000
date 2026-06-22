#include "CmajorSvfFilter.h"

FilterModel::State* CmajorSvfFilter::constructState(void* mem) const {
    auto* vs = new (mem) VoiceState();
    vs->l.prepare(sampleRate_);
    vs->r.prepare(sampleRate_);
    return vs;
}

void CmajorSvfFilter::reset(State& s) const noexcept {
    auto& vs = static_cast<VoiceState&>(s);
    vs.l.reset(); vs.r.reset();
}

void CmajorSvfFilter::setCommon(float cutoffHz, float resonance, float /*drive*/) noexcept {
    cutoffHz_  = cutoffHz;
    resonance_ = resonance;   // drive ignored — linear pilot
}

void CmajorSvfFilter::processStereo(State& s, float* left, float* right, int n) const noexcept {
    auto& vs = static_cast<VoiceState&>(s);
    vs.l.setParams(cutoffHz_, resonance_, tap_);
    vs.r.setParams(cutoffHz_, resonance_, tap_);
    vs.l.process(left,  n);
    vs.r.process(right, n);
}
