#include "LabeledKnob.h"

LabeledKnob::LabeledKnob(const juce::String& caption) {
    slider_.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, 16);
    addAndMakeVisible(slider_);

    caption_.setText(caption, juce::dontSendNotification);
    caption_.setJustificationType(juce::Justification::centred);
    caption_.setFont(juce::Font(11.0f));
    addAndMakeVisible(caption_);
}

void LabeledKnob::setCaption(const juce::String& caption) {
    caption_.setText(caption, juce::dontSendNotification);
}

void LabeledKnob::resized() {
    auto area = getLocalBounds();
    caption_.setBounds(area.removeFromBottom(16));
    slider_.setBounds(area);
}
