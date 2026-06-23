#include <juce_core/juce_core.h>
#include "../src/dsp/spine/cmajor/MoogLadderAdapter.h"
#include <vector>
#include <cmath>
#include <cstddef>

// Task 1: prove codegen + in-place embedding + small footprint, and that the
// one-pole actually attenuates highs.
struct MoogPipelineTests : public juce::UnitTest {
    MoogPipelineTests() : juce::UnitTest("MoogPipeline") {}
    static constexpr double kSR = 48000.0;

    static double rms(MoogLadderAdapter& a, double freq) {
        std::vector<float> buf(8192);
        for (int i = 0; i < (int) buf.size(); ++i)
            buf[(size_t) i] = (float) std::sin(2.0 * juce::MathConstants<double>::pi * freq * i / kSR);
        a.process(buf.data(), (int) buf.size());
        double e = 0; for (int i = 4096; i < (int) buf.size(); ++i) e += double(buf[(size_t)i])*buf[(size_t)i];
        return std::sqrt(e / 4096.0);
    }

    void runTest() override {
        beginTest("adapter is in-place and embeds cheaply");
        // Footprint sanity: with the small-block codegen the adapter is small.
        logMessage("sizeof(MoogLadderAdapter) = " + juce::String((int) sizeof(MoogLadderAdapter)));
        expect(sizeof(MoogLadderAdapter) <= 512, "adapter larger than expected — check maxFramesPerBlock");

        beginTest("placement-construct into a raw buffer (no heap) and run");
        alignas(16) unsigned char buf[sizeof(MoogLadderAdapter)];
        auto* a = new (buf) MoogLadderAdapter();
        a->prepare(kSR); a->reset(); a->setCutoff(500.0f);
        const double low  = rms(*a, 200.0);
        a->reset(); a->setCutoff(500.0f);
        const double high = rms(*a, 5000.0);
        expect(std::isfinite(low) && std::isfinite(high), "non-finite output");
        expect(high < low * 0.5, "one-pole LP did not attenuate 5 kHz vs 200 Hz");
        a->~MoogLadderAdapter();
    }
};
static MoogPipelineTests moogPipelineTestsInstance;
