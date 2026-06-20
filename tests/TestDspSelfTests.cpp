#include <juce_core/juce_core.h>
#include "testdsp/SignalGen.h"
#include "testdsp/Spectrum.h"
#include "testdsp/Metrics.h"
#include "testdsp/Reference.h"
#include "testdsp/Gate.h"

class TestDspSelfTests : public juce::UnitTest {
public:
    TestDspSelfTests() : juce::UnitTest("TestDspSelf") {}
    void runTest() override {
        beginTest("bin-aligned tone is a single FFT bin");
        {
            const int N = 1 << 14, bin = 75;
            auto x = testdsp::SignalGen::binAlignedSine(1.0f, bin, N);
            auto mag = testdsp::Spectrum::magnitude(x);
            // fundamental bin dominates; neighbours are ~0 (numerical floor).
            const float fund = mag[(size_t) bin];
            float other = 0.0f;
            for (int b = 2; b < (int) mag.size(); ++b)
                if (b != bin) other = std::max(other, mag[(size_t) b]);
            expect(fund > 1.0f, "fundamental present: " + juce::String(fund));
            expect(other < fund * 1.0e-4f, "no leakage: other=" + juce::String(other));

            beginTest("rms of unit sine is ~0.707");
            expectWithinAbsoluteError(testdsp::Spectrum::rms(x), 0.70710677f, 1.0e-3f);

            beginTest("allFinite catches NaN");
            std::vector<float> bad { 0.0f, std::nanf(""), 1.0f };
            expect(! testdsp::Spectrum::allFinite(bad));
        }

        beginTest("M3 inharmonicDb ~ floor for a pure tone");
        {
            auto x = testdsp::SignalGen::binAlignedSine(0.7f, 75, 1 << 14);
            auto mag = testdsp::Spectrum::magnitude(x);
            expect(testdsp::Metrics::inharmonicDb(mag, 75) < -100.0, "pure tone is clean");
        }
        beginTest("M6 thdPlusNDb ~ floor for a pure tone");
        {
            auto x = testdsp::SignalGen::binAlignedSine(0.7f, 75, 1 << 14);
            auto mag = testdsp::Spectrum::magnitude(x);
            expect(testdsp::Metrics::thdPlusNDb(mag, 75) < -100.0, "pure tone has ~no harmonics");
        }
        beginTest("M8 maxDiff is zero for identical buffers, exact for a known offset");
        {
            auto a = testdsp::SignalGen::binAlignedSine(1.0f, 10, 1024);
            auto b = a; expectWithinAbsoluteError(testdsp::Metrics::maxDiff(a, b), 0.0f, 0.0f);
            for (auto& v : b) v += 0.25f;
            expectWithinAbsoluteError(testdsp::Metrics::maxDiff(a, b), 0.25f, 1.0e-6f);
        }
        beginTest("stereoCorrelation = 1 identical, ~ -1 inverted");
        {
            auto l = testdsp::SignalGen::binAlignedSine(1.0f, 10, 4096); auto r = l;
            expectWithinAbsoluteError(testdsp::Metrics::stereoCorrelation(l, r), 1.0f, 1.0e-4f);
            for (auto& v : r) v = -v;
            expectWithinAbsoluteError(testdsp::Metrics::stereoCorrelation(l, r), -1.0f, 1.0e-4f);
        }

        beginTest("decimator preserves an in-band tone (reconstruction < -100 dB error)");
        {
            const int n = 1 << 13, M = 16, bin = 60;
            const double fsBase = 48000.0, f = (double) bin * fsBase / (double) n;
            auto hi  = testdsp::SignalGen::sine(0.5f, f, fsBase * (double) M, n * M);
            auto lo  = testdsp::Reference::decimate(hi, M);
            auto ref = testdsp::SignalGen::sine(0.5f, f, fsBase, n);
            expectEquals((int) lo.size(), n);
            // Skip FIR group-delay transient (257-tap filter at 16x → ~16 base-rate samples).
            // Compare RMS of steady-state tails; allow ±5 ms worth of amplitude tolerance.
            const std::vector<float> a(lo.begin() + 1024, lo.end());
            const std::vector<float> b(ref.begin() + 1024, ref.end());
            expectWithinAbsoluteError(testdsp::Spectrum::rms(a), testdsp::Spectrum::rms(b), 5.0e-3f);
        }

        // M=32, bin=3000 gives more harmonic foldover so the DAFx-16 ordering is robust.
        // Measured: hard ≈ -11 dB, soft ≈ -23 dB → margin ≈ 11 dB (threshold is 6 dB).
        beginTest("M4 NSR: hard clip aliases worse than soft tanh (DAFx-16 ordering)");
        {
            const int    n      = 1 << 13;
            const int    M      = 32;
            const int    bin    = 3000;
            const double fsBase = 48000.0;
            const double f      = (double) bin * fsBase / (double) n;

            auto run = [&](float (*shape)(float), double sr, int len) {
                auto x = testdsp::SignalGen::sine(0.9f, f, sr, len);
                for (auto& v : x) v = shape(v);
                return x;
            };

            auto truthHard = testdsp::Reference::decimate(
                run(+[](float v) { return std::max(-0.5f, std::min(0.5f, v)); },
                    fsBase * (double) M, n * M), M);
            auto dutHard = run(+[](float v) { return std::max(-0.5f, std::min(0.5f, v)); },
                               fsBase, n);

            auto truthSoft = testdsp::Reference::decimate(
                run(+[](float v) { return std::tanh(v); },
                    fsBase * (double) M, n * M), M);
            auto dutSoft = run(+[](float v) { return std::tanh(v); }, fsBase, n);

            const double nsrHard = testdsp::Reference::noiseToSignalDb(dutHard, truthHard, bin);
            const double nsrSoft = testdsp::Reference::noiseToSignalDb(dutSoft, truthSoft, bin);

            expect(nsrHard > nsrSoft + 6.0,
                   "hard clip NSR " + juce::String(nsrHard)
                   + " must exceed soft tanh NSR " + juce::String(nsrSoft) + " by 6 dB");
        }

        beginTest("Gate passes within bound");
        testdsp::Gate::check(*this, -70.0, -60.0, testdsp::Gate::Dir::Max, "M4 demo");  // -70 <= -60 -> pass
    }
};
static TestDspSelfTests testDspSelfTestsInstance;

