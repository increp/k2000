// SpineDcBlockerHarnessTests.cpp — gated fixtures for DcBlocker
//
// M14a: DC is removed — feed (0.5 + sine 200 Hz); tail mean |DC| ≤ 0.02.
// M14b: Audio RMS preserved — same signal; RMS of tail output ≥ 0.5× input RMS.
// M14c: L/R independence — feed opposite-sign DC offsets to L and R; both converge.
//        (Port of HuggettNonlinearTests "DcBlocker keeps L/R state independent".)
//
// Include paths: include root is tests/, component at ../../src/...

#include <juce_core/juce_core.h>
#include "testdsp/Gate.h"
#include "testdsp/Spectrum.h"
#include "../../src/dsp/spine/DcBlocker.h"
#include <cmath>
#include <vector>

class SpineDcBlockerHarnessTests : public juce::UnitTest {
public:
    SpineDcBlockerHarnessTests() : juce::UnitTest("SpineDcBlockerHarness") {}

    static constexpr double kSR  = 48000.0;
    static constexpr int    kN   = 8192;          // total samples
    static constexpr int    kTail = 6000;          // tail start for DC measurement

    // Feed 0.5 + sine(f) through ch 0, collect output and input into separate vectors.
    static void runDcPlusSine(double f, float dcOffset,
                              std::vector<float>& outBuf,
                              std::vector<float>& inBuf) {
        DcBlocker dc;
        dc.prepare(kSR);
        dc.reset();
        outBuf.resize((size_t) kN);
        inBuf .resize((size_t) kN);
        for (int i = 0; i < kN; ++i) {
            const float x = dcOffset + (float) std::sin(
                2.0 * juce::MathConstants<double>::pi * f * i / kSR);
            inBuf [(size_t) i] = x;
            outBuf[(size_t) i] = dc.process(x, 0);
        }
    }

    void runTest() override {

        // ---- M14a: DC removed from tail (|mean| ≤ 0.02) ----
        beginTest("SpineDcBlocker M14a DC removed (tail |mean| <= 0.02)");
        {
            std::vector<float> outBuf, inBuf;
            runDcPlusSine(200.0, 0.5f, outBuf, inBuf);

            double tailSum = 0.0;
            for (int i = kTail; i < kN; ++i) {
                tailSum += double(outBuf[(size_t) i]);
            }
            const double tailMean = tailSum / (kN - kTail);
            std::printf("[SpineDcBlockerHarness] M14a tail mean=%.5f\n", tailMean);
            testdsp::Gate::check(*this, std::abs(tailMean), 0.02,
                                 testdsp::Gate::Dir::Max, "M14a DC tail mean");
        }

        // ---- M14b: audio RMS preserved (≥ 0.5× input RMS) ----
        beginTest("SpineDcBlocker M14b audio RMS preserved (>= 0.5x input)");
        {
            std::vector<float> outBuf, inBuf;
            runDcPlusSine(200.0, 0.5f, outBuf, inBuf);

            // Compare tail RMS (after settling).
            const std::vector<float> tailOut(outBuf.begin() + kTail, outBuf.end());
            const std::vector<float> tailIn (inBuf .begin() + kTail, inBuf .end());
            const float rmsOut = testdsp::Spectrum::rms(tailOut);
            const float rmsIn  = testdsp::Spectrum::rms(tailIn);
            std::printf("[SpineDcBlockerHarness] M14b rmsOut=%.4f rmsIn=%.4f ratio=%.4f\n",
                        (double) rmsOut, (double) rmsIn, (double) rmsOut / std::max(1e-6f, rmsIn));
            testdsp::Gate::check(*this, (double) rmsOut, 0.5 * (double) rmsIn,
                                 testdsp::Gate::Dir::Min, "M14b audio RMS preserved");
        }

        // ---- M14c: L/R independence — opposite-sign DC, both converge ----
        // Port of HuggettNonlinearTests "DcBlocker keeps L/R state independent":
        // if state were shared, opposite-sign DC inputs would cross-contaminate and
        // at least one channel would NOT converge near zero.
        beginTest("SpineDcBlocker M14c L/R independence (opposite DC, both -> 0)");
        {
            DcBlocker dc;
            dc.prepare(kSR);
            dc.reset();
            float lastL = 0.0f, lastR = 0.0f;
            for (int i = 0; i < kN; ++i) {
                lastL = dc.process(+0.5f, 0);
                lastR = dc.process(-0.5f, 1);
            }
            std::printf("[SpineDcBlockerHarness] M14c lastL=%.5f lastR=%.5f\n",
                        (double) lastL, (double) lastR);
            testdsp::Gate::check(*this, (double) std::abs(lastL), 0.05,
                                 testdsp::Gate::Dir::Max, "M14c L DC removed");
            testdsp::Gate::check(*this, (double) std::abs(lastR), 0.05,
                                 testdsp::Gate::Dir::Max, "M14c R DC removed");
        }
    }
};
static SpineDcBlockerHarnessTests spineDcBlockerHarnessTestsInstance;
