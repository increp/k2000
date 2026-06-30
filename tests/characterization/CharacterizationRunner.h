#pragma once
#include "OperatingPoint.h"
#include "FilterUnderTest.h"
#include <juce_core/juce_core.h>
#include <map>
#include <vector>

namespace chz {

// Grid — the parameter space swept by CharacterizationRunner.
// coarseGrid() is the fast/CI grid; fullGrid() is the heavy exhaustive grid.
struct Grid {
    std::vector<Mode>   modes;
    std::vector<double> cutoffs, resonances, drives, hostRates, probeFreqs;
    std::vector<int>    osFactors;
    std::vector<OsMode> osModes;
};

// Fast/CI grid: single 96 kHz host rate, 4 OS factors, Live only, coarse cutoff/res sweep.
Grid coarseGrid();

// Heavy exhaustive grid: 5 host rates, OS {1,2,4,8} x {Live,Render}, dense cutoff/res/drive.
Grid fullGrid();

// Headline metric summary.  Keys are e.g. "moog/LP24/fc1000/corner_hz".
// Summary key uniqueness: for grids with multiple os/res/host combos per (model,mode,cutoff),
// the summary is keyed on the BASE operating point (osFactor=1, OsMode::Live, lowest resonance
// in the grid, highest-quality host rate = 96000 or the grid's first if not present).
// This keeps keys deterministic regardless of grid density.
using Summary = std::map<juce::String, double>;

struct CharacterizationRunner {
    // Log-spaced frequency grid with n points from f0 to f1 (inclusive).
    static std::vector<double> logFreqs(double f0, double f1, int n);

    // Run B1 (magnitude, dual-method) + B4 (phase/group delay) over the grid for one filter.
    // Writes response.csv into outDir (directory is created if absent).
    // Returns headline metrics. B2/B3 seam: extend via runB1OnePoint helper is already
    // isolated; add runB2/runB3 helpers and call them from run() for 8b.
    static Summary run(FilterUnderTest& fut, const Grid& g, const juce::File& outDir);

private:
    // Per-point B1+B4 measurement. Returns headline metrics for this point.
    // If (mode, cutoff) is the base-case operating point, it populates summaryOut.
    struct B1Result {
        double cornerHz      = -1.0;
        double slopeDbOct    = -1.0;
        double methodDeltaDb = -1.0;
    };

    static B1Result runB1OnePoint(FilterUnderTest& fut, const OperatingPoint& op,
                                   const std::vector<double>& probeFreqs,
                                   juce::String& csvRows);

    // Interpolate magDb (sampled at freqs) at target frequency, linearly on log-freq axis.
    static double interpMag(const std::vector<double>& freqs, const std::vector<double>& magDb,
                            double targetHz);
};

} // namespace chz
