#include "LabeledKnob.h"
#include "VintageLookAndFeel.h"

LabeledKnob::LabeledKnob(const juce::String& caption) {
    slider_.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, captionH_);
    addAndMakeVisible(slider_);

    caption_.setText(caption, juce::dontSendNotification);
    caption_.setJustificationType(juce::Justification::centred);
    caption_.setFont(VintageLookAndFeel::condensedFont(11.0f));
    addAndMakeVisible(caption_);
}

void LabeledKnob::setCaption(const juce::String& caption) {
    caption_.setText(caption, juce::dontSendNotification);
}

void LabeledKnob::setCaptionColour(juce::Colour c) {
    caption_.setColour(juce::Label::textColourId, c);
}

void LabeledKnob::resized() {
    auto area = getLocalBounds();
    caption_.setBounds(area.removeFromBottom(captionH_));
    slider_.setBounds(area);
}
