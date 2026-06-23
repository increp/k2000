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
    static double mag(MoogLadder& m, FilterModel::State& st, double fc, double probe) {
        m.setCommon((float) fc, 0.0f, 0.0f); m.reset(st);
        std::vector<float> l(16384), r(16384);
        for (int i = 0; i < (int) l.size(); ++i)
            l[(size_t)i] = r[(size_t)i] = (float) std::sin(2.0*juce::MathConstants<double>::pi*probe*i/kSR);
        m.processStereo(st, l.data(), r.data(), (int) l.size());
        double e = 0; for (int i = 8192; i < (int) l.size(); ++i) e += double(l[(size_t)i])*l[(size_t)i];
        return std::sqrt(e / 8192.0) * std::sqrt(2.0);   // ~amplitude of a unit sine
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
    }
};
static MoogLadderTests moogLadderTestsInstance;
