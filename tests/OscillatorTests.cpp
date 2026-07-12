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
        runBlendRatioTest();
        runZeroSumSilenceTest();
        runPulseDutyEdgeTest();
        runTriangleIndependentOfPulseDutyTest();
    }

private:
    static double rms(const std::vector<float>& buf) {
        double s = 0;
        for (float v : buf) s += double(v) * v;
        return std::sqrt(s / buf.size());
    }

    static double magAtFreq(const std::vector<float>& buf, int k) {
        const int N = (int) buf.size();
        double real = 0, imag = 0;
        for (int n = 0; n < N; ++n) {
            double ang = -2.0 * juce::MathConstants<double>::pi * k * n / N;
            real += buf[n] * std::cos(ang);
            imag += buf[n] * std::sin(ang);
        }
        return std::sqrt(real * real + imag * imag);
    }

    void runSineFundamentalTest() {
        beginTest("sine at 440Hz has FFT peak near bin 440 (1Hz res, 48k samples)");
        Oscillator osc;
        osc.prepare(SR);
        osc.setBlend(1.0f, 0.0f, 0.0f, 0.0f);
        osc.setFrequency(440.0f);

        const int N = 48000; // 1 Hz bin resolution
        std::vector<float> buf(N);
        for (int i = 0; i < N; ++i) buf[i] = osc.processSample();

        double bestMag = 0; int bestBin = 0;
        for (int k = 430; k <= 450; ++k) {
            double mag = magAtFreq(buf, k);
            if (mag > bestMag) { bestMag = mag; bestBin = k; }
        }
        expect(std::abs(bestBin - 440) <= 1,
            juce::String("expected peak near 440, got ") + juce::String(bestBin));
    }

    void runSawHasHarmonicsTest() {
        beginTest("saw at 1kHz has measurable energy at 2kHz, 3kHz harmonics");
        Oscillator osc;
        osc.prepare(SR);
        osc.setBlend(0.0f, 0.0f, 1.0f, 0.0f);
        osc.setFrequency(1000.0f);

        const int N = 48000;
        std::vector<float> buf(N);
        for (int i = 0; i < N; ++i) buf[i] = osc.processSample();

        double m1k = magAtFreq(buf, 1000);
        double m2k = magAtFreq(buf, 2000);
        double m3k = magAtFreq(buf, 3000);

        // Saw harmonics fall as 1/n, so m2k ≈ m1k/2, m3k ≈ m1k/3.
        expect(m1k > 0);
        expect(m2k > m1k * 0.2, "2kHz harmonic should be substantial");
        expect(m3k > m1k * 0.15, "3kHz harmonic should be substantial");
    }

    void runResetTest() {
        beginTest("reset returns phase to zero");
        Oscillator osc;
        osc.prepare(SR);
        osc.setBlend(1.0f, 0.0f, 0.0f, 0.0f);
        osc.setFrequency(440.0f);
        for (int i = 0; i < 100; ++i) osc.processSample();

        osc.reset();
        float s0 = osc.processSample();
        expectWithinAbsoluteError(s0, 0.0f, 1e-3f);
    }

    void runZeroFreqTest() {
        beginTest("zero frequency produces silence (or DC) without exploding");
        Oscillator osc;
        osc.prepare(SR);
        osc.setBlend(0.0f, 0.0f, 1.0f, 0.0f);
        osc.setFrequency(0.0f);
        std::vector<float> buf(BLOCK);
        for (int i = 0; i < BLOCK; ++i) buf[i] = osc.processSample();
        for (float v : buf) expect(std::abs(v) <= 1.5f, "output must be bounded");
    }

    void runBlockContinuityTest() {
        beginTest("two consecutive blocks produce continuous output (no glitch at boundary)");
        Oscillator osc;
        osc.prepare(SR);
        osc.setBlend(1.0f, 0.0f, 0.0f, 0.0f);
        osc.setFrequency(440.0f);
        std::vector<float> a(BLOCK), b(BLOCK);
        osc.processBlock(a.data(), BLOCK);
        osc.processBlock(b.data(), BLOCK);
        float delta = std::abs(b[0] - a[BLOCK - 1]);
        expectLessThan(delta, 0.1f);
    }

    void runBlendRatioTest() {
        beginTest("blend is proportional: (0.05, 0.19, 0, 0) sounds like a 5:19 sine:tri ratio, not quiet");
        // Two oscillators with the SAME ratio but different absolute weights
        // must produce the SAME waveform shape (same peak amplitude, same
        // sample-by-sample values after accounting for phase), because the
        // math divides by the weight total.
        Oscillator a, b;
        a.prepare(SR); b.prepare(SR);
        a.setBlend(0.05f, 0.19f, 0.0f, 0.0f);   // literal small weights, 5:19 ratio
        b.setBlend(0.5f, 1.9f, 0.0f, 0.0f);     // same ratio, 10x the absolute weights
        a.setFrequency(220.0f); b.setFrequency(220.0f);

        std::vector<float> bufA(BLOCK), bufB(BLOCK);
        a.processBlock(bufA.data(), BLOCK);
        b.processBlock(bufB.data(), BLOCK);
        for (int i = 0; i < BLOCK; ++i)
            expectWithinAbsoluteError(bufA[i], bufB[i], 1e-5f);

        // And a single full-weight sine (0,0,0,0 total contribution from tri/saw/pulse)
        // must reach a peak close to 1.0 -- proportional blending should not
        // silently attenuate a single-shape blend versus today's single-oscillator peak.
        Oscillator pureSine;
        pureSine.prepare(SR);
        pureSine.setBlend(1.0f, 0.0f, 0.0f, 0.0f);
        pureSine.setFrequency(220.0f);
        float peak = 0.0f;
        for (int i = 0; i < BLOCK; ++i) peak = std::max(peak, std::abs(pureSine.processSample()));
        expect(peak > 0.9f, "single full-weight sine should reach close to unity peak");
    }

    void runZeroSumSilenceTest() {
        beginTest("all four weights at zero produces silence, not NaN/inf");
        Oscillator osc;
        osc.prepare(SR);
        osc.setBlend(0.0f, 0.0f, 0.0f, 0.0f);
        osc.setFrequency(440.0f);
        std::vector<float> buf(BLOCK);
        osc.processBlock(buf.data(), BLOCK);
        for (float v : buf) {
            expect(std::isfinite(v), "zero-sum blend must not produce NaN/inf");
            expectWithinAbsoluteError(v, 0.0f, 1e-9f);
        }
    }

    void runPulseDutyEdgeTest() {
        beginTest("pulse duty cycle places the falling edge at the requested fraction of the period");
        Oscillator osc;
        osc.prepare(SR);
        osc.setBlend(0.0f, 0.0f, 0.0f, 1.0f);  // pure pulse
        osc.setPulseDuty(0.25f);
        osc.setFrequency(100.0f);              // 480 samples/cycle at 48kHz -- coarse enough to see the edge clearly

        const int periodSamples = (int) std::round(SR / 100.0);
        std::vector<float> buf(periodSamples);
        for (int i = 0; i < periodSamples; ++i) buf[i] = osc.processSample();

        // High for roughly the first 25% of the period, low for the rest
        // (polyBLEP softens a couple of samples right at each edge, so check
        // well clear of both transitions).
        const int highRegionEnd = (int) (periodSamples * 0.25 * 0.8);      // well before the falling edge
        const int lowRegionStart = (int) (periodSamples * 0.25 * 1.2);     // well after the falling edge
        for (int i = 2; i < highRegionEnd; ++i)
            expect(buf[i] > 0.8f, "expected high region within first ~25% of the period");
        for (int i = lowRegionStart; i < periodSamples - 2; ++i)
            expect(buf[i] < -0.8f, "expected low region after the 25% duty point");
    }

    void runTriangleIndependentOfPulseDutyTest() {
        beginTest("triangle output is unaffected by pulseDuty (always derives from its own fixed 50% square)");
        Oscillator a, b;
        a.prepare(SR); b.prepare(SR);
        a.setBlend(0.0f, 1.0f, 0.0f, 0.0f);
        b.setBlend(0.0f, 1.0f, 0.0f, 0.0f);
        a.setPulseDuty(0.5f);
        b.setPulseDuty(0.1f);   // very different duty -- triangle must not care
        a.setFrequency(220.0f); b.setFrequency(220.0f);

        std::vector<float> bufA(BLOCK), bufB(BLOCK);
        a.processBlock(bufA.data(), BLOCK);
        b.processBlock(bufB.data(), BLOCK);
        for (int i = 0; i < BLOCK; ++i)
            expectWithinAbsoluteError(bufA[i], bufB[i], 1e-6f);
    }
};

static OscillatorTest oscillatorTestInstance;
