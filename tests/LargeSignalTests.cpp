#include <juce_core/juce_core.h>
#include "../src/Layer.h"
#include "../src/Voice.h"
#include "../src/dsp/ParamSnapshot.h"
#include "characterization/FilterUnderTest.h"
#include "characterization/CharacterizationRunner.h"   // logFreqs
#include "testdsp/SteppedSine.h"
#include "testdsp/LevelResponse.h"
#include <cmath>
#include <cstdio>
#include <vector>

// SP-B Phase 0 (2026-07-02): the Huggett large-signal read. SP-A measured the
// SMALL-SIGNAL resonant peak at +89 dB (linearized: the nonlinear stages never
// engage at whisper level). This test excites each filter AT its resonant peak
// across input levels and reads the REAL compression law: where the resonance
// saturator's self-limiting engages and what the effective peak gain is at
// musical levels. Analysis-only (voicing HELD for SP-D); opt-in:
//
//   BERNIE_RUN_LARGESIGNAL=1 ./build/tests/k2000_tests
//
// Frequency note: LevelResponse bin-aligns the tone (bin width 5.86 Hz at
// 96 kHz / 16384), so the probe sits within ~3 Hz of the found peak — "at the
// peak" within the peak's own ~15 Hz width; small-signal gain read here is a
// hair under the true summit, the compression LAW is unaffected.

struct LargeSignalTests : public juce::UnitTest {
    LargeSignalTests() : juce::UnitTest("LargeSignal") {}

    void runTest() override {
        if (std::getenv("BERNIE_RUN_LARGESIGNAL") == nullptr) {
            beginTest("skipped (set BERNIE_RUN_LARGESIGNAL=1 for the SP-B large-signal read)");
            expect(true);
            return;
        }

        const double sr = 96000.0;
        const double fc = 1000.0;

        std::vector<double> amps;
        for (int d = -100; d <= -6; d += 2) amps.push_back((double) d);

        beginTest("gain-vs-level at the resonant peak (LP24 fc1000)");
        std::printf("model    | res  | peakHz  | ss gain dB | gain@-40 | gain@-18 | gain@-6 | knee1dB in | knee6dB in\n");
        std::printf("---------|------|---------|------------|----------|----------|---------|------------|-----------\n");

        for (const char* model : { "huggett", "moog" }) {
            for (double res : { 0.7, 0.9, 1.0 }) {
                auto fut = (juce::String(model) == "moog") ? chz::makeMoogFut()
                                                           : chz::makeHuggettFut();
                chz::OperatingPoint op;
                op.mode = chz::Mode::LP24;
                op.cutoffHz = fc; op.resonance = res; op.drive = 0.0;
                op.osFactor = 1;  op.osMode = chz::OsMode::Live;
                op.hostSampleRate = sr;
                fut->setOperatingPoint(op);
                fut->reset();

                // 1) find the small-signal resonant peak (dense single-method sweep).
                auto probes = chz::CharacterizationRunner::logFreqs(500.0, 2000.0, 1500);
                auto r = testdsp::SteppedSine::transfer(*fut, probes, sr, 1.0e-4f);
                double peakHz = fc, peakDb = -1.0e9;
                for (size_t i = 0; i < probes.size(); ++i)
                    if (r.magDb[i] > peakDb) { peakDb = r.magDb[i]; peakHz = probes[i]; }

                // 2) level sweep AT the peak.
                fut->setOperatingPoint(op);
                auto pts = testdsp::LevelResponse::measure(*fut, peakHz, sr, amps);

                auto gainAt = [&](double inDb) {
                    for (const auto& p : pts)
                        if (std::abs(p.inDbfs - inDb) < 0.5) return p.gainDb;
                    return -300.0;
                };
                const double ss    = pts.front().gainDb;   // -100 dBFS ~ linear regime
                const double knee1 = testdsp::LevelResponse::kneeInDbfs(pts, 1.0);
                const double knee6 = testdsp::LevelResponse::kneeInDbfs(pts, 6.0);

                std::printf("%-8s | %.2f | %7.1f | %10.2f | %8.2f | %8.2f | %7.2f | %10.2f | %10.2f\n",
                            model, res, peakHz, ss, gainAt(-40.0), gainAt(-18.0),
                            gainAt(-6.0), knee1, knee6);
                std::fflush(stdout);

                // Full curve rows for the report (model,res,inDbfs,gainDb,outPeakDbfs,thdDb).
                for (const auto& p : pts)
                    std::printf("CSV,%s,%.2f,%.1f,%.3f,%.3f,%.2f\n",
                                model, res, p.inDbfs, p.gainDb, p.outPeakDbfs, p.thdDb);
                std::fflush(stdout);

                expect(std::isfinite(ss), "small-signal gain finite");
            }
        }

        // ---- Dropout reproduction (Q27 field report): at max resonance the sound
        // "disappears completely and then re-appears". Hypothesis: the expansive
        // loop drives the state to Inf/NaN (mute) until something recovers it.
        // BERNIE_RUN_DROPOUT=1 adds a 20 s res=1.0 run at -6 dBFS with per-block
        // forensic stats.
        if (std::getenv("BERNIE_RUN_DROPOUT") == nullptr) return;

        beginTest("dropout forensics: huggett LP24 res=1.0, -6 dBFS, 20 s");
        {
            auto fut = chz::makeHuggettFut();
            chz::OperatingPoint op;
            op.mode = chz::Mode::LP24; op.cutoffHz = fc; op.resonance = 1.0;
            op.drive = 0.0; op.osFactor = 1; op.osMode = chz::OsMode::Live;
            op.hostSampleRate = sr;
            fut->setOperatingPoint(op);
            fut->reset();

            const int blockN = 4800;                    // 50 ms at 96k
            const int blocks = 400;                     // 20 s
            const float amp = 0.5f;                     // -6 dBFS
            double phase = 0.0;
            const double inc = 2.0 * juce::MathConstants<double>::pi * 110.0 / sr;
            int nanBlocks = 0, silentBlocks = 0, hotBlocks = 0;
            int lastState = -1;                          // 0 normal, 1 silent, 2 nonfinite
            std::vector<float> buf((size_t) blockN);
            for (int b = 0; b < blocks; ++b) {
                for (int i = 0; i < blockN; ++i) {
                    buf[(size_t) i] = amp * (float) std::sin(phase);
                    phase += inc;
                    if (phase > 2.0 * juce::MathConstants<double>::pi)
                        phase -= 2.0 * juce::MathConstants<double>::pi;
                }
                fut->process(buf.data(), blockN);
                bool finite = true; float mx = 0.0f;
                for (float v : buf) {
                    if (!std::isfinite(v)) { finite = false; break; }
                    mx = std::max(mx, std::abs(v));
                }
                int state = !finite ? 2 : (mx < 1.0e-6f ? 1 : 0);
                if (!finite) ++nanBlocks;
                else if (state == 1) ++silentBlocks;
                if (mx > 100.0f) ++hotBlocks;
                if (state != lastState) {
                    std::printf("DROPOUT t=%5.2fs state=%s maxAbs=%g\n",
                                b * blockN / sr,
                                state == 2 ? "NONFINITE" : state == 1 ? "SILENT" : "normal",
                                (double) mx);
                    std::fflush(stdout);
                    lastState = state;
                }
            }
            std::printf("DROPOUT summary: %d blocks — nonfinite %d, silent %d, >100 linear (>+40 dBFS) %d\n",
                        blocks, nanBlocks, silentBlocks, hotBlocks);
            std::fflush(stdout);
            expect(true);   // forensics only; the numbers are the deliverable
        }

        beginTest("dropout forensics: FULL VOICE path, saw, res=1.0, os8, 20 s (field conditions)");
        {
            const double vsr = 48000.0;
            const int    N   = 512;
            const int    os  = 8;
            Layer layer;
            layer.prepare(vsr * os, N * os);
            ParamSnapshot s {};
            s.oscWaveform = 0;                     // saw — harmonics land on the peak
            s.ampAttackS = 0.001f; s.ampDecayS = 0.1f;
            s.ampSustain = 1.0f;   s.ampReleaseS = 0.1f;
            s.algorithmId = 1;                     // thru
            s.svfCutoffHz = 1000.0f; s.svfResonance = 1.0f;
            s.spineModel = 0; s.spineSlope = 1;    // Huggett LP24
            layer.updateParameters(s);
            Voice v;
            v.setLayer(&layer);
            v.prepare(vsr, N, os);
            v.noteOn(57, 1.0f);                    // A3 110 Hz

            const int blocks = (int) (20.0 * vsr / N);
            int nanBlocks = 0, silentBlocks = 0, hotBlocks = 0;
            int lastState = -1;
            std::vector<float> l((size_t) N), r((size_t) N);
            float worst = 0.0f;
            for (int b = 0; b < blocks; ++b) {
                std::fill(l.begin(), l.end(), 0.0f);
                std::fill(r.begin(), r.end(), 0.0f);
                v.render(l.data(), r.data(), N);
                bool finite = true; float mx = 0.0f;
                for (float x : l) {
                    if (!std::isfinite(x)) { finite = false; break; }
                    mx = std::max(mx, std::abs(x));
                }
                worst = std::max(worst, mx);
                int state = !finite ? 2 : (mx < 1.0e-6f ? 1 : 0);
                if (!finite) ++nanBlocks;
                else if (state == 1) ++silentBlocks;
                if (mx > 100.0f) ++hotBlocks;
                if (state != lastState) {
                    std::printf("VDROPOUT t=%5.2fs state=%s maxAbs=%g\n",
                                b * (double) N / vsr,
                                state == 2 ? "NONFINITE" : state == 1 ? "SILENT" : "normal",
                                (double) mx);
                    std::fflush(stdout);
                    lastState = state;
                }
            }
            std::printf("VDROPOUT summary: %d blocks — nonfinite %d, silent %d, hot(>+40dBFS) %d, worst maxAbs %g\n",
                        blocks, nanBlocks, silentBlocks, hotBlocks, (double) worst);
            std::fflush(stdout);
            expect(true);
        }
    }
};

static LargeSignalTests largeSignalTestsInstance;
