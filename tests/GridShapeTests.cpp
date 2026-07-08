#include <juce_core/juce_core.h>
#include "characterization/CharacterizationRunner.h"
#include <cmath>
#include <vector>

// Purpose-driven characterization grids (Q28, design spec
// docs/superpowers/specs/2026-07-07-purpose-grids-design.md). chz::fullGrid()
// crosses every axis with every other (~40 h/model) and is dead weight as a
// routine instrument. Each purpose grid is dense only along the axes its
// purpose needs: spd (SP-D hardware-comparison map), osalias (OS/aliasing
// verification), rates (host-rate portability), largesig (drive/resonance
// law). This test locks each factory's axes to the spec's §3 table so a
// future edit can't silently drift the grids the dashboard's --grid options
// and the SP-D/SP-B/SP-C consumers depend on.

namespace {

bool approxEqual(double a, double b, double tol = 1.0e-6) {
    return std::abs(a - b) <= tol;
}

template <typename T>
bool contains(const std::vector<T>& v, T val) {
    for (const auto& x : v) if (x == val) return true;
    return false;
}

bool containsD(const std::vector<double>& v, double val, double tol = 1.0e-6) {
    for (double x : v) if (approxEqual(x, val, tol)) return true;
    return false;
}

} // namespace

struct GridShapeTests : public juce::UnitTest {
    GridShapeTests() : juce::UnitTest("GridShape") {}

    void runTest() override {
        beginTest("purpose grids match the v5.23 design axes");

        // ---- spdGrid: SP-D hardware-comparison map ----
        // os{8} osMode{Live} rate{96000} modes all 5 cutoffs 15 log 50Hz-16kHz
        // res{0,.2,.4,.6,.8,1.0} drives{0} 400 probes 20Hz-24kHz
        {
            const chz::Grid g = chz::spdGrid();
            expect(g.modes.size() == 5, "spd: 5 modes (all)");
            expect(g.cutoffs.size() == 15, "spd: 15 cutoffs");
            expect(g.resonances.size() == 6, "spd: 6 resonances");
            expect(g.drives.size() == 1 && approxEqual(g.drives[0], 0.0), "spd: drives == {0.0}");
            expect(g.hostRates.size() == 1 && approxEqual(g.hostRates[0], 96000.0),
                   "spd: hostRates == {96000}");
            expect(g.osFactors.size() == 1 && g.osFactors[0] == 8, "spd: osFactors == {8}");
            expect(g.osModes.size() == 1 && g.osModes[0] == chz::OsMode::Live,
                   "spd: osModes == {Live}");
            expect(g.probeFreqs.size() == 400, "spd: 400 probes");
            expect(!g.cutoffs.empty() && approxEqual(g.cutoffs.front(), 50.0, 0.5),
                   "spd: first cutoff ~= 50 Hz");
            expect(!g.cutoffs.empty() && approxEqual(g.cutoffs.back(), 16000.0, 5.0),
                   "spd: last cutoff ~= 16000 Hz");
            expect(!g.probeFreqs.empty() && approxEqual(g.probeFreqs.front(), 20.0, 0.5),
                   "spd: first probe ~= 20 Hz");
            expect(!g.probeFreqs.empty() && approxEqual(g.probeFreqs.back(), 24000.0, 5.0),
                   "spd: last probe ~= 24000 Hz");
            for (double r : { 0.0, 0.2, 0.4, 0.6, 0.8, 1.0 })
                expect(containsD(g.resonances, r), "spd: resonances contains " + juce::String(r, 2));

            const size_t spdPoints = g.modes.size() * g.cutoffs.size() * g.resonances.size()
                                    * g.drives.size() * g.osFactors.size() * g.osModes.size()
                                    * g.hostRates.size();
            expect(spdPoints == 450, "spd: axis-combo product == 450 (5x15x6x1x1x1x1)");
        }

        // ---- osAliasGrid: OS/aliasing verification ----
        // os{1,2,4,8} osModes{Live,Render} rate{96000} modes{LP24,BP}
        // cutoffs{4k,8k,16k} res{0.9,1.0} drives{0,1} 200 probes
        {
            const chz::Grid g = chz::osAliasGrid();
            expect(g.osFactors.size() == 4, "osalias: 4 osFactors");
            for (int os : { 1, 2, 4, 8 }) expect(contains(g.osFactors, os),
                   "osalias: osFactors contains " + juce::String(os));
            expect(g.osModes.size() == 2
                   && contains(g.osModes, chz::OsMode::Live)
                   && contains(g.osModes, chz::OsMode::Render), "osalias: osModes == {Live, Render}");
            expect(g.hostRates.size() == 1 && approxEqual(g.hostRates[0], 96000.0),
                   "osalias: hostRates == {96000}");
            expect(g.modes.size() == 2
                   && contains(g.modes, chz::Mode::LP24)
                   && contains(g.modes, chz::Mode::BP), "osalias: modes == {LP24, BP}");
            expect(g.cutoffs.size() == 3, "osalias: 3 cutoffs");
            for (double c : { 4000.0, 8000.0, 16000.0 }) expect(containsD(g.cutoffs, c, 1.0),
                   "osalias: cutoffs contains " + juce::String(c, 0));
            expect(g.resonances.size() == 2
                   && containsD(g.resonances, 0.9) && containsD(g.resonances, 1.0),
                   "osalias: resonances == {0.9, 1.0}");
            expect(g.drives.size() == 2 && containsD(g.drives, 0.0) && containsD(g.drives, 1.0),
                   "osalias: drives == {0, 1}");
            expect(g.probeFreqs.size() == 200, "osalias: 200 probes");

            const size_t osaliasPoints = g.osFactors.size() * g.osModes.size() * g.hostRates.size()
                                       * g.modes.size() * g.cutoffs.size() * g.resonances.size()
                                       * g.drives.size();
            expect(osaliasPoints == 192, "osalias: 192 = 4x2x1x2x3x2x2 axis-combo product");
        }

        // ---- hostRateGrid: host-rate portability spot-check ----
        // rates{44100,48000,88200,96000,192000} os{1,8} osMode{Live}
        // modes{LP24,HP} cutoffs{250,1k,4k} res{0,0.9} drives{0} 200 probes
        {
            const chz::Grid g = chz::hostRateGrid();
            expect(g.hostRates.size() == 5, "rates: 5 hostRates");
            for (double r : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
                expect(containsD(g.hostRates, r, 0.5), "rates: hostRates contains " + juce::String(r, 0));
            expect(containsD(g.hostRates, 44100.0, 0.5), "rates: includes 44100");
            expect(containsD(g.hostRates, 192000.0, 0.5), "rates: includes 192000");
            expect(g.osFactors.size() == 2
                   && contains(g.osFactors, 1) && contains(g.osFactors, 8), "rates: osFactors == {1, 8}");
            expect(g.osModes.size() == 1 && g.osModes[0] == chz::OsMode::Live,
                   "rates: osModes == {Live}");
            expect(g.modes.size() == 2
                   && contains(g.modes, chz::Mode::LP24) && contains(g.modes, chz::Mode::HP),
                   "rates: modes == {LP24, HP}");
            expect(g.cutoffs.size() == 3, "rates: 3 cutoffs");
            for (double c : { 250.0, 1000.0, 4000.0 }) expect(containsD(g.cutoffs, c, 0.5),
                   "rates: cutoffs contains " + juce::String(c, 0));
            expect(g.resonances.size() == 2 && containsD(g.resonances, 0.0) && containsD(g.resonances, 0.9),
                   "rates: resonances == {0, 0.9}");
            expect(g.drives.size() == 1 && approxEqual(g.drives[0], 0.0), "rates: drives == {0.0}");
            expect(g.probeFreqs.size() == 200, "rates: 200 probes");

            const size_t ratesPoints = g.hostRates.size() * g.osFactors.size() * g.osModes.size()
                                      * g.modes.size() * g.cutoffs.size() * g.resonances.size()
                                      * g.drives.size();
            expect(ratesPoints == 120, "rates: 120 = 5x2x1x2x3x2x1 axis-combo product");
        }

        // ---- largeSignalGrid: drive/resonance law (Q27/SP-B axis) ----
        // os{1,8} osMode{Live} rate{96000} modes{LP24} cutoffs{250,1k,4k}
        // res{0,.3,.6,.8,.9,1.0} drives{0,.25,.5,.75,1.0} 200 probes
        {
            const chz::Grid g = chz::largeSignalGrid();
            expect(g.osFactors.size() == 2 && contains(g.osFactors, 1) && contains(g.osFactors, 8),
                   "largesig: osFactors == {1, 8}");
            expect(g.osModes.size() == 1 && g.osModes[0] == chz::OsMode::Live,
                   "largesig: osModes == {Live}");
            expect(g.hostRates.size() == 1 && approxEqual(g.hostRates[0], 96000.0),
                   "largesig: hostRates == {96000}");
            expect(g.modes.size() == 1 && g.modes[0] == chz::Mode::LP24, "largesig: modes == {LP24}");
            expect(g.cutoffs.size() == 3, "largesig: 3 cutoffs");
            for (double c : { 250.0, 1000.0, 4000.0 }) expect(containsD(g.cutoffs, c, 0.5),
                   "largesig: cutoffs contains " + juce::String(c, 0));
            expect(g.resonances.size() == 6, "largesig: 6 resonances");
            for (double r : { 0.0, 0.3, 0.6, 0.8, 0.9, 1.0 })
                expect(containsD(g.resonances, r), "largesig: resonances contains " + juce::String(r, 2));
            expect(g.drives.size() == 5, "largesig: 5 drives");
            for (double d : { 0.0, 0.25, 0.5, 0.75, 1.0 })
                expect(containsD(g.drives, d), "largesig: drives contains " + juce::String(d, 2));
            expect(g.probeFreqs.size() == 200, "largesig: 200 probes");

            expect(g.resonances.size() * g.drives.size() == 30,
                   "largesig: 6 res x 5 drives == 30 operating points");
        }
    }
};

static GridShapeTests gridShapeTestsInstance;
