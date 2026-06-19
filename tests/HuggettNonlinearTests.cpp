#include <juce_core/juce_core.h>
#include "../src/dsp/spine/AsymSaturator.h"
#include "../src/dsp/spine/DcBlocker.h"
#include "../src/dsp/spine/NlSvfCell.h"
#include "../src/dsp/spine/HuggettFilter.h"
#include <cmath>
#include <vector>
#include <memory>

class HuggettNonlinearTests : public juce::UnitTest {
public:
    HuggettNonlinearTests() : juce::UnitTest("HuggettNonlinear") {}

    // RMS of a buffer.
    static float rms(const std::vector<float>& v) {
        double s = 0; for (float x : v) s += double(x) * x;
        return (float) std::sqrt(s / v.size());
    }

    void runTest() override {
        beginTest("AsymSaturator: disengaged at zero drive, engaged when driven");
        {
            AsymSaturator sat;
            sat.setDrive(0.0f, 0.0f, 30.0f);
            expect(!sat.engaged(), "zero drive is a no-op");
            sat.setDrive(0.5f, 0.18f, 30.0f);
            expect(sat.engaged(), "driven stage engages");
        }

        beginTest("AsymSaturator: adds even harmonics (asymmetry) and is bounded");
        {
            AsymSaturator sat; sat.setDrive(1.0f, 0.25f, 30.0f);
            AsymSaturator::State st; st.reset();
            const double sr = 48000.0, f = 220.0;
            float peak = 0.0f; double dcAcc = 0.0; int N = 4096;
            for (int i = 0; i < N; ++i) {
                float x = 0.7f * std::sin(2.0 * juce::MathConstants<double>::pi * f * i / sr);
                float y = sat.process(x, 0, st);
                peak = std::max(peak, std::abs(y));
                if (i > N / 2) dcAcc += y;            // asymmetric shaper -> nonzero DC
            }
            expect(peak < 2.0f, "output bounded: " + juce::String(peak));
            expect(std::abs(dcAcc) > 1.0e-3, "asymmetry produces DC offset");
        }

        beginTest("DcBlocker removes a constant offset, keeps audio");
        {
            DcBlocker dc; dc.prepare(48000.0); dc.reset();
            std::vector<float> out;
            for (int i = 0; i < 8192; ++i) {
                float x = 0.5f + std::sin(2.0 * juce::MathConstants<double>::pi * 200.0 * i / 48000.0);
                out.push_back(dc.process(x, 0));
            }
            double tail = 0; for (int i = 6000; i < 8192; ++i) tail += out[(size_t) i];
            expect(std::abs(tail / 2192.0) < 0.02, "DC removed from tail");
            std::vector<float> ac(out.begin() + 6000, out.end());
            expect(rms(ac) > 0.5f, "audio preserved: " + juce::String(rms(ac)));
        }

        beginTest("DcBlocker keeps L/R state independent");
        {
            DcBlocker dc; dc.prepare(48000.0); dc.reset();
            // Feed ch0 a +0.5 DC offset and ch1 a -0.5 DC offset for many samples.
            float lastL = 0.0f, lastR = 0.0f;
            for (int i = 0; i < 8192; ++i) {
                lastL = dc.process(+0.5f, 0);
                lastR = dc.process(-0.5f, 1);
            }
            // Each channel independently converges toward removing its own DC.
            expect(std::abs(lastL) < 0.05f, "L DC removed: " + juce::String(lastL));
            expect(std::abs(lastR) < 0.05f, "R DC removed: " + juce::String(lastR));
            // If state were shared, the opposite-sign inputs would cross-contaminate
            // and at least one would NOT converge near zero.
        }

        beginTest("NlSvfCell ~= linear at low resonance (no saturation drift)");
        {
            NlSvfCell c; c.prepare(48000.0); c.setCutoff(1000.0f); c.setResonance(0.1f); c.setResSat(0.1f);
            float peakLow = 0, peakHigh = 0;
            auto sweep = [&](double f, float& peak){ c.reset(); const int N=8192;
                for (int i=0;i<N;++i){ float x=std::sin(2.0*juce::MathConstants<double>::pi*f*i/48000.0);
                    float l=x,r=x; c.process(l,r,NlSvfCell::LP); if(i>N/2) peak=std::max(peak,std::abs(l)); } };
            sweep(100.0, peakLow); sweep(10000.0, peakHigh);
            expect(peakLow > 0.7f && peakHigh < 0.1f, "LP shape intact at low res");
        }

        beginTest("NlSvfCell self-oscillation is bounded at max resonance");
        {
            NlSvfCell c; c.prepare(48000.0); c.setCutoff(800.0f); c.setResonance(1.0f); c.setResSat(1.0f);
            c.reset();
            float peak = 0; bool nan = false;
            // tiny impulse to kick it, then run 0.5 s of silence
            float l = 1.0f, r = 1.0f; c.process(l, r, NlSvfCell::LP);
            for (int i = 0; i < 24000; ++i) {
                float a = 0.0f, b = 0.0f; c.process(a, b, NlSvfCell::LP);
                if (!std::isfinite(a)) nan = true;
                if (i > 2000) peak = std::max(peak, std::abs(a));
            }
            expect(!nan, "no NaN/Inf");
            expect(peak < 4.0f, "self-osc amplitude bounded: " + juce::String(peak));
        }

        beginTest("HuggettFilter at zero drive/resonance == bare linear cell");
        {
            HuggettFilter h; h.prepare(48000.0); h.setMode(HuggettFilter::Mode::LP);
            h.setSlope(HuggettFilter::Slope::db12); h.setSeparation(0.0f);
            h.setCommon(1200.0f, 0.0f, 0.0f); h.setPostDrive(0.0f);
            std::unique_ptr<FilterModel::State> st(h.makeState()); h.reset(*st);

            NlSvfCell ref; ref.prepare(48000.0); ref.setCutoff(1200.0f); ref.setResonance(0.0f); ref.setResSat(0.0f); ref.reset();

            float maxDiff = 0.0f;
            for (int i = 0; i < 4096; ++i) {
                float x = 0.5f * std::sin(2.0 * juce::MathConstants<double>::pi * 300.0 * i / 48000.0);
                float hl = x, hr = x; h.processStereo(*st, &hl, &hr, 1);
                float rl = x, rr = x; ref.process(rl, rr, NlSvfCell::LP);
                maxDiff = std::max(maxDiff, std::abs(hl - rl));
            }
            expect(maxDiff < 1.0e-5f, "zero-drive path is bit-for-bit linear: maxDiff=" + juce::String(maxDiff));
        }

        beginTest("NlSvfCell: loud input droops cutoff (darker) vs quiet");
        {
            auto hfMag = [](float amp){
                NlSvfCell c; c.prepare(48000.0); c.setCutoff(2000.0f); c.setResonance(0.0f); c.setResSat(0.0f);
                c.reset(); const int N=16384; float peak=0;
                for (int i=0;i<N;++i){
                    // Drive droop through the real per-block mechanism (once per 64-sample block).
                    if (i % 64 == 0) c.updateBlock();
                    float x=amp*std::sin(2.0*juce::MathConstants<double>::pi*2000.0*i/48000.0);
                    float l=x,r=x; c.process(l,r,NlSvfCell::LP); if(i>N/2) peak=std::max(peak,std::abs(l)); }
                return peak / amp;   // normalized gain at cutoff
            };
            expect(hfMag(2.0f) < hfMag(0.05f) * 0.99f, "loud input is darker (droop active)");
        }

        beginTest("HuggettFilter: driving changes harmonic content but stays bounded");
        {
            HuggettFilter h; h.prepare(48000.0); h.setMode(HuggettFilter::Mode::LP);
            h.setSlope(HuggettFilter::Slope::db24); h.setSeparation(0.0f);
            std::unique_ptr<FilterModel::State> st(h.makeState());

            auto runSaw = [&](float drive){
                h.setCommon(2000.0f, 0.3f, drive); h.setPostDrive(drive); h.reset(*st);
                std::vector<float> out; const int N = 4096;
                for (int i = 0; i < N; ++i) {
                    float ph = (float) std::fmod(220.0 * i / 48000.0, 1.0);
                    float x = 0.6f * (2.0f * (float) ph - 1.0f);     // saw
                    float l = x, r = x; h.processStereo(*st, &l, &r, 1);
                    out.push_back(l);
                }
                return out;
            };
            auto clean = runSaw(0.0f);
            auto dirty = runSaw(1.0f);
            expect(rms(dirty) > 0.0f, "driven output is non-silent");
            for (float v : dirty) expect(std::abs(v) < 4.0f, "driven output bounded");
            // crude harmonic-change proxy: driven differs materially from clean
            double diff = 0; for (size_t i = 0; i < clean.size(); ++i) diff += std::abs(dirty[i] - clean[i]);
            expect(diff / (double) clean.size() > 0.01, "drive changes the signal");
        }
    }
};
static HuggettNonlinearTests huggettNonlinearTestsInstance;
