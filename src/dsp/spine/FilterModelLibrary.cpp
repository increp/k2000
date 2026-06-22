#include "FilterModelLibrary.h"
#include "HuggettFilter.h"
#include "../../util/Utf8.h"
#include "SpineState.h"
// Q18 governance: every registered model's State must fit the per-voice slot.
static_assert(sizeof(HuggettFilter::VoiceState)  <= kMaxSpineStateBytes,
              "HuggettFilter::VoiceState exceeds kMaxSpineStateBytes — bump it (Q18) or slim the model");
static_assert(alignof(HuggettFilter::VoiceState) <= kSpineStateAlign,
              "HuggettFilter::VoiceState over-aligned for the spine slot");

namespace {
struct Entry {
    const char* id;
    const char* displayName;
    std::unique_ptr<FilterModel> (*make)();
};
const Entry kEntries[] = {
    { "huggett", "Huggett", []() -> std::unique_ptr<FilterModel> { return std::make_unique<HuggettFilter>(); } },
};
}  // namespace

namespace FilterModelLibrary {
std::size_t count() { return std::size(kEntries); }

juce::String id(std::size_t i) {
    return i < count() ? juce::String(kEntries[i].id) : juce::String(kEntries[0].id);
}

juce::StringArray names() {
    juce::StringArray s;
    for (const auto& e : kEntries) s.add(util::u8(e.displayName));
    return s;
}

std::unique_ptr<FilterModel> create(std::size_t i) {
    return kEntries[i < count() ? i : 0].make();
}
}  // namespace FilterModelLibrary
