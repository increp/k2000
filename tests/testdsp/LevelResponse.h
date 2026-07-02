#pragma once
#include "SignalGen.h"
#include "Spectrum.h"
#include "Metrics.h"
#include "Level.h"
#include <vector>
#include <cmath>

// testdsp::LevelResponse -- multi-level excitation (spec §4.1 "large-signal").
// Sweeps a bin-aligned tone across input levels and reads, per level: output
// peak/RMS (dBFS), fundamental gain (dB, FFT-bin amplitude out/in), THD+N (dB).
// Extractors reduce the curve to the two headline numbers: the compression knee
// (input dBFS where fundamental gain has dropped dropDb below small-signal gain)
// and headroom-to-clip (input dBFS where output peak reaches ceilingDbfs).
// -300 sentinel = the sweep never reached the condition.
namespace testdsp {

struct LevelResponse {
    struct Point {
        double inDbfs      = -300.0;
        double outPeakDbfs = -300.0;
        double outRmsDbfs  = -300.0;
        double gainDb      = -300.0;
        double thdDb       = -300.0;
    };

    template <typename Adapter>
    static std::vector<Point> measure(Adapter& a, double f0, double sr,
                                      const std::vector<double>& ampsDbfs) {
        const int N   = 1 << 14;
        const int bin = std::max(2, (int) std::lround(f0 * N / sr));
        std::vector<Point> pts;
        pts.reserve(ampsDbfs.size());
        for (double aDb : ampsDbfs) {
            const float amp = (float) std::pow(10.0, aDb / 20.0);
            a.reset();
            // Warm-up so stateful devices reach steady state (harmless for memoryless).
            auto warm = SignalGen::sine(amp, (double) bin * sr / N, sr, 8192);
            a.process(warm.data(), (int) warm.size());
            auto cap = SignalGen::binAlignedSine(amp, bin, N);
            a.process(cap.data(), N);
            auto mag = Spectrum::magnitude(cap);

            Point p;
            p.inDbfs      = aDb;
            p.outPeakDbfs = Level::peakDbfs(cap);
            p.outRmsDbfs  = Level::rmsDbfs(cap);
            // Unnormalized real FFT: a bin-aligned sine of amplitude A reads |X| = A*N/2.
            const double outAmp = 2.0 * (double) mag[(size_t) bin] / (double) N;
            p.gainDb = 20.0 * std::log10(std::max(outAmp / (double) amp, 1.0e-15));
            p.thdDb  = Metrics::thdPlusNDb(mag, bin);
            pts.push_back(p);
        }
        return pts;
    }

    static double kneeInDbfs(const std::vector<Point>& pts, double dropDb) {
        if (pts.size() < 2) return -300.0;
        const double g0 = pts.front().gainDb;
        for (size_t i = 1; i < pts.size(); ++i) {
            const double d0 = g0 - pts[i - 1].gainDb;
            const double d1 = g0 - pts[i].gainDb;
            if (d1 >= dropDb) {
                if (d1 <= d0) return pts[i].inDbfs;   // non-monotonic guard
                const double t = (dropDb - d0) / (d1 - d0);
                return pts[i - 1].inDbfs + t * (pts[i].inDbfs - pts[i - 1].inDbfs);
            }
        }
        return -300.0;
    }

    static double headroomToClipInDbfs(const std::vector<Point>& pts, double ceilingDbfs) {
        for (size_t i = 0; i < pts.size(); ++i) {
            if (pts[i].outPeakDbfs >= ceilingDbfs) {
                if (i == 0) return pts[0].inDbfs;
                const double p0 = pts[i - 1].outPeakDbfs, p1 = pts[i].outPeakDbfs;
                if (p1 <= p0) return pts[i].inDbfs;   // non-monotonic guard
                const double t = (ceilingDbfs - p0) / (p1 - p0);
                return pts[i - 1].inDbfs + t * (pts[i].inDbfs - pts[i - 1].inDbfs);
            }
        }
        return -300.0;
    }
};

} // namespace testdsp
