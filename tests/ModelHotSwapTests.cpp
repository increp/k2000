#include <juce_core/juce_core.h>
#include "../src/dsp/spine/SpineFilterSlot.h"
#include "../src/dsp/spine/SpineState.h"
#include "../src/Voice.h"
#include "../src/Layer.h"
#include "../src/params/ParamSnapshot.h"
#include "fixtures/CountingFilterModel.h"
#include <vector>
#include <cmath>

struct ModelHotSwapTests : public juce::UnitTest {
    ModelHotSwapTests() : juce::UnitTest("ModelHotSwap") {}
    static constexpr double kSR = 48000.0;

    // process n samples of a steady 0.5 DC-ish tone through the slot at the given fade.
    static void block(SpineFilterSlot& slot, const FilterModel* m, float fadeMs,
                      std::vector<float>& l, std::vector<float>& r) {
        std::fill(l.begin(), l.end(), 0.5f);
        std::fill(r.begin(), r.end(), 0.5f);
        slot.processStereo(nullptr, false, m, fadeMs, 0.0f, l.data(), r.data(), (int) l.size());
    }

    void runTest() override {
        const int before = CountingFilterModel::liveStates();

        CountingFilterModel a; a.prepare(kSR); a.setCommon(20000.0f, 0, 0); a.setGain(1.0f);
        CountingFilterModel b; b.prepare(kSR); b.setCommon(20000.0f, 0, 0); b.setGain(0.25f);

        SpineFilterSlot slot;
        slot.prepare(kSR, 64, &a, nullptr);   // start on model a
        std::vector<float> l(64), r(64);

        beginTest("steady state outputs the active model (gain 1.0)");
        block(slot, &a, 25.0f, l, r);
        expect(std::abs(l[63] - 0.5f) < 1e-3f, "model A steady output wrong");

        beginTest("click-free switch: no large sample-to-sample jump across the change");
        // begin fade A->B; capture the first fade block
        std::vector<float> prev = l;
        block(slot, &b, 25.0f, l, r);  // fadeLen = round(25ms*48k/1000)=1200 samples >> 64
        float maxJump = std::abs(l[0] - prev.back());
        for (size_t i = 1; i < l.size(); ++i) maxJump = std::max(maxJump, std::abs(l[i] - l[i-1]));
        expect(maxJump < 0.05f, "discontinuity at switch: " + juce::String(maxJump, 4));

        beginTest("fade completes toward model B (gain 0.25) within ~fadeLen");
        // 1200-sample fade; push ~1300 samples in 64-sample blocks
        for (int done = 64; done < 1400; done += 64) block(slot, &b, 25.0f, l, r);
        expect(std::abs(l[63] - 0.125f) < 5e-3f, "did not settle to model B (0.5*0.25=0.125)");

        beginTest("no leak: live states return to baseline+1 (one active buffer)");
        expect(CountingFilterModel::liveStates() == before + 1,
               "live states = " + juce::String(CountingFilterModel::liveStates() - before));

        beginTest("coalesce depth-1: A->B then mid-fade ->A settles back to A");
        {
            SpineFilterSlot s2; s2.prepare(kSR, 64, &a, nullptr);
            block(s2, &a, 25.0f, l, r);
            block(s2, &b, 25.0f, l, r);          // start A->B
            block(s2, &a, 25.0f, l, r);          // mid-fade re-target -> A (pending)
            for (int done = 0; done < 3000; done += 64) block(s2, &a, 25.0f, l, r);
            expect(std::abs(l[63] - 0.5f) < 5e-3f, "coalesced fade did not settle back to A");
        }

        beginTest("bind() snaps to a different model with no fade (Q17b unit)");
        {
            SpineFilterSlot s3; s3.prepare(kSR, 64, &a, nullptr);
            s3.bind(&b, nullptr);   // note-start onto model B (a stolen voice's new layer)
            std::fill(l.begin(), l.end(), 0.5f); std::fill(r.begin(), r.end(), 0.5f);
            s3.processStereo(nullptr, false, &b, 25.0f, 0.0f, l.data(), r.data(), 64);
            expect(std::isfinite(l[63]), "non-finite output after bind");
            expect(std::abs(l[63] - 0.125f) < 5e-3f, "bind did not snap to B (0.5*0.25)");
        }

        beginTest("bind() cancels an in-flight fade and leaks no state");
        {
            const int base = CountingFilterModel::liveStates();
            SpineFilterSlot s4; s4.prepare(kSR, 64, &a, nullptr);   // base+1 (A)
            s4.processStereo(nullptr, false, &b, 25.0f, 0.0f, l.data(), r.data(), 64);  // start A->B: base+2
            s4.bind(&a, nullptr);   // steal mid-fade back to A: frees B -> base+1
            expect(CountingFilterModel::liveStates() == base + 1,
                   "live after bind = " + juce::String(CountingFilterModel::liveStates() - base));
        }

        beginTest("live crossfade Huggett <-> Moog stays finite and bounded");
        {
            static constexpr int kHotBlock = 512;
            Layer hotLayer; hotLayer.prepare(48000.0, kHotBlock);
            Voice hotVoice; hotVoice.setLayer(&hotLayer); hotVoice.prepare(48000.0, kHotBlock);
            // svfResonance: Huggett maps resonance to Q = 0.5 + r^2*49.5; use 0.1
            // (Q ≈ 1.0) so the saw's harmonics near the resonant peak stay bounded.
            ParamSnapshot ps; ps.spineModel = 0; ps.svfCutoffHz = 800.0f; ps.svfResonance = 0.1f; ps.spineModelFadeMs = 25.0f;
            hotLayer.updateParameters(ps); hotVoice.noteOn(60, 1.0f);
            std::vector<float> lBuf(kHotBlock), rBuf(kHotBlock); bool finite = true;
            // finite is tracked over every block; the peak magnitude is accumulated into
            // whichever accumulator the caller passes, so the bound can be measured over the
            // crossfade window ALONE (not Huggett's pre-switch steady state).
            auto blockRun = [&](float& peakAccum) {
                std::fill(lBuf.begin(),lBuf.end(),0.0f); std::fill(rBuf.begin(),rBuf.end(),0.0f);
                hotVoice.render(lBuf.data(), rBuf.data(), kHotBlock);
                for (float x : lBuf) { finite = finite && std::isfinite(x); peakAccum = std::max(peakAccum, std::abs(x)); } };
            float huggettPeak = 0.0f, crossfadePeak = 0.0f;
            blockRun(huggettPeak);                                       // pre-switch: Huggett alone
            ps.spineModel = 1; hotLayer.updateParameters(ps);            // switch to Moog -> fade
            for (int blk = 0; blk < 8; ++blk) blockRun(crossfadePeak);   // the crossfade + settle
            expect(finite, "non-finite across Huggett->Moog crossfade");
            expect(crossfadePeak < 4.0f, "crossfade output blew up: " + juce::String(crossfadePeak));
        }
    }
};
static ModelHotSwapTests modelHotSwapTestsInstance;
