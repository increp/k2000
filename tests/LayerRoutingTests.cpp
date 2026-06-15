#include <juce_core/juce_core.h>
#include "../src/LayerRouting.h"

class LayerRoutingTests : public juce::UnitTest {
public:
    LayerRoutingTests() : juce::UnitTest("LayerRouting") {}

    void runTest() override {
        beginTest("disabled layer never matches");
        {
            LayerRouting r;  // defaults: enabled=false in this test we set true below
            r.enable = false;
            expect(!r.matches(60, 100, 1));
        }

        beginTest("full-range enabled layer matches anything on its channel");
        {
            LayerRouting r;
            r.enable = true; r.keyLo = 0; r.keyHi = 127; r.velLo = 1; r.velHi = 127;
            r.channel = 0;  // Omni
            expect(r.matches(0, 1, 1));
            expect(r.matches(127, 127, 16));
        }

        beginTest("key range gates (split)");
        {
            LayerRouting r; r.enable = true; r.velLo = 1; r.velHi = 127; r.channel = 0;
            r.keyLo = 60; r.keyHi = 127;
            expect(!r.matches(59, 100, 1));
            expect(r.matches(60, 100, 1));
        }

        beginTest("velocity range gates");
        {
            LayerRouting r; r.enable = true; r.keyLo = 0; r.keyHi = 127; r.channel = 0;
            r.velLo = 64; r.velHi = 127;
            expect(!r.matches(60, 63, 1));
            expect(r.matches(60, 64, 1));
        }

        beginTest("channel filter: specific channel only matches that channel");
        {
            LayerRouting r; r.enable = true; r.keyLo = 0; r.keyHi = 127; r.velLo = 1; r.velHi = 127;
            r.channel = 2;  // channel 2 only
            expect(r.matches(60, 100, 2));
            expect(!r.matches(60, 100, 1));
        }
    }
};

static LayerRoutingTests layerRoutingTestsInstance;
