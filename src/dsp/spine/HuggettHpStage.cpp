#include "HuggettHpStage.h"

HuggettHpStage::State* HuggettHpStage::makeState() const {
    auto* st = new State();
    st->a.prepare(sampleRate_);
    st->b.prepare(sampleRate_);
    st->dc.prepare(sampleRate_);
    return st;
}

void HuggettHpStage::reset(State& s) const noexcept {
    s.a.reset(); s.b.reset(); s.dc.reset();
}

void HuggettHpStage::processStereo(State& s, float* left, float* right, int n) const noexcept {
    // Clean HP — no drive shaper. The DC blocker / droop are only needed when the
    // resonance saturator is engaged (resonance > 0); a pure linear HP needs neither.
    const bool nl = resonance_ > 0.0f;

    s.a.setCutoff(cutoffHz_); s.a.setResonance(resonance_); s.a.setResSat(resonance_);
    s.b.setCutoff(cutoffHz_); s.b.setResonance(resonance_); s.b.setResSat(resonance_);

    for (int i = 0; i < n; ++i) {
        float l = left[i], r = right[i];
        s.a.process(l, r, NlSvfCell::HP);
        if (slope_ == Slope::db24) s.b.process(l, r, NlSvfCell::HP);
        if (nl) { l = s.dc.process(l, 0); r = s.dc.process(r, 1); }
        left[i] = l; right[i] = r;
    }
}
