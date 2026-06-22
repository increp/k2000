#include <juce_core/juce_core.h>
#include "../src/dsp/spine/cmajor/SvfLinearAdapter.h"
#include "../src/dsp/spine/NlSvfCell.h"
#include <chrono>
#include <vector>
#include <cmath>

struct CmajorSvfPerfTests : public juce::UnitTest {
    CmajorSvfPerfTests() : juce::UnitTest("CmajorSvfPerf") {}

    void runTest() override {
        beginTest("256-voice perf: Cmajor adapter vs NlSvfCell (ratio is a decision input)");
        const double sr = 48000.0;
        const int kVoices = 256, kBlock = 128, kBlocks = 200;  // ~200 blocks of 128 @ 256 voices

        std::vector<float> block((size_t)kBlock);
        for (int i = 0; i < kBlock; ++i) block[(size_t)i] = 0.3f * (float) std::sin(0.05 * i);

        // --- Cmajor adapters ---
        std::vector<SvfLinearAdapter> cm((size_t)kVoices);
        for (auto& a : cm) { a.prepare(sr); a.reset(); a.setParams(1000.0f, 0.3f, 0); }
        auto t0 = std::chrono::high_resolution_clock::now();
        double sinkA = 0.0;
        for (int b = 0; b < kBlocks; ++b)
            for (auto& a : cm) { auto buf = block; a.process(buf.data(), kBlock); sinkA += buf[(size_t)(kBlock-1)]; }
        auto t1 = std::chrono::high_resolution_clock::now();

        // --- C++ NlSvfCell baseline ---
        std::vector<NlSvfCell> nl((size_t)kVoices);
        for (auto& c : nl) { c.prepare(sr); c.reset(); c.setCutoff(1000.0f); c.setResonance(0.3f); c.setResSat(0.0f); }
        auto t2 = std::chrono::high_resolution_clock::now();
        double sinkB = 0.0;
        for (int b = 0; b < kBlocks; ++b)
            for (auto& c : nl) { auto buf = block; for (int i = 0; i < kBlock; ++i) { float l = buf[(size_t)i], r = l; c.process(l, r, 0); buf[(size_t)i] = l; } sinkB += buf[(size_t)(kBlock-1)]; }
        auto t3 = std::chrono::high_resolution_clock::now();

        const double cmMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
        const double nlMs = std::chrono::duration<double, std::milli>(t3 - t2).count();
        std::printf("\n=== 256-voice SVF perf (%d blocks x %d samples) ===\n", kBlocks, kBlock);
        std::printf("Cmajor adapter: %8.2f ms\n", cmMs);
        std::printf("NlSvfCell C++ : %8.2f ms\n", nlMs);
        std::printf("ratio (cmaj/cpp): %6.2fx   [sinks %.3f %.3f]\n", cmMs / nlMs, sinkA, sinkB);
        std::printf("sizeof(SvfLinearAdapter)=%zu  sizeof(NlSvfCell)=%zu (note: adapter holds a unique_ptr to the generated state)\n",
                    sizeof(SvfLinearAdapter), sizeof(NlSvfCell));

        expect(std::isfinite(sinkA) && std::isfinite(sinkB), "both paths produce finite output");
        expect(cmMs > 0.0 && nlMs > 0.0, "both paths ran");
    }
};
static CmajorSvfPerfTests cmajorSvfPerfTestsInstance;
