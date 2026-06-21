#include <juce_core/juce_core.h>
#include "../src/dsp/spine/HuggettFilter.h"
#include <cmath>
#include <memory>

// Steady-state magnitude response (dB) of a HuggettFilter at probe frequency f.
// Configured via a caller lambda so each test sets mode/slope/separation/routing.
struct HuggettSeparationTests : public juce::UnitTest {
    HuggettSeparationTests() : juce::UnitTest("HuggettSeparation") {}

    static constexpr double kSR = 48000.0;

    template <typename Cfg>
    static double respDb(Cfg&& cfg, double f, float cutoff, float res) {
        HuggettFilter h; h.prepare(kSR);
        cfg(h);
        h.setCommon(cutoff, res, 0.0f); h.setPostDrive(0.0f);
        std::unique_ptr<FilterModel::State> st(h.makeState()); h.reset(*st);
        const int warm = 8192, meas = 8192;
        double inSq = 0.0, outSq = 0.0;
        for (int i = 0; i < warm + meas; ++i) {
            const float x = 0.3f * (float) std::sin(2.0 * juce::MathConstants<double>::pi * f * i / kSR);
            float l = x, r = x; h.processStereo(*st, &l, &r, 1);
            if (i >= warm) { inSq += double(x) * x; outSq += double(l) * l; }
        }
        return 20.0 * std::log10(std::max(1e-7, std::sqrt(outSq / inSq)));
    }

    // Count local maxima in a magnitude curve that stand at least `minProm` dB
    // above both neighbours — a coarse "distinct resonance" counter.
    static int countPeaks(const std::vector<double>& db, double minProm) {
        int peaks = 0;
        for (size_t i = 1; i + 1 < db.size(); ++i)
            if (db[i] - db[i-1] > minProm && db[i] - db[i+1] > minProm) ++peaks;
        return peaks;
    }

    void runTest() override {
        beginTest("T1: separation is alive in BOTH 12 dB and 24 dB (LP)");
        for (auto slope : { HuggettFilter::Slope::db12, HuggettFilter::Slope::db24 }) {
            auto cfg = [slope](HuggettFilter& h) {
                h.setMode(HuggettFilter::Mode::LP); h.setSlope(slope);
            };
            auto cfg0 = [&](HuggettFilter& h){ cfg(h); h.setSeparation(0.0f); };
            auto cfg2 = [&](HuggettFilter& h){ cfg(h); h.setSeparation(2.0f); };
            const double r0 = respDb(cfg0, 1500.0, 1000.0f, 0.0f);
            const double r2 = respDb(cfg2, 1500.0, 1000.0f, 0.0f);
            expect(std::abs(r2 - r0) > 3.0,
                   "separation must change the 1500 Hz LP response by >3 dB (slope "
                   + juce::String(slope == HuggettFilter::Slope::db12 ? "12" : "24") + " dB): got "
                   + juce::String(std::abs(r2 - r0), 2) + " dB");
        }

        beginTest("T2: sep=0 single-mode slopes are correct (12 dB ~1-pole-pair, 24 dB ~2x steeper)");
        {
            auto lp = [](HuggettFilter::Slope s) {
                return [s](HuggettFilter& h){ h.setMode(HuggettFilter::Mode::LP); h.setSlope(s); h.setSeparation(0.0f); };
            };
            // Rolloff across one octave (2000->4000 Hz), an octave above the 1000 Hz cutoff.
            const double d12 = respDb(lp(HuggettFilter::Slope::db12), 2000.0, 1000.0f, 0.0f)
                             - respDb(lp(HuggettFilter::Slope::db12), 4000.0, 1000.0f, 0.0f);
            const double d24 = respDb(lp(HuggettFilter::Slope::db24), 2000.0, 1000.0f, 0.0f)
                             - respDb(lp(HuggettFilter::Slope::db24), 4000.0, 1000.0f, 0.0f);
            expect(d12 > 9.0 && d12 < 15.0, "12 dB LP ~12 dB/oct, got " + juce::String(d12, 1));
            expect(d24 > 21.0 && d24 < 27.0, "24 dB LP ~24 dB/oct, got " + juce::String(d24, 1));
        }

        beginTest("T3: two distinct resonances appear and pull apart with separation (ParLPLP)");
        {
            const double freqs[] = { 200, 300, 450, 650, 900, 1300, 1900, 2700, 4000, 6000 };
            auto curve = [&](float sep) {
                std::vector<double> v;
                for (double f : freqs)
                    v.push_back(respDb([sep](HuggettFilter& h){
                        h.setRouting(HuggettFilter::Routing::ParLPLP); h.setSeparation(sep);
                    }, f, 1000.0f, 0.97f));
                return v;
            };
            expect(countPeaks(curve(0.0f), 1.0) <= 1, "sep=0 ParLPLP has a single peak");
            expect(countPeaks(curve(3.0f), 1.0) >= 2, "sep=3 ParLPLP shows >=2 distinct resonances");
        }

        beginTest("T4: routing shapes (LP+HP complementary vs HP+HP low-rejecting)");
        {
            // LP+HP parallel at moderate separation passes both low and high, dips mid.
            auto lphp = [](HuggettFilter& h){ h.setRouting(HuggettFilter::Routing::ParLPHP); h.setSeparation(2.0f); };
            const double low  = respDb(lphp, 200.0,  1000.0f, 0.2f);
            const double mid  = respDb(lphp, 1000.0, 1000.0f, 0.2f);
            const double high = respDb(lphp, 6000.0, 1000.0f, 0.2f);
            expect(low - mid > 2.0 && high - mid > 2.0,
                   "LP+HP dips in the middle: low " + juce::String(low,1)
                   + " mid " + juce::String(mid,1) + " high " + juce::String(high,1));
            // HP+HP rejects lows relative to highs.
            auto hphp = [](HuggettFilter& h){ h.setRouting(HuggettFilter::Routing::ParHPHP); h.setSeparation(1.0f); };
            expect(respDb(hphp, 6000.0, 1000.0f, 0.2f) - respDb(hphp, 150.0, 1000.0f, 0.2f) > 12.0,
                   "HP+HP passes highs far above lows");
        }

        beginTest("T5: stable + finite at max resonance and max separation, all single modes");
        for (auto mode : { HuggettFilter::Mode::LP, HuggettFilter::Mode::BP, HuggettFilter::Mode::HP }) {
            for (auto slope : { HuggettFilter::Slope::db12, HuggettFilter::Slope::db24 }) {
                for (float sep : { -4.0f, 4.0f }) {
                    HuggettFilter h; h.prepare(kSR);
                    h.setMode(mode); h.setSlope(slope); h.setSeparation(sep);
                    h.setCommon(1000.0f, 0.999f, 0.0f); h.setPostDrive(0.0f);
                    std::unique_ptr<FilterModel::State> st(h.makeState()); h.reset(*st);
                    float peak = 0.0f; bool finite = true;
                    for (int blk = 0; blk < 64; ++blk) {
                        float l[128], r[128];
                        for (int i = 0; i < 128; ++i) {
                            l[i] = r[i] = 0.5f * (float) std::sin(2.0 * juce::MathConstants<double>::pi * 220.0 * (blk*128+i) / kSR);
                        }
                        h.processStereo(*st, l, r, 128);
                        for (int i = 0; i < 128; ++i) { if (!std::isfinite(l[i])) finite = false; peak = std::max(peak, std::abs(l[i])); }
                    }
                    expect(finite, "output finite at res=0.999");
                    expect(peak < 100.0f, "no runaway (peak " + juce::String(peak, 2) + ")");
                }
            }
        }
    }
};
static HuggettSeparationTests huggettSeparationTestsInstance;
