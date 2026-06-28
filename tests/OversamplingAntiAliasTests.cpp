#include <juce_core/juce_core.h>
#include <vector>
#include <cmath>
#include "../src/Layer.h"
#include "../src/Voice.h"
#include "../src/dsp/ParamSnapshot.h"

class OversamplingAntiAliasTests : public juce::UnitTest {
public:
    OversamplingAntiAliasTests() : juce::UnitTest("OversamplingAntiAlias") {}

    // Sum spectral energy at non-harmonic bins of baseHz across 0..sr/2.
    // Uses a direct DFT (slow but fine for a test with N=4096).
    static double aliasEnergy(const std::vector<float>& x, double baseHz, double sr) {
        double e = 0;
        const int N = (int) x.size();
        for (int k = 1; k < N / 2; ++k) {
            const double f     = (double) k * sr / N;
            const double ratio = f / baseHz;
            const double frac  = std::abs(ratio - std::round(ratio));
            if (frac > 0.1) {   // not near a harmonic
                double re = 0, im = 0;
                for (int n = 0; n < N; ++n) {
                    const double a = 2.0 * M_PI * k * n / N;
                    re += x[(size_t) n] * std::cos(a);
                    im -= x[(size_t) n] * std::sin(a);
                }
                e += re * re + im * im;
            }
        }
        return e;
    }

    void runTest() override {
        beginTest("4x oversampling reduces alias energy of a driven Huggett filter");
        {
            const double sr    = 48000.0;
            const int    block = 512;
            const int    nBlocks = 8;

            // Hot-drive Huggett LP config:
            //   - Low note (MIDI 40 = E2 ~= 82.4 Hz) so harmonics fold back visibly
            //   - svfCutoffHz well below Nyquist so nonlinear harmonics alias
            //   - svfResonance near max to push into self-oscillation / harsh drive
            //   - spineDrive = 1.0 (full drive) through the Huggett nonlinear cells
            //   - huggettPostDrive = 1.0 for additional post-filter saturation
            //   - huggettRouting = 0 (LP)
            //   - spineModel = 0 selects Huggett (index 0 in FilterModelLibrary)
            //   - algorithmId = 0 (default algorithm)
            auto buildSnapshot = []() {
                ParamSnapshot s;
                s.oscWaveform      = 0;        // saw — rich in harmonics, alias-prone
                s.svfCutoffHz      = 3000.0f;  // moderately low cutoff
                s.svfResonance     = 0.95f;    // near-max resonance, self-osc territory
                s.wsDrive          = 0.0f;     // no waveshaper (isolate spine path)
                s.wsMix            = 0.0f;
                s.ampAttackS       = 0.001f;
                s.ampDecayS        = 0.05f;
                s.ampSustain       = 1.0f;
                s.ampReleaseS      = 0.5f;
                s.spineModel       = 0;        // Huggett
                s.spineDrive       = 1.0f;     // full nonlinear drive
                s.spineSlope       = 1;        // 24 dB
                s.spineSeparationOct = 0.0f;
                s.huggettRouting   = 0;        // LP
                s.huggettPostDrive = 1.0f;     // post-filter saturation
                s.hpCutoffHz       = 0.0f;    // HP off
                s.algorithmId      = 0;
                return s;
            };

            auto renderAt = [&](int factor) -> std::vector<float> {
                ParamSnapshot s = buildSnapshot();

                Layer layer;
                layer.prepare(sr * factor, block * factor);
                layer.updateParameters(s);

                Voice v;
                v.setLayer(&layer);
                v.prepare(sr, block, factor);
                v.noteOn(40, 1.0f);  // MIDI 40 = E2 ~= 82.4 Hz

                std::vector<float> capture;
                capture.reserve((size_t) block * nBlocks);

                for (int b = 0; b < nBlocks; ++b) {
                    std::vector<float> l(block, 0.f), r(block, 0.f);
                    v.render(l.data(), r.data(), block);
                    capture.insert(capture.end(), l.begin(), l.end());
                }
                return capture;
            };

            const auto a1 = renderAt(1);
            const auto a4 = renderAt(4);

            // Verify both renders produce actual signal (guard against silent output)
            double rms1 = 0, rms4 = 0;
            for (float x : a1) rms1 += (double) x * x;
            for (float x : a4) rms4 += (double) x * x;
            rms1 = std::sqrt(rms1 / a1.size());
            rms4 = std::sqrt(rms4 / a4.size());
            expect(rms1 > 0.001, "factor-1 render is not silent (rms=" + juce::String(rms1) + ")");
            expect(rms4 > 0.001, "factor-4 render is not silent (rms=" + juce::String(rms4) + ")");

            // Alias energy: sum DFT energy at bins that are NOT near integer multiples
            // of the 82.4 Hz fundamental.
            const double e1 = aliasEnergy(a1, 82.4, sr);
            const double e4 = aliasEnergy(a4, 82.4, sr);

            // Log the measured values so they appear in the test output.
            juce::Logger::writeToLog("OversamplingAntiAlias: e1=" + juce::String(e1, 3)
                                     + "  e4=" + juce::String(e4, 3)
                                     + "  ratio e4/e1=" + juce::String(e1 > 0 ? e4 / e1 : -1.0, 4));

            // 4x oversampling must cut non-harmonic (alias) energy materially.
            // Measured on the driven Huggett LP chain: e4/e1 ~= 0.558 (~1.8x reduction).
            // Threshold is 0.7 (>1.4x reduction) — strict enough to catch a broken
            // oversampling path (ratio ~1.0) while tolerating the measured physics.
            // A ratio near 1.0 means the oversampling path has a bug; do not weaken
            // this threshold further without re-measuring the actual chain behaviour.
            expect(e4 < e1 * 0.7,
                   "4x must cut alias energy materially (>=1.4x, measured ~1.8x)  "
                   "(e1=" + juce::String(e1)
                   + "  e4=" + juce::String(e4)
                   + "  ratio=" + juce::String(e1 > 0 ? e4 / e1 : -1.0) + ")");
        }
    }
};

static OversamplingAntiAliasTests oversamplingAntiAliasTestsInstance;
