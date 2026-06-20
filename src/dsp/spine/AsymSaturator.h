#pragma once
#include <cmath>
#include <algorithm>

// Asymmetric tanh drive shaper with partial RMS level compensation. Memoryless:
//   g(x) = comp * tanh(gain*x + bias)
// Config (gain/bias/comp) is shared across voices; the shaper itself holds no
// per-voice state. (A 1st-order ADAA variant was tried and measured WORSE than
// plain tanh across k2000's drive range — see OverdriveDiagnosticTests — so the
// shaper is plain tanh; oversampling, if ever needed, is a v5.1 HQ-tier concern.)
class AsymSaturator {
public:
    // drive01 in [0,1] -> up to maxDriveDb of input gain. biasFixed is the stage's
    // fixed asymmetry (even-harmonic) offset. Call once per block.
    void setDrive(float drive01, float biasFixed, float maxDriveDb) noexcept {
        const float dB = std::max(0.0f, drive01) * maxDriveDb;
        gain_ = std::pow(10.0f, dB / 20.0f);
        bias_ = biasFixed;
        const float full = (gain_ > 1.0f) ? (1.0f / std::tanh(gain_)) : 1.0f;
        comp_ = 1.0f + 0.75f * (full - 1.0f);          // ~75% RMS compensation // CALIB
        engaged_ = (gain_ > 1.0001f) || (std::abs(bias_) > 1.0e-6f);
    }

    bool engaged() const noexcept { return engaged_; }

    float process(float x) const noexcept {
        return comp_ * std::tanh(gain_ * x + bias_);
    }

private:
    float gain_ = 1.0f, bias_ = 0.0f, comp_ = 1.0f;
    bool  engaged_ = false;
};
