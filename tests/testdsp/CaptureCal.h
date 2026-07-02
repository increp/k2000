#pragma once
#include "SteppedSine.h"
#include "Level.h"
#include <vector>
#include <cmath>

// testdsp::CaptureCal -- capture-chain calibration math (spec §5.2, the
// "calibration weight"). Loopback: measure the chain with NO device in it;
// compensate: subtract the chain's coloration from a device-through-chain
// measurement; levelOffsetFromTone: absolute-level anchor from the documented
// calibration tone (nominal -18 dBFS). Validated synthetically now; SP-D points
// it at the real interface. compensate() requires the SAME probe grid used for
// calibration (no resampling — YAGNI until SP-D shows a need).
namespace testdsp {

struct CaptureCal {
    struct Calibration {
        std::vector<double> freqs;        // probe grid the chain was measured on
        std::vector<double> chainMagDb;   // chain-only (loopback) response, dB
    };

    template <typename Chain>
    static Calibration calibrateChain(Chain& chain, const std::vector<double>& probes,
                                      double sr, float amp) {
        auto r = SteppedSine::transfer(chain, probes, sr, amp);
        Calibration c;
        c.freqs      = probes;
        c.chainMagDb = r.magDb;
        return c;
    }

    // measuredMagDb must be sampled on cal.freqs (same grid, same order).
    static std::vector<double> compensate(const Calibration& cal,
                                          const std::vector<double>& measuredMagDb) {
        std::vector<double> out(measuredMagDb.size(), -300.0);
        const size_t n = std::min(cal.chainMagDb.size(), measuredMagDb.size());
        for (size_t i = 0; i < n; ++i)
            out[i] = measuredMagDb[i] - cal.chainMagDb[i];
        return out;
    }

    // Captured calibration tone -> chain level offset in dB (0 = transparent).
    // nominalPeakDbfs is the tone's PEAK level as emitted (sine RMS = peak - 3.01).
    static double levelOffsetFromTone(const std::vector<float>& capturedTone,
                                      double nominalPeakDbfs) {
        return Level::rmsDbfs(capturedTone) - (nominalPeakDbfs - 3.0103);
    }
};

} // namespace testdsp
