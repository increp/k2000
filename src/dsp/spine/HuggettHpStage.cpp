#include "HuggettHpStage.h"

HuggettHpStage::State* HuggettHpStage::makeState() const {
    auto* st = new State();
    st->a.prepare(sampleRate_);
    st->b.prepare(sampleRate_);
    st->dc.prepare(sampleRate_);
    return st;
}

void HuggettHpStage::reset(State& s) const noexcept {
    s.a.reset(); s.b.reset(); s.pre.reset(); s.dc.reset();
}

void HuggettHpStage::processStereo(State& s, float* left, float* right, int n) const noexcept {
    s.a.setCutoff(cutoffHz_); s.a.setResonance(resonance_); s.a.setResSat(resonance_);
    s.b.setCutoff(cutoffHz_); s.b.setResonance(resonance_); s.b.setResSat(resonance_);

    // Gate on the drive PARAMETER, not AsymSaturator::engaged(), because engaged()
    // returns true whenever bias != 0 (our kHpBias = 0.10f is always non-zero),
    // meaning it can never be the sole gate for a "no drive" bypass path.
    // Gate DC blocker on drive OR resonance: pure linear HP with no drive/resonance
    // needs no DC blocker.
    AsymSaturator pre; pre.setDrive(drive_, kHpBias, kHpDriveDb);
    const bool preOn = drive_ > 0.0f;
    const bool dcOn  = drive_ > 0.0f || resonance_ > 0.0f;

    for (int i = 0; i < n; ++i) {
        float l = left[i], r = right[i];
        if (preOn) { l = pre.process(l, 0, s.pre); r = pre.process(r, 1, s.pre); }
        s.a.process(l, r, NlSvfCell::HP);
        if (slope_ == Slope::db24) s.b.process(l, r, NlSvfCell::HP);
        if (dcOn) { l = s.dc.process(l, 0); r = s.dc.process(r, 1); }
        left[i] = l; right[i] = r;
    }
}
