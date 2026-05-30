#include <juce_core/juce_core.h>
#include "../src/dsp/Oscillator.h"
#include <vector>
#include <cmath>

class OscillatorTest : public juce::UnitTest {
public:
    OscillatorTest() : juce::UnitTest("Oscillator") {}

    static constexpr double SR = 48000.0;
    static constexpr int BLOCK = 512;

    void runTest() override {
        runSineFundamentalTest();
        runSawHasHarmonicsTest();
        runResetTest();
        runZeroFreqTest();
        runBlockContinuityTest();
    }

private:
    static double rms(const std::vector<float>& buf) {
        double s = 0;
        for (float v : buf) s += double(v) * v;
        return std::sqrt(s / buf.size());
    }

    void runSineFundamentalTest() {
        beginTest("sine at 440Hz has FFT peak near bin 440 (1Hz res, 48k samples)");
        Oscillator osc;
        osc.prepare(SR);
        osc.setWaveform(Oscillator::Waveform::Sine);
        osc.setFrequency(440.0f);

        const int N = 48000; // 1 Hz bin resolution
        std::vector<float> buf(N);
        for (int i = 0; i < N; ++i) buf[i] = osc.processSample();

        // Find peak bin via brute-force DFT around 440Hz only — cheaper than FFT.
        double bestMag = 0; int bestBin = 0;
        for (int k = 430; k <= 450; ++k) {
            double real = 0, imag = 0;
            for (int n = 0; n < N; ++n) {
                double ang = -2.0 * juce::MathConstants<double>::pi * k * n / N;
                real += buf[n] * std::cos(ang);
                imag += buf[n] * std::sin(ang);
            }
            double mag = std::sqrt(real * real + imag * imag);
            if (mag > bestMag) { bestMag = mag; bestBin = k; }
        }
        expect(std::abs(bestBin - 440) <= 1,
            juce::String("expected peak near 440, got ") + juce::String(bestBin));
    }

    void runSawHasHarmonicsTest() {
        beginTest("saw at 1kHz has measurable energy at 2kHz, 3kHz harmonics");
        Oscillator osc;
        osc.prepare(SR);
        osc.setWaveform(Oscillator::Waveform::Saw);
        osc.setFrequency(1000.0f);

        const int N = 48000;
        std::vector<float> buf(N);
        for (int i = 0; i < N; ++i) buf[i] = osc.processSample();

        auto magAt = [&](int k) {
            double real = 0, imag = 0;
            for (int n = 0; n < N; ++n) {
                double ang = -2.0 * juce::MathConstants<double>::pi * k * n / N;
                real += buf[n] * std::cos(ang);
                imag += buf[n] * std::sin(ang);
            }
            return std::sqrt(real * real + imag * imag);
        };

        double m1k = magAt(1000);
        double m2k = magAt(2000);
        double m3k = magAt(3000);

        // Saw harmonics fall as 1/n, so m2k ≈ m1k/2, m3k ≈ m1k/3.
        // Generous tolerance: each higher harmonic must be at least 1/5 the fundamental.
        expect(m1k > 0);
        expect(m2k > m1k * 0.2, "2kHz harmonic should be substantial");
        expect(m3k > m1k * 0.15, "3kHz harmonic should be substantial");
    }

    void runResetTest() {
        beginTest("reset returns phase to zero");
        Oscillator osc;
        osc.prepare(SR);
        osc.setWaveform(Oscillator::Waveform::Sine);
        osc.setFrequency(440.0f);
        for (int i = 0; i < 100; ++i) osc.processSample();

        osc.reset();
        // After reset, the first sample of a sine at 440Hz starting from phase 0
        // should be very close to 0.
        float s0 = osc.processSample();
        expectWithinAbsoluteError(s0, 0.0f, 1e-3f);
    }

    void runZeroFreqTest() {
        beginTest("zero frequency produces silence (or DC) without exploding");
        Oscillator osc;
        osc.prepare(SR);
        osc.setWaveform(Oscillator::Waveform::Saw);
        osc.setFrequency(0.0f);
        std::vector<float> buf(BLOCK);
        for (int i = 0; i < BLOCK; ++i) buf[i] = osc.processSample();
        // Whatever the output, it must be bounded.
        for (float v : buf) expect(std::abs(v) <= 1.5f, "output must be bounded");
    }

    void runBlockContinuityTest() {
        beginTest("two consecutive blocks produce continuous output (no glitch at boundary)");
        Oscillator osc;
        osc.prepare(SR);
        osc.setWaveform(Oscillator::Waveform::Sine);
        osc.setFrequency(440.0f);
        std::vector<float> a(BLOCK), b(BLOCK);
        osc.processBlock(a.data(), BLOCK);
        osc.processBlock(b.data(), BLOCK);
        // Sample at boundary: |b[0] - a[BLOCK-1]| should be small for a smooth sine
        float delta = std::abs(b[0] - a[BLOCK - 1]);
        // For 440Hz sine at 48kHz, one-sample delta is < ~0.06
        expectLessThan(delta, 0.1f);
    }
};

static OscillatorTest oscillatorTestInstance;
