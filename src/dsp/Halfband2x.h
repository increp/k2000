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
    // Halfband: every even tap is EXACTLY zero except the center (enforced in
    // buildCoeffs), so the even output phase is a pure scaled delay and only the
    // odd taps need a MAC — half the multiplies of the naive form. Numerics are
    // guarded by the golden equivalence vector in Halfband2xTests.
    void upsample(const float* in, int n, float* out) noexcept {
        constexpr int M = (kNumTaps - 1) / 2;        // center tap (the one nonzero even tap)
        for (int i = 0; i < n; ++i) {
            for (int j = kHistUp_ - 1; j > 0; --j) histUp_[(size_t) j] = histUp_[(size_t) j - 1];
            histUp_[0] = in[i];
            double o = 0.0;                          // odd-tap polyphase branch
            for (int t = 1; t < kNumTaps; t += 2) o += (double) h_[(size_t) t] * histUp_[(size_t) ((t-1)/2)];
            out[2*i]   = (float) (2.0 * (double) h_[(size_t) M] * histUp_[(size_t) (M/2)]);
            out[2*i+1] = (float) (2.0 * o);
        }
    }

    // 2n samples -> n base-rate samples (decimate by 2 after the AA filter).
    // Decimates on the EVEN phase (out[i] evaluates the filter over x[2i-t], like
    // the reference two-clock direct form — integer-sample round-trip preserved),
    // but exploits the exact halfband zeros (h[t] = 0 for even t != M, and h[0] = 0
    // so the just-arrived x[2i] contributes nothing) and advances the history by 2
    // in ONE pass: ~half the MACs and half the shift work of the naive form.
    // Invariant at loop top: histDown_[k] = x[2i-1-k]; so x[2i-t] = histDown_[t-1].
    void downsample(const float* in, int n, float* out) noexcept {
        constexpr int M = (kNumTaps - 1) / 2;        // 36 — the only nonzero even tap
        for (int i = 0; i < n; ++i) {
            double acc = (double) h_[(size_t) M] * histDown_[(size_t) (M - 1)];
            for (int t = 1; t < kNumTaps; t += 2)
                acc += (double) h_[(size_t) t] * histDown_[(size_t) (t - 1)];
            out[(size_t) i] = (float) acc;
            for (int j = kNumTaps - 1; j >= 2; --j) histDown_[(size_t) j] = histDown_[(size_t) (j - 2)];
            histDown_[1] = in[2*i];
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
                          : (k % 2 == 0) ? 0.0     // exact halfband zeros (sin(pi*k/2)=0) — load-bearing for the fast paths
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
