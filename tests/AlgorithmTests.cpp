#include <juce_core/juce_core.h>
#include "../src/dsp/Algorithm.h"

class AlgorithmTests : public juce::UnitTest {
public:
    AlgorithmTests() : juce::UnitTest("Algorithm") {}

    void runTest() override {
        beginTest("V1 fixed algorithm has 2 slots in expected order");
        const Algorithm a = Algorithm::v1Fixed();
        expectEquals((int) a.slotCount, 2);
        expect(a.blockTypePerSlot[0] == BlockTypeId::SvfFilter);
        expect(a.blockTypePerSlot[1] == BlockTypeId::Waveshaper);

        beginTest("Algorithm copy-constructs cleanly (passive data)");
        Algorithm b = a;
        expectEquals((int) b.slotCount, 2);
        expect(b.blockTypePerSlot[0] == BlockTypeId::SvfFilter);
        expect(b.blockTypePerSlot[1] == BlockTypeId::Waveshaper);
    }
};

static AlgorithmTests algorithmTestsInstance;
