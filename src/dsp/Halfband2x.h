#pragma once
#include <array>
#include <cmath>

// Linear-phase halfband FIR for 2x up/down-sampling. Portable (no JUCE).
// Cutoff = 0.25 cyc/sample at the 2x rate (= base Nyquist). Blackman-Harris
// windowed sinc; symmetric => linear phase, group delay (kNumTaps-1)/2 at 2x rate.
class Halfband2x {
public:
    static constexpr int kNumTaps = 73;              // odd; center M = 36 (even)
    static constexpr int delay2x() { return (kNumTaps - 1) / 2; }   // 36 samples @ 2x rate

    Halfband2x() { buildCoeffs(); reset(); }

    void reset() noexcept { histUp_.fill(0.0f); histDown_.fill(0.0f); }

    // n base-rate samples -> 2n samples (polyphase, x2 gain compensation).
    void upsample(const float* in, int n, float* out) noexcept {
        for (int i = 0; i < n; ++i) {
            for (int j = kHistUp_ - 1; j > 0; --j) histUp_[(size_t) j] = histUp_[(size_t) j - 1];
            histUp_[0] = in[i];
            double e = 0.0, o = 0.0;                 // even-tap / odd-tap polyphase branches
            for (int t = 0; t < kNumTaps; t += 2) e += (double) h_[(size_t) t] * histUp_[(size_t) (t/2)];
            for (int t = 1; t < kNumTaps; t += 2) o += (double) h_[(size_t) t] * histUp_[(size_t) ((t-1)/2)];
            out[2*i]   = (float) (2.0 * e);
            out[2*i+1] = (float) (2.0 * o);
        }
    }

    // 2n samples -> n base-rate samples (decimate by 2 after the AA filter).
    // Decimate on the EVEN phase: clock in[2i], evaluate the filter, then clock
    // in[2i+1] without evaluating. Keeps downsample(upsample(x)) a clean
    // integer-sample round-trip (no half-sample offset).
    void downsample(const float* in, int n, float* out) noexcept {
        for (int i = 0; i < n; ++i) {
            for (int j = kNumTaps - 1; j > 0; --j) histDown_[(size_t) j] = histDown_[(size_t) j - 1];
            histDown_[0] = in[2*i];
            double acc = 0.0;
            for (int t = 0; t < kNumTaps; ++t) acc += (double) h_[(size_t) t] * histDown_[(size_t) t];
            out[(size_t) i] = (float) acc;
            for (int j = kNumTaps - 1; j > 0; --j) histDown_[(size_t) j] = histDown_[(size_t) j - 1];
            histDown_[0] = in[2*i + 1];
        }
    }

private:
    static constexpr int kHistUp_ = (kNumTaps + 1) / 2;   // 37 base samples spanned

    void buildCoeffs() noexcept {
        constexpr double pi = 3.14159265358979323846;
        const int M = (kNumTaps - 1) / 2;
        double sum = 0.0;
        for (int n = 0; n < kNumTaps; ++n) {
            const int k = n - M;
            const double s = (k == 0) ? 0.5
                          : 0.5 * std::sin(0.5 * pi * k) / (0.5 * pi * k);   // 0.5*sinc(0.5k)
            const double w = 0.35875
                           - 0.48829 * std::cos(2.0*pi*n/(kNumTaps-1))
                           + 0.14128 * std::cos(4.0*pi*n/(kNumTaps-1))
                           - 0.01168 * std::cos(6.0*pi*n/(kNumTaps-1));       // Blackman-Harris
            const double sw = s * w;
            h_[(size_t) n] = (float) sw;
            sum += sw;
        }
        for (int n = 0; n < kNumTaps; ++n) h_[(size_t) n] = (float) (h_[(size_t) n] / sum); // DC gain = 1
    }

    std::array<float, kNumTaps>  h_{};
    std::array<float, kHistUp_>  histUp_{};
    std::array<float, kNumTaps>  histDown_{};
};
