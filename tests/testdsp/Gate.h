#pragma once
#include <juce_core/juce_core.h>

namespace testdsp {
struct Gate {
    enum class Dir { Max, Min };

    template <class UT>
    static void check(UT& t, double measured, double threshold, Dir dir, const juce::String& label) {
        const bool pass = (dir == Dir::Max) ? (measured <= threshold) : (measured >= threshold);
        const juce::String op = (dir == Dir::Max) ? " <= " : " >= ";
        t.expect(pass, label + ": " + juce::String(measured, 3) + op + "gate " + juce::String(threshold, 3));
    }
};
} // namespace testdsp
