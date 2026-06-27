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
            for (int i = 0; i < N; ++i) in[i] = std::sin(2*M_PI*f*i);
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
            for (int i = 0; i < 2*N; ++i) in[i] = std::sin(2*M_PI*f2*i);
            hb.downsample(in.data(), N, out.data());
            std::vector<float> tail(out.begin() + 200, out.end());
            // it would alias to |0.30-0.5|*2 = 0.40 base-normalized; assert it's crushed
            double peak = 0; for (double f = 0.0; f <= 0.5; f += 0.005) peak = std::max(peak, mag(tail, f));
            const double db = 20.0 * std::log10(peak + 1e-12);
            expect(db < -80.0, "image rejection >=80 dB (got " + juce::String(db,1) + " dB)");
        }
    }
};
static Halfband2xTests halfband2xTestsInstance;
