#pragma once
#include "Spectrum.h"
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <cmath>

// testdsp::Response — steady-state magnitude and self-oscillation pitch helpers.
//
// magDb<Adapter>(a, f, sr, amp)
//   Feed a sine at `f` Hz through adapter `a`, discard a warm-up window, measure
//   RMS of remaining output vs RMS of remaining input, return 20*log10(|out|/|in|).
//   Adapter contract: prepare(double sr), reset(), process(float* buf, int n).
//
// peakFreqHz(signal, sr)
//   Finds the peak FFT magnitude bin and converts it to Hz.
//   Useful for measuring self-oscillation pitch after a kick impulse.

namespace testdsp {

struct Response {
    static constexpr int kWarmSamples = 8192;
    static constexpr int kMeasSamples = 8192;

    // Steady-state magnitude response: |out|/|in| in dB (RMS-based).
    // Adapter must have: void prepare(double sr), void reset(), void process(float* buf, int n).
    template <typename Adapter>
    static double magDb(Adapter& a, double f, double sr, float amp) {
        a.reset();
        const int total = kWarmSamples + kMeasSamples;
        double inSq  = 0.0;
        double outSq = 0.0;
        float buf[1];
        for (int i = 0; i < total; ++i) {
            const float x = amp * (float) std::sin(2.0 * juce::MathConstants<double>::pi * f * i / sr);
            buf[0] = x;
            a.process(buf, 1);
            if (i >= kWarmSamples) {
                inSq  += double(x)      * x;
                outSq += double(buf[0]) * buf[0];
            }
        }
        if (inSq <= 0.0 || outSq <= 0.0) return -300.0;
        return 20.0 * std::log10(std::sqrt(outSq / inSq));
    }

    // FFT-peak bin → Hz.  signal must be a power-of-two length.
    // Skips DC (bin 0) — self-oscillation is always above 0 Hz.
    static double peakFreqHz(const std::vector<float>& signal, double sr) {
        const auto mag = Spectrum::magnitude(signal);
        int    peakBin  = 1;
        float  peakMag  = mag[1];
        for (int b = 2; b < (int) mag.size(); ++b) {
            if (mag[(size_t) b] > peakMag) {
                peakMag = mag[(size_t) b];
                peakBin = b;
            }
        }
        return (double) peakBin * sr / (double) signal.size();
    }
};

} // namespace testdsp
