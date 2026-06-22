#include <juce_core/juce_core.h>
#include "../src/dsp/spine/HuggettFilter.h"
#include "../src/dsp/spine/FilterModelLibrary.h"
#include "../src/dsp/spine/SpineState.h"
#include <vector>
#include <cstddef>
#include <cmath>

// Task 1: in-place state lifecycle equals the heap path, and every registered
// model fits the per-voice slot budget (Q18).
struct InPlaceStateTests : public juce::UnitTest {
    InPlaceStateTests() : juce::UnitTest("InPlaceState") {}
    static constexpr double kSR = 48000.0;

    static void runTone(const FilterModel& m, FilterModel::State& st, std::vector<float>& out) {
        for (int i = 0; i < (int) out.size(); ++i) {
            float l = 0.3f * (float) std::sin(2.0 * juce::MathConstants<double>::pi * 200.0 * i / kSR);
            float r = l;
            m.processStereo(st, &l, &r, 1);
            out[(size_t) i] = l;
        }
    }

    void runTest() override {
        beginTest("constructState (in-place) matches makeState (heap) sample-for-sample");
        HuggettFilter h; h.prepare(kSR);
        h.setMode(HuggettFilter::Mode::LP); h.setSlope(HuggettFilter::Slope::db24);
        h.setSeparation(0.0f); h.setCommon(1000.0f, 0.3f, 0.0f);

        std::unique_ptr<FilterModel::State> heap(h.makeState()); h.reset(*heap);
        std::vector<float> heapOut(4096), inplaceOut(4096);
        runTone(h, *heap, heapOut);

        alignas(kSpineStateAlign) std::byte buf[kMaxSpineStateBytes];
        FilterModel::State* st = h.constructState(buf); h.reset(*st);
        runTone(h, *st, inplaceOut);
        h.destroyState(st);

        bool identical = true;
        for (size_t i = 0; i < heapOut.size(); ++i) identical = identical && (heapOut[i] == inplaceOut[i]);
        expect(identical, "in-place output diverged from heap output");

        beginTest("every registered model fits kMaxSpineStateBytes (Q18)");
        for (std::size_t i = 0; i < FilterModelLibrary::count(); ++i) {
            auto m = FilterModelLibrary::create(i);
            expect(m->stateSize()  <= kMaxSpineStateBytes, "model " + juce::String((int) i) + " state too large");
            expect(m->stateAlign() <= kSpineStateAlign,    "model " + juce::String((int) i) + " over-aligned");
        }
    }
};
static InPlaceStateTests inPlaceStateTestsInstance;
