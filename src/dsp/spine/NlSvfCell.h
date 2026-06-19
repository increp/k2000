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
        env_[0]=env_[1]=0.0f;
    }
    void setCutoff(float hz) noexcept    { if (std::abs(hz - cutoffHz_) > 0.0f) { cutoffHz_ = hz; dirty_ = true; } }
    void setResonance(float r) noexcept  { if (std::abs(r - resonance_) > 0.0f) { resonance_ = r; dirty_ = true; } }
    void setResSat(float amt) noexcept   { resSat_ = std::clamp(amt, 0.0f, 1.0f); }
    // Gate the level-dependent cutoff droop. When false, env_ is frozen and
    // gmScale is forced to 1.0 — so the linear path stays linear by construction.
    void setDroopActive(bool a) noexcept { droopActive_ = a; }

    // Called once per block by the owning filter so the droop envelope is applied
    // fresh each block (rather than only when setCutoff/setResonance detects a change).
    void updateBlock() noexcept { dirty_ = true; }

    void process(float& left, float& right, int tap) noexcept {
        if (dirty_) recompute();
        left  = step(left,  0, tap);
        right = step(right, 1, tap);
    }

private:
    float step(float v0, int ch, int tap) noexcept {
        // Track a slow per-channel input magnitude envelope for the droop.        // CALIB
        // env_ is read by recompute(); dirty_ is set by updateBlock() (called once
        // per block by the owning filter) so recompute() runs per-block, not per-sample.
        // Only updated when droopActive_ is true; frozen otherwise (linear path stays linear).
        if (droopActive_) env_[ch] += 0.0005f * (std::abs(v0) - env_[ch]);
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
    // Asymmetric, monotonic, bounded soft clip for the resonance feedback,
    // normalized to unit slope at the origin so the correction (satRes(x)-x)
    // is O(x^2) and vanishes at low level (true low-level linear equivalence).
    static float satRes(float x) noexcept {
        constexpr float b = 0.18f;                 // asymmetry (even harmonics) // CALIB
        const float s = 1.0f / padTanhDeriv(b);    // unit-slope normalization
        return (padTanh(x + b) - padTanh(b)) * s;  // f(0)=0, f'(0)=1
    }
    // d/dx of the UNCLAMPED Padé 3/2 tanh: padTanh(x)=x(27+x^2)/(27+9x^2).
    static float padTanhDeriv(float x) noexcept {
        const float x2 = x * x;
        const float den = 27.0f + 9.0f * x2;
        const float num = (27.0f + 3.0f * x2) * den - (27.0f * x + x * x2) * (18.0f * x);
        return num / (den * den);
    }
    static float padTanh(float x) noexcept {           // Padé 3/2 tanh, clamped
        const float x2 = x * x;
        return std::clamp(x * (27.0f + x2) / (27.0f + 9.0f * x2), -1.0f, 1.0f);
    }
    void recompute() noexcept {
        const float cutoff = std::clamp(cutoffHz_, 16.0f, float(sampleRate_ * 0.45));
        const float res    = std::clamp(resonance_, 0.0f, 0.999f);
        const float Q = 0.5f + res * res * 49.5f;      // reaches Q~50 (self-osc) // CALIB
        // Level-dependent cutoff droop: OTA "darkens when loud" tell.
        // Use the louder channel's envelope. Droop engages only above a threshold
        // (≈0.6 envelope units) so that low-level signals (peak ≤ 0.5) produce
        // env << threshold → zero droop → preserves linear-equivalence to 1e-5.
        // At loud input (amp ≈ 2, env ≈ 1.27) the excess is ~0.67 → measurable sag. // CALIB
        constexpr float kDroopThresh = 0.6f;          // below this: no droop         // CALIB
        constexpr float kDroopGain   = 0.40f;         // droop strength above thresh  // CALIB
        // When droopActive_ is false, drv=0 → excess=0 → gmScale=1.0 exactly (linear by construction).
        const float drv = droopActive_ ? std::max(env_[0], env_[1]) : 0.0f;
        const float excess = std::max(0.0f, drv - kDroopThresh);
        const float gmScale = 1.0f / (1.0f + kDroopGain * excess * excess);          // CALIB
        g_ = float(std::tan(juce::MathConstants<double>::pi * cutoff / sampleRate_)) * gmScale;
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
    bool  droopActive_ = false;   // gate: false → env_ frozen, gmScale=1.0, linear by construction
    float ic1_[2]={0,0}, ic2_[2]={0,0}, bp_[2]={0,0};
    float env_[2]={0,0};    // slow input-magnitude envelope for level-dependent droop
};
