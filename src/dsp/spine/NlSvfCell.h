#pragma once
#include <juce_core/juce_core.h>
#include <cmath>
#include <algorithm>

// Nonlinear TPT state-variable cell. Linear core identical to TptSvfCell (Cytomic
// SvfLinearTrapOptimised), plus a resonance-loop saturator injected as a delta on
// the cell input (the "fbExtra" technique): preserves the closed-form solve and
// vanishes as the signal gets small, so it stays ~linear at low level. The Q range
// is widened so it can self-oscillate near max resonance — only stable because the
// saturator self-limits the loop. See docs/architecture/nonlinear-filter-modeling.md.
class NlSvfCell {
public:
    enum Tap { LP = 0, HP = 1, BP = 2, Notch = 3 };

    void prepare(double sampleRate) noexcept { sampleRate_ = sampleRate; dirty_ = true; }
    void reset() noexcept {
        ic1_[0]=ic2_[0]=ic1_[1]=ic2_[1]=0.0f; bp_[0]=bp_[1]=0.0f;
    }
    void setCutoff(float hz) noexcept    { if (hz != cutoffHz_) { cutoffHz_ = hz; dirty_ = true; } }
    void setResonance(float r) noexcept  { if (r != resonance_) { resonance_ = r; dirty_ = true; } }
    void setResSat(float amt) noexcept   { resSat_ = std::clamp(amt, 0.0f, 1.0f); }

    void process(float& left, float& right, int tap) noexcept {
        if (dirty_) recompute();
        left  = step(left,  0, tap);
        right = step(right, 1, tap);
    }

private:
    float step(float v0, int ch, int tap) noexcept {
        if (resSat_ > 0.0f) {
            const float bpPrev = bp_[ch];
            v0 -= k_ * resSat_ * (satRes(bpPrev) - bpPrev);   // nonlinear correction only
        }
        const float v3 = v0 - ic2_[ch];
        const float v1 = a1_ * ic1_[ch] + a2_ * v3;
        const float v2 = ic2_[ch] + a2_ * ic1_[ch] + a3_ * v3;
        ic1_[ch] = 2.0f * v1 - ic1_[ch];
        ic2_[ch] = 2.0f * v2 - ic2_[ch];
        bp_[ch]  = v1;
        switch (tap) {
            case HP:    return v0 - k_ * v1 - v2;
            case BP:    return v1;
            case Notch: return v0 - k_ * v1;
            case LP:
            default:    return v2;
        }
    }
    static float satRes(float x) noexcept {            // asymmetric, monotonic, bounded
        constexpr float b = 0.18f;                     // asymmetry // CALIB
        return padTanh(x + b) - padTanh(b);            // f(0)=0
    }
    static float padTanh(float x) noexcept {           // Padé 3/2 tanh, clamped
        const float x2 = x * x;
        return std::clamp(x * (27.0f + x2) / (27.0f + 9.0f * x2), -1.0f, 1.0f);
    }
    void recompute() noexcept {
        const float cutoff = std::clamp(cutoffHz_, 16.0f, float(sampleRate_ * 0.45));
        const float res    = std::clamp(resonance_, 0.0f, 0.999f);
        const float Q = 0.5f + res * res * 49.5f;      // reaches Q~50 (self-osc) // CALIB
        g_ = float(std::tan(juce::MathConstants<double>::pi * cutoff / sampleRate_));
        k_ = 1.0f / Q;
        a1_ = 1.0f / (1.0f + g_ * (g_ + k_));
        a2_ = g_ * a1_;
        a3_ = g_ * a2_;
        dirty_ = false;
    }
    double sampleRate_ = 44100.0;
    float cutoffHz_ = 1000.0f, resonance_ = 0.0f, resSat_ = 0.0f;
    float g_=0, k_=0, a1_=0, a2_=0, a3_=0;
    bool  dirty_ = true;
    float ic1_[2]={0,0}, ic2_[2]={0,0}, bp_[2]={0,0};
};
