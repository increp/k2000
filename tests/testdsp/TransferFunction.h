#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include <cmath>

// testdsp::TransferFunction — evaluate H(f) from an impulse response.
// Single-bin DFT at each requested frequency (Goertzel-style direct sum), so the
// frequency grid is arbitrary/log-spaced and independent of FFT bin spacing.
//   magDb = 20*log10(|H|), phaseRad = unwrapped arg(H),
//   groupDelaySec = -d(phase)/d(omega) via central finite difference on the grid.
namespace testdsp {

struct TransferFunction {
    struct Result {
        std::vector<double> freqHz, magDb, phaseRad, groupDelaySec;
    };

    static Result fromImpulse(const std::vector<float>& ir, double sr,
                              const std::vector<double>& freqs) {
        Result r; r.freqHz = freqs;
        const int N = (int) ir.size();
        const double twoPi = 2.0 * juce::MathConstants<double>::pi;
        std::vector<double> rawPhase;
        rawPhase.reserve(freqs.size());
        for (double f : freqs) {
            double re = 0.0, im = 0.0;
            const double w = twoPi * f / sr;
            for (int n = 0; n < N; ++n) {
                re += (double) ir[(size_t) n] * std::cos(w * n);
                im -= (double) ir[(size_t) n] * std::sin(w * n);
            }
            const double mag = std::sqrt(re * re + im * im);
            r.magDb.push_back(mag > 0.0 ? 20.0 * std::log10(mag) : -300.0);
            rawPhase.push_back(std::atan2(im, re));
        }
        // Unwrap phase along the grid.
        r.phaseRad = rawPhase;
        for (size_t i = 1; i < r.phaseRad.size(); ++i) {
            double d = r.phaseRad[i] - r.phaseRad[i - 1];
            while (d >  juce::MathConstants<double>::pi) { r.phaseRad[i] -= twoPi; d -= twoPi; }
            while (d < -juce::MathConstants<double>::pi) { r.phaseRad[i] += twoPi; d += twoPi; }
        }
        // Group delay = -dphase/domega, central difference (one-sided at the ends).
        r.groupDelaySec.assign(freqs.size(), 0.0);
        for (size_t i = 0; i < freqs.size(); ++i) {
            const size_t lo = (i == 0) ? 0 : i - 1;
            const size_t hi = (i + 1 < freqs.size()) ? i + 1 : i;
            if (hi == lo) continue;                                // single-point grid: leave at 0
            const double dW   = twoPi * (freqs[hi] - freqs[lo]);   // delta-omega (rad/s)
            const double dPhi = r.phaseRad[hi] - r.phaseRad[lo];
            r.groupDelaySec[i] = -dPhi / dW;
        }
        return r;
    }
};

} // namespace testdsp
