#include <juce_core/juce_core.h>
#include "../src/dsp/spine/cmajor/NlSvfDriveLeanAdapter.h"
#include "../src/dsp/spine/NlSvfCell.h"
#include "../src/dsp/spine/AsymSaturator.h"
#include <chrono>
#include <vector>
#include <cmath>

// The fused processor (SVF + resonance saturator + drive in ONE advance) is the realistic
// v6 shape: the per-advance overhead is paid once for a multi-stage chain, not once per node.
// The block-size sweep isolates that overhead — if the ratio falls as the block grows, the
// gap is per-advance overhead (amortizable), not per-sample codegen cost.
struct NlSvfFusedPerfTests : public juce::UnitTest {
    NlSvfFusedPerfTests() : juce::UnitTest("NlSvfFusedPerf") {}
    static constexpr double kSR = 48000.0;

    void runTest() override {
        beginTest("fused NlSvfDrive matches C++ NlSvfCell->AsymSaturator chain (per-sample)");
        {
            const int n = 8192;
            std::vector<float> a((size_t)n), b((size_t)n);
            for (int i = 0; i < n; ++i)
                a[(size_t)i] = b[(size_t)i] = 0.5f * (float) std::sin(2.0*juce::MathConstants<double>::pi*220.0*i/kSR);

            NlSvfDriveLeanAdapter f; f.prepare(kSR); f.reset();
            f.setParams(1200.0f, 0.7f, 0.7f, 0, 0.5f, 0.25f, 30.0f);
            f.process(a.data(), n);

            NlSvfCell c; c.prepare(kSR); c.reset(); c.setCutoff(1200.0f); c.setResonance(0.7f); c.setResSat(0.7f);
            AsymSaturator s; s.setDrive(0.5f, 0.25f, 30.0f);
            for (int i = 0; i < n; ++i) { float l = b[(size_t)i], r = l; c.process(l, r, 0); b[(size_t)i] = s.process(l); }

            double m = 0.0; for (int i = 0; i < n; ++i) m = std::max(m, (double) std::abs(a[(size_t)i] - b[(size_t)i]));
            expect(m < 2.0e-3, "fused per-sample within 2e-3: max err " + juce::String(m, 7));
            logMessage("Fused vs C++ chain worst per-sample error: " + juce::String(m, 8));
        }

        beginTest("fused-processor perf vs block size (per-advance overhead amortization)");
        const int kVoices = 256, kMono = 2 * kVoices;
        const int kTotal = 25600;   // samples per channel, fixed across block sizes
        const int blockSizes[] = { 64, 128, 256, 512 };
        std::printf("\n=== Fused NlSvfDrive: 512 channel-streams, %d samples/ch (equal work) ===\n", kTotal);
        std::printf("%-8s %12s %14s %10s\n", "block", "fused(ms)", "cppChain(ms)", "ratio");
        using clock = std::chrono::high_resolution_clock;
        double sink = 0.0;
        for (int B : blockSizes) {
            const int blocks = kTotal / B;
            std::vector<float> block((size_t)B);
            for (int i = 0; i < B; ++i) block[(size_t)i] = 0.3f * (float) std::sin(0.05 * i);

            // fused Cmajor (512 mono, zero-copy)
            std::vector<NlSvfDriveLeanAdapter> fu((size_t)kMono);
            for (auto& a : fu) { a.prepare(kSR); a.reset(); a.setParams(1000.0f, 0.7f, 0.7f, 0, 0.5f, 0.25f, 30.0f);
                                 std::copy(block.begin(), block.end(), a.inBlock()); }
            auto t0 = clock::now();
            for (int bk = 0; bk < blocks; ++bk) for (auto& a : fu) { a.advanceBlock(B); sink += a.outBlock()[B-1]; }
            auto t1 = clock::now();

            // C++ chain (256 stereo NlSvfCell -> AsymSaturator) = 512 channel-streams
            std::vector<NlSvfCell> nl((size_t)kVoices);
            for (auto& c : nl) { c.prepare(kSR); c.reset(); c.setCutoff(1000.0f); c.setResonance(0.7f); c.setResSat(0.7f); }
            AsymSaturator s; s.setDrive(0.5f, 0.25f, 30.0f);
            auto t2 = clock::now();
            for (int bk = 0; bk < blocks; ++bk) for (auto& c : nl) {
                auto bl = block; auto br = block;
                for (int i = 0; i < B; ++i) { float l = bl[(size_t)i], r = br[(size_t)i]; c.process(l, r, 0); bl[(size_t)i] = s.process(l); br[(size_t)i] = s.process(r); }
                sink += bl[(size_t)(B-1)] + br[(size_t)(B-1)];
            }
            auto t3 = clock::now();

            const double fMs = std::chrono::duration<double,std::milli>(t1-t0).count();
            const double cMs = std::chrono::duration<double,std::milli>(t3-t2).count();
            std::printf("%-8d %12.2f %14.2f %9.2fx\n", B, fMs, cMs, fMs/cMs);
        }
        std::printf("(single-stage NlSvf was ~1.95x at block 128; lower fused ratios => overhead amortizes)\n");
        expect(std::isfinite(sink), "finite");
    }
};
static NlSvfFusedPerfTests nlSvfFusedPerfTestsInstance;
