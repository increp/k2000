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
        const juce::juce_wchar rightArrow = 0x2192;  // →

        beginTest("algoNames() count matches the library");
        const juce::StringArray names = params::algoNames();
        expectEquals(names.size(), (int) AlgorithmLibrary::count());

        beginTest("the arrow in 'Filter -> Shaper' is decoded as UTF-8 U+2192");
        // Library entry 0 is filter_then_shaper (see AlgorithmLibraryTests).
        expect(names[0].containsChar(rightArrow),
               "names[0] should contain U+2192, got: " + names[0]);
        expect(names[0] == juce::String(juce::CharPointer_UTF8("Filter \xE2\x86\x92 Shaper")),
               "names[0] mismatch, got: " + names[0]);

        beginTest("no name contains a lone 0xE2 byte (the ASCII-decode artefact)");
        for (const auto& n : names)
            expect(! n.containsChar((juce::juce_wchar) 0xE2),
                   "name has a stray 0xE2 (â), got: " + n);
    }
};

static AlgorithmNameTests algorithmNameTestsInstance;
