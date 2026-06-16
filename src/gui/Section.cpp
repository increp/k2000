#include "Section.h"
#include "SummitLookAndFeel.h"

Section::Section(const juce::String& title, bool spine, bool reserved)
    : title_(title), spine_(spine), reserved_(reserved) {}

void Section::setReserved(bool reserved) {
    if (reserved_ != reserved) {
        reserved_ = reserved;
        repaint();
    }
}

juce::Rectangle<int> Section::contentBounds() const {
    return getLocalBounds().reduced(6).withTrimmedTop(titleH_);
}

void Section::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat().reduced(1.0f);
    const float alpha = reserved_ ? 0.40f : 1.0f;

    g.setColour(SummitLookAndFeel::moduleBg.withMultipliedAlpha(alpha));
    g.fillRoundedRectangle(bounds, 4.0f);

    const auto edge = (spine_ ? SummitLookAndFeel::spineEdge
                              : SummitLookAndFeel::moduleEdge).withMultipliedAlpha(alpha);
    g.setColour(edge);
    g.drawRoundedRectangle(bounds, 4.0f, 1.0f);

    g.setColour((reserved_ ? SummitLookAndFeel::textDim
                           : SummitLookAndFeel::textBright).withMultipliedAlpha(alpha));
    g.setFont(juce::Font(11.0f, juce::Font::bold));
    g.drawText(title_.toUpperCase(),
               getLocalBounds().reduced(6).removeFromTop(titleH_),
               juce::Justification::topLeft);
}
