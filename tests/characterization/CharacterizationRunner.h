#pragma once
#include "OperatingPoint.h"
#include "DeviceUnderTest.h"
#include "FilterUnderTest.h"
#include <juce_core/juce_core.h>
#include <functional>
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

// Purpose-driven grids (Q28, docs/superpowers/specs/2026-07-07-purpose-grids-design.md).
// fullGrid() crosses every axis with every other (~40 h/model) and is dead weight as a
// routine instrument; each of these is dense only along the axes its purpose needs.
// Axes are pinned exactly per the design spec §3 — do not silently drift them.

// SP-D hardware-comparison map: the response map future Summit/Arturia captures are
// compared against. os8/Live/96k, all 5 modes, 15 log cutoffs 50 Hz-16 kHz, 6 resonances,
// drive=0 (drive lives in largeSignalGrid), 400 probes 20 Hz-24 kHz.
Grid spdGrid();

// OS/aliasing verification: aliasing falls as OS rises, at the points where aliasing lives.
// os{1,2,4,8} x osMode{Live,Render}, 96k, modes{LP24,BP}, cutoffs{4k,8k,16k}, res{0.9,1.0},
// drives{0,1}, 200 probes.
Grid osAliasGrid();

// Host-rate invariance spot-check. rates{44100,48000,88200,96000,192000}, os{1,8}/Live,
// modes{LP24,HP}, cutoffs{250,1k,4k}, res{0,0.9}, drive=0, 200 probes.
Grid hostRateGrid();

// Drive/resonance law (Q27/SP-B axis) — the operating-point lattice SP-B's level battery
// will reuse. os{1,8}/Live, 96k, LP24 only, cutoffs{250,1k,4k}, 6 resonances, 5 drives,
// 200 probes.
Grid largeSignalGrid();

// Headline metric summary.  Keys are e.g. "moog/LP24/fc1000/corner_hz".
// Summary key uniqueness: for grids with multiple os/res/host combos per (model,mode,cutoff),
// the summary is keyed on the BASE operating point (osFactor=1, OsMode::Live, lowest resonance
// in the grid, highest-quality host rate = 96000 or the grid's first if not present).
// This keeps keys deterministic regardless of grid density.
using Summary = std::map<juce::String, double>;

struct CharacterizationRunner {
    // Log-spaced frequency grid with n points from f0 to f1 (inclusive).
    static std::vector<double> logFreqs(double f0, double f1, int n);

    // Live progress sink (engagement item 6): called after every measured point with
    // (done, total, label). total is computed up front and stays stable; done ends
    // exactly at total. Default-empty: existing call sites are unaffected; printing is
    // the caller's job (the heavy runner wires a stderr line with ETA; tests stay silent).
    using Progress = std::function<void(int done, int total, const juce::String& label)>;

    // Run B1 (magnitude, dual-method) + B4 (phase/group delay) over the grid for one filter.
    // Writes response.csv into outDir (directory is created if absent).
    // Returns headline metrics. B2/B3 seam: extend via runB1OnePoint helper is already
    // isolated; add runB2/runB3 helpers and call them from run() for 8b.
    static Summary run(DeviceUnderTest& fut, const Grid& g, const juce::File& outDir,
                       const Progress& progress = {});

private:
    // Per-point B1+B4 measurement. Returns headline metrics for this point.
    // If (mode, cutoff) is the base-case operating point, it populates summaryOut.
    struct B1Result {
        double cornerHz      = -1.0;
        double slopeDbOct    = -1.0;
        double methodDeltaDb = -1.0;
        double peakGainDb     = -300.0;
        double passbandGainDb = -300.0;
    };

    static B1Result runB1OnePoint(DeviceUnderTest& fut, const OperatingPoint& op,
                                   const std::vector<double>& probeFreqs,
                                   juce::String& csvRows);

    // B2: resonance + self-oscillation at high resonance for a (mode, cutoff) pair.
    // op must already have its resonance set to the max resonance in the grid.
    struct B2Result {
        double selfoscHz       = -1.0;   // measured self-osc pitch (FFT peak)
        double selfoscCentsErr = -1.0;   // 1200 * log2(measured / cutoff)
    };

    static B2Result runB2OnePoint(DeviceUnderTest& fut, const OperatingPoint& op,
                                   juce::String& csvRows);

    // B3: distortion + aliasing at a single operating point (osFactor is part of op).
    // probeHz is the tone used for THD and aliasing measurements.
    struct B3Result {
        double thdDb    = -1.0;
        double aliasDb  = -1.0;
    };

    static B3Result runB3OnePoint(DeviceUnderTest& fut, const OperatingPoint& op,
                                   double probeHz, juce::String& csvRows);

    // M4 Trigger driver: capture a Generator device's emission at the base host
    // rate (tone frequency = first grid cutoff, snapped to an FFT bin for
    // leak-free level reading) and record absolute + A-weighted level.
    static Summary runGeneratorCapture(DeviceUnderTest& dut, const Grid& g,
                                       const juce::File& outDir);

    // Interpolate magDb (sampled at freqs) at target frequency, linearly on log-freq axis.
    static double interpMag(const std::vector<double>& freqs, const std::vector<double>& magDb,
                            double targetHz);
};

} // namespace chz
