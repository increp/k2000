#include <juce_core/juce_core.h>
#include "characterization/FilterUnderTest.h"
#include "characterization/CharacterizationRunner.h"   // logFreqs
#include "testdsp/SteppedSine.h"
#include "testdsp/Level.h"
#include <chrono>
#include <cstdlib>
#include <vector>

using namespace chz;

// M3 Task 3: the ACCURATE Huggett-vs-Moog resonant-peak disparity, measured with a
// DENSE single-method (stepped-sine) sweep so the narrow Huggett peak is captured at
// its true height (measured ~+89 dB), not the coarse gate's undersampled proxy (~+34 dB).
// Single-method by design: no dual-method agreement is involved here, so the sharp-peak
// method divergence that protects the fast gate is a non-issue.
struct LevelDisparityTests : public juce::UnitTest {
    LevelDisparityTests() : juce::UnitTest("LevelDisparity") {}

    static double truePeakGainDb(const juce::String& which, const std::vector<double>& probes) {
        auto fut = (which == "moog") ? makeMoogFut() : makeHuggettFut();
        OperatingPoint op;
        op.mode           = Mode::LP24;
        op.cutoffHz       = 1000.0;
        op.resonance      = 0.9;
        op.drive          = 0.0;
        op.osFactor       = 1;
        op.osMode         = OsMode::Live;
        op.hostSampleRate = 96000.0;
        fut->setOperatingPoint(op);
        auto r = testdsp::SteppedSine::transfer(*fut, probes, 96000.0, 0.05f);
        return testdsp::Level::peakGainDb(r.magDb);
    }

    void runTest() override {
        beginTest("Huggett resonant peak is far hotter than Moog (dense-sweep true disparity)");

        // OPT-IN: this 40000-point sweep takes ~14 min (measured 854 s), so it is skipped by
        // default and runs only when BERNIE_RUN_DISPARITY is set. The always-on gate's coarse
        // peak_gain_db proxy guards resonance on every build; this measures the TRUE peak on demand.
        if (std::getenv("BERNIE_RUN_DISPARITY") == nullptr) {
            logMessage("LevelDisparity: SKIPPED (opt-in). Set BERNIE_RUN_DISPARITY=1 to run the "
                       "~14 min 40000-point sweep that measures the true resonant peak (~+89 dB).");
            expect(true);   // opt-in skip marker (deliberate no-op so the block registers a pass)
            return;
        }

        // 40000-point full-band log sweep. This resonance is pathologically sharp (two cascaded
        // high-Q cells): a 40-probe gate sees only +34 dB, a 200-probe grid only +70 dB, and
        // ~40000 probes are needed to catch the true ~+89 dB tip (measured 84 dB above Moog).
        const auto probes = CharacterizationRunner::logFreqs(10.0, 25000.0, 40000);

        const auto t0 = std::chrono::steady_clock::now();
        const double moogPeak = truePeakGainDb("moog",    probes);
        const double hugPeak  = truePeakGainDb("huggett", probes);
        const auto t1 = std::chrono::steady_clock::now();
        const double secs = std::chrono::duration<double>(t1 - t0).count();

        logMessage("LevelDisparity: 40000-probe dense sweep took " + juce::String(secs, 1)
                   + " s (both models). Moog peak=" + juce::String(moogPeak, 2)
                   + " dB, Huggett peak=" + juce::String(hugPeak, 2)
                   + " dB, disparity=" + juce::String(hugPeak - moogPeak, 2) + " dB.");

        // Re-anchored after the Q27 bounded-resonance fix (2026-07-02). Pre-fix this
        // sweep read +88.97 dB (the anti-damped defect; see
        // docs/reviews/2026-07-02-huggett-large-signal-read.md). Post-fix the state
        // rails compress the response AT THIS PROBE LEVEL to ~+26 dB; the tiny-signal
        // (-80 dBFS) peak remains ~+61 dB (verified by LargeSignalTests) — the
        // near-self-osc linear character is intact, the follow-through is bounded.
        expect(hugPeak > 15.0 && hugPeak < 40.0,
               "Huggett dense-sweep peak should sit in the bounded band (+15..+40 dB)");
        expect(moogPeak < 10.0, "Moog peak should be modest (internally bounded ladder)");
        expect(hugPeak - moogPeak > 10.0,
               "Huggett stays distinctly hotter than Moog (character preserved, > 10 dB)");
    }
};

static LevelDisparityTests levelDisparityTestsInstance;
