#include <juce_audio_processors/juce_audio_processors.h>
#include "../src/params/Parameters.h"
#include "../src/dsp/AlgorithmLibrary.h"

// Algorithm display names carry a UTF-8 arrow (U+2192, bytes E2 86 92). They
// must reach the choice param / UI combo decoded as UTF-8, not byte-per-char
// ASCII (which renders "Filter â<box><box> Shaper"). See juce::String(const
// char*): it assumes ASCII and mangles >127 bytes.
class AlgorithmNameTests : public juce::UnitTest {
public:
    AlgorithmNameTests() : juce::UnitTest("AlgorithmName") {}

    void runTest() override {
        beginTest("algoNames() count matches the library");
        const juce::StringArray names = params::algoNames();
        expectEquals(names.size(), (int) AlgorithmLibrary::count());

        beginTest("entry 0 (filter_then_shaper) is now 'Shaper' — filter retired to spine");
        // Library entry 0 is filter_then_shaper; in v5 the filter moved to the
        // always-on spine, so the graph block is just "Shaper".
        expect(names[0] == "Shaper",
               "names[0] should be 'Shaper', got: " + names[0]);

        beginTest("no name contains a lone 0xE2 byte (the ASCII-decode artefact)");
        for (const auto& n : names)
            expect(! n.containsChar((juce::juce_wchar) 0xE2),
                   "name has a stray 0xE2 (â), got: " + n);
    }
};

static AlgorithmNameTests algorithmNameTestsInstance;
