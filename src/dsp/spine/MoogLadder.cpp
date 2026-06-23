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
    // Task 6: phase reset on note-on (independent of the full state clear in reset()).
    vs.l.noteReset(); vs.r.noteReset();
}

void MoogLadder::setFundamental(State& s, float hz) const noexcept {
    // Stored per-voice and re-forwarded every block in processStereo: the generated
    // reset() memsets the whole processor state (including the queued event ring), so
    // a fundamental queued before reset() would be lost. Caching + per-block re-send
    // mirrors how cutoff/res/bass are already forwarded.
    static_cast<VoiceState&>(s).fundamentalHz = hz;
}

void MoogLadder::processStereo(State& s, float* left, float* right, int n) const noexcept {
    auto& vs = static_cast<VoiceState&>(s);
    const int slope = (slope_ == Slope::db12) ? 0 : 1;
    const int mode  = static_cast<int>(mode_);
    vs.l.setParams(cutoffHz_, resonance_, drive_, slope, mode);
    vs.r.setParams(cutoffHz_, resonance_, drive_, slope, mode);
    vs.l.setBass(bassAmount_, bassWave_, bassOctave_);
    vs.r.setBass(bassAmount_, bassWave_, bassOctave_);
    vs.l.setFundamental(vs.fundamentalHz);
    vs.r.setFundamental(vs.fundamentalHz);
    vs.l.process(left,  n);
    vs.r.process(right, n);
}
