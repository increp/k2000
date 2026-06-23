#include <juce_core/juce_core.h>
#include "../src/dsp/spine/MoogLadder.h"
#include "../src/dsp/spine/SpineState.h"
#include <vector>
#include <cmath>
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

        beginTest("self-oscillation: sustains + tracks fc within 3%");
        {
            for (double fc : { 220.0, 880.0 }) {
                MoogLadder mo; mo.prepare(kSR); mo.setSlope(MoogLadder::Slope::db24);
                std::unique_ptr<FilterModel::State> so(mo.makeState());
                mo.setCommon((float) fc, 1.0f, 0.0f); mo.reset(*so);
                std::vector<float> l(1 << 15, 0.0f), r(1 << 15, 0.0f);
                l[0] = r[0] = 1.0f;                       // impulse kick
                mo.processStereo(*so, l.data(), r.data(), (int) l.size());
                // sustained: tail energy is non-trivial
                double tail = 0; for (int i = (int)l.size()-4096; i < (int)l.size(); ++i) tail += std::abs(l[(size_t)i]);
                expect(tail > 1.0, "self-oscillation did not sustain at fc=" + juce::String(fc));
                // pitch: zero-crossing rate over the tail ~ fc
                int zc = 0; for (int i = (int)l.size()-8192+1; i < (int)l.size(); ++i)
                    if ((l[(size_t)i-1] <= 0.0f) != (l[(size_t)i] <= 0.0f)) ++zc;
                const double f = zc * 0.5 * kSR / 8192.0;
                expect(std::abs(f - fc) / fc < 0.03, "self-osc pitch off: " + juce::String(f,1) + " vs " + juce::String(fc));
            }
        }
    }
};
static MoogLadderTests moogLadderTestsInstance;
