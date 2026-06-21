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

HuggettFilter::Resolved HuggettFilter::resolve() const noexcept {
    using N = NlSvfCell;
    const bool sep0 = std::abs(separationOct_) < 1.0e-6f;
    switch (routing_) {
        case Routing::LP:
            if (slope_ == Slope::db24) return { N::LP, N::LP, true,  false, 1.0f };
            return { N::LP, N::LP, false, sep0, 1.0f };               // 12 dB: single iff sep==0 (D1a)
        case Routing::HP:
            if (slope_ == Slope::db24) return { N::HP, N::HP, true,  false, 1.0f };
            return { N::HP, N::HP, false, sep0, 1.0f };
        case Routing::BP:
            // a@cutA = HP (low edge) -> b@cutB = LP (high edge); separation = bandwidth.
            return { N::HP, N::LP, true, false, 1.0f };
        // --- Summit dual routings: slope ignored, both sections at cutA/cutB ---
        case Routing::SeriesLPHP: return { N::LP, N::HP, true,  false, 1.0f };
        case Routing::SeriesLPBP: return { N::LP, N::BP, true,  false, 1.0f };
        case Routing::SeriesHPBP: return { N::HP, N::BP, true,  false, 1.0f };
        case Routing::ParLPHP:    return { N::LP, N::HP, false, false, 0.5f };  // complementary -> sum/halve
        case Routing::ParLPBP:    return { N::LP, N::BP, false, false, 0.5f };
        case Routing::ParHPBP:    return { N::HP, N::BP, false, false, 0.5f };
        case Routing::ParLPLP:    return { N::LP, N::LP, false, false, 1.0f };  // same-tap -> keep +6 dB bump
        case Routing::ParBPBP:    return { N::BP, N::BP, false, false, 1.0f };
        case Routing::ParHPHP:    return { N::HP, N::HP, false, false, 1.0f };
        default:                  return { N::LP, N::LP, true,  false, 1.0f };
    }
}

void HuggettFilter::processStereo(State& s, float* left, float* right, int n) const noexcept {
    auto& vs = static_cast<VoiceState&>(s);
    const Resolved rz = resolve();
    const float half = separationOct_ * 0.5f;                        // CALIB: symmetric half-octave split
    const float cutA = cutoffHz_ * std::pow(2.0f, -half);
    const float cutB = cutoffHz_ * std::pow(2.0f,  half);

    const bool preOn     = preDrive_  > 0.0f;
    const bool postOn    = postDrive_ > 0.0f;
    const bool nonlinear = preOn || postOn || (resonance_ > 0.0f);

    vs.a.setCutoff(cutA); vs.a.setResonance(resonance_); vs.a.setResSat(resonance_);
    vs.b.setCutoff(cutB); vs.b.setResonance(resonance_); vs.b.setResSat(resonance_);

    for (int i = 0; i < n; ++i) {
        float l = left[i], r = right[i];
        if (preOn) { l = preSat_.process(l); r = preSat_.process(r); }

        if (rz.single) {
            vs.a.process(l, r, rz.tapA);
        } else if (rz.series) {
            vs.a.process(l, r, rz.tapA);
            vs.b.process(l, r, rz.tapB);
        } else { // parallel: both sections see the same input, outputs summed
            float la = l, ra = r, lb = l, rb = r;
            vs.a.process(la, ra, rz.tapA);
            vs.b.process(lb, rb, rz.tapB);
            l = rz.parGain * (la + lb);
            r = rz.parGain * (ra + rb);
        }

        if (postOn) { l = postSat_.process(l); r = postSat_.process(r); }
        if (nonlinear) { l = vs.dc.process(l, 0); r = vs.dc.process(r, 1); }
        left[i] = l; right[i] = r;
    }
}
