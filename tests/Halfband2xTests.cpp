#include <juce_core/juce_core.h>
#include <cmath>
#include <vector>
#include "../src/dsp/Halfband2x.h"

class Halfband2xTests : public juce::UnitTest {
public:
    Halfband2xTests() : juce::UnitTest("Halfband2x") {}

    // Peak magnitude of a real signal at normalized freq f (cycles/sample) via Goertzel-ish DFT.
    static double mag(const std::vector<float>& x, double f) {
        double re = 0, im = 0;
        for (size_t n = 0; n < x.size(); ++n) {
            const double a = 2.0 * 3.14159265358979323846 * f * (double) n;
            re += x[n] * std::cos(a); im -= x[n] * std::sin(a);
        }
        return std::sqrt(re*re + im*im) / (double) x.size() * 2.0;
    }

    void runTest() override {
        const int N = 4096;

        beginTest("upsample passband (200 Hz @ 48k) preserved, ~unity");
        {
            Halfband2x hb; hb.reset();
            std::vector<float> in(N), out(2*N);
            const double f = 200.0 / 48000.0;  // base-rate normalized
            for (int i = 0; i < N; ++i) in[i] = std::sin(2*juce::MathConstants<double>::pi*f*i);
            hb.upsample(in.data(), N, out.data());
            // at 2x rate the tone sits at f/2; skip the filter warm-up
            std::vector<float> tail(out.begin() + 200, out.end());
            const double g = mag(tail, f/2.0);
            expect(g > 0.9 && g < 1.1, "upsampled passband gain ~1 (got " + juce::String(g,3) + ")");
        }

        beginTest("downsample rejects an image above base-Nyquist");
        {
            // A tone at 0.30 cyc/sample (2x domain) = 28.8k @ 96k, i.e. above 24k base-Nyquist.
            Halfband2x hb; hb.reset();
            std::vector<float> in(2*N), out(N);
            const double f2 = 0.30;
            for (int i = 0; i < 2*N; ++i) in[i] = std::sin(2*juce::MathConstants<double>::pi*f2*i);
            hb.downsample(in.data(), N, out.data());
            std::vector<float> tail(out.begin() + 200, out.end());
            // it would alias to |0.30-0.5|*2 = 0.40 base-normalized; assert it's crushed
            double peak = 0; for (double f = 0.0; f <= 0.5; f += 0.005) peak = std::max(peak, mag(tail, f));
            const double db = 20.0 * std::log10(peak + 1e-12);
            expect(db < -80.0, "image rejection >=80 dB (got " + juce::String(db,1) + " dB)");
        }

        beginTest("golden equivalence vector (guards optimizations against numeric drift)");
        {
            // Captured 2026-07-02 from the reference (pre-optimization) implementation
            // on deterministic LCG noise. An optimized Halfband2x (e.g. exploiting the
            // halfband zero taps) must reproduce these within float noise. If a
            // DELIBERATE filter redesign changes them, recapture and say so in the diff.
            unsigned s = 12345u;
            auto rnd = [&]() { s = s*1664525u + 1013904223u; return ((s >> 9) / 4194304.0f) - 1.0f; };
            const int M = 256;
            std::vector<float> in((size_t) M), up((size_t) (2*M)), dn((size_t) M);
            for (auto& v : in) v = 0.5f * rnd();
            Halfband2x hbU, hbD;
            hbU.upsample(in.data(), M, up.data());
            hbD.downsample(up.data(), M, dn.data());

            const int   upIdx[]  = { 50, 100, 200, 300, 400, 511 };
            const float upRef[]  = { 0.04834877f, -0.03733288f, 0.02827655f,
                                     -0.08027717f, -0.38160583f, -0.33816302f };
            const int   dnIdx[]  = { 50, 100, 150, 200, 250, 255 };
            const float dnRef[]  = { 0.20530711f, 0.18134883f, 0.14807343f,
                                     -0.39674664f, -0.07224100f, 0.22509778f };
            for (int k = 0; k < 6; ++k) {
                expectWithinAbsoluteError(up[(size_t) upIdx[k]], upRef[k], 2.0e-5f);
                expectWithinAbsoluteError(dn[(size_t) dnIdx[k]], dnRef[k], 2.0e-5f);
            }
        }

        beginTest("round-trip reconstructs the input at an integer base-sample delay");
        {
            // Feed a low-freq sine through upsample -> downsample and assert the
            // output equals the input delayed by the (empirically measured) round-
            // trip latency, within the passband. The delay MUST be an integer number
            // of base samples (the even-phase decimation guarantees no half-sample
            // offset). We measure delay by maximizing cross-correlation, then verify
            // the residual after aligning is tiny.
            Halfband2x hb; hb.reset();
            std::vector<float> in(N), up(2*N), out(N);
            const double f = 200.0 / 48000.0;  // base-rate normalized, well inside passband
            for (int i = 0; i < N; ++i) in[i] = std::sin(2*juce::MathConstants<double>::pi*f*i);
            hb.upsample(in.data(), N, up.data());
            hb.downsample(up.data(), N, out.data());

            // Find the integer base-sample delay D that best aligns out[n] ~ in[n-D],
            // scanning a generous range that covers the ~36-sample group delay.
            int bestD = -1; double bestCorr = -1.0;
            for (int D = 0; D <= 80; ++D) {
                double num = 0.0, den = 0.0;
                for (int n = D + 200; n < N; ++n) {           // skip warm-up
                    num += (double) out[(size_t) n] * (double) in[(size_t) (n - D)];
                    den += (double) in[(size_t) (n - D)] * (double) in[(size_t) (n - D)];
                }
                const double corr = (den > 0.0) ? num / den : 0.0;  // ~unity gain at the right D
                if (std::abs(corr - 1.0) < std::abs(bestCorr - 1.0) || bestD < 0) {
                    bestCorr = corr; bestD = D;
                }
            }
            // The alignment gain at the best integer delay should be ~1 (passband, unity).
            expect(bestD >= 0 && std::abs(bestCorr - 1.0) < 0.05,
                   "round-trip aligns at integer delay D=" + juce::String(bestD)
                   + " with gain " + juce::String(bestCorr,4));

            // Residual error after aligning out[n] against in[n-bestD], over the steady
            // region, must be small (good passband reconstruction). Normalize by input RMS.
            double errSq = 0.0, sigSq = 0.0; int cnt = 0;
            for (int n = bestD + 300; n < N; ++n) {
                const double d = (double) out[(size_t) n] - (double) in[(size_t) (n - bestD)];
                errSq += d * d; sigSq += (double) in[(size_t) (n - bestD)] * (double) in[(size_t) (n - bestD)];
                ++cnt;
            }
            const double nrmseDb = 20.0 * std::log10(std::sqrt(errSq / std::max(1, cnt))
                                                     / (std::sqrt(sigSq / std::max(1, cnt)) + 1e-12) + 1e-12);
            expect(nrmseDb < -40.0,
                   "round-trip reconstruction error <= -40 dB (got " + juce::String(nrmseDb,1) + " dB)");
        }
    }
};
static Halfband2xTests halfband2xTestsInstance;
