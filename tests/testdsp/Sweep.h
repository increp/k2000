#pragma once
#include "Spectrum.h"
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <cmath>

// testdsp::Sweep — Farina exponential-sine-sweep (ESS) measurement core.
// Generate a log sweep f0->f1 over durSec; the inverse filter is the time-reversed
// sweep with an amplitude envelope that flattens its spectrum, so deconvolution of
// the system output with it yields the system impulse response. FFT-based linear
// convolution. See Farina, "Simultaneous measurement of impulse response and
// distortion with a swept-sine technique" (AES 2000).
namespace testdsp {

struct Sweep {
    // Exponential sine sweep, length round(durSec*sr) samples.
    static std::vector<float> ess(double f0, double f1, double durSec, double sr, float amp) {
        const int n = (int) std::lround(durSec * sr);
        const double w0 = 2.0 * juce::MathConstants<double>::pi * f0;
        const double w1 = 2.0 * juce::MathConstants<double>::pi * f1;
        const double K  = w0 * durSec / std::log(w1 / w0);
        const double Lr = std::log(w1 / w0) / durSec;     // 1/L
        std::vector<float> x((size_t) n);
        for (int i = 0; i < n; ++i) {
            const double t = (double) i / sr;
            x[(size_t) i] = amp * (float) std::sin(K * (std::exp(t * Lr) - 1.0));
        }
        return x;
    }

    // Inverse filter: time-reversed sweep, amplitude-modulated by e^{-t/L} (applied in
    // the reversed-time domain, amplifying high-freq content) so that conv(sweep, inverse)
    // ~ delta. Normalised so the delta peak ~1.
    static std::vector<float> inverseFilter(double f0, double f1, double durSec, double sr) {
        auto sweep = ess(f0, f1, durSec, sr, 1.0f);
        const int n = (int) sweep.size();
        const double Lr = std::log(f1 / f0) / durSec;   // = log(w1/w0)/durSec = 1/L
        std::vector<float> inv((size_t) n);
        // Apply the e^{-t/L} envelope in reversed-time coordinates: j is the position in inv,
        // t = j/sr is the time in the reversed signal (0 = high-freq end, T = low-freq end).
        // Stationary-phase analysis shows |G(f)| ~ sqrt(f), which exactly cancels the ESS
        // spectral slope |X(f)| ~ 1/sqrt(f), giving flat |X(f)*G(f)| = const.
        for (int j = 0; j < n; ++j) {
            const double t = (double) j / sr;
            const double env = std::exp(-t * Lr);
            inv[(size_t) j] = sweep[(size_t) (n - 1 - j)] * (float) env;
        }
        // Normalise so the autoconvolution peak is ~1 (keeps IR magnitudes interpretable).
        auto probe = impulseResponseRaw(sweep, inv);
        float pk = 0.0f; for (float v : probe) pk = std::max(pk, std::abs(v));
        if (pk > 0.0f) for (auto& v : inv) v /= pk;
        return inv;
    }

    // System output (already captured) deconvolved with the inverse filter -> IR.
    static std::vector<float> impulseResponse(const std::vector<float>& output,
                                              const std::vector<float>& invFilter) {
        return impulseResponseRaw(output, invFilter);
    }

private:
    // FFT linear convolution of a and b (full length a+b-1), real part returned.
    static std::vector<float> impulseResponseRaw(const std::vector<float>& a,
                                                 const std::vector<float>& b) {
        const int full = (int) a.size() + (int) b.size() - 1;
        int order = 0; while ((1 << order) < full) ++order;
        const int N = 1 << order;
        juce::dsp::FFT fft(order);
        std::vector<float> fa((size_t) (2 * N), 0.0f), fb((size_t) (2 * N), 0.0f);
        for (size_t i = 0; i < a.size(); ++i) fa[i] = a[i];
        for (size_t i = 0; i < b.size(); ++i) fb[i] = b[i];
        fft.performRealOnlyForwardTransform(fa.data());
        fft.performRealOnlyForwardTransform(fb.data());
        std::vector<float> prod((size_t) (2 * N), 0.0f);
        for (int k = 0; k <= N / 2; ++k) {
            const double ar = fa[(size_t) (2 * k)], ai = fa[(size_t) (2 * k + 1)];
            const double br = fb[(size_t) (2 * k)], bi = fb[(size_t) (2 * k + 1)];
            prod[(size_t) (2 * k)]     = (float) (ar * br - ai * bi);
            prod[(size_t) (2 * k + 1)] = (float) (ar * bi + ai * br);
        }
        fft.performRealOnlyInverseTransform(prod.data());
        std::vector<float> out((size_t) full);
        for (int i = 0; i < full; ++i) out[(size_t) i] = prod[(size_t) i];
        return out;
    }
};

} // namespace testdsp
