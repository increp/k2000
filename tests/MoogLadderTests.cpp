#include <juce_core/juce_core.h>
#include "../src/dsp/spine/MoogLadder.h"
#include "../src/dsp/spine/SpineState.h"
#include <vector>
#include <cmath>
#include <cstring>
#include <cstddef>

static_assert(sizeof(MoogLadder::VoiceState) <= kMaxSpineStateBytes,
              "MoogLadder::VoiceState exceeds kMaxSpineStateBytes — bump SpineState.h");

struct MoogLadderTests : public juce::UnitTest {
    MoogLadderTests() : juce::UnitTest("MoogLadder") {}
    static constexpr double kSR = 48000.0;

    // steady passband/stopband magnitude (linear) of the LP at `probe` Hz, cutoff `fc`.
    // Always sets resonance=0 and drive=0 (linear path).
    static double mag(MoogLadder& m, FilterModel::State& st, double fc, double probe) {
        m.setCommon((float) fc, 0.0f, 0.0f); m.reset(st);
        std::vector<float> l(16384), r(16384);
        for (int i = 0; i < (int) l.size(); ++i)
            l[(size_t)i] = r[(size_t)i] = (float) std::sin(2.0*juce::MathConstants<double>::pi*probe*i/kSR);
        m.processStereo(st, l.data(), r.data(), (int) l.size());
        double e = 0; for (int i = 8192; i < (int) l.size(); ++i) e += double(l[(size_t)i])*l[(size_t)i];
        return std::sqrt(e / 8192.0) * std::sqrt(2.0);   // ~amplitude of a unit sine
    }

    // Like mag() but with a specific Mode set. Resets state each call. Linear path (res=0, drv=0).
    static double magMode(MoogLadder& m, FilterModel::State& st, double fc, double probe, MoogLadder::Mode mode) {
        m.setCommon((float) fc, 0.0f, 0.0f); m.setMode(mode); m.reset(st);
        std::vector<float> l(16384), r(16384);
        for (int i = 0; i < (int) l.size(); ++i)
            l[(size_t)i] = r[(size_t)i] = (float) std::sin(2.0*juce::MathConstants<double>::pi*probe*i/kSR);
        m.processStereo(st, l.data(), r.data(), (int) l.size());
        double e = 0; for (int i = 8192; i < (int) l.size(); ++i) e += double(l[(size_t)i])*l[(size_t)i];
        m.setMode(MoogLadder::Mode::LP);   // restore default
        return std::sqrt(e / 8192.0) * std::sqrt(2.0);
    }

    // Like mag() but holds the resonance and drive already set on `m` via setCommon().
    // Used by the resonance-peak test where mag() would clobber the resonance.
    static double magR(MoogLadder& m, FilterModel::State& st, double probe) {
        m.reset(st);
        std::vector<float> l(16384), r(16384);
        for (int i = 0; i < (int) l.size(); ++i)
            l[(size_t)i] = r[(size_t)i] = (float) std::sin(2.0*juce::MathConstants<double>::pi*probe*i/kSR);
        m.processStereo(st, l.data(), r.data(), (int) l.size());
        double e = 0; for (int i = 8192; i < (int) l.size(); ++i) e += double(l[(size_t)i])*l[(size_t)i];
        return std::sqrt(e / 8192.0) * std::sqrt(2.0);
    }

    void runTest() override {
        MoogLadder m; m.prepare(kSR); m.setSlope(MoogLadder::Slope::db24);
        std::unique_ptr<FilterModel::State> st(m.makeState());

        beginTest("linear passband ~unity, far stopband strongly attenuated");
        const double pass = mag(m, *st, 1000.0, 100.0);    // well below fc
        const double stop = mag(m, *st, 1000.0, 8000.0);   // 3 octaves above fc
        expect(pass > 0.7 && pass < 1.4, "passband not ~unity: " + juce::String(pass,3));
        expect(stop < 0.05, "stopband not attenuated: " + juce::String(stop,4));

        beginTest("24 dB/oct: one octave above fc ~ -24 dB relative to 2 octaves below");
        const double ref  = mag(m, *st, 1000.0, 250.0);
        const double oneA = mag(m, *st, 1000.0, 2000.0);
        const double dB = 20.0*std::log10(oneA/ref);
        expect(dB < -18.0 && dB > -30.0, "slope at fc*2 not ~ -24 dB: " + juce::String(dB,1));

        beginTest("12 dB tap (y2)");
        m.setSlope(MoogLadder::Slope::db12);
        const double ref2  = mag(m, *st, 1000.0, 250.0);
        const double one2  = mag(m, *st, 1000.0, 2000.0);
        const double dB2 = 20.0*std::log10(one2/ref2);
        expect(dB2 < -8.0 && dB2 > -16.0, "12 dB tap slope not ~ -12 dB: " + juce::String(dB2,1));

        beginTest("Q18: MoogLadder::VoiceState fits kMaxSpineStateBytes");
        expect(m.stateSize() <= kMaxSpineStateBytes,
               "VoiceState " + juce::String((int)m.stateSize()) + " > kMaxSpineStateBytes");

        beginTest("resonance grows the peak at fc and thins the bass");
        {
            MoogLadder mr; mr.prepare(kSR); mr.setSlope(MoogLadder::Slope::db24);
            std::unique_ptr<FilterModel::State> s2(mr.makeState());
            mr.setCommon(1000.0f, 0.0f, 0.0f);
            // Use magR() to preserve the resonance set by setCommon() — mag() would reset it to 0.
            const double peakLoRes = magR(mr, *s2, 1000.0);
            const double bassLoRes = magR(mr, *s2, 80.0);
            mr.setCommon(1000.0f, 0.9f, 0.0f);
            const double peakHiRes = magR(mr, *s2, 1000.0);
            const double bassHiRes = magR(mr, *s2, 80.0);
            expect(peakHiRes > peakLoRes * 1.5, "resonance did not grow the peak");
            expect(bassHiRes < bassLoRes,        "bass did not thin as resonance rose");
        }

        beginTest("self-oscillation: sustains + tracks fc within 3% (7-point sweep)");
        {
            // Helper: measure self-osc pitch at `fc` using zero-crossing rate over the
            // last `zcWindow` samples of a `bufLen`-sample buffer kicked by a single impulse.
            auto measurePitch = [&](double fc, int bufLen, int zcWindow) -> std::pair<double,double> {
                MoogLadder mo; mo.prepare(kSR); mo.setSlope(MoogLadder::Slope::db24);
                std::unique_ptr<FilterModel::State> so(mo.makeState());
                mo.setCommon((float) fc, 1.0f, 0.0f); mo.reset(*so);
                std::vector<float> l(bufLen, 0.0f), rv(bufLen, 0.0f);
                l[0] = rv[0] = 1.0f;   // impulse kick
                mo.processStereo(*so, l.data(), rv.data(), bufLen);
                // tail energy check (last 4096 samples)
                double tail = 0.0;
                for (int i = bufLen - 4096; i < bufLen; ++i) tail += std::abs(l[(size_t)i]);
                // zero-crossing rate over zcWindow samples at the very end
                int zc = 0;
                for (int i = bufLen - zcWindow + 1; i < bufLen; ++i)
                    if ((l[(size_t)i-1] <= 0.0f) != (l[(size_t)i] <= 0.0f)) ++zc;
                const double f = zc * 0.5 * kSR / (double)zcWindow;
                return { f, tail };
            };

            // 7 gated cutoffs spanning ~6 octaves. Buffer = 1<<16 (65536 samples) so
            // 55 Hz has ~75 cycles over 65536 samples; zc window = 16384 samples.
            const int kBuf = 1 << 16;
            const int kZcWin = 16384;
            for (double fc : { 55.0, 110.0, 220.0, 440.0, 880.0, 1760.0, 3520.0 }) {
                auto [f, tail] = measurePitch(fc, kBuf, kZcWin);
                const double errPct = std::abs(f - fc) / fc * 100.0;
                expect(tail > 1.0, "self-oscillation did not sustain at fc=" + juce::String(fc));
                expect(std::abs(f - fc) / fc < 0.03,
                       "self-osc pitch off at fc=" + juce::String(fc) + ": measured=" +
                       juce::String(f,1) + " err=" + juce::String(errPct,2) + "%");
                logMessage("self-osc fc=" + juce::String(fc) + " -> measured=" +
                           juce::String(f,1) + " err=" + juce::String(errPct,2) + "%");
            }

            // Non-gating diagnostics at 5000 and 7000 Hz (high-end drift visible, not blocking).
            for (double fc : { 5000.0, 7000.0 }) {
                auto [f, tail] = measurePitch(fc, kBuf, kZcWin);
                const double errPct = std::abs(f - fc) / fc * 100.0;
                logMessage("DIAG (non-gating) fc=" + juce::String(fc) + " -> measured=" +
                           juce::String(f,1) + " err=" + juce::String(errPct,2) + "%");
            }
        }

        beginTest("ROBUSTNESS: self-osc is loud at res=1.0 and engages over a range (not a knife-edge)");
        {
            // Regression gate for the resonance-taper calibration: r must EXCEED the
            // Barkhausen threshold (r=4) near max resonance so the loop grows past
            // threshold and the per-stage tanh sets a real limit-cycle amplitude — a
            // robust, audible self-oscillation. The previous taper mapped res=1.0 -> r=4
            // EXACTLY (threshold), so it only marginally sustained the seed (tail peak
            // ~0.008) and did NOT oscillate at all below res=1.0 (knife-edge).
            // Measure the self-osc TAIL PEAK (post-limiter) from a single impulse kick.
            auto tailPeak = [&](double fc, float res) -> float {
                MoogLadder mp; mp.prepare(kSR); mp.setSlope(MoogLadder::Slope::db24);
                std::unique_ptr<FilterModel::State> sp(mp.makeState());
                mp.setCommon((float) fc, res, 0.0f); mp.reset(*sp);
                const int N = 1 << 16;
                std::vector<float> l(N, 0.0f), r(N, 0.0f);
                l[0] = r[0] = 1.0f;   // impulse kick
                mp.processStereo(*sp, l.data(), r.data(), N);
                float pk = 0.0f;
                for (int i = N - 16384; i < N; ++i) pk = std::max(pk, std::abs(l[(size_t)i]));
                return pk;
            };
            // At res=1.0 the self-osc must be LOUD (tail peak > 0.2) at a couple of cutoffs.
            for (double fc : { 220.0, 880.0 }) {
                const float pk = tailPeak(fc, 1.0f);
                logMessage("ROBUST self-osc fc=" + juce::String(fc) + " res=1.00 tail peak=" + juce::String(pk, 4));
                expect(pk > 0.2f,
                       "self-osc not robust at res=1.0 fc=" + juce::String(fc) + ": tail peak=" + juce::String(pk, 4));
            }
            // It must also sustain at res=0.98 (oscillates over a RANGE, not just the exact top).
            const float pk98 = tailPeak(440.0, 0.98f);
            logMessage("ROBUST self-osc fc=440 res=0.98 tail peak=" + juce::String(pk98, 4));
            expect(pk98 > 0.05f,
                   "self-osc did not sustain at res=0.98 (knife-edge): tail peak=" + juce::String(pk98, 4));
        }

        beginTest("bounded + finite at max res/drive/loud input");
        {
            MoogLadder mb; mb.prepare(kSR); mb.setSlope(MoogLadder::Slope::db24);
            std::unique_ptr<FilterModel::State> sb(mb.makeState());
            mb.setCommon(800.0f, 1.0f, 1.0f); mb.reset(*sb);
            std::vector<float> l(1 << 16), r(1 << 16);
            for (int i=0;i<(int)l.size();++i){ l[(size_t)i]=r[(size_t)i]=4.0f*(float)std::sin(2.0*juce::MathConstants<double>::pi*120.0*i/kSR);}
            mb.processStereo(*sb, l.data(), r.data(), (int) l.size());
            float peak = 0; bool finite = true;
            for (int i=0;i<(int)l.size();++i){ peak=std::max(peak,std::abs(l[(size_t)i])); finite = finite && std::isfinite(l[(size_t)i]); }
            expect(finite, "non-finite under extreme drive");
            expect(peak < 2.0f, "output exceeded the limiter ceiling: " + juce::String(peak));
            logMessage("bounded test: peak=" + juce::String(peak, 4));

            // Non-gating diagnostic: peak/RMS score on a self-oscillating run at fc=440 Hz.
            // Measures the degree of "clean-ness" via peak-to-RMS ratio (near-sine ~ sqrt(2)=1.414).
            // At max resonance, zero input, the self-osc should be a near-sine.
            MoogLadder md; md.prepare(kSR); md.setSlope(MoogLadder::Slope::db24);
            std::unique_ptr<FilterModel::State> sd(md.makeState());
            md.setCommon(440.0f, 1.0f, 0.0f); md.reset(*sd);
            const int kDiagBuf = 1 << 16;
            std::vector<float> dl(kDiagBuf, 0.0f), dr(kDiagBuf, 0.0f);
            dl[0] = dr[0] = 1.0f;   // impulse kick
            md.processStereo(*sd, dl.data(), dr.data(), kDiagBuf);
            // Measure over the last 8192 samples (settled self-osc)
            const int kDiagWin = 8192;
            float dPeak = 0.0f; double dSumSq = 0.0;
            for (int i = kDiagBuf - kDiagWin; i < kDiagBuf; ++i) {
                float v = dl[(size_t)i];
                dPeak = std::max(dPeak, std::abs(v));
                dSumSq += double(v) * double(v);
            }
            float dRMS = (float)std::sqrt(dSumSq / kDiagWin);
            float dCrestFactor = (dRMS > 1e-9f) ? (dPeak / dRMS) : 0.0f;
            logMessage("DIAG self-osc fc=440 peak=" + juce::String(dPeak, 4)
                       + " RMS=" + juce::String(dRMS, 4)
                       + " crest=" + juce::String(dCrestFactor, 4)
                       + " (near-sine target ~1.414)");
            // Gate: the Pirkle output limiter (CALIB_LIM_CEIL=1.5) must bound pure-resonance self-osc.
            // Without the limiter the drv=0 ladder rings far above 1.5 — this is what makes the limiter a tested feature.
            expect(dPeak < 1.5f, "self-osc peak exceeded limiter ceiling: " + juce::String(dPeak, 4));
            expect(dCrestFactor > 1.3f && dCrestFactor < 1.6f,
                   "self-osc crest factor outside near-sine band: " + juce::String(dCrestFactor, 4));
        }

        beginTest("HP mode: low-frequency magnitude attenuated vs LP");
        {
            MoogLadder mh; mh.prepare(kSR); mh.setSlope(MoogLadder::Slope::db24);
            std::unique_ptr<FilterModel::State> sh(mh.makeState());
            // fc=1000 Hz; probe far below (100 Hz). LP should pass, HP should strongly attenuate.
            const double lpLow = magMode(mh, *sh, 1000.0, 100.0, MoogLadder::Mode::LP);
            const double hpLow = magMode(mh, *sh, 1000.0, 100.0, MoogLadder::Mode::HP);
            logMessage("HP test: LP@100Hz=" + juce::String(lpLow,4) + " HP@100Hz=" + juce::String(hpLow,4));
            // HP must attenuate lows by >6 dB (factor 0.5) relative to LP
            expect(hpLow < lpLow * 0.5,
                   "HP mode did not attenuate lows vs LP: LP=" + juce::String(lpLow,4) + " HP=" + juce::String(hpLow,4));
        }

        beginTest("BP mode: mid-band probe exceeds both low and high probes");
        {
            MoogLadder mb2; mb2.prepare(kSR); mb2.setSlope(MoogLadder::Slope::db24);
            std::unique_ptr<FilterModel::State> sb2(mb2.makeState());
            // fc=1000 Hz; low probe=100 Hz, band probe=1000 Hz, high probe=8000 Hz.
            const double bpLow  = magMode(mb2, *sb2, 1000.0,  100.0, MoogLadder::Mode::BP);
            const double bpMid  = magMode(mb2, *sb2, 1000.0, 1000.0, MoogLadder::Mode::BP);
            const double bpHigh = magMode(mb2, *sb2, 1000.0, 8000.0, MoogLadder::Mode::BP);
            logMessage("BP test: low=" + juce::String(bpLow,4) + " mid=" + juce::String(bpMid,4) + " high=" + juce::String(bpHigh,4));
            // BP mid-band peak must exceed both low and high by >3 dB (factor 1.41)
            expect(bpMid > bpLow  * 1.41,
                   "BP mid not > low by >3 dB: mid=" + juce::String(bpMid,4) + " low=" + juce::String(bpLow,4));
            expect(bpMid > bpHigh * 1.41,
                   "BP mid not > high by >3 dB: mid=" + juce::String(bpMid,4) + " high=" + juce::String(bpHigh,4));
        }

        beginTest("setSeparation is a no-op: identical output for sep=0 vs sep=2");
        {
            // MoogLadder is a single-ladder model; setSeparation has no analog.
            // Run the same input through two fresh states with different separation
            // values — the output buffers must be byte-identical.
            const int kN = 4096;
            std::vector<float> lA(kN), rA(kN), lB(kN), rB(kN);
            for (int i = 0; i < kN; ++i)
                lA[(size_t)i] = rA[(size_t)i] = lB[(size_t)i] = rB[(size_t)i] =
                    (float) std::sin(2.0 * juce::MathConstants<double>::pi * 440.0 * i / kSR);

            MoogLadder ms; ms.prepare(kSR); ms.setSlope(MoogLadder::Slope::db24);
            std::unique_ptr<FilterModel::State> sA(ms.makeState());
            std::unique_ptr<FilterModel::State> sB(ms.makeState());

            ms.setCommon(1000.0f, 0.5f, 0.0f);
            ms.setSeparation(0.0f); ms.reset(*sA);
            ms.processStereo(*sA, lA.data(), rA.data(), kN);

            ms.setCommon(1000.0f, 0.5f, 0.0f);
            ms.setSeparation(2.0f); ms.reset(*sB);
            ms.processStereo(*sB, lB.data(), rB.data(), kN);

            expect(std::memcmp(lA.data(), lB.data(), (size_t)kN * sizeof(float)) == 0,
                   "setSeparation(2) produced different output than setSeparation(0) on left channel");
            expect(std::memcmp(rA.data(), rB.data(), (size_t)kN * sizeof(float)) == 0,
                   "setSeparation(2) produced different output than setSeparation(0) on right channel");
        }

        beginTest("Arturia golden match (skipped until data captured)");
        {
            juce::File g = juce::File(BERNIE_GOLDEN_DIR).getChildFile("moog/response.csv");
            if (! g.existsAsFile()) { logMessage("no golden Arturia data — skipping"); }
            else {
                MoogLadder mg; mg.prepare(kSR); mg.setSlope(MoogLadder::Slope::db24);
                std::unique_ptr<FilterModel::State> sg(mg.makeState());
                for (auto& line : juce::StringArray::fromLines(g.loadFileAsString())) {
                    auto c = juce::StringArray::fromTokens(line, ",", "");
                    if (c.size() < 4 || ! c[0].containsOnly("0123456789.")) continue;
                    const double fc = c[0].getDoubleValue(), res = c[1].getDoubleValue(),
                                 pr = c[2].getDoubleValue(), wantDb = c[3].getDoubleValue();
                    mg.setCommon((float) fc, (float) res, 0.0f);
                    const double gotDb = 20.0*std::log10(std::max(1e-6, mag(mg, *sg, fc, pr)));
                    expect(std::abs(gotDb - wantDb) < 6.0,   // CALIB: tighten during calibration
                           "Arturia mismatch @fc=" + juce::String(fc) + " pr=" + juce::String(pr));
                }
            }
        }

        beginTest("bass voice adds energy at the played fundamental; amount=0 is a no-op");
        {
            MoogLadder mb; mb.prepare(kSR);
            std::unique_ptr<FilterModel::State> s0(mb.makeState());
            std::unique_ptr<FilterModel::State> s1(mb.makeState());
            mb.setCommon(1500.0f, 0.0f, 0.0f); mb.setSlope(MoogLadder::Slope::db24);

            auto run = [&](FilterModel::State& state, float amount)->std::vector<float> {
                mb.setBass(amount, /*sine*/0, /*oct*/0);
                mb.setFundamental(state, 110.0f); mb.reset(state);
                std::vector<float> l(16384, 0.0f), r(16384, 0.0f);  // SILENT input
                mb.processStereo(state, l.data(), r.data(), (int) l.size());
                return l;
            };
            const auto off = run(*s0, 0.0f);
            const auto on  = run(*s1, 0.8f);

            // amount=0: pure silence in -> bit-identical silence out.
            // fpclassify(v)==FP_ZERO is the warning-free spelling of "exactly +/-0".
            bool zero = true; for (float v : off) zero = zero && (std::fpclassify(v) == FP_ZERO);
            expect(zero, "bassAmount=0 was not a no-op on silent input");
            // amount>0: energy present, and concentrated near 110 Hz (Goertzel)
            double e = 0; for (float v : on) e += double(v)*v;
            expect(e > 1.0, "bass voice produced no energy");

            // Pitch check via zero-crossing rate over the last 8192 samples (~110 Hz).
            const int zcWin = 8192;
            int zc = 0;
            for (int i = (int) on.size() - zcWin + 1; i < (int) on.size(); ++i)
                if ((on[(size_t)i-1] <= 0.0f) != (on[(size_t)i] <= 0.0f)) ++zc;
            const double f = zc * 0.5 * kSR / (double) zcWin;
            logMessage("bass voice oct0 measured pitch=" + juce::String(f,1) + " Hz");
            expect(std::abs(f - 110.0) / 110.0 < 0.05,
                   "bass voice pitch off at oct0: measured=" + juce::String(f,1) + " Hz");
        }
    }
};
static MoogLadderTests moogLadderTestsInstance;
