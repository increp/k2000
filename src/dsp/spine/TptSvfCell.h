#pragma once
#include <juce_core/juce_core.h>
#include <cmath>
#include <algorithm>

// One heap-free stereo TPT state-variable cell. Same topology as the v1
// SVFFilter block, but a value type with its own L/R integrator state so a
// model can hold several. Taps: LP/BP/HP/Notch.
class TptSvfCell {
public:
    enum Tap { LP = 0, HP = 1, BP = 2, Notch = 3 };

    void prepare(double sampleRate) noexcept { sampleRate_ = sampleRate; dirty_ = true; }
    void reset() noexcept { ic1_[0] = ic2_[0] = ic1_[1] = ic2_[1] = 0.0f; }

    void setCutoff(float hz) noexcept    { if (hz != cutoffHz_)  { cutoffHz_  = hz; dirty_ = true; } }
    void setResonance(float r) noexcept  { if (r != resonance_)  { resonance_ = r;  dirty_ = true; } }

    // Process one stereo sample in place at the given tap.
    void process(float& left, float& right, int tap) noexcept {
        if (dirty_) recompute();
        left  = step(left,  0, tap);
        right = step(right, 1, tap);
    }

private:
    float step(float v0, int ch, int tap) noexcept {
        const float v3 = v0 - ic2_[ch];
        const float v1 = a1_ * ic1_[ch] + a2_ * v3;
        const float v2 = ic2_[ch] + a2_ * ic1_[ch] + a3_ * v3;
        ic1_[ch] = 2.0f * v1 - ic1_[ch];
        ic2_[ch] = 2.0f * v2 - ic2_[ch];
        switch (tap) {
            case HP:    return v0 - k_ * v1 - v2;
            case BP:    return v1;
            case Notch: return v0 - k_ * v1;
            case LP:
            default:    return v2;
        }
    }

    void recompute() noexcept {
        const float cutoff = std::clamp(cutoffHz_, 16.0f, float(sampleRate_ * 0.45));
        const float res    = std::clamp(resonance_, 0.0f, 0.999f);
        const float Q = 0.5f + res * res * 8.5f;   // matches SVFFilter's calibrated range
        g_ = float(std::tan(juce::MathConstants<double>::pi * cutoff / sampleRate_));
        k_ = 1.0f / Q;
        a1_ = 1.0f / (1.0f + g_ * (g_ + k_));
        a2_ = g_ * a1_;
        a3_ = g_ * a2_;
        dirty_ = false;
    }

    double sampleRate_ = 44100.0;
    float cutoffHz_ = 1000.0f, resonance_ = 0.0f;
    float g_ = 0, k_ = 0, a1_ = 0, a2_ = 0, a3_ = 0;
    bool dirty_ = true;
    float ic1_[2] = {0, 0}, ic2_[2] = {0, 0};
};
