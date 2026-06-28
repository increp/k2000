#include <juce_core/juce_core.h>
#include "../src/dsp/spine/cmajor/MoogLadderAdapter.h"

// Test that the generated MoogLadder codegen max-frequency cap has been raised to
// support oversampled rates (8x@48k = 384kHz, 8x@96k = 768kHz).
// We access the cap via MoogLadderAdapter::getMaxFrequency() to avoid including the
// generated header directly (it defines a class named MoogLadder in the global
// namespace, which collides with src/dsp/spine/MoogLadder.h in the same binary).
class MoogMaxFreqTests : public juce::UnitTest {
public:
    MoogMaxFreqTests() : juce::UnitTest("MoogMaxFreq") {}
    void runTest() override {
        beginTest("Moog codegen max-frequency cap raised for oversampling");
        {
            MoogLadderAdapter adapter;
            const double maxF = adapter.getMaxFrequency();
            expect(maxF >= 768000.0,
                   "max frequency must allow >= 8x at 96k (got " + juce::String(maxF) + ")");
            // prepare() at 8x@48k = 384 kHz must not jassert or throw
            adapter.prepare(384000.0);
            expect(true, "prepare(384000) completed without assertion");
        }
    }
};
static MoogMaxFreqTests moogMaxFreqTestsInstance;
