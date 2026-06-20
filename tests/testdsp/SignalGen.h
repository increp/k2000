#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include <cmath>
#include <cstdint>

namespace testdsp {
struct SignalGen {
    static constexpr double pi() { return juce::MathConstants<double>::pi; }

    static std::vector<float> sine(float amp, double freqHz, double sr, int n) {
        std::vector<float> v((size_t) n);
        for (int i = 0; i < n; ++i)
            v[(size_t) i] = amp * (float) std::sin(2.0 * pi() * freqHz * i / sr);
        return v;
    }
    static std::vector<float> binAlignedSine(float amp, int bin, int n) {
        std::vector<float> v((size_t) n);
        for (int i = 0; i < n; ++i)
            v[(size_t) i] = amp * (float) std::sin(2.0 * pi() * bin * i / n);
        return v;
    }
    static std::vector<float> impulse(float amp, int n) {
        std::vector<float> v((size_t) n, 0.0f); if (n > 0) v[0] = amp; return v;
    }
    static std::vector<float> dc(float val, int n) { return std::vector<float>((size_t) n, val); }
    static std::vector<float> silence(int n)       { return std::vector<float>((size_t) n, 0.0f); }
    static std::vector<float> whiteNoise(float amp, int n, uint32_t seed) {
        juce::Random rng((juce::int64) seed);
        std::vector<float> v((size_t) n);
        for (int i = 0; i < n; ++i) v[(size_t) i] = amp * (float) (rng.nextDouble() * 2.0 - 1.0);
        return v;
    }
};
} // namespace testdsp
