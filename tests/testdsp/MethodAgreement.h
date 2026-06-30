#pragma once
#include <vector>
#include <cmath>
#include <algorithm>

// testdsp::MethodAgreement — compares two magnitude curves sampled at the SAME
// frequency grid. The dual-method gate: the Farina ESS magnitude must match the
// trusted stepped-sine magnitude within tolerance, proving the deconvolution.
namespace testdsp {

struct MethodAgreement {
    // Max |magA[i] - magB[i]| in dB over the shared grid. Callers pass curves sampled at
    // identical frequencies; if lengths differ, the comparison is clamped to the shorter
    // (defensive — a mismatched grid is a caller error, never silently padded).
    static double maxMagDeltaDb(const std::vector<double>& magA,
                                const std::vector<double>& magB) {
        const size_t n = std::min(magA.size(), magB.size());
        double worst = 0.0;
        for (size_t i = 0; i < n; ++i)
            worst = std::max(worst, std::abs(magA[i] - magB[i]));
        return worst;
    }

    // Max |magA[i] - magB[i]| restricted to the MEANINGFUL band: points where the trusted
    // reference curve magA is within floorBelowPeakDb of its own peak. The stopband below that
    // — where both methods approach their numerical / noise floor and disagreement is a
    // measurement artifact, not a real divergence (e.g. a Moog HP filter at -60 dB scatters
    // ~5 dB between methods, vs <0.7 dB down to -40 dB) — is excluded. Default 40 dB covers
    // passband + corner + initial transition, where the dual-method cross-check is reliable.
    // magA must be the trusted reference (stepped-sine).
    static double maxMagDeltaDbInBand(const std::vector<double>& magA,
                                      const std::vector<double>& magB,
                                      double floorBelowPeakDb = 40.0) {
        const size_t n = std::min(magA.size(), magB.size());
        if (n == 0) return 0.0;
        double peak = magA[0];
        for (size_t i = 1; i < n; ++i) peak = std::max(peak, magA[i]);
        const double thresh = peak - floorBelowPeakDb;
        double worst = 0.0;
        for (size_t i = 0; i < n; ++i)
            if (magA[i] >= thresh)
                worst = std::max(worst, std::abs(magA[i] - magB[i]));
        return worst;
    }
};

} // namespace testdsp
