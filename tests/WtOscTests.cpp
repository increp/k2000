#include <juce_core/juce_core.h>
#include "../src/dsp/spine/cmajor/WtOscAdapter.h"
#include <cmath>
#include <vector>

struct WtOscTests : public juce::UnitTest {
    WtOscTests() : juce::UnitTest("WtOsc") {}

    // Goertzel single-bin magnitude at frequency f over buf.
    static double bin(const std::vector<float>& buf, double f, double sr) {
        const double w = 2.0 * juce::MathConstants<double>::pi * f / sr;
        const double c = 2.0 * std::cos(w);
        double s1 = 0, s2 = 0;
        for (float x : buf) { const double s0 = x + c * s1 - s2; s2 = s1; s1 = s0; }
        return std::sqrt(s1*s1 + s2*s2 - c*s1*s2);
    }

    void runTest() override {
        beginTest("table pushed from C++ plays back as the expected single-cycle waveform");
        const double sr = 48000.0;
        const int N = WtOscAdapter::kTableSize;
        std::vector<float> table((size_t)N);
        for (int i = 0; i < N; ++i)
            table[(size_t)i] = (float) std::sin(2.0*juce::MathConstants<double>::pi*i/N);

        WtOscAdapter o; o.prepare(sr); o.reset();
        o.setTable(table.data(), N); o.setFrequency(440.0f);

        const int n = 16384;
        std::vector<float> buf((size_t)n);
        o.process(buf.data(), n);

        double rms = 0.0; for (float x : buf) rms += (double)x*x; rms = std::sqrt(rms / n);
        const double atF   = bin(buf, 440.0, sr);
        const double atOff = bin(buf, 1500.0, sr);   // an unrelated frequency: should be far lower
        expect(std::isfinite(rms) && rms > 0.1, "non-trivial output (rms " + juce::String(rms,4) + ")");
        expect(atF > atOff * 8.0, "energy concentrated at 440 Hz (atF " + juce::String(atF,2)
               + " vs off " + juce::String(atOff,2) + ") — proves the table crossed and is used");
    }
};
static WtOscTests wtOscTestsInstance;
