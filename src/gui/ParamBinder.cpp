#include "ParamBinder.h"

void ParamBinder::bind(juce::Slider& s, const juce::String& paramId) {
    auto& slot = sliders_[&s];   // default-constructs an empty unique_ptr on first bind
    slot.reset();  // MUST precede make_unique: the new attachment's ctor syncs while the old one is still live

    // SliderAtt's ctor unconditionally overwrites s.textFromValueFunction /
    // s.valueFromTextFunction with the parameter's own (see
    // juce_ParameterAttachments.cpp). Caller-installed display formatting
    // (e.g. vfmt::apply's instrument-style "100%" / "0 st" / "0 ct" text) must
    // survive binding and every future re-bind, so save it before attaching
    // and restore it after.
    auto savedTextFromValue = s.textFromValueFunction;
    auto savedValueFromText = s.valueFromTextFunction;

    slot = std::make_unique<SliderAtt>(apvts_, paramId, s);

    if (savedTextFromValue || savedValueFromText) {
        s.textFromValueFunction = std::move(savedTextFromValue);
        s.valueFromTextFunction = std::move(savedValueFromText);
        s.updateText();
    }
    // else: slider never had custom formatting (e.g. Key/Vel) -- leave the
    // attachment-installed parameter text functions in place, unchanged.
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
