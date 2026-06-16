#include "ParamBinder.h"

void ParamBinder::bind(juce::Slider& s, const juce::String& paramId) {
    auto& slot = sliders_[&s];   // default-constructs an empty unique_ptr on first bind
    slot.reset();  // MUST precede make_unique: the new attachment's ctor syncs while the old one is still live
    slot = std::make_unique<SliderAtt>(apvts_, paramId, s);
}

void ParamBinder::bind(juce::ComboBox& c, const juce::String& paramId) {
    auto& slot = combos_[&c];
    slot.reset();  // MUST precede make_unique: the new attachment's ctor syncs while the old one is still live
    slot = std::make_unique<ComboAtt>(apvts_, paramId, c);
}

void ParamBinder::bind(juce::Button& b, const juce::String& paramId) {
    auto& slot = buttons_[&b];
    slot.reset();  // MUST precede make_unique: the new attachment's ctor syncs while the old one is still live
    slot = std::make_unique<ButtonAtt>(apvts_, paramId, b);
}

void ParamBinder::clear() {
    sliders_.clear();
    combos_.clear();
    buttons_.clear();
}
