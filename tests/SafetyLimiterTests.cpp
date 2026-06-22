#include <juce_core/juce_core.h>
#include "../src/dsp/SafetyLimiter.h"
#include <cmath>
#include <vector>

struct SafetyLimiterTests : public juce::UnitTest {
    SafetyLimiterTests() : juce::UnitTest("SafetyLimiter") {}
    static constexpr double kSR = 48000.0;
    static float ceilingLin() { return std::pow(10.0f, -12.0f / 20.0f); }  // matches kSafetyCeilingDb

    void runTest() override {
        const float ceil = ceilingLin();

        beginTest("ceiling enforced: a 4x-over signal never exceeds the ceiling");
        {
            SafetyLimiter lim; lim.prepare(kSR); lim.reset();
            const int n = 4096;
            std::vector<float> l((size_t)n), r((size_t)n);
            for (int i = 0; i < n; ++i)
                l[(size_t)i] = r[(size_t)i] = 4.0f * (float) std::sin(2.0*juce::MathConstants<double>::pi*220.0*i/kSR);
            lim.process(l.data(), r.data(), n);
            float mx = 0.0f; for (int i = 0; i < n; ++i) mx = std::max(mx, std::max(std::abs(l[(size_t)i]), std::abs(r[(size_t)i])));
            expect(mx <= ceil + 1e-6f, "max " + juce::String(mx,6) + " must be <= ceiling " + juce::String(ceil,6));
        }

        beginTest("no overshoot on a silence->huge transient (first sample already capped)");
        {
            SafetyLimiter lim; lim.prepare(kSR); lim.reset();
            std::vector<float> l{ 0.0f, 0.0f, 9.0f, 9.0f };
            lim.process(l.data(), nullptr, (int) l.size());
            for (float x : l) expect(std::abs(x) <= ceil + 1e-6f, "sample " + juce::String(x,6) + " <= ceiling");
        }

        beginTest("below-ceiling signal passes through unchanged");
        {
            SafetyLimiter lim; lim.prepare(kSR); lim.reset();
            const int n = 2048;
            std::vector<float> in((size_t)n), l((size_t)n);
            for (int i = 0; i < n; ++i) in[(size_t)i] = l[(size_t)i] = 0.5f * ceil * (float) std::sin(2.0*juce::MathConstants<double>::pi*440.0*i/kSR);
            lim.process(l.data(), nullptr, n);
            float d = 0.0f; for (int i = 0; i < n; ++i) d = std::max(d, std::abs(l[(size_t)i] - in[(size_t)i]));
            expect(d < 1e-7f, "below-ceiling output must be bit-identical (max diff " + juce::String(d,9) + ")");
        }

        beginTest("stereo-linked: same gain applied to both channels (ratio preserved)");
        {
            SafetyLimiter lim; lim.prepare(kSR); lim.reset();
            const int n = 1024;
            std::vector<float> l((size_t)n), r((size_t)n);
            for (int i = 0; i < n; ++i) { l[(size_t)i] = 4.0f; r[(size_t)i] = 1.0f; }  // loud L, quieter R, same sign
            lim.process(l.data(), r.data(), n);
            // After the first sample the gain is settled; L/R ratio must equal the input 4:1.
            const float ratio = l[(size_t)(n-1)] / r[(size_t)(n-1)];
            expect(std::abs(ratio - 4.0f) < 1e-3f, "L/R ratio should be 4.0, got " + juce::String(ratio,5));
        }

        beginTest("gain reduction reports 0 below ceiling, >0 above");
        {
            SafetyLimiter lim; lim.prepare(kSR); lim.reset();
            std::vector<float> quiet((size_t)256, 0.1f * ceil);
            lim.process(quiet.data(), nullptr, 256);
            expect(lim.gainReductionDb() < 0.01f, "no GR below ceiling");
            std::vector<float> loud((size_t)256, 5.0f);
            lim.process(loud.data(), nullptr, 256);
            expect(lim.gainReductionDb() > 1.0f, "GR > 0 above ceiling");
        }

        beginTest("release recovers after a loud burst");
        {
            SafetyLimiter lim; lim.prepare(kSR); lim.reset();
            std::vector<float> loud((size_t)256, 5.0f);
            lim.process(loud.data(), nullptr, 256);
            const float grAfterBurst = lim.gainReductionDb();
            // feed quiet blocks; GR should fall back toward 0 within a few release times
            float grLater = grAfterBurst;
            // ~320 ms of quiet (>= 4 release time-constants at 80 ms) so recovery is robust, not marginal
            for (int b = 0; b < 60; ++b) { std::vector<float> q((size_t)256, 0.05f * ceil); lim.process(q.data(), nullptr, 256); grLater = lim.gainReductionDb(); }
            expect(grLater < grAfterBurst * 0.1f, "GR should recover toward 0 (after " + juce::String(grAfterBurst,3)
                   + " -> " + juce::String(grLater,3) + ")");
        }
    }
};
static SafetyLimiterTests safetyLimiterTestsInstance;
