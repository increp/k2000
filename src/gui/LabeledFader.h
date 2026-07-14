#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// A vertical fader with its caption above and value box below -- the Wave
// Recipe control (reference mockup, VCO 1 row). Sibling of LabeledKnob: the
// owner binds slider() through ParamBinder; LabeledFader owns no attachment.
class LabeledFader : public juce::Component {
public:
    explicit LabeledFader(const juce::String& caption);

    juce::Slider& slider() { return slider_; }

    void resized() override;

private:
    static constexpr int captionH_ = 18;

    juce::Slider slider_{ juce::Slider::LinearVertical,
                          juce::Slider::TextBoxBelow };
    juce::Label  caption_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LabeledFader)
};
