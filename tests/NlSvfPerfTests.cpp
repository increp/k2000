#include <juce_core/juce_core.h>
#include "../src/dsp/spine/cmajor/NlSvfAdapter.h"
#include "../src/dsp/spine/cmajor/NlSvfLeanAdapter.h"
#include "../src/dsp/spine/NlSvfCell.h"
#include <chrono>
#include <vector>
#include <cmath>

struct NlSvfPerfTests : public juce::UnitTest {
    NlSvfPerfTests() : juce::UnitTest("NlSvfPerf") {}

    void runTest() override {
        beginTest("256-voice nonlinear perf: copy vs lean(zero-copy) adapter vs NlSvfCell");
        const double sr = 48000.0;
        const int kVoices = 256, kBlock = 128, kBlocks = 200;

        // FAIRNESS: NlSvfCell::process filters BOTH channels per call (stereo), so 256 cells
        // = 512 channel-streams of work. The Cmajor NlSvf is mono (1 channel/instance), so we
        // run 2*kVoices = 512 mono adapters to match — an apples-to-apples 512-vs-512 compare.
        const int kMono = 2 * kVoices;

        std::vector<float> block((size_t)kBlock);
        for (int i = 0; i < kBlock; ++i) block[(size_t)i] = 0.3f * (float) std::sin(0.05 * i);

        using clock = std::chrono::high_resolution_clock;

        // --- copy adapter (512 mono channels) ---
        std::vector<NlSvfAdapter> cp((size_t)kMono);
        for (auto& a : cp) { a.prepare(sr); a.reset(); a.setParams(1000.0f, 0.7f, 0.7f, 0); }
        auto t0 = clock::now(); double sinkCp = 0.0;
        for (int b = 0; b < kBlocks; ++b) for (auto& a : cp) { auto buf = block; a.process(buf.data(), kBlock); sinkCp += buf[(size_t)(kBlock-1)]; }
        auto t1 = clock::now();

        // --- lean / zero-copy adapter (512 mono channels; write inBlock, advance, read outBlock) ---
        std::vector<NlSvfLeanAdapter> ln((size_t)kMono);
        for (auto& a : ln) { a.prepare(sr); a.reset(); a.setParams(1000.0f, 0.7f, 0.7f, 0);
                             std::copy(block.begin(), block.end(), a.inBlock()); }   // load input once
        auto t2 = clock::now(); double sinkLn = 0.0;
        for (int b = 0; b < kBlocks; ++b) for (auto& a : ln) { a.advanceBlock(kBlock); sinkLn += a.outBlock()[kBlock-1]; }
        auto t3 = clock::now();

        // --- NlSvfCell baseline (resSat on): 256 stereo cells = 512 channel-streams ---
        std::vector<NlSvfCell> nl((size_t)kVoices);
        for (auto& c : nl) { c.prepare(sr); c.reset(); c.setCutoff(1000.0f); c.setResonance(0.7f); c.setResSat(0.7f); }
        auto t4 = clock::now(); double sinkNl = 0.0;
        for (int b = 0; b < kBlocks; ++b) for (auto& c : nl) { auto buf = block; for (int i = 0; i < kBlock; ++i) { float l = buf[(size_t)i], r = l; c.process(l, r, 0); buf[(size_t)i] = l; } sinkNl += buf[(size_t)(kBlock-1)]; }
        auto t5 = clock::now();

        const double cpMs = std::chrono::duration<double,std::milli>(t1-t0).count();
        const double lnMs = std::chrono::duration<double,std::milli>(t3-t2).count();
        const double nlMs = std::chrono::duration<double,std::milli>(t5-t4).count();
        std::printf("\n=== NlSvf perf: 512 channel-streams (%d blocks x %d samples, equal work) ===\n", kBlocks, kBlock);
        std::printf("copy adapter (512 mono) : %8.2f ms  (%.2fx vs cpp)\n", cpMs, cpMs/nlMs);
        std::printf("lean adapter (512 mono) : %8.2f ms  (%.2fx vs cpp)\n", lnMs, lnMs/nlMs);
        std::printf("NlSvfCell C++ (256 stereo): %8.2f ms\n", nlMs);
        std::printf("sizeof: NlSvfAdapter=%zu NlSvfLeanAdapter=%zu NlSvfCell=%zu [sinks %.3f %.3f %.3f]\n",
                    sizeof(NlSvfAdapter), sizeof(NlSvfLeanAdapter), sizeof(NlSvfCell), sinkCp, sinkLn, sinkNl);

        expect(std::isfinite(sinkCp) && std::isfinite(sinkLn) && std::isfinite(sinkNl), "all paths finite");
        expect(cpMs > 0.0 && lnMs > 0.0 && nlMs > 0.0, "all paths ran");
    }
};
static NlSvfPerfTests nlSvfPerfTestsInstance;
