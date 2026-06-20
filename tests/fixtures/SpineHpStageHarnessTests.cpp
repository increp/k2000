// SpineHpStageHarnessTests.cpp — gated fixtures for HuggettHpStage
//
// M10 HP corner: highs pass at fc=1000 Hz, 24 dB mode — f=8000 Hz output ≥ 0.7×.
// M10 slope: 24 dB cuts lows more steeply than 12 dB below corner.
//   Ported thresholds from HpPreFilterTests:
//     high pass:  gain(8 kHz, db12) ≥ 0.7
//     low cut:    gain(200 Hz, db12) < 0.1
//     24 steeper: gain(200 Hz, db24) < gain(200 Hz, db12)
//     24 strong:  gain(200 Hz, db24) < 0.04
// M11 resonant peak: at res=0.5 (linear, small-signal) the HP stage shows a resonant
//   peak just above fc; measured peak freq within ±2% of fc, height ≥ 1 dB (HP peak).
//   Analytic HP peak (2-pole HP 12 dB/oct): H_hp = u^2 / sqrt((1-u^2)^2 + k^2*u^2)
//   where u = f/fc.  At u > 1 the HP peak is above 0 dB when Q is high.
// M2: self-oscillation bounded ≤ 4.0 at max resonance (24 dB, fc=1200 Hz).
//
// Include paths: include root is tests/, component at ../../src/...

#include <juce_core/juce_core.h>
#include "testdsp/Gate.h"
#include "testdsp/Spectrum.h"
#include "../../src/dsp/spine/HuggettHpStage.h"
#include <cmath>
#include <memory>
#include <vector>

class SpineHpStageHarnessTests : public juce::UnitTest {
public:
    SpineHpStageHarnessTests() : juce::UnitTest("SpineHpStageHarness") {}

    static constexpr double kSR = 48000.0;

    // Peak magnitude response of HP stage at frequency f (unit-amplitude sine input).
    // Returns peak output over second half of N-sample window (settling discarded).
    // Matches HpPreFilterTests "mag" lambda: unit amp, peak in steady-state window.
    static float peakMag(HuggettHpStage& hp, HuggettHpStage::State& st, double f) {
        hp.reset(st);
        const int N = 8192;
        float peak = 0.0f;
        for (int i = 0; i < N; ++i) {
            const float x = (float) std::sin(
                2.0 * juce::MathConstants<double>::pi * f * i / kSR);
            float l = x, r = x;
            hp.processStereo(st, &l, &r, 1);
            if (i > N / 2) {
                peak = std::max(peak, std::abs(l));
            }
        }
        return peak;
    }

    // RMS-based magnitude response in dB — steadier than peak for resonant cases.
    static double magDb(HuggettHpStage& hp, HuggettHpStage::State& st,
                        double f, float amp = 0.05f) {
        hp.reset(st);
        const int warm = 8192, meas = 8192;
        double inSq = 0.0, outSq = 0.0;
        for (int i = 0; i < warm + meas; ++i) {
            const float x = amp * (float) std::sin(
                2.0 * juce::MathConstants<double>::pi * f * i / kSR);
            float l = x, r = x;
            hp.processStereo(st, &l, &r, 1);
            if (i >= warm) {
                inSq  += double(x) * x;
                outSq += double(l) * l;
            }
        }
        if (inSq <= 0.0 || outSq <= 0.0) { return -300.0; }
        return 20.0 * std::log10(std::sqrt(outSq / inSq));
    }

    void runTest() override {

        // ---- M10: HP corner — highs pass, lows cut (port of HpPreFilterTests) ----
        beginTest("SpineHpStage M10 HP corner: highs pass, lows cut (12 vs 24)");
        {
            HuggettHpStage hp;
            hp.prepare(kSR);
            std::unique_ptr<HuggettHpStage::State> st(hp.makeState());

            // High-frequency pass (12 dB) — same as HpPreFilterTests: unit amp, peak
            hp.setParams(1000.0f, 0.0f, HuggettHpStage::Slope::db12);
            const float highPass = peakMag(hp, *st, 8000.0);
            std::printf("[SpineHpStageHarness] M10 hp(8k,db12)=%.3f\n", (double) highPass);
            testdsp::Gate::check(*this, (double) highPass, 0.7,
                                 testdsp::Gate::Dir::Min, "M10 highs pass (db12)");

            // Low cut (12 dB) — unit amp
            hp.setParams(1000.0f, 0.0f, HuggettHpStage::Slope::db12);
            const float low12 = peakMag(hp, *st, 200.0);
            std::printf("[SpineHpStageHarness] M10 low12=%.3f\n", (double) low12);
            testdsp::Gate::check(*this, (double) low12, 0.1,
                                 testdsp::Gate::Dir::Max, "M10 lows cut (db12)");

            // Low cut (24 dB) — unit amp
            hp.setParams(1000.0f, 0.0f, HuggettHpStage::Slope::db24);
            const float low24 = peakMag(hp, *st, 200.0);
            std::printf("[SpineHpStageHarness] M10 low24=%.3f  (< low12=%.3f)\n",
                        (double) low24, (double) low12);
            testdsp::Gate::check(*this, double(low24 < low12), 0.5,
                                 testdsp::Gate::Dir::Min, "M10 24 dB steeper than 12 dB below corner");
            testdsp::Gate::check(*this, (double) low24, 0.04,
                                 testdsp::Gate::Dir::Max, "M10 24 dB strongly cuts lows");
        }

        // ---- M11: resonant peak at HP stage ----
        // At res=0.5, the HP stage (inner NlSvfCell) produces a resonant peak just
        // above fc.  We measure the peak in the 24 dB path (two series cells).
        // Expected: peak above fc, height > 0 dB (above flat HP response above fc).
        // Gate: peak freq within ±2% of fc, measured height ≥ 1 dB above pass-band level.
        beginTest("SpineHpStage M11 resonant peak above HP corner");
        {
            const double fc = 1000.0;
            HuggettHpStage hp;
            hp.prepare(kSR);
            std::unique_ptr<HuggettHpStage::State> st(hp.makeState());
            hp.setParams((float) fc, 0.5f, HuggettHpStage::Slope::db24);

            // Scan around fc to find peak.
            double bestDb = -300.0;
            double bestF  = fc;
            std::printf("[SpineHpStageHarness] M11 HP resonant scan (res=0.5, fc=%.0f):\n", fc);
            const int nScan = 25;
            for (int i = 0; i < nScan; ++i) {
                const double fScan = fc * 0.9 + i * fc * 0.5 / nScan;  // 900..1400 Hz
                const double m     = magDb(hp, *st, fScan, 0.005f);
                std::printf("  f=%.0f: %.2f dB\n", fScan, m);
                if (m > bestDb) { bestDb = m; bestF = fScan; }
            }
            // Also measure steady-state gain well above fc (pass-band reference)
            const double passRef = magDb(hp, *st, 8000.0, 0.1f);
            std::printf("[SpineHpStageHarness] M11 peak at f=%.1f  H=%.2f dB  pass_ref=%.2f dB\n",
                        bestF, bestDb, passRef);

            // Gate: peak freq within ±2% of fc
            const double freqErrPct = std::abs(bestF - fc) / fc * 100.0;
            testdsp::Gate::check(*this, freqErrPct, 2.0,
                                 testdsp::Gate::Dir::Max, "M11 HP peak freq +-2%");

            // Gate: peak height >= pass-band reference + 1 dB (proves a resonant bump)
            testdsp::Gate::check(*this, bestDb, passRef + 1.0,
                                 testdsp::Gate::Dir::Min, "M11 HP resonant bump >= passband + 1 dB");
        }

        // ---- M2: self-osc bounded ≤ 4.0 at max resonance (port of HpPreFilterTests) ----
        beginTest("SpineHpStage M2 self-osc bounded <= 4.0 (max res, db24)");
        {
            HuggettHpStage hp;
            hp.prepare(kSR);
            std::unique_ptr<HuggettHpStage::State> st(hp.makeState());
            hp.setParams(1200.0f, 1.0f, HuggettHpStage::Slope::db24);
            hp.reset(*st);

            // kick
            float kl = 1.0f, kr = 1.0f;
            hp.processStereo(*st, &kl, &kr, 1);

            float peak = 0.0f;
            bool  nan  = false;
            for (int i = 0; i < 24000; ++i) {
                float a = 0.0f, b = 0.0f;
                hp.processStereo(*st, &a, &b, 1);
                if (! std::isfinite(a)) { nan = true; }
                if (i > 2000) {
                    peak = std::max(peak, std::abs(a));
                }
            }
            std::printf("[SpineHpStageHarness] M2 peak=%.4f  nan=%d\n", (double) peak, (int) nan);
            expect(! nan, "M2 no NaN/Inf");
            testdsp::Gate::check(*this, (double) peak, 4.0,
                                 testdsp::Gate::Dir::Max, "M2 HP self-osc bounded");
        }
    }
};
static SpineHpStageHarnessTests spineHpStageHarnessTestsInstance;
