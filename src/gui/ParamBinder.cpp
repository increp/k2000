#include "ParamBinder.h"

void ParamBinder::bind(juce::Slider& s, const juce::String& paramId) {
    auto it = sliders_.find(&s);
    if (it == sliders_.end()) {
        // First bind: whatever functions the slider carries now are
        // caller-installed — no attachment has touched this slider yet.
        SliderBinding b;
        b.callerText  = s.textFromValueFunction;
        b.callerValue = s.valueFromTextFunction;
        it = sliders_.emplace(&s, std::move(b)).first;
    }

    it->second.attachment.reset();   // detach BEFORE rebind (see class comment)
    it->second.attachment = std::make_unique<SliderAtt>(apvts_, paramId, s);

    // The attachment ctor installed the parameter's text functions. If the
    // caller had its own (vfmt), reinstate those; otherwise keep the
    // parameter's — fresh from THIS bind, never a stale earlier one.
    if (it->second.callerText != nullptr || it->second.callerValue != nullptr) {
        s.textFromValueFunction = it->second.callerText;
        s.valueFromTextFunction = it->second.callerValue;
        s.updateText();
    }
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
    // Restore each slider's caller-installed text functions (null included)
    // before dropping the bindings: attachment dtors never clear the
    // functions they installed, so a slider re-bound after clear() would
    // otherwise snapshot stale attachment lambdas as "caller-installed".
    for (auto& [slider, binding] : sliders_) {
        slider->textFromValueFunction = binding.callerText;
        slider->valueFromTextFunction = binding.callerValue;
    }
    sliders_.clear();
    combos_.clear();
    buttons_.clear();
}
