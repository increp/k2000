#include "MoogLadder.h"
#include <new>

FilterModel::State* MoogLadder::constructState(void* mem) const {
    auto* vs = new (mem) VoiceState();
    vs->l.prepare(sampleRate_); vs->r.prepare(sampleRate_);
    return vs;
}

void MoogLadder::reset(State& s) const noexcept {
    auto& vs = static_cast<VoiceState&>(s);
    vs.l.reset(); vs.r.reset();
}

void MoogLadder::processStereo(State& s, float* left, float* right, int n) const noexcept {
    auto& vs = static_cast<VoiceState&>(s);
    const int slope = (slope_ == Slope::db12) ? 0 : 1;
    vs.l.setParams(cutoffHz_, resonance_, drive_, slope);
    vs.r.setParams(cutoffHz_, resonance_, drive_, slope);
    vs.l.process(left,  n);
    vs.r.process(right, n);
}
