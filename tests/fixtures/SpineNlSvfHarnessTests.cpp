// SpineNlSvfHarnessTests.cpp — gated fixtures for NlSvfCell
//
// M1  : output finite after impulse + silence.
// M2  : self-osc amplitude bounded ≤ 4.0 at max resonance.
// M9  : zipper — hold tone while ramping cutoff per block; NSR must not exceed
//        the quiet-baseline inharmonic floor.
// M10 : LP/HP/BP passband response vs Cytomic analytic prototype ≤ 0.5 dB
//        (f ≤ fc for LP, f ≥ fc for HP, near-fc for BP — well below Nyquist).
//        BP analytic: |H_bp(u)| = u / sqrt((1-u²)² + k²u²)  [k = 1/Q, u = f/fc].
// M11 : resonant peak: measured freq within ±2 %, height within ±1.5 dB of
//        analytic peak (linear, small-signal).  Analytic: H_lp_peak = 1/(k*sqrt(1-k²/4)).
//        Measured baseline at res=0.5: f≈998.5 Hz, H≈22.20 dB (see comment).
// M12 : self-osc pitch within ±0.75 % of cutoff (BP output, zero-crossing estimator)
//        across cutoffs {200, 1000, 5000} Hz and sample rates {44100, 48000, 96000}.
//        Gate ±0.75 % (≈13 cents), user decision 2026-06-20: worst case is 5000 Hz at
//        44100/48000 Hz (≈-0.60 %, inherent gray-box delayed-feedback pitch shift).
// M13 : low-level near-linear: at tiny amplitude (0.001) with resSat=0 maxDiff vs
//        analytic LP amplitude is negligible.
// M15 : denormal flush — impulse kick + 1 s silence → tail energy ≤ −300 dB.
//
// Include paths: include root is tests/, component headers at ../../src/...

#include <juce_core/juce_core.h>
#include "testdsp/SignalGen.h"
#include "testdsp/Spectrum.h"
#include "testdsp/Metrics.h"
#include "testdsp/Gate.h"
#include "testdsp/Response.h"
#include "testdsp/ProcessAdapter.h"
#include "../../src/dsp/spine/NlSvfCell.h"
#include <cmath>
#include <vector>
#include <memory>

class SpineNlSvfHarnessTests : public juce::UnitTest {
public:
    SpineNlSvfHarnessTests() : juce::UnitTest("SpineNlSvfHarness") {}

    static constexpr double kSR = 48000.0;

    // Kick + run impulse-response helper.
    // Returns maxAbs of the settling window [skipSamples, skipSamples + collectSamples).
    static float kickAndMeasure(float cutoff, float res, float resSat,
                                double sr, int skipSamples, int collectSamples) {
        NlSvfCell c;
        c.prepare(sr);
        c.setCutoff(cutoff);
        c.setResonance(res);
        c.setResSat(resSat);
        c.reset();
        float l = 1.0f, r = 1.0f;
        c.process(l, r, NlSvfCell::LP);
        float peak = 0.0f;
        bool nan = false;
        for (int i = 0; i < skipSamples + collectSamples; ++i) {
            float a = 0.0f, b = 0.0f;
            c.process(a, b, NlSvfCell::LP);
            if (! std::isfinite(a)) {
                nan = true;
            }
            if (i >= skipSamples) {
                peak = std::max(peak, std::abs(a));
            }
        }
        return nan ? std::numeric_limits<float>::infinity() : peak;
    }

    // Steady-state magnitude response via RMS in/out.
    // Returns dB.  Uses tap = NlSvfCell::LP/HP/BP.
    static double magDbAtTap(float cutoff, float res, float resSat,
                             double sr, double f, int tap, float amp = 0.05f) {
        NlSvfCell c;
        c.prepare(sr);
        c.setCutoff(cutoff);
        c.setResonance(res);
        c.setResSat(resSat);
        c.reset();

        const int warm = 8192, meas = 8192;
        double inSq = 0.0, outSq = 0.0;
        for (int i = 0; i < warm + meas; ++i) {
            const float x = amp * (float) std::sin(
                2.0 * juce::MathConstants<double>::pi * f * i / sr);
            float l = x, r = x;
            c.process(l, r, tap);
            if (i >= warm) {
                inSq  += double(x) * x;
                outSq += double(l) * l;
            }
        }
        if (inSq <= 0.0 || outSq <= 0.0) { return -300.0; }
        return 20.0 * std::log10(std::sqrt(outSq / inSq));
    }

    // Analytic 2-pole LP |H|: -10*log10((1-u^2)^2 + k^2*u^2),  u = f/fc.
    static double analyticLPdB(double f, double fc, double k) {
        const double u  = f / fc;
        const double u2 = u * u;
        return -10.0 * std::log10((1.0 - u2) * (1.0 - u2) + k * k * u2);
    }
    // Analytic HP: add 20*log10(u^2) to LP.
    static double analyticHPdB(double f, double fc, double k) {
        const double u  = f / fc;
        const double u2 = u * u;
        return -10.0 * std::log10((1.0 - u2) * (1.0 - u2) + k * k * u2)
               + 20.0 * std::log10(u2);
    }
    // Analytic BP: H_bp(u) = u / sqrt((1-u^2)^2 + k^2*u^2).
    static double analyticBPdB(double f, double fc, double k) {
        const double u  = f / fc;
        const double u2 = u * u;
        return 10.0 * std::log10(u2) - 10.0 * std::log10((1.0 - u2) * (1.0 - u2) + k * k * u2);
    }

    // Q from NlSvfCell's formula: Q = 0.5 + res^2 * 49.5
    static double cellQ(float res) {
        return 0.5 + double(res) * double(res) * 49.5;
    }
    static double cellK(float res) { return 1.0 / cellQ(res); }

    // Zero-crossing pitch estimator — positive zero crossings → average period → Hz.
    // Skips first `skipSamples` before counting.
    static double zeroCrossingHz(const std::vector<float>& sig, double sr) {
        std::vector<double> crossings;
        for (int i = 1; i < (int) sig.size(); ++i) {
            if (sig[(size_t)(i-1)] <= 0.0f && sig[(size_t) i] > 0.0f) {
                const double frac =
                    -double(sig[(size_t)(i-1)]) / double(sig[(size_t) i] - sig[(size_t)(i-1)]);
                crossings.push_back(double(i-1) + frac);
            }
        }
        if ((int) crossings.size() < 4) { return 0.0; }
        const int nc = (int) crossings.size();
        double sumP = 0.0;
        int cnt = 0;
        for (int i = 1; i < nc; ++i) {
            sumP += crossings[(size_t) i] - crossings[(size_t)(i-1)];
            ++cnt;
        }
        return cnt > 0 ? sr / (sumP / cnt) : 0.0;
    }

    // Collect BP self-osc after a kick, discarding first discSamples.
    static std::vector<float> collectBPSelfOsc(float cutoff, float res, float resSat,
                                                double sr, int discSamples, int collectSamples) {
        NlSvfCell c;
        c.prepare(sr);
        c.setCutoff(cutoff);
        c.setResonance(res);
        c.setResSat(resSat);
        c.reset();
        // kick
        float kl = 1.0f, kr = 1.0f;
        c.process(kl, kr, NlSvfCell::BP);
        for (int i = 0; i < discSamples; ++i) {
            float a = 0.0f, b = 0.0f;
            c.process(a, b, NlSvfCell::BP);
        }
        std::vector<float> sig((size_t) collectSamples);
        for (int i = 0; i < collectSamples; ++i) {
            float a = 0.0f, b = 0.0f;
            c.process(a, b, NlSvfCell::BP);
            sig[(size_t) i] = a;
        }
        return sig;
    }

    void runTest() override {

        // ---- M1: finiteness after impulse + silence ----
        beginTest("SpineNlSvf M1 finite (max res, 0.5 s)");
        {
            const float peak = kickAndMeasure(800.0f, 1.0f, 1.0f, kSR, 2000, 24000);
            expect(std::isfinite(peak), "M1 output must be finite");
        }

        // ---- M2: bounded self-osc (amplitude ≤ 4.0) ----
        beginTest("SpineNlSvf M2 self-osc bounded <= 4.0");
        {
            const float peak = kickAndMeasure(800.0f, 1.0f, 1.0f, kSR, 2000, 22000);
            std::printf("[SpineNlSvfHarness] M2 peak=%.4f\n", peak);
            testdsp::Gate::check(*this, (double) peak, 4.0,
                                 testdsp::Gate::Dir::Max, "M2 bounded self-osc");
        }

        // ---- M10: LP passband response vs Cytomic analytic (≤ 0.5 dB) ----
        // res=0 → Q=0.5, k=2.0.  Test f ≤ fc (passband, well below Nyquist).
        // TPT≈analog for f ≤ fc; tolerance 0.5 dB.
        beginTest("SpineNlSvf M10 LP passband vs analytic (res=0, fc=1000 Hz, f<=fc)");
        {
            const double fc     = 1000.0;
            const double res    = 0.0;
            const double k      = cellK((float) res);
            // LP test: below cutoff
            const double freqs[] = { 100.0, 300.0, 500.0, 700.0, 1000.0 };
            std::printf("[SpineNlSvfHarness] M10 LP (fc=1000, k=%.3f):\n", k);
            for (double f : freqs) {
                const double meas = magDbAtTap((float) fc, (float) res, 0.0f, kSR, f, NlSvfCell::LP);
                const double an   = analyticLPdB(f, fc, k);
                const double err  = std::abs(meas - an);
                std::printf("  f=%.0f: measured=%.2f  analytic=%.2f  err=%.3f dB\n",
                            f, meas, an, err);
                testdsp::Gate::check(*this, err, 0.5,
                                     testdsp::Gate::Dir::Max,
                                     "M10 LP f=" + juce::String((int) f));
            }
        }

        beginTest("SpineNlSvf M10 HP passband vs analytic (res=0, fc=1000 Hz, f>=fc)");
        {
            const double fc     = 1000.0;
            const double res    = 0.0;
            const double k      = cellK((float) res);
            const double freqs[] = { 1000.0, 2000.0, 4000.0 };
            std::printf("[SpineNlSvfHarness] M10 HP (fc=1000, k=%.3f):\n", k);
            for (double f : freqs) {
                const double meas = magDbAtTap((float) fc, (float) res, 0.0f, kSR, f, NlSvfCell::HP);
                const double an   = analyticHPdB(f, fc, k);
                const double err  = std::abs(meas - an);
                std::printf("  f=%.0f: measured=%.2f  analytic=%.2f  err=%.3f dB\n",
                            f, meas, an, err);
                testdsp::Gate::check(*this, err, 0.5,
                                     testdsp::Gate::Dir::Max,
                                     "M10 HP f=" + juce::String((int) f));
            }
        }

        beginTest("SpineNlSvf M10 BP passband vs analytic (res=0, fc=1000 Hz)");
        {
            const double fc     = 1000.0;
            const double res    = 0.0;
            const double k      = cellK((float) res);
            // BP analytic: H_bp = u / sqrt(...).  At u<0.5 and u>2 the BP drops steeply;
            // test near fc where TPT≈analog.
            const double freqs[] = { 500.0, 700.0, 1000.0, 1500.0, 2000.0 };
            std::printf("[SpineNlSvfHarness] M10 BP (fc=1000, k=%.3f):\n", k);
            for (double f : freqs) {
                const double meas = magDbAtTap((float) fc, (float) res, 0.0f, kSR, f, NlSvfCell::BP);
                const double an   = analyticBPdB(f, fc, k);
                const double err  = std::abs(meas - an);
                std::printf("  f=%.0f: measured=%.2f  analytic=%.2f  err=%.3f dB\n",
                            f, meas, an, err);
                testdsp::Gate::check(*this, err, 0.5,
                                     testdsp::Gate::Dir::Max,
                                     "M10 BP f=" + juce::String((int) f));
            }
        }

        // ---- M11: resonant peak frequency ±2%, height ±1.5 dB vs analytic ----
        // res=0.5 (small signal, resSat=0 → linear): Q = 0.5 + 0.25*49.5 = 12.875, k = 1/Q.
        // Analytic LP peak at u_peak = sqrt(1 - k^2/2), H_peak = 1 / (k*sqrt(1 - k^2/4)).
        // Baseline measured 2026-06-20: f_peak ≈ 998.5 Hz, H_peak ≈ 22.20 dB.
        // We gate against analytic; if they diverge by > 1.5 dB the gate reports it.
        beginTest("SpineNlSvf M11 resonant peak freq +-2%, height +-1.5 dB");
        {
            const double fc   = 1000.0;
            const double res  = 0.5;
            const double k    = cellK((float) res);
            const double Q    = cellQ((float) res);
            // Analytic peak
            const double uPeak   = std::sqrt(std::max(0.0, 1.0 - k * k / 2.0));
            const double fPeak   = fc * uPeak;   // ≈ 998.5 Hz
            const double hPeak   = 1.0 / (k * std::sqrt(std::max(1e-12, 1.0 - k * k / 4.0)));
            const double hPeakDb = 20.0 * std::log10(hPeak);   // ≈ 22.20 dB
            std::printf("[SpineNlSvfHarness] M11 analytic: k=%.4f Q=%.2f f_peak=%.2f H_peak=%.2f dB\n",
                        k, Q, fPeak, hPeakDb);

            // Scan a fine grid around the analytic peak to find measured peak.
            double bestMeas = -300.0;
            double bestF    = fPeak;
            const int nScan = 30;
            for (int i = 0; i < nScan; ++i) {
                const double fScan = fc * 0.85 + i * fc * 0.3 / nScan;  // 850..1150 Hz
                const double meas  = magDbAtTap((float) fc, (float) res, 0.0f, kSR, fScan,
                                                NlSvfCell::LP, 0.005f);
                if (meas > bestMeas) { bestMeas = meas; bestF = fScan; }
            }
            std::printf("[SpineNlSvfHarness] M11 measured: f_peak=%.2f H_peak=%.2f dB\n",
                        bestF, bestMeas);

            const double freqErrPct = std::abs(bestF - fPeak) / fPeak * 100.0;
            const double heightErr  = std::abs(bestMeas - hPeakDb);

            std::printf("[SpineNlSvfHarness] M11 freq_err=%.3f%%  height_err=%.3f dB\n",
                        freqErrPct, heightErr);

            testdsp::Gate::check(*this, freqErrPct, 2.0,
                                 testdsp::Gate::Dir::Max, "M11 peak freq +-2%");
            testdsp::Gate::check(*this, heightErr, 1.5,
                                 testdsp::Gate::Dir::Max, "M11 peak height +-1.5 dB");
        }

        // ---- M12: self-osc pitch within ±0.75% of cutoff ----
        // Gate: ±0.75% (≈13 cents), user decision 2026-06-20. Worst measured case is
        // fc=5000 Hz at 44100/48000 Hz (≈-0.60% / ≈-0.56%, inherent to the gray-box
        // one-sample-delayed resonance feedback at high fc/fs) — now within the gate.
        // Measurement: BP output, zero-crossing estimator, discard 0.1 s then collect 0.5 s.
        {
            const float  cutoffs[] = { 200.0f, 1000.0f, 5000.0f };
            const double srs[]     = { 44100.0, 48000.0, 96000.0 };
            std::printf("[SpineNlSvfHarness] M12 self-osc pitch:\n");
            std::printf("  %-8s %-8s %10s %10s %12s\n",
                        "cutoff", "sr", "measHz", "errPct", "errCents");
            for (float fc : cutoffs) {
                for (double sr : srs) {
                    beginTest("SpineNlSvf M12 self-osc pitch +-0.75% @ fc=" +
                              juce::String((int) fc) + " sr=" + juce::String((int) sr));
                    const int discSamp    = (int) (sr * 0.1);
                    const int collectSamp = (int) (sr * 0.5);
                    const auto sig = collectBPSelfOsc(fc, 1.0f, 1.0f, sr,
                                                      discSamp, collectSamp);
                    const double measHz = zeroCrossingHz(sig, sr);
                    const double errPct = (measHz - double(fc)) / double(fc) * 100.0;
                    const double errAbs = std::abs(errPct);
                    double errCents = 0.0;
                    if (measHz > 0.0) {
                        errCents = 1200.0 * std::log2(measHz / double(fc));
                    }
                    std::printf("  %-8.0f %-8.0f %10.3f %10.4f %12.3f\n",
                                (double) fc, sr, measHz, errPct, errCents);
                    testdsp::Gate::check(*this, errAbs, 0.75,
                                         testdsp::Gate::Dir::Max,
                                         "M12 pitch err%");
                }
            }
        }

        // ---- M13: low-level ≈ linear (small-signal, resSat=0, compare to analytic) ----
        // At tiny amplitude (0.001), with resSat=0, LP output should match the linear
        // SVF analytic gain within 0.1 dB at a frequency well in the passband.
        beginTest("SpineNlSvf M13 low-level linear (amp=0.001, f=200 Hz, fc=1000)");
        {
            const double fc  = 1000.0;
            const double f   = 200.0;
            const double res = 0.0;
            const double k   = cellK((float) res);
            const double meas = magDbAtTap((float) fc, (float) res, 0.0f, kSR, f,
                                          NlSvfCell::LP, 0.001f);
            const double an   = analyticLPdB(f, fc, k);
            const double err  = std::abs(meas - an);
            std::printf("[SpineNlSvfHarness] M13 measured=%.3f analytic=%.3f err=%.4f dB\n",
                        meas, an, err);
            testdsp::Gate::check(*this, err, 0.1,
                                 testdsp::Gate::Dir::Max, "M13 low-level linear");
        }

        // ---- M15: denormal flush — 1 s silence after kick, tail energy ≤ -300 dB ----
        beginTest("SpineNlSvf M15 denormal flush (tail ≤ -300 dB after 1 s silence)");
        {
            NlSvfCell c;
            c.prepare(kSR);
            c.setCutoff(800.0f);
            c.setResonance(0.3f);   // just below self-osc to ensure fast decay
            c.setResSat(0.3f);
            c.reset();
            // kick
            float kl = 1.0f, kr = 1.0f;
            c.process(kl, kr, NlSvfCell::LP);
            // run 1 s of silence
            const int totalSamples = (int) kSR;   // 1 s
            // measure tail energy in the last 0.5 s
            const int tailStart = totalSamples / 2;
            double tailSq = 0.0;
            for (int i = 0; i < totalSamples; ++i) {
                float a = 0.0f, b = 0.0f;
                c.process(a, b, NlSvfCell::LP);
                if (i >= tailStart) {
                    tailSq += double(a) * a;
                }
            }
            // Normalise to per-sample energy, convert to dB
            const int tailCount = totalSamples - tailStart;
            const double tailEnergyDb = (tailSq > 0.0)
                ? 10.0 * std::log10(tailSq / tailCount)
                : -400.0;
            std::printf("[SpineNlSvfHarness] M15 tail energy=%.1f dB\n", tailEnergyDb);
            testdsp::Gate::check(*this, tailEnergyDb, -300.0,
                                 testdsp::Gate::Dir::Max, "M15 denormal flush");
        }

        // ---- M9: zipper — ramp cutoff per block while playing a tone ----
        // Hold a 200 Hz sine; ramp cutoff from 400 Hz to 4000 Hz over 2 s in 128-sample blocks.
        // NSR (inharmonic energy vs fundamental) must not exceed a quiet-baseline floor
        // established by the SAME tone through a STATIC cutoff.  Gate: NSR_ramp <= NSR_static + 3 dB.
        beginTest("SpineNlSvf M9 zipper NSR <= static baseline + 3 dB");
        {
            const double   f0        = 200.0;
            const int      N         = 1 << 14;   // 16384 samples
            const int      blockSize = 128;

            // Baseline: static cutoff at midpoint of ramp (1200 Hz)
            NlSvfCell cStat;
            cStat.prepare(kSR);
            cStat.setCutoff(1200.0f);
            cStat.setResonance(0.0f);
            cStat.setResSat(0.0f);
            cStat.reset();

            std::vector<float> staticOut((size_t) N);
            for (int i = 0; i < N; ++i) {
                const float x = 0.5f * (float) std::sin(
                    2.0 * juce::MathConstants<double>::pi * f0 * i / kSR);
                float l = x, r = x;
                cStat.process(l, r, NlSvfCell::LP);
                staticOut[(size_t) i] = l;
            }

            // Ramp: cutoff moves from 400 to 4000 Hz, updated once per block.
            NlSvfCell cRamp;
            cRamp.prepare(kSR);
            cRamp.setResonance(0.0f);
            cRamp.setResSat(0.0f);
            cRamp.setCutoff(400.0f);
            cRamp.reset();

            std::vector<float> rampOut((size_t) N);
            {
                int pos = 0;
                const int numBlocks = N / blockSize;
                for (int b = 0; b < numBlocks; ++b) {
                    const float t        = (float) b / (float) numBlocks;
                    const float cutBlock = 400.0f + t * (4000.0f - 400.0f);
                    cRamp.setCutoff(cutBlock);
                    for (int j = 0; j < blockSize; ++j) {
                        const float x = 0.5f * (float) std::sin(
                            2.0 * juce::MathConstants<double>::pi * f0 * pos / kSR);
                        float l = x, r = x;
                        cRamp.process(l, r, NlSvfCell::LP);
                        rampOut[(size_t) pos] = l;
                        ++pos;
                    }
                }
            }

            // FFT magnitude spectrum for both.
            const auto magStat = testdsp::Spectrum::magnitude(staticOut);
            const auto magRamp = testdsp::Spectrum::magnitude(rampOut);

            // Fundamental bin for 200 Hz in a 16384-sample window at 48000 Hz.
            // bin = round(f0 / (sr/N)) = round(200 / (48000/16384)) ≈ 68.
            const int fundBin = (int) std::round(f0 / (kSR / N));

            const double nsrStat = testdsp::Metrics::inharmonicDb(magStat, fundBin);
            const double nsrRamp = testdsp::Metrics::inharmonicDb(magRamp, fundBin);

            std::printf("[SpineNlSvfHarness] M9 fundBin=%d  NSR_static=%.2f dB  NSR_ramp=%.2f dB\n",
                        fundBin, nsrStat, nsrRamp);

            // Gate: ramp NSR must not exceed static NSR + 3 dB headroom.
            testdsp::Gate::check(*this, nsrRamp, nsrStat + 3.0,
                                 testdsp::Gate::Dir::Max, "M9 zipper NSR");
        }
    }
};
static SpineNlSvfHarnessTests spineNlSvfHarnessTestsInstance;
