#pragma once
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
    void setCutoff(float hz) noexcept    { if (std::abs(hz - cutoffHz_) > 0.0f) { cutoffHz_ = hz; dirty_ = true; } }
    void setResonance(float r) noexcept  { if (std::abs(r - resonance_) > 0.0f) { resonance_ = r; dirty_ = true; } }
    void setResSat(float amt) noexcept   { resSat_ = std::clamp(amt, 0.0f, 1.0f); }

    void process(float& left, float& right, int tap) noexcept {
        if (dirty_) recompute();
        left  = step(left,  0, tap);
        right = step(right, 1, tap);
    }

private:
    float step(float v0, int ch, int tap) noexcept {
        const bool nl = resSat_ > 0.0f;
        if (nl) {
            const float bpPrev = bp_[ch];
            // Q27 fix (2026-07-02): operands were transposed — the delta used to be
            // (satRes(bp) - bp), i.e. POSITIVE band-pass feedback growing with
            // amplitude (anti-damping -> +86 dB gain, +89 dBFS output at musical
            // input). Correct sense: extra DAMPING that grows as the loop saturates,
            // still O(x^2) so the low-level linear equivalence is untouched.
            // |k_|: in the regenerative top of the knob k_ goes negative (self-osc);
            // the saturation delta must stay a damping term regardless.
            v0 -= std::abs(k_) * resSat_ * (bpPrev - satRes(bpPrev));
        }
        const float v3 = v0 - ic2_[ch];
        const float v1 = a1_ * ic1_[ch] + a2_ * v3;
        const float v2 = ic2_[ch] + a2_ * ic1_[ch] + a3_ * v3;
        ic1_[ch] = 2.0f * v1 - ic1_[ch];
        ic2_[ch] = 2.0f * v2 - ic2_[ch];
        if (nl) {
            // OTA-style state rails: the absolute bound (real integrators clip at the
            // supply; this is what makes analog resonance self-limit and what sets the
            // self-osc amplitude). Blended by resSat so engagement is CONTINUOUS in
            // the knob — a binary gate here clicked on the first increment (field bug
            // 2026-07-02).
            ic1_[ch] += resSat_ * (rail(ic1_[ch]) - ic1_[ch]);
            ic2_[ch] += resSat_ * (rail(ic2_[ch]) - ic2_[ch]);
        }
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
    // Soft state rail at ±4 (OTA supply analogue). Unit slope at 0; deviation is
    // negligible for |x| < 1, so normal-level audio never touches it. // CALIB
    static float rail(float x) noexcept { return padTanh(x * 0.25f) * 4.0f; }
    void recompute() noexcept {
        const float cutoff = std::clamp(cutoffHz_, 16.0f, float(sampleRate_ * 0.45));
        const float res    = std::clamp(resonance_, 0.0f, 0.999f);
        const float Q = 0.5f + res * res * 49.5f;      // reaches Q~50 // CALIB
        constexpr double kPi = 3.14159265358979323846;
        g_ = float(std::tan(kPi * cutoff / sampleRate_));
        k_ = 1.0f / Q;
        // Regenerative top of the knob (field bug 2026-07-02: after the Q27 fix the
        // filter no longer whistled — the old 'self-osc' was powered by the defect).
        // Real analog crosses the oscillation threshold near max resonance: damping
        // fades through zero to slightly negative; the state rails set the whistle
        // amplitude. res is clamped to 0.999 so t <= 0.98. // CALIB start + depth
        {
            constexpr float kOscStart = 0.95f;
            constexpr float kOscDepth = 0.012f;
            if (res > kOscStart) {
                const float t = (res - kOscStart) / (1.0f - kOscStart);
                k_ = k_ * (1.0f - t) - kOscDepth * t;
            }
        }
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
