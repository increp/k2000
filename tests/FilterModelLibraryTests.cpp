#include <juce_core/juce_core.h>
#include "../src/dsp/spine/FilterModelLibrary.h"
#include "../src/dsp/spine/MoogLadder.h"

class FilterModelLibraryTests : public juce::UnitTest {
public:
    FilterModelLibraryTests() : juce::UnitTest("FilterModelLibrary") {}
    void runTest() override {
        beginTest("entry 0 is Huggett and is stable");
        expect(FilterModelLibrary::count() >= 1);
        expect(FilterModelLibrary::id(0) == juce::String("huggett"));

        beginTest("names() count matches and is non-empty");
        const auto names = FilterModelLibrary::names();
        expectEquals(names.size(), (int) FilterModelLibrary::count());
        expect(names[0].isNotEmpty());

        beginTest("create() returns a usable model");
        auto m = FilterModelLibrary::create(0);
        expect(m != nullptr);

        beginTest("Moog is registered as the second model");
        {
            expect(FilterModelLibrary::count() == 2, "expected 2 models, got " + juce::String((int) FilterModelLibrary::count()));
            expect(FilterModelLibrary::names().contains("Moog"), "names() missing Moog");
            expect(FilterModelLibrary::id(1) == "moog", "id(1) != moog");
            auto m = FilterModelLibrary::create(1);
            expect(dynamic_cast<MoogLadder*>(m.get()) != nullptr, "create(1) is not a MoogLadder");
        }
    }
};
static FilterModelLibraryTests filterModelLibraryTestsInstance;
