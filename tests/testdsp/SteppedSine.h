#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include <cmath>

// testdsp::SteppedSine — the trusted REFERENCE transfer-function engine.
// For each frequency f, drives a pure sine through the adapter, discards a warm-up
// window, then lock-in detects the steady-state response: correlate the output with
// sin(wt) and cos(wt) over the measurement window to recover complex amplitude.
//   magDb  = 20*log10(|H|),  phaseRad = arg(H) relative to the input sine.
// Adapter contract: void reset(); void process(float* buf, int n);  (no prepare — sr is a param)
namespace testdsp {

struct ComplexResponse {
    std::vector<double> freqHz, magDb, phaseRad;
};

struct SteppedSine {
    static constexpr int kWarmSamples = 8192;
    static constexpr int kMeasSamples = 16384;

    template <typename Adapter>
    static ComplexResponse transfer(Adapter& a, const std::vector<double>& freqs,
                                    double sr, float amp) {
        ComplexResponse out;
        out.freqHz = freqs;
        out.magDb.reserve(freqs.size());
        out.phaseRad.reserve(freqs.size());
        const double twoPi = 2.0 * juce::MathConstants<double>::pi;
        for (double f : freqs) {
            a.reset();
            const int total = kWarmSamples + kMeasSamples;
            double sinCorr = 0.0, cosCorr = 0.0;   // correlate output against sin(wt) and cos(wt) at f
            float buf[1];
            for (int i = 0; i < total; ++i) {
                const double ph = twoPi * f * i / sr;
                buf[0] = amp * (float) std::sin(ph);
                a.process(buf, 1);
                if (i >= kWarmSamples) {
                    sinCorr += (double) buf[0] * std::sin(ph);
                    cosCorr += (double) buf[0] * std::cos(ph);
                }
            }
            // Lock-in: output sine component amplitude is 2/N * (sinCorr + j*cosCorr); input amplitude is amp.
            const double scale = 2.0 / (double) kMeasSamples;
            const double re = sinCorr * scale, im = cosCorr * scale;
            const double magLin = std::sqrt(re * re + im * im) / (double) amp;
            out.magDb.push_back(magLin > 0.0 ? 20.0 * std::log10(magLin) : -300.0);
            out.phaseRad.push_back(std::atan2(im, re));
        }
        return out;
    }
};

} // namespace testdsp
