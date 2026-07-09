#include <juce_core/juce_core.h>
#include <vector>
#include <cmath>
#include "../src/Layer.h"
#include "../src/Voice.h"
#include "../src/dsp/ParamSnapshot.h"

class OversamplingAntiAliasTests : public juce::UnitTest {
public:
    OversamplingAntiAliasTests() : juce::UnitTest("OversamplingAntiAlias") {}

    // Single direct-DFT pass returning BOTH the inharmonic (alias) energy and the
    // total spectral energy over bins k=1..N/2-1. The metric we actually gate on is
    // the alias FRACTION (alias/total): it is amplitude-independent, so a render that
    // is merely quieter at a higher factor does NOT register as less aliasing — only
    // a genuine drop in inharmonic content relative to the signal does.
    // Direct DFT is O(N^2) — fine for N=4096 in a unit test.
    struct Spectrum { double alias = 0.0; double total = 0.0; };
    static Spectrum spectrum(const std::vector<float>& x, double baseHz, double sr) {
        Spectrum out;
        const int N = (int) x.size();
        for (int k = 1; k < N / 2; ++k) {
            double re = 0, im = 0;
            for (int n = 0; n < N; ++n) {
                const double a = 2.0 * juce::MathConstants<double>::pi * k * n / N;
                re += x[(size_t) n] * std::cos(a);
                im -= x[(size_t) n] * std::sin(a);
            }
            const double mag2 = re * re + im * im;
            out.total += mag2;

            const double f     = (double) k * sr / N;
            const double ratio = f / baseHz;
            const double frac  = std::abs(ratio - std::round(ratio));
            if (frac > 0.1)   // not near an integer harmonic of the fundamental
                out.alias += mag2;
        }
        return out;
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
                ParamSnapshot s;               // defaults to VCO1 100% saw — rich in harmonics, alias-prone
                s.svfCutoffHz      = 8000.0f;  // open enough to pass the drive harmonics
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
                v.noteOn(88, 1.0f);  // MIDI 88 = E6 ~= 1318.5 Hz — drive harmonics fold fast at 48k

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

            // Alias FRACTION = inharmonic energy / total energy. This is the metric we
            // gate on: it is amplitude-independent, so it isolates TRUE alias reduction
            // from a mere overall gain drop (a quieter factor-4 render cancels in the
            // numerator/denominator). Bins counted are NOT near integer multiples of the
            // 82.4 Hz fundamental.
            // SCOPE LIMIT: this gates the oversampling integration / rate-scaling and the
            // alias FRACTION of the driven filter; it does NOT independently prove the
            // halfband filter's stopband depth (that is covered by Halfband2x/oversampler
            // unit tests).
            const Spectrum s1 = spectrum(a1, 1318.5, sr);
            const Spectrum s4 = spectrum(a4, 1318.5, sr);
            const double frac1 = s1.total > 0 ? s1.alias / s1.total : -1.0;
            const double frac4 = s4.total > 0 ? s4.alias / s4.total : -1.0;
            const double fracRatio = frac1 > 0 ? frac4 / frac1 : -1.0;

            // Log the measured values so they appear in the test output.
            juce::Logger::writeToLog("OversamplingAntiAlias: frac1=" + juce::String(frac1, 5)
                                     + "  frac4=" + juce::String(frac4, 5)
                                     + "  ratio frac4/frac1=" + juce::String(fracRatio, 4));

            // 4x oversampling must cut the alias FRACTION materially. Re-anchored after
            // the Q27 bounded-resonance fix (the old 82 Hz anchor relied on the expansive
            // scream, which was ~54% aliasing by itself): driven Huggett at E6,
            // frac1 ~= 0.257, frac4 ~= 0.014, ratio ~= 0.056 (~18x). Threshold 0.7 is strict enough
            // to catch a broken oversampling path (ratio ~1.0) while honoring the physics.
            // A ratio near 1.0 means the oversampling path is not reducing aliasing; do
            // not weaken this threshold without re-measuring the actual chain behaviour.
            expect(frac4 < frac1 * 0.7,
                   "4x must cut alias FRACTION materially (>=1.4x)  "
                   "(frac1=" + juce::String(frac1, 5)
                   + "  frac4=" + juce::String(frac4, 5)
                   + "  ratio=" + juce::String(fracRatio, 4) + ")");
        }
    }
};

static OversamplingAntiAliasTests oversamplingAntiAliasTestsInstance;
