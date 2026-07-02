#include <juce_core/juce_core.h>
#include "../src/dsp/spine/NlSvfCell.h"
#include "characterization/FilterUnderTest.h"
#include "testdsp/LevelResponse.h"
#include <cmath>
#include <vector>

// Q27 regression tests (ruled a DEFECT 2026-07-02): the resonance-loop
// nonlinearity must SELF-LIMIT — bounded forced response and monotone
// non-expanding gain — not anti-damp. The original satRes delta had its
// operands transposed (positive BP feedback growing with amplitude), producing
// +86 dB gain and +89 dBFS output at musical input (see
// docs/reviews/2026-07-02-huggett-large-signal-read.md).

struct HuggettBoundedResonanceTests : public juce::UnitTest {
    HuggettBoundedResonanceTests() : juce::UnitTest("HuggettBoundedResonance") {}

    void runTest() override {
        beginTest("forced response at max resonance is bounded (single cell)");
        {
            NlSvfCell c; c.prepare(96000.0);
            c.setCutoff(1000.0f); c.setResonance(1.0f); c.setResSat(1.0f);
            c.reset();
            float peak = 0.0f; bool finite = true;
            const double inc = 2.0 * juce::MathConstants<double>::pi * 1000.0 / 96000.0;
            double ph = 0.0;
            for (int i = 0; i < 96000; ++i) {           // 1 s, -6 dBFS AT the cutoff
                float l = 0.5f * (float) std::sin(ph), r = l;
                ph += inc;
                c.process(l, r, NlSvfCell::LP);
                if (!std::isfinite(l)) { finite = false; break; }
                if (i > 4800) peak = std::max(peak, std::abs(l));
            }
            expect(finite, "no NaN/Inf under sustained drive");
            expect(peak < 6.0f, "forced peak bounded (< ~+15 dBFS), got " + juce::String(peak));
        }

        beginTest("gain does not grow with input level (monotone non-expansion, LP24 res 0.9)");
        {
            auto fut = chz::makeHuggettFut();
            chz::OperatingPoint op;
            op.mode = chz::Mode::LP24; op.cutoffHz = 1000.0; op.resonance = 0.9;
            op.drive = 0.0; op.osFactor = 1; op.osMode = chz::OsMode::Live;
            op.hostSampleRate = 96000.0;
            fut->setOperatingPoint(op);
            fut->reset();
            // At the resonant peak (~999.5 Hz measured): gain at -6 dBFS must not
            // exceed gain at -60 dBFS. Pre-fix this fails by ~+24 dB.
            std::vector<double> amps { -60.0, -6.0 };
            auto pts = testdsp::LevelResponse::measure(*fut, 999.5, 96000.0, amps);
            expect(pts[1].gainDb <= pts[0].gainDb + 1.0,
                   "expansive resonance: gain(-6)=" + juce::String(pts[1].gainDb, 2)
                   + " > gain(-60)=" + juce::String(pts[0].gainDb, 2));
            // And the output at musical drive must stay in a sane absolute range.
            expect(pts[1].outPeakDbfs < 20.0,
                   "output at -6 dBFS input should be < +20 dBFS, got "
                   + juce::String(pts[1].outPeakDbfs, 1));
        }
    }
};

static HuggettBoundedResonanceTests huggettBoundedResonanceTestsInstance;
