#pragma once
#include <cmath>
#include <algorithm>

// Asymmetric tanh drive shaper with 1st-order antiderivative antialiasing (ADAA)
// and partial RMS level compensation. Config (gain/bias/comp) is shared across
// voices; the ADAA memory is per-voice (State). g(x) = comp * tanh(gain*x + bias).
// ADAA is applied on g(x): y[n] = (G(x[n]) - G(x[n-1])) / (x[n] - x[n-1]),
// G(x) = (comp/gain) * logcosh(gain*x + bias). Midpoint fallback when x ~= x[-1].
class AsymSaturator {
public:
    struct State {
        float x1[2] = {0.0f, 0.0f};   // previous raw input, per channel
        float G1[2] = {0.0f, 0.0f};   // previous antiderivative value, per channel
        void reset() noexcept { x1[0]=x1[1]=0.0f; G1[0]=G1[1]=0.0f; }
    };

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

    float process(float x, int ch, State& s) const noexcept {
        const float u = gain_ * x + bias_;
        const float G = (comp_ / gain_) * logcosh(u);
        const float dx = x - s.x1[ch];
        float y;
        if (std::abs(dx) > 1.0e-5f)
            y = (G - s.G1[ch]) / dx;
        else
            y = comp_ * std::tanh(gain_ * (0.5f * (x + s.x1[ch])) + bias_); // midpoint
        s.x1[ch] = x;
        s.G1[ch] = G;
        return y;
    }

private:
    static float logcosh(float z) noexcept {
        const float a = std::abs(z);
        return a + std::log1p(std::exp(-2.0f * a)) - 0.6931472f; // ln 2
    }
    float gain_ = 1.0f, bias_ = 0.0f, comp_ = 1.0f;
    bool  engaged_ = false;
};
