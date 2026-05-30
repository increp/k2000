#include <juce_core/juce_core.h>

class SmokeTest : public juce::UnitTest {
public:
    SmokeTest() : juce::UnitTest("Smoke") {}
    void runTest() override {
        beginTest("test harness is wired");
        expect(1 + 1 == 2);
    }
};

static SmokeTest smokeTestInstance;
