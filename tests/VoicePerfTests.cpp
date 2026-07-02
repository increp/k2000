#include <juce_core/juce_core.h>
#include "../src/Layer.h"
#include "../src/Voice.h"
#include "../src/dsp/ParamSnapshot.h"
#include "../src/dsp/Oscillator.h"
#include "../src/dsp/VoiceOversampler.h"
#include "../src/dsp/spine/SpineFilterSlot.h"
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

#ifndef NDEBUG
        // Guard learned the hard way (2026-07-02): the first published pricing table
        // was measured on a Debug (-O0) build dir and was ~5-10x wrong. Perf numbers
        // from unoptimized builds are meaningless — refuse to print them.
        beginTest("REFUSED: Debug build — run the pricing from a Release build dir");
        expect(true);
        return;
#endif

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

        // ---- Section breakdown (BERNIE_VOICEPERF_SECTIONS=1): where does the OS
        // cost live? Times each render stage in isolation, mirroring Voice::render.
        if (std::getenv("BERNIE_VOICEPERF_SECTIONS") == nullptr) return;

        beginTest("section breakdown (huggett, light, os=1 vs os=2)");
        for (int os : { 1, 2 }) {
            const int nOs = N * os;
            Layer layer;
            layer.prepare(sr * os, nOs);
            ParamSnapshot s {};
            s.oscWaveform = 0; s.svfCutoffHz = 1000.0f; s.svfResonance = 0.2f;
            s.spineModel = 0; s.spineSlope = 1;
            s.ampSustain = 1.0f;
            layer.updateParameters(s);

            Oscillator osc; osc.prepare(sr); osc.setFrequency(110.0f);
            osc.setWaveform(Oscillator::Waveform::Saw);
            VoiceOversampler ovs; ovs.prepare(N); ovs.setFactor(os);
            auto wsState = layer.block(BlockTypeId::Waveshaper).makeVoiceState();
            SpineFilterSlot slot;
            slot.prepare(sr * os, nOs, layer.spineModel(), layer.hpStage());

            std::vector<float> base((size_t) N), osMono((size_t) nOs),
                               l((size_t) nOs), r((size_t) nOs),
                               dl((size_t) N), dr((size_t) N);

            auto time = [&](auto&& fn) {
                const int reps = 4000;
                for (int i = 0; i < 64; ++i) fn();          // warm-up
                const auto t0 = std::chrono::steady_clock::now();
                for (int i = 0; i < reps; ++i) fn();
                const auto t1 = std::chrono::steady_clock::now();
                const double audio = (double) reps * N / sr;
                return 100.0 * std::chrono::duration<double>(t1 - t0).count() / audio;
            };

            const double cOsc  = time([&] { osc.processBlock(base.data(), N); });
            const double cUp   = time([&] { ovs.processMonoUp(base.data(), N, osMono.data()); });
            const double cWs   = time([&] { layer.block(BlockTypeId::Waveshaper)
                                                 .process(*wsState, osMono.data(), nOs); });
            const double cSpine = time([&] {
                std::copy(osMono.begin(), osMono.end(), l.begin());
                std::copy(osMono.begin(), osMono.end(), r.begin());
                slot.processStereo(layer.hpStage(), false, layer.spineModel(),
                                   25.0f, 110.0f, l.data(), r.data(), nOs);
            });
            const double cDown = time([&] { ovs.processStereoDown(l.data(), r.data(), N,
                                                                  dl.data(), dr.data()); });
            logMessage(juce::String::formatted(
                "os=%d  osc %.3f%%  up %.3f%%  shaper %.3f%%  spine(+copy) %.3f%%  down %.3f%%",
                os, cOsc, cUp, cWs, cSpine, cDown));
            expect(std::isfinite((float) (cOsc + cUp + cWs + cSpine + cDown)));
        }
    }
};

static VoicePerfTests voicePerfTestsInstance;
