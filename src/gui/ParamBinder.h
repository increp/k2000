#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>
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

    // Per-slider record: the attachment plus the text functions the CALLER had
    // installed before the slider's first bind (vfmt formatting). Snapshotting
    // at first bind is the only reliable signal — the attachment ctor
    // overwrites the slider's functions and its dtor does NOT clear them, so
    // the slider's current state is meaningless on later binds.
    struct SliderBinding {
        std::unique_ptr<SliderAtt> attachment;
        std::function<juce::String(double)> callerText;
        std::function<double(const juce::String&)> callerValue;
    };

    APVTS& apvts_;
    std::map<juce::Slider*, SliderBinding>                sliders_;
    std::map<juce::ComboBox*, std::unique_ptr<ComboAtt>>  combos_;
    std::map<juce::Button*,   std::unique_ptr<ButtonAtt>> buttons_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParamBinder)
};
