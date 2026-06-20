#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>
#include "../src/dsp/spine/AsymSaturator.h"
#include "../src/dsp/spine/NlSvfCell.h"
#include "../src/dsp/spine/HuggettFilter.h"
#include "../src/dsp/spine/HuggettHpStage.h"
#include <cmath>
#include <cstdio>
#include <vector>
#include <memory>

// Overdrive / nonlinear-path DIAGNOSTICS — the seed of a per-component test
// platform. This is a measurement harness, not a pass/fail gate: it prints
// objective artifact metrics so we can see WHERE crackles and aliasing come from
// instead of guessing by ear. It only asserts finiteness; the numbers go to stdout.
//
// Metric: feed a bin-aligned pure sine, FFT the output, and compare energy at the
// harmonic bins (k*f0) against energy at every other bin. For a clean nonlinearity
// all distortion lands on harmonics; aliasing folds onto inharmonic bins, and a
// per-block coefficient "click" adds energy at the block rate (also inharmonic).
// So "inharmonic energy below the fundamental" is a single artifact score in dB.
class OverdriveDiagnosticTests : public juce::UnitTest {
public:
    OverdriveDiagnosticTests() : juce::UnitTest("OverdriveDiagnostic") {}

    static constexpr int    kN       = 1 << 14;  // 16384-pt FFT
    static constexpr int    kWarm    = 4096;     // discarded settling samples (filters have memory)
    static constexpr double kSR      = 48000.0;
    static constexpr int    kFundBin = 75;       // f0 = 75 * 48000/16384 ≈ 219.7 Hz

    static double pi() { return juce::MathConstants<double>::pi; }

    static std::vector<float> magSpectrum(const std::vector<float>& x) {
        juce::dsp::FFT fft(14);
        std::vector<float> buf((size_t) (2 * kN), 0.0f);
        for (int i = 0; i < kN; ++i) buf[(size_t) i] = x[(size_t) i];
        fft.performRealOnlyForwardTransform(buf.data());
        std::vector<float> mag((size_t) (kN / 2));
        for (int b = 0; b < kN / 2; ++b) {
            const float re = buf[(size_t) (2 * b)], im = buf[(size_t) (2 * b + 1)];
            mag[(size_t) b] = std::sqrt(re * re + im * im);
        }
        return mag;
    }

    // 10*log10( sum(inharmonic bin energy) / fundamental bin energy ). Lower = cleaner.
    static double inharmonicDb(const std::vector<float>& mag, int spacing) {
        double fund = 0.0, inh = 0.0;
        for (int b = 2; b < (int) mag.size(); ++b) {     // skip DC + bin 1 (DC-blocker residue)
            const double e = double(mag[(size_t) b]) * mag[(size_t) b];
            if (b % spacing == 0) { if (b == spacing) fund = e; }
            else inh += e;
        }
        return (fund > 0.0) ? 10.0 * std::log10(inh / fund) : 0.0;
    }

    static float maxAbs(const std::vector<float>& v) {
        float m = 0.0f; for (float x : v) m = std::max(m, std::abs(x)); return m;
    }
    static bool allFinite(const std::vector<float>& v) {
        for (float x : v) if (! std::isfinite(x)) return false; return true;
    }

    // Frequency is fixed at kFundBin cycles per kN samples, so any kN-length slice
    // is bin-aligned regardless of len (lets us discard a warm-up window).
    static std::vector<float> sine(float amp, int len) {
        std::vector<float> v((size_t) len);
        for (int i = 0; i < len; ++i)
            v[(size_t) i] = amp * (float) std::sin(2.0 * pi() * kFundBin * i / kN);
        return v;
    }

    // ---- AsymSaturator (plain tanh) vs an inline reference (should be identical) ----
    static std::vector<float> driveShaper(float amp, float drive, float bias, float driveDb) {
        AsymSaturator sat; sat.setDrive(drive, bias, driveDb);
        auto in = sine(amp, kN); std::vector<float> out((size_t) kN);
        for (int i = 0; i < kN; ++i) out[(size_t) i] = sat.process(in[(size_t) i]);
        return out;
    }
    static std::vector<float> driveNaive(float amp, float drive, float bias, float driveDb) {
        const float gain = std::pow(10.0f, (drive * driveDb) / 20.0f);
        const float full = (gain > 1.0f) ? (1.0f / std::tanh(gain)) : 1.0f;
        const float comp = 1.0f + 0.75f * (full - 1.0f);
        auto in = sine(amp, kN); std::vector<float> out((size_t) kN);
        for (int i = 0; i < kN; ++i) out[(size_t) i] = comp * std::tanh(gain * in[(size_t) i] + bias);
        return out;
    }

    // ---- Full HuggettFilter, post-drive only (res=0, pre=0), processed in blocks ----
    static std::vector<float> filterPostDrive(float amp, float postDrive, int blockSize) {
        HuggettFilter h; h.prepare(kSR); h.setMode(HuggettFilter::Mode::LP);
        h.setSlope(HuggettFilter::Slope::db24); h.setSeparation(0.0f);
        h.setCommon(1200.0f, 0.0f, 0.0f);     // resonance 0, pre-drive 0
        h.setPostDrive(postDrive);
        std::unique_ptr<FilterModel::State> st(h.makeState()); h.reset(*st);
        const int total = kWarm + kN;
        auto in = sine(amp, total); std::vector<float> full((size_t) total);
        int i = 0;
        while (i < total) {
            const int n = std::min(blockSize, total - i);
            std::vector<float> l(in.begin() + i, in.begin() + i + n), r = l;
            h.processStereo(*st, l.data(), r.data(), n);
            for (int k = 0; k < n; ++k) full[(size_t) (i + k)] = l[(size_t) k];
            i += n;
        }
        return std::vector<float>(full.begin() + kWarm, full.end());   // drop settling window
    }

    // Steady-state magnitude response (|out|/|in| RMS) of the HP stage at one freq.
    static float hpGainAt(const HuggettHpStage& hp, HuggettHpStage::State& st, double f, float amp) {
        hp.reset(st);
        const int warm = 8192, meas = 8192;
        double inSq = 0.0, outSq = 0.0;
        for (int i = 0; i < warm + meas; ++i) {
            const float x = amp * (float) std::sin(2.0 * pi() * f * i / kSR);
            float l = x, r = x;
            hp.processStereo(st, &l, &r, 1);
            if (i >= warm) { inSq += double(x) * x; outSq += double(l) * l; }
        }
        return (inSq > 0.0) ? (float) std::sqrt(outSq / inSq) : 0.0f;
    }

    void runTest() override {
        beginTest("AsymSaturator (plain tanh): aliasing vs drive, matches inline reference");
        {
            std::printf("\n=== AsymSaturator post-stage (plain tanh, bias=0.15, maxDb=24) ===\n");
            std::printf("%6s %6s %10s %12s %12s\n", "amp", "drive", "peak", "alias(shaper)", "alias(ref)");
            for (float amp : { 0.7f, 1.5f }) {
                for (float drive : { 0.0f, 0.3f, 0.6f, 1.0f }) {
                    auto a  = driveShaper(amp, drive, 0.15f, 24.0f);
                    auto nv = driveNaive(amp, drive, 0.15f, 24.0f);
                    float md = 0.0f;
                    for (int i = 0; i < kN; ++i) md = std::max(md, std::abs(a[(size_t) i] - nv[(size_t) i]));
                    std::printf("%6.2f %6.2f %10.4f %12.2f %12.2f\n",
                                amp, drive, maxAbs(a),
                                inharmonicDb(magSpectrum(a), kFundBin),
                                inharmonicDb(magSpectrum(nv), kFundBin));
                    expect(allFinite(a), "shaper output finite");
                    expect(md < 1.0e-6f, "AsymSaturator == plain tanh reference");
                }
            }
        }

        beginTest("HuggettFilter post-drive path: per-block (128) vs per-sample");
        {
            std::printf("\n=== HuggettFilter, post-drive only, res=0 (LP 1200 Hz, 24 dB) ===\n");
            std::printf("%6s %9s %10s %14s %16s\n", "amp", "postDrv", "peak", "alias(blk128)", "maxDiff blk-vs-1");
            for (float amp : { 0.5f, 2.0f }) {
                for (float pd : { 0.5f, 1.0f }) {
                    auto blk = filterPostDrive(amp, pd, 128);
                    auto smp = filterPostDrive(amp, pd, 1);
                    float md = 0.0f;
                    for (int i = 0; i < kN; ++i) md = std::max(md, std::abs(blk[(size_t) i] - smp[(size_t) i]));
                    std::printf("%6.2f %9.2f %10.4f %14.2f %16.6f\n",
                                amp, pd, maxAbs(blk), inharmonicDb(magSpectrum(blk), kFundBin), md);
                    expect(allFinite(blk), "block-processed output finite");
                }
            }
        }

        beginTest("HuggettHpStage: resonant peak structure (cutoff 1000 Hz, db24, drive 0)");
        {
            HuggettHpStage hp; hp.prepare(kSR);
            std::unique_ptr<HuggettHpStage::State> st(hp.makeState());
            const double freqs[] = { 100, 200, 300, 500, 700, 1000, 1300, 1600, 2000,
                                     2600, 3400, 4500, 6000, 8000, 11000, 15000, 20000 };
            std::printf("\n=== HuggettHpStage |H(f)| dB  (HP cutoff 1000 Hz, db24) ===\n");
            std::printf("%6s", "res\\f");
            for (double f : freqs) std::printf("%7.0f", f);
            std::printf("\n");
            for (float res : { 0.15f, 0.5f, 1.0f }) {
                hp.setParams(1000.0f, res, HuggettHpStage::Slope::db24);
                std::printf("%6.2f", res);
                for (double f : freqs)
                    std::printf("%7.1f", 20.0f * std::log10(std::max(1e-7f, hpGainAt(hp, *st, f, 0.3f))));
                std::printf("\n");
                expect(true);
            }
        }

        beginTest("HuggettFilter separation: LP response vs sep (db12 vs db24)");
        {
            auto resp = [](HuggettFilter::Slope slope, float sepOct, double f) {
                HuggettFilter h; h.prepare(kSR); h.setMode(HuggettFilter::Mode::LP);
                h.setSlope(slope); h.setSeparation(sepOct);
                h.setCommon(1000.0f, 0.0f, 0.0f); h.setPostDrive(0.0f);
                std::unique_ptr<FilterModel::State> st(h.makeState()); h.reset(*st);
                const int warm = 8192, meas = 8192; double inSq = 0.0, outSq = 0.0;
                for (int i = 0; i < warm + meas; ++i) {
                    const float x = 0.3f * (float) std::sin(2.0 * pi() * f * i / kSR);
                    float l = x, r = x; h.processStereo(*st, &l, &r, 1);
                    if (i >= warm) { inSq += double(x) * x; outSq += double(l) * l; }
                }
                return 20.0 * std::log10(std::max(1e-7, std::sqrt(outSq / inSq)));
            };
            const double freqs[] = { 250, 500, 1000, 2000, 4000, 8000 };
            std::printf("\n=== HuggettFilter LP |H(f)| dB vs separation (cutoff 1000, res 0) ===\n");
            for (auto slope : { HuggettFilter::Slope::db12, HuggettFilter::Slope::db24 }) {
                std::printf("--- %s ---\n", slope == HuggettFilter::Slope::db12 ? "12 dB" : "24 dB");
                std::printf("%8s", "sep\\f");
                for (double f : freqs) std::printf("%8.0f", f);
                std::printf("\n");
                for (float sep : { -2.0f, -1.0f, 0.0f, 1.0f, 2.0f }) {
                    std::printf("%8.1f", sep);
                    for (double f : freqs) std::printf("%8.1f", resp(slope, sep, f));
                    std::printf("\n");
                }
            }
            expect(true);
        }
        std::printf("\n");
    }
};
static OverdriveDiagnosticTests overdriveDiagnosticTestsInstance;
