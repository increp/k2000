// SpineHuggettHarnessTests.cpp — gated fixtures for the HuggettFilter spine path
//
// M1: output is finite.
// M2: maxAbs ≤ 4.0 (bounded output under drive + resonance).
// M8: block-processed vs per-sample maxDiff ≤ 1e-5 (regression for the g_eff droop
//     click removed in v5.1.0 — droop is now gated on drive/resonance; the linear path
//     is linear by construction so block == per-sample within float rounding).
// M13: zero drive / resonance == bare NlSvfCell LP output ≤ 1e-5.
// Stereo: mono sine fed as stereo → L/R correlation ≥ 0.999 (spine is mono-coefficient).
//
// Mirrors OverdriveDiagnosticTests::filterPostDrive for the M8 block-vs-sample path.
// Mirrors HuggettNonlinearTests "zero drive/resonance == bare linear cell" for M13.

#include <juce_core/juce_core.h>
#include "testdsp/SignalGen.h"
#include "testdsp/Metrics.h"
#include "testdsp/Gate.h"
#include "../../src/dsp/spine/HuggettFilter.h"
#include "../../src/dsp/spine/NlSvfCell.h"
#include <cmath>
#include <vector>
#include <memory>

class SpineHuggettHarnessTests : public juce::UnitTest {
public:
    SpineHuggettHarnessTests() : juce::UnitTest("SpineHuggettHarness") {}

    static constexpr double kSR       = 48000.0;
    static constexpr int    kFundBin  = 75;         // matches OverdriveDiagnosticTests
    static constexpr int    kN        = 1 << 14;    // 16384-pt window
    static constexpr int    kWarm     = 4096;        // settling samples to discard

    // Build a sine at kFundBin cycles per kN samples (bin-aligned for any kN-length slice).
    static std::vector<float> makeSine(float amp, int totalLen) {
        std::vector<float> v((size_t) totalLen);
        for (int i = 0; i < totalLen; ++i) {
            v[(size_t) i] = amp * (float) std::sin(
                2.0 * juce::MathConstants<double>::pi * (double) kFundBin * i / (double) kN);
        }
        return v;
    }

    // Process HuggettFilter in blocks of `blockSize`, return kN steady-state samples
    // (discards kWarm warm-up samples).  Mirrors filterPostDrive in OverdriveDiagnosticTests.
    static std::vector<float> filterInBlocks(float amp, float postDrive, int blockSize) {
        HuggettFilter h;
        h.prepare(kSR);
        h.setMode(HuggettFilter::Mode::LP);
        h.setSlope(HuggettFilter::Slope::db24);
        h.setSeparation(0.0f);
        h.setCommon(1200.0f, 0.0f, 0.0f);   // resonance 0, pre-drive 0
        h.setPostDrive(postDrive);

        std::unique_ptr<FilterModel::State> st(h.makeState());
        h.reset(*st);

        const int total = kWarm + kN;
        auto in = makeSine(amp, total);
        std::vector<float> full((size_t) total);

        int i = 0;
        while (i < total) {
            const int blk = std::min(blockSize, total - i);
            std::vector<float> lBuf(in.begin() + i, in.begin() + i + blk);
            std::vector<float> rBuf = lBuf;
            h.processStereo(*st, lBuf.data(), rBuf.data(), blk);
            for (int k = 0; k < blk; ++k) {
                full[(size_t) (i + k)] = lBuf[(size_t) k];
            }
            i += blk;
        }
        // Drop settling window; return exactly kN samples.
        return std::vector<float>(full.begin() + kWarm, full.end());
    }

    // Process HuggettFilter as stereo, return L and R buffers separately.
    static void filterStereo(float amp, float postDrive, int blockSize,
                              std::vector<float>& outL, std::vector<float>& outR) {
        HuggettFilter h;
        h.prepare(kSR);
        h.setMode(HuggettFilter::Mode::LP);
        h.setSlope(HuggettFilter::Slope::db24);
        h.setSeparation(0.0f);
        h.setCommon(1200.0f, 0.0f, 0.0f);
        h.setPostDrive(postDrive);

        std::unique_ptr<FilterModel::State> st(h.makeState());
        h.reset(*st);

        const int total = kWarm + kN;
        auto in = makeSine(amp, total);
        std::vector<float> fullL((size_t) total), fullR((size_t) total);

        int i = 0;
        while (i < total) {
            const int blk = std::min(blockSize, total - i);
            std::vector<float> lBuf(in.begin() + i, in.begin() + i + blk);
            std::vector<float> rBuf = lBuf;
            h.processStereo(*st, lBuf.data(), rBuf.data(), blk);
            for (int k = 0; k < blk; ++k) {
                fullL[(size_t) (i + k)] = lBuf[(size_t) k];
                fullR[(size_t) (i + k)] = rBuf[(size_t) k];
            }
            i += blk;
        }
        outL = std::vector<float>(fullL.begin() + kWarm, fullL.end());
        outR = std::vector<float>(fullR.begin() + kWarm, fullR.end());
    }

    void runTest() override {

        // --- M1: finiteness ---
        beginTest("SpineHuggett M1 finite (post-drive 1.0, amp 2.0)");
        {
            const auto sig = filterInBlocks(2.0f, 1.0f, 128);
            expect(testdsp::Metrics::finite(sig), "output must be finite");
        }

        // --- M2: maxAbs <= 4.0 ---
        beginTest("SpineHuggett M2 maxAbs <= 4.0 (post-drive 1.0, amp 2.0)");
        {
            const auto sig = filterInBlocks(2.0f, 1.0f, 128);
            const float peak = testdsp::Metrics::maxAbs(sig);
            std::printf("[SpineHuggettHarness] M2 peak=%.4f\n", peak);
            testdsp::Gate::check(*this, (double) peak, 4.0,
                                 testdsp::Gate::Dir::Max, "M2 maxAbs");
        }

        // --- M8: block-vs-per-sample (the g_eff droop regression gate) ---
        // Process amp=2.0 sine through the post-drive path in blocks of 128 AND per-sample
        // (blockSize=1). The droop removal in v5.1.0 makes these bit-for-bit identical
        // within float rounding. Gate: maxDiff <= 1e-5.
        for (int blockSize : { 64, 128, 256 }) {
            beginTest("SpineHuggett M8 block-vs-sample <= 1e-5 (blockSize=" + juce::String(blockSize) + ")");
            {
                const auto blk = filterInBlocks(2.0f, 1.0f, blockSize);
                const auto smp = filterInBlocks(2.0f, 1.0f, 1);
                const float maxDiff = testdsp::Metrics::maxDiff(blk, smp);
                std::printf("[SpineHuggettHarness] M8 blockSize=%d  maxDiff=%.2e\n",
                            blockSize, (double) maxDiff);
                testdsp::Gate::check(*this, (double) maxDiff, 1.0e-5,
                                     testdsp::Gate::Dir::Max,
                                     "M8 block-vs-sample blockSize=" + juce::String(blockSize));
            }
        }

        // --- M13: zero drive/resonance == bare NlSvfCell (db12, cutoff 1200 Hz) ---
        // HuggettFilter at zero drive must route through the linear SVF code path,
        // producing output bit-for-bit with a standalone NlSvfCell (which is linear
        // when resSat==0). Mirrors HuggettNonlinearTests "zero drive/resonance == bare
        // linear cell".
        beginTest("SpineHuggett M13 zero-drive == bare NlSvfCell <= 1e-5");
        {
            const double f300 = 300.0;
            const int    nM13 = 4096;

            HuggettFilter h;
            h.prepare(48000.0);
            h.setMode(HuggettFilter::Mode::LP);
            h.setSlope(HuggettFilter::Slope::db12);
            h.setSeparation(0.0f);
            h.setCommon(1200.0f, 0.0f, 0.0f);
            h.setPostDrive(0.0f);
            std::unique_ptr<FilterModel::State> st(h.makeState());
            h.reset(*st);

            NlSvfCell ref;
            ref.prepare(48000.0);
            ref.setCutoff(1200.0f);
            ref.setResonance(0.0f);
            ref.setResSat(0.0f);
            ref.reset();

            float maxDiff = 0.0f;
            for (int i = 0; i < nM13; ++i) {
                const float x = 0.5f * (float) std::sin(
                    2.0 * juce::MathConstants<double>::pi * f300 * i / 48000.0);
                float hl = x, hr = x;
                h.processStereo(*st, &hl, &hr, 1);
                float rl = x, rr = x;
                ref.process(rl, rr, NlSvfCell::LP);
                maxDiff = std::max(maxDiff, std::abs(hl - rl));
            }
            std::printf("[SpineHuggettHarness] M13 maxDiff=%.2e\n", (double) maxDiff);
            testdsp::Gate::check(*this, (double) maxDiff, 1.0e-5,
                                 testdsp::Gate::Dir::Max, "M13 zero-drive == bare NlSvfCell");
        }

        // --- Stereo gate: mono sine fed as stereo -> L/R correlation >= 0.999 ---
        // The spine uses mono coefficients; both channels receive identical input, so
        // L and R must be virtually identical (correlation ≥ 0.999).
        beginTest("SpineHuggett stereo L==R correlation >= 0.999");
        {
            std::vector<float> L, R;
            filterStereo(0.9f, 0.5f, 128, L, R);
            const float corr = testdsp::Metrics::stereoCorrelation(L, R);
            std::printf("[SpineHuggettHarness] stereo correlation=%.6f\n", (double) corr);
            testdsp::Gate::check(*this, (double) corr, 0.999,
                                 testdsp::Gate::Dir::Min, "stereo L==R");
        }
    }
};
static SpineHuggettHarnessTests spineHuggettHarnessTestsInstance;
