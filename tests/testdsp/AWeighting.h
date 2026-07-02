#pragma once
#include "Spectrum.h"
#include <vector>
#include <cmath>

// testdsp::AWeighting -- the IEC 61672 A-weighting curve as a labeled perceptual
// lens. Reported BESIDE flat dBFS numbers, never instead of them (spec rule:
// perceptual lenses are additional; the physics stays primary).
//
// aWeightedRmsDbfs is frequency-domain (Parseval): intended for bin-aligned tones
// and broadband noise. No analysis window is applied, so a non-bin-aligned tone
// leaks; callers that need exact single-tone numbers must bin-align (as the
// Generator capture driver does).
namespace testdsp {

struct AWeighting {
    // A-curve gain at f in dB (0 dB at 1 kHz). -300 sentinel for f <= 0.
    static double aWeightDb(double f) {
        if (f <= 0.0) return -300.0;
        const double f2 = f * f;
        const double r = (12194.0 * 12194.0 * f2 * f2)
                       / ((f2 + 20.6 * 20.6)
                          * std::sqrt((f2 + 107.7 * 107.7) * (f2 + 737.9 * 737.9))
                          * (f2 + 12194.0 * 12194.0));
        return 20.0 * std::log10(std::max(r, 1.0e-30)) + 2.0;
    }

    // A-weighted RMS of a time buffer, dBFS(A). Truncates to the largest
    // power-of-two prefix (Spectrum::magnitude requirement). -300 for empty.
    // Parseval with JUCE's unnormalized real FFT: sum x^2 = (1/N) * sum |X|^2,
    // and magnitude() returns bins 0..N/2-1, so rms = sqrt(2 * sum_{b>=1} |Xb*wb|^2) / N.
    // Bin 0 (DC) is skipped: A(0) = -inf; idle DC offset is a FLAT-metric concern.
    static double aWeightedRmsDbfs(const std::vector<float>& buf, double sr) {
        if (buf.empty()) return -300.0;
        int n = 1;
        while (n * 2 <= (int) buf.size()) n *= 2;
        std::vector<float> x(buf.begin(), buf.begin() + n);
        auto mag = Spectrum::magnitude(x);
        double acc = 0.0;
        for (int b = 1; b < (int) mag.size(); ++b) {
            const double f = (double) b * sr / n;
            const double w = std::pow(10.0, aWeightDb(f) / 20.0);
            const double m = (double) mag[(size_t) b] * w;
            acc += 2.0 * m * m;
        }
        const double rms = std::sqrt(acc) / (double) n;
        return 20.0 * std::log10(std::max(rms, 1.0e-9));
    }
};

} // namespace testdsp
