#include <juce_core/juce_core.h>
#include "../src/dsp/Envelope.h"

class EnvelopeTest : public juce::UnitTest {
public:
    EnvelopeTest() : juce::UnitTest("Envelope") {}

    static constexpr double SR = 48000.0;

    void runTest() override {
        beginTest("starts at zero and is idle");
        Envelope e;
        e.prepare(SR);
        e.setParameters(0.01f, 0.1f, 0.5f, 0.2f);
        expectWithinAbsoluteError(e.nextSample(), 0.0f, 1e-6f);
        expect(!e.isActive());

        beginTest("attack ramps from 0 to ~1 over the attack time");
        e.reset();
        e.noteOn();
        // After ~attack samples we should be near 1.0
        int attackSamples = int(0.01 * SR);
        for (int i = 0; i < attackSamples; ++i) e.nextSample();
        float v = e.nextSample();
        expect(v > 0.9f, juce::String("expected near 1 after attack, got ") + juce::String(v));

        beginTest("decay falls toward sustain");
        // Run long enough for decay to settle
        for (int i = 0; i < int(SR); ++i) e.nextSample();
        float settled = e.nextSample();
        expectWithinAbsoluteError(settled, 0.5f, 0.05f);

        beginTest("noteOff drops to zero by end of release");
        e.noteOff();
        int releaseSamples = int(0.2 * SR);
        for (int i = 0; i < releaseSamples * 3; ++i) e.nextSample();
        expectWithinAbsoluteError(e.nextSample(), 0.0f, 1e-3f);
        expect(!e.isActive());

        beginTest("reset clears state");
        e.noteOn();
        e.nextSample();
        e.reset();
        expectWithinAbsoluteError(e.nextSample(), 0.0f, 1e-6f);
        expect(!e.isActive());
    }
};

static EnvelopeTest envelopeTestInstance;
