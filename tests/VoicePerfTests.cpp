#include <juce_core/juce_core.h>
#include "../src/Layer.h"
#include "../src/Voice.h"
#include "../src/dsp/ParamSnapshot.h"
#include <chrono>
#include <cstdlib>
#include <vector>

// Q23 voice-cost pricing (register, resolved-in-progress 2026-07-02): measure the
// REAL production per-voice cost — Voice::render through osc + env + spine filter
// with per-voice oversampling — per model x OS factor, and derive how many voices
// fit in realtime on this machine. Opt-in (minutes-long, machine-specific):
//
//   BERNIE_RUN_VOICEPERF=1 ./build/tests/k2000_tests
//
// Numbers are a PRICE, not a gate: they feed the Q2 (256-voice) re-check and the
// Q11 per-phase budget setting. Wall-clock on a dev box varies run to run; read
// the table as order-of-magnitude truth, not a golden.

struct VoicePerfTests : public juce::UnitTest {
    VoicePerfTests() : juce::UnitTest("VoicePerf") {}

    struct Config { const char* name; float res, drive; };

    void runTest() override {
        if (std::getenv("BERNIE_RUN_VOICEPERF") == nullptr) {
            beginTest("skipped (set BERNIE_RUN_VOICEPERF=1 to run the pricing measurement)");
            expect(true);
            return;
        }

        const double sr = 48000.0;
        const int    N  = 512;
        const double audioSecondsPerPoint = 0.5;
        const int    blocks = (int) (audioSecondsPerPoint * sr / N);

        const Config configs[] = { { "light (res .2, drive 0)",  0.2f, 0.0f },
                                   { "heavy (res .9, drive .5)", 0.9f, 0.5f } };
        const char* models[] = { "huggett", "moog" };

        beginTest("voice-cost pricing table (this machine, single core)");
        logMessage("model    | config                    | os | %CPU/voice | voices realtime");
        logMessage("---------|---------------------------|----|------------|----------------");

        for (int model = 0; model < 2; ++model) {
            for (const auto& cfg : configs) {
                for (int os : { 1, 2, 4, 8 }) {
                    // Mirror production: Program::prepare gives the Layer the
                    // oversampled rate; Voice::prepare gets base rate + factor.
                    Layer layer;
                    layer.prepare(sr * os, N * os);

                    ParamSnapshot s {};
                    s.oscWaveform = 0;              // saw (worst-case harmonics)
                    s.svfCutoffHz = 1000.0f;
                    s.svfResonance = cfg.res;
                    s.spineDrive   = cfg.drive;
                    s.spineModel   = model;
                    s.spineSlope   = 1;             // 24 dB
                    s.ampAttackS = 0.001f; s.ampDecayS = 0.1f;
                    s.ampSustain = 1.0f;   s.ampReleaseS = 0.1f;
                    layer.updateParameters(s);

                    Voice v;
                    v.setLayer(&layer);
                    v.prepare(sr, N, os);
                    v.noteOn(57, 1.0f);             // A3 110 Hz

                    std::vector<float> outL((size_t) N), outR((size_t) N);
                    for (int b = 0; b < 8; ++b) {   // warm-up: caches, env attack
                        std::fill(outL.begin(), outL.end(), 0.0f);
                        std::fill(outR.begin(), outR.end(), 0.0f);
                        v.render(outL.data(), outR.data(), N);
                    }

                    const auto t0 = std::chrono::steady_clock::now();
                    for (int b = 0; b < blocks; ++b) {
                        std::fill(outL.begin(), outL.end(), 0.0f);
                        std::fill(outR.begin(), outR.end(), 0.0f);
                        v.render(outL.data(), outR.data(), N);
                    }
                    const auto t1 = std::chrono::steady_clock::now();

                    const double wall  = std::chrono::duration<double>(t1 - t0).count();
                    const double audio = (double) blocks * N / sr;
                    const double cost  = wall / audio;          // fraction of one core
                    const int    fit   = cost > 0.0 ? (int) (1.0 / cost) : -1;

                    expect(std::isfinite(outL[0]), "render output finite");
                    expect(cost > 0.0, "measurable cost");

                    logMessage(juce::String::formatted("%-8s | %-25s | %2d | %9.2f%% | %6d",
                                                       models[model], cfg.name, os,
                                                       cost * 100.0, fit));
                }
            }
        }
        logMessage("(voices realtime = single-core; multiply by usable cores for a machine budget)");
    }
};

static VoicePerfTests voicePerfTestsInstance;
