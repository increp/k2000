#include <juce_core/juce_core.h>

class SmokeTest : public juce::UnitTest {
public:
    SmokeTest() : juce::UnitTest("Smoke") {}
    void runTest() override {
        beginTest("arithmetic works");
        expect(1 + 1 == 2, "this should fail until we fix it");
    }
};

static SmokeTest smokeTestInstance;
