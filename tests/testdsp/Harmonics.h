#pragma once
#include "Spectrum.h"
#include "Metrics.h"
#include "SignalGen.h"
#include <vector>
#include <cmath>

// testdsp::Harmonics — stepped THD at a single tone (B3 distortion is single-method).
// Drives a bin-aligned tone through the adapter, discards a warm-up window, then
// measures THD+N (dB) from the FFT magnitude via Metrics::thdPlusNDb.
namespace testdsp {

struct Harmonics {
    template <typename Adapter>
    static double thdDb(Adapter& a, double f0, double sr, float amp) {
        const int N = 1 << 14;
        const int bin = std::max(2, (int) std::lround(f0 * N / sr));
        a.reset();
        // Warm up so the filter reaches steady state, then capture N bin-aligned samples.
        std::vector<float> warm = SignalGen::sine(amp, (double) bin * sr / N, sr, 8192);
        a.process(warm.data(), (int) warm.size());
        std::vector<float> cap = SignalGen::binAlignedSine(amp, bin, N);
        a.process(cap.data(), N);
        auto mag = Spectrum::magnitude(cap);
        return Metrics::thdPlusNDb(mag, bin);
    }
};

} // namespace testdsp
