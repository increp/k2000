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

        beginTest("max resonance genuinely self-oscillates (sustained whistle, rail-limited)");
        {
            // Field bug 2026-07-02: pre-Q27-fix the 'whistle' was powered by the
            // anti-damping defect; the first fix removed it entirely (kicked ring
            // decayed). Analog crosses the oscillation threshold near the knob top:
            // res=1.0 must SUSTAIN, amplitude set by the state rails, not blow up.
            NlSvfCell c; c.prepare(48000.0);
            c.setCutoff(1000.0f); c.setResonance(1.0f); c.setResSat(1.0f);
            c.reset();
            float l = 1.0f, r = 1.0f; c.process(l, r, NlSvfCell::LP);   // kick
            float early = 0.0f, late = 0.0f; bool finite = true;
            for (int i = 0; i < 144000; ++i) {                          // 3 s
                float a = 0.0f, b = 0.0f; c.process(a, b, NlSvfCell::LP);
                if (!std::isfinite(a)) { finite = false; break; }
                const float m = std::abs(a);
                if (i >= 24000 && i < 48000)  early = std::max(early, m);   // 0.5-1.0 s
                if (i >= 120000)              late  = std::max(late,  m);   // 2.5-3.0 s
            }
            expect(finite, "whistle finite");
            expect(late > 0.05f, "self-osc sustains (late peak " + juce::String(late, 4) + ")");
            expect(late > 0.5f * early, "no decay (early " + juce::String(early, 4)
                                        + " late " + juce::String(late, 4) + ")");
            expect(late < 6.0f, "whistle amplitude rail-limited");
        }

        beginTest("first resonance increment is click-free (continuous engagement)");
        {
            // Field bug 2026-07-02: the nonlinear path engaged BINARILY at res>0
            // (state rails at full strength from the first knob increment) -> click +
            // sudden character change. Twin-run difference test: identical hot signal,
            // one run holds res=0, the other steps to the first increment mid-stream.
            // The difference right after the step (the click burst) must not dwarf the
            // settled steady-state difference (the legitimate tiny response change).
            auto run = [&](bool stepRes) {
                NlSvfCell c; c.prepare(48000.0);
                c.setCutoff(1000.0f); c.setResonance(0.0f); c.setResSat(0.0f);
                c.reset();
                const double inc = 2.0 * juce::MathConstants<double>::pi * 400.0 / 48000.0;
                double ph = 0.0;
                std::vector<float> out; out.reserve(9600);
                for (int i = 0; i < 9600; ++i) {
                    if (stepRes && i == 4800) { c.setResonance(0.02f); c.setResSat(0.02f); }
                    float l = 1.6f * (float) std::sin(ph), r = l;   // hot pre-VCA level
                    ph += inc;
                    c.process(l, r, NlSvfCell::LP);
                    out.push_back(l);
                }
                return out;
            };
            const auto hold = run(false);
            const auto step = run(true);
            float burst = 0.0f, settled = 0.0f;
            for (int i = 4800; i < 4800 + 240; ++i)          // first 5 ms after the step
                burst = std::max(burst, std::abs(step[(size_t) i] - hold[(size_t) i]));
            for (int i = 8640; i < 9600; ++i)                // 80-100 ms later (settled)
                settled = std::max(settled, std::abs(step[(size_t) i] - hold[(size_t) i]));
            expect(burst < settled * 2.0f + 2.0e-3f,
                   "click burst at engagement (burst " + juce::String(burst, 5)
                   + " vs settled diff " + juce::String(settled, 5) + ")");
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
