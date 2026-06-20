#pragma once
#include "Spectrum.h"
#include "SignalGen.h"
#include <vector>
#include <cmath>

namespace testdsp {
struct Reference {
    // Kaiser-windowed sinc low-pass.
    // fc: normalized cutoff (0..0.5), taps: filter length (odd recommended), beta: Kaiser beta.
    static std::vector<double> firLowpass(double fc, int taps, double beta) {
        std::vector<double> h((size_t) taps);
        const int M = taps - 1;
        const double i0b = besselI0(beta);
        double sum = 0.0;
        for (int i = 0; i < taps; ++i) {
            const double m = i - M / 2.0;
            const double sinc = (std::abs(m) < 1e-9)
                ? 2.0 * fc
                : std::sin(2.0 * juce::MathConstants<double>::pi * fc * m)
                  / (juce::MathConstants<double>::pi * m);
            const double r = 2.0 * (double) i / (double) M - 1.0;
            const double w = besselI0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0b;
            h[(size_t) i] = sinc * w;
            sum += h[(size_t) i];
        }
        for (auto& v : h) v /= sum;   // unity DC gain
        return h;
    }

    static double besselI0(double x) {
        double s = 1.0, term = 1.0;
        for (int k = 1; k < 25; ++k) {
            term *= (x * x) / (4.0 * (double) k * (double) k);
            s += term;
        }
        return s;
    }

    // Kaiser-windowed-sinc anti-alias FIR decimator.
    // Cutoff is set just below base-rate Nyquist (0.5/M * 0.9 in normalised high-rate units).
    // Returns hi.size() / M samples.
    static std::vector<float> decimate(const std::vector<float>& hi, int M) {
        const std::vector<double> fir = firLowpass(0.5 / (double) M * 0.9, 257, 8.0);
        const int taps  = (int) fir.size();
        const int hiLen = (int) hi.size();
        const int outN  = hiLen / M;
        std::vector<float> out((size_t) outN, 0.0f);
        for (int o = 0; o < outN; ++o) {
            const int center = o * M;
            double acc = 0.0;
            for (int t = 0; t < taps; ++t) {
                const int idx = center + (t - taps / 2);
                if (idx >= 0 && idx < hiLen)
                    acc += fir[(size_t) t] * (double) hi[(size_t) idx];
            }
            out[(size_t) o] = (float) acc;
        }
        return out;
    }

    // M4: noise-to-signal ratio in dB.
    // dut and truth are base-rate time-domain signals (same length, power of two).
    // Uses magnitude-spectrum difference as a phase-insensitive proxy (v1 / DAFx-16 ordering test).
    // Returns 10*log10( sum_non-fund |md - mt|^2 / mt[fundamentalBin]^2 ).
    static double noiseToSignalDb(const std::vector<float>& dut,
                                  const std::vector<float>& truth,
                                  int fundamentalBin) {
        const auto md = Spectrum::magnitude(dut);
        const auto mt = Spectrum::magnitude(truth);
        double noise = 0.0, sig = 0.0;
        for (int b = 2; b < (int) md.size(); ++b) {
            if (b == fundamentalBin) {
                sig = double(mt[(size_t) b]) * double(mt[(size_t) b]);
            } else {
                const double diff = double(md[(size_t) b]) - double(mt[(size_t) b]);
                noise += diff * diff;
            }
        }
        return sig > 0.0 ? 10.0 * std::log10(noise / sig) : 0.0;
    }
};
} // namespace testdsp
