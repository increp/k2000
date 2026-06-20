#pragma once
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <cmath>

namespace testdsp {
struct Spectrum {
    // Real-only FFT magnitude. x.size() must be a power of two. Returns N/2 bins.
    static std::vector<float> magnitude(const std::vector<float>& x) {
        const int n = (int) x.size();
        const int order = (int) std::log2((double) n);
        jassert((1 << order) == n);            // power-of-two required
        juce::dsp::FFT fft(order);
        std::vector<float> buf((size_t) (2 * n), 0.0f);
        for (int i = 0; i < n; ++i) buf[(size_t) i] = x[(size_t) i];
        fft.performRealOnlyForwardTransform(buf.data());
        std::vector<float> mag((size_t) (n / 2));
        for (int b = 0; b < n / 2; ++b) {
            const float re = buf[(size_t) (2 * b)], im = buf[(size_t) (2 * b + 1)];
            mag[(size_t) b] = std::sqrt(re * re + im * im);
        }
        return mag;
    }
    static float rms(const float* x, int n) {
        double s = 0.0; for (int i = 0; i < n; ++i) s += double(x[i]) * x[i];
        return n > 0 ? (float) std::sqrt(s / n) : 0.0f;
    }
    static float rms(const std::vector<float>& x) { return rms(x.data(), (int) x.size()); }
    static float maxAbs(const std::vector<float>& x) {
        float m = 0.0f; for (float v : x) m = std::max(m, std::abs(v)); return m;
    }
    static bool allFinite(const std::vector<float>& x) {
        for (float v : x) {
            if (! std::isfinite(v)) return false;
        }
        return true;
    }
};
} // namespace testdsp
