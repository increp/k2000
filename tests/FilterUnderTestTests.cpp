#include <juce_core/juce_core.h>
#include "characterization/FilterUnderTest.h"
#include "testdsp/SteppedSine.h"
#include <memory>

struct FilterUnderTestTests : public juce::UnitTest {
    FilterUnderTestTests() : juce::UnitTest("FilterUnderTest") {}
    void runTest() override {
        const double sr = 96000.0;

        beginTest("Moog FUT: supports LP/BP/HP, rejects Notch");
        {
            auto fut = chz::makeMoogFut();
            expect(fut->supports(chz::Mode::LP24));
            expect(fut->supports(chz::Mode::BP));
            expect(! fut->supports(chz::Mode::Notch), "Moog has no Notch");
            expect(fut->name() == "moog", "name is moog");
        }

        beginTest("Huggett FUT: supports Notch");
        {
            auto fut = chz::makeHuggettFut();
            expect(fut->supports(chz::Mode::Notch), "Huggett supports Notch");
            expect(fut->name() == "huggett");
        }

        beginTest("Moog LP24 passes bass, rejects treble (socket drives a real measurement)");
        {
            auto fut = chz::makeMoogFut();
            chz::OperatingPoint op; op.mode = chz::Mode::LP24; op.cutoffHz = 1000.0;
            op.resonance = 0.0; op.drive = 0.0; op.hostSampleRate = sr; op.osFactor = 1;
            fut->setOperatingPoint(op);
            auto r = testdsp::SteppedSine::transfer(*fut, { 100.0, 8000.0 }, sr, 0.05f);
            expect(r.magDb[0] > -3.0, "100 Hz near passband: " + juce::String(r.magDb[0], 1));
            expect(r.magDb[1] < -24.0, "8 kHz strongly attenuated: " + juce::String(r.magDb[1], 1));
        }
    }
};
static FilterUnderTestTests filterUnderTestTestsInstance;
