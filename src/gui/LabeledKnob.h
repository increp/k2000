#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// A rotary slider with a caption beneath it — the atomic Summit control.
// The owner binds slider() through ParamBinder; LabeledKnob owns no attachment.
class LabeledKnob : public juce::Component {
public:
    explicit LabeledKnob(const juce::String& caption);

    juce::Slider& slider() { return slider_; }
    void setCaption(const juce::String& caption);

    void resized() override;

private:
    juce::Slider slider_{ juce::Slider::RotaryHorizontalVerticalDrag,
                          juce::Slider::TextBoxBelow };
    juce::Label  caption_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LabeledKnob)
};
