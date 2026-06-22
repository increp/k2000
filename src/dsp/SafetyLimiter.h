#pragma once
#include <algorithm>
#include <cmath>

// Global output safety limiter: stereo-linked, zero-latency. A fast peak limiter
// (instant attack, exponential release) rides peaks down to the ceiling; a hard-clip
// backstop makes exceeding the ceiling impossible. Header-only, dependency-free.
// See docs/superpowers/specs/2026-06-21-safety-limiter-design.md.
class SafetyLimiter {
public:
    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        ceilingLin_ = std::pow(10.0f, kSafetyCeilingDb / 20.0f);
        relCoeff_   = (float) std::exp(-1.0 / (double(kReleaseMs) * 0.001 * sampleRate_));
        reset();
    }
    void reset() noexcept { gain_ = 1.0f; grMaxDb_ = 0.0f; }

    // In-place, stereo-linked. right may be nullptr (mono). n samples.
    void process(float* left, float* right, int n) noexcept {
        grMaxDb_ = 0.0f;
        for (int i = 0; i < n; ++i) {
            const float l = left[i];
            const float r = right ? right[i] : 0.0f;
            const float peak = std::max(std::abs(l), std::abs(r));
            const float target = (peak > ceilingLin_) ? (ceilingLin_ / peak) : 1.0f;  // <= 1
            // instant attack (clamp down now); exponential release (ease back up)
            gain_ = (target < gain_) ? target : (target + (gain_ - target) * relCoeff_);
            left[i] = std::clamp(l * gain_, -ceilingLin_, ceilingLin_);   // + hard-clip backstop
            if (right) right[i] = std::clamp(r * gain_, -ceilingLin_, ceilingLin_);
            if (gain_ < 1.0f) grMaxDb_ = std::max(grMaxDb_, -20.0f * std::log10(gain_));
        }
    }

    float gainReductionDb() const noexcept { return grMaxDb_; }

private:
    static constexpr float kSafetyCeilingDb = -12.0f;  // CALIB — tune after smoke-test
    static constexpr float kReleaseMs       = 40.0f;   // CALIB
    double sampleRate_ = 48000.0;
    float  ceilingLin_ = 0.2511886f;   // pow(10, -12/20); recomputed in prepare()
    float  relCoeff_   = 0.0f;
    float  gain_       = 1.0f;
    float  grMaxDb_    = 0.0f;
};
