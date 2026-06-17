#include <juce_core/juce_core.h>
#include "../src/dsp/AlgorithmLibrary.h"

class AlgorithmLibraryTests : public juce::UnitTest {
public:
    AlgorithmLibraryTests() : juce::UnitTest("AlgorithmLibrary") {}

    void runTest() override {
        beginTest("library has the 4 algorithms; entry 0 is filter_then_shaper (shaper-only, spine carries filter)");
        expectEquals((int) AlgorithmLibrary::count(), 4);
        const Algorithm& a0 = AlgorithmLibrary::byIndex(0);
        expect(juce::String(a0.id) == "filter_then_shaper");
        expectEquals((int) a0.slotCount, 1);
        expect(a0.blockTypePerSlot[0] == BlockTypeId::Waveshaper);

        beginTest("ids are unique");
        for (std::size_t i = 0; i < AlgorithmLibrary::count(); ++i)
            for (std::size_t j = i + 1; j < AlgorithmLibrary::count(); ++j)
                expect(juce::String(AlgorithmLibrary::byIndex(i).id)
                       != juce::String(AlgorithmLibrary::byIndex(j).id));

        beginTest("every algorithm is well-formed: known types, no duplicate type, no SvfFilter (moved to spine)");
        for (std::size_t i = 0; i < AlgorithmLibrary::count(); ++i) {
            const Algorithm& a = AlgorithmLibrary::byIndex(i);
            expect(a.slotCount <= Algorithm::kMaxSlots);
            bool seen[kNumBlockTypes] = {false, false, false};
            for (std::size_t s = 0; s < a.slotCount; ++s) {
                const int t = (int) a.blockTypePerSlot[s];
                expect(t > 0 && t < (int) kNumBlockTypes, "block type in palette");
                expect(a.blockTypePerSlot[s] != BlockTypeId::SvfFilter,
                       "SvfFilter must not appear in any algorithm (v5: filter lives in the spine)");
                expect(!seen[t], "no duplicate block type within an algorithm");
                seen[t] = true;
            }
        }

        beginTest("thru algorithm is empty");
        const Algorithm& thru = AlgorithmLibrary::byIndex(AlgorithmLibrary::indexOfId("thru"));
        expectEquals((int) thru.slotCount, 0);
    }
};

static AlgorithmLibraryTests algorithmLibraryTestsInstance;
