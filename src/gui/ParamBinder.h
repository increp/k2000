#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <map>
#include <memory>

// Owns APVTS attachments for a set of controls, keyed by the control pointer.
//
// Contract: rebinding a control that is already bound destroys its previous
// attachment BEFORE constructing the new one. Otherwise the new attachment's
// initial param->control sync moves the control while the old attachment is
// still listening, and the old attachment writes that value back into the
// parameter you just left. (This is exactly the v4 layer-switch corruption.)
class ParamBinder {
public:
    explicit ParamBinder(juce::AudioProcessorValueTreeState& apvts) : apvts_(apvts) {}

    void bind(juce::Slider& s,   const juce::String& paramId);
    void bind(juce::ComboBox& c, const juce::String& paramId);
    void bind(juce::Button& b,   const juce::String& paramId);

    // Detach every attachment this binder owns.
    void clear();

private:
    using APVTS     = juce::AudioProcessorValueTreeState;
    using SliderAtt = APVTS::SliderAttachment;
    using ComboAtt  = APVTS::ComboBoxAttachment;
    using ButtonAtt = APVTS::ButtonAttachment;

    APVTS& apvts_;
    std::map<juce::Slider*,   std::unique_ptr<SliderAtt>> sliders_;
    std::map<juce::ComboBox*, std::unique_ptr<ComboAtt>>  combos_;
    std::map<juce::Button*,   std::unique_ptr<ButtonAtt>> buttons_;
};
