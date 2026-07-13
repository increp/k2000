#include "LabeledFader.h"
#include "VintageLookAndFeel.h"

LabeledFader::LabeledFader(const juce::String& caption) {
    slider_.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 18);
    addAndMakeVisible(slider_);

    caption_.setText(caption, juce::dontSendNotification);
    caption_.setJustificationType(juce::Justification::centred);
    caption_.setFont(VintageLookAndFeel::condensedFont(14.0f));
    addAndMakeVisible(caption_);
}

void LabeledFader::resized() {
    auto area = getLocalBounds();
    caption_.setBounds(area.removeFromTop(captionH_));
    slider_.setBounds(area);
}
