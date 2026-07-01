#pragma once
#include <vector>
#include <cmath>
#include <algorithm>

// testdsp::Level -- absolute-level / gain reductions. Pure functions over a
// magnitude response (dB, absolute gain from SteppedSine) or a time buffer (dBFS,
// 0 dBFS = +-1.0 full scale). No device knowledge.
namespace testdsp {

struct Level {
    // Highest gain anywhere in the response (e.g. the resonant peak), dB.
    static double peakGainDb(const std::vector<double>& magDb) {
        if (magDb.empty()) return -300.0;   // no-data sentinel (matches passbandGainDb)
        double m = -300.0;
        for (double v : magDb) m = std::max(m, v);
        return m;
    }

    enum class Passband { Low, High };

    // Gain at the passband anchor: lowest probe (LP / DC-side) or highest probe (HP), dB.
    static double passbandGainDb(const std::vector<double>& magDb, Passband p) {
        if (magDb.empty()) return -300.0;
        return p == Passband::Low ? magDb.front() : magDb.back();
    }

    // Peak sample level, dBFS (0 dBFS = +-1.0).
    static double peakDbfs(const std::vector<float>& buf) {
        if (buf.empty()) return -300.0;     // no-data sentinel (distinct from silence)
        float pk = 0.0f;
        for (float v : buf) pk = std::max(pk, std::abs(v));
        return 20.0 * std::log10(std::max((double) pk, 1.0e-9));
    }

    // RMS level, dBFS.
    static double rmsDbfs(const std::vector<float>& buf) {
        if (buf.empty()) return -300.0;     // no-data sentinel (distinct from silence)
        double s = 0.0;
        for (float v : buf) s += (double) v * v;
        const double r = std::sqrt(s / (double) buf.size());
        return 20.0 * std::log10(std::max(r, 1.0e-9));
    }

    // Crest factor = peak - rms, dB (0 for DC, ~3.01 for a sine).
    static double crestFactorDb(const std::vector<float>& buf) {
        if (buf.empty()) return -300.0;     // no-data sentinel (empty is NOT a 0 dB-crest DC signal)
        return peakDbfs(buf) - rmsDbfs(buf);
    }
};

} // namespace testdsp
