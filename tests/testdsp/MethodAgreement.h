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
};

} // namespace testdsp
