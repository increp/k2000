#include <juce_core/juce_core.h>
#include "testdsp/Sweep.h"
#include "testdsp/Spectrum.h"
#include <cmath>
#include <vector>

struct SweepTests : public juce::UnitTest {
    SweepTests() : juce::UnitTest("Sweep") {}
    void runTest() override {
        const double sr = 96000.0, f0 = 20.0, f1 = 24000.0, dur = 1.0;

        // The defining Farina property: deconvolving an ESS-driven identity system with the
        // inverse filter yields a band-limited impulse whose magnitude spectrum is FLAT across
        // the swept band. A 20 Hz-24 kHz impulse is inherently spread over tens of ms (20 Hz =
        // 50 ms period), so a single-sample / few-sample time-domain test is physically wrong;
        // flatness of the in-band spectrum is the correct, downstream-relevant check.
        beginTest("ESS deconvolves to a spectrally-flat band-limited impulse (identity system)");
        {
            auto sweep = testdsp::Sweep::ess(f0, f1, dur, sr, 0.5f);
            auto inv   = testdsp::Sweep::inverseFilter(f0, f1, dur, sr);
            auto ir    = testdsp::Sweep::impulseResponse(sweep, inv);   // identity: output == sweep

            int peak = 0; double pmax = 0.0;
            for (int i = 0; i < (int) ir.size(); ++i)
                if (std::abs((double) ir[(size_t) i]) > pmax) { pmax = std::abs((double) ir[(size_t) i]); peak = i; }
            expect(pmax > 0.0, "peak present");

            // Window the impulse (rectangular, centered on the peak) and inspect its spectrum.
            const int W = 1 << 15;                        // 32768 -> 2.93 Hz/bin at 96 kHz
            std::vector<float> win((size_t) W, 0.0f);
            for (int i = 0; i < W; ++i) {
                const int idx = peak - W / 2 + i;
                if (idx >= 0 && idx < (int) ir.size()) win[(size_t) i] = ir[(size_t) idx];
            }
            auto mag = testdsp::Spectrum::magnitude(win);   // W/2 bins, bin k = k*sr/W Hz

            auto magDbAt = [&](double f) {
                const int bin = juce::jlimit(1, (int) mag.size() - 1, (int) std::lround(f * W / sr));
                return 20.0 * std::log10 (std::max (1.0e-12f, mag[(size_t) bin]));
            };

            // Flatness across an interior band [2*f0, f1/2], referenced to 1 kHz (avoids the
            // band edges where the sweep tapers).
            const double ref = magDbAt(1000.0);
            double worst = 0.0;
            for (double f = 2.0 * f0; f <= 0.5 * f1 + 1.0; f *= 2.0) {
                const double d = magDbAt(f) - ref;
                worst = std::max(worst, std::abs(d));
                logMessage("  |H| @ " + juce::String(f, 0) + " Hz = "
                           + juce::String(magDbAt(f), 2) + " dB (rel 1k: " + juce::String(d, 2) + ")");
            }
            expect(worst < 3.0,
                   "in-band spectrum flat within 3 dB; worst deviation = " + juce::String(worst, 2) + " dB");
        }
    }
};
static SweepTests sweepTestsInstance;
