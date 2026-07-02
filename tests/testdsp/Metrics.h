#pragma once
#include "Spectrum.h"
#include <vector>
#include <cmath>

namespace testdsp {
struct Metrics {
    static bool  finite(const std::vector<float>& y) { return Spectrum::allFinite(y); }
    static float maxAbs(const std::vector<float>& y) { return Spectrum::maxAbs(y); }

    // M3: inharmonic energy below the fundamental, dB. `mag` = Spectrum::magnitude(signal).
    static double inharmonicDb(const std::vector<float>& mag, int fundamentalBin) {
        double fund = 0.0, inh = 0.0;
        for (int b = 2; b < (int) mag.size(); ++b) {
            const double e = double(mag[(size_t) b]) * mag[(size_t) b];
            if (b % fundamentalBin == 0) { if (b == fundamentalBin) fund = e; }
            else inh += e;
        }
        return fund > 0.0 ? 10.0 * std::log10(inh / fund) : 0.0;
    }
    // M4 perceptual lens: inharmonic energy SPLIT at the fundamental, each half in
    // dB relative to the fundamental. Below-fundamental aliasing is exposed and
    // dissonant (no harmonic masking under it) — the perceptually weighted half.
    // Harmonic bins (multiples of fundamentalBin) are excluded, like inharmonicDb.
    struct AliasSplit { double belowDb; double aboveDb; };
    static AliasSplit aliasSplit(const std::vector<float>& mag, int fundamentalBin) {
        double fund = 0.0, below = 0.0, above = 0.0;
        for (int b = 2; b < (int) mag.size(); ++b) {
            const double e = double(mag[(size_t) b]) * mag[(size_t) b];
            if (b % fundamentalBin == 0) { if (b == fundamentalBin) fund = e; continue; }
            if (b < fundamentalBin) below += e; else above += e;
        }
        if (fund <= 0.0) return { 0.0, 0.0 };
        return { 10.0 * std::log10(std::max(below, 1.0e-30) / fund),
                 10.0 * std::log10(std::max(above, 1.0e-30) / fund) };
    }
    // M6: (everything except fundamental) / fundamental, dB.
    static double thdPlusNDb(const std::vector<float>& mag, int fundamentalBin) {
        double fund = 0.0, rest = 0.0;
        for (int b = 2; b < (int) mag.size(); ++b) {
            const double e = double(mag[(size_t) b]) * mag[(size_t) b];
            if (b == fundamentalBin) fund = e; else rest += e;
        }
        return fund > 0.0 ? 10.0 * std::log10(rest / fund) : 0.0;
    }
    static float maxSampleDelta(const std::vector<float>& y) {
        float m = 0.0f;
        for (size_t i = 1; i < y.size(); ++i) m = std::max(m, std::abs(y[i] - y[i-1]));
        return m;
    }
    static float maxDiff(const std::vector<float>& a, const std::vector<float>& b) {
        float m = 0.0f; const size_t n = std::min(a.size(), b.size());
        for (size_t i = 0; i < n; ++i) m = std::max(m, std::abs(a[i] - b[i]));
        return m;
    }
    static double dcOffset(const std::vector<float>& y, int fromIndex) {
        double s = 0.0; int c = 0;
        for (int i = fromIndex; i < (int) y.size(); ++i) { s += y[(size_t) i]; ++c; }
        return c > 0 ? s / c : 0.0;
    }
    static float stereoCorrelation(const std::vector<float>& l, const std::vector<float>& r) {
        double sll = 0, srr = 0, slr = 0; const size_t n = std::min(l.size(), r.size());
        for (size_t i = 0; i < n; ++i) { sll += double(l[i])*l[i]; srr += double(r[i])*r[i]; slr += double(l[i])*r[i]; }
        const double d = std::sqrt(sll * srr);
        return d > 0.0 ? (float) (slr / d) : 0.0f;
    }
};
} // namespace testdsp
