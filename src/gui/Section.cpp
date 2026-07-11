#include "Section.h"
#include "VintageLookAndFeel.h"

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
    const float alpha = reserved_ ? 0.80f : 1.0f;   // reserved reads as a panel, not a hole

    // Recessed charcoal panel: fill, dark edge, faint embossed top lip.
    VintageLookAndFeel::fillModulePanel(g, bounds, 5.0f, alpha);
    g.setColour(VintageLookAndFeel::panelEdge.withMultipliedAlpha(alpha));
    g.drawRoundedRectangle(bounds, 5.0f, 1.5f);
    g.setColour(juce::Colours::white.withAlpha(0.04f * alpha));
    g.drawLine(bounds.getX() + 6.0f, bounds.getY() + 2.0f,
               bounds.getRight() - 6.0f, bounds.getY() + 2.0f, 1.0f);

    // Corner screws (small — panel-level, not chassis-level).
    if (getWidth() > 70 && getHeight() > 40) {
        const float r = 3.5f, inset = 8.0f;
        VintageLookAndFeel::drawScrew(g, bounds.getX() + inset,     bounds.getY() + inset,      r);
        VintageLookAndFeel::drawScrew(g, bounds.getRight() - inset, bounds.getY() + inset,      r);
        VintageLookAndFeel::drawScrew(g, bounds.getX() + inset,     bounds.getBottom() - inset, r);
        VintageLookAndFeel::drawScrew(g, bounds.getRight() - inset, bounds.getBottom() - inset, r);
    }

    // Title strip; spine sections get the brass underline (the re-expressed
    // constant-Summit-spine accent -- see the reskin spec, Sec 2).
    // Titles sit on a busy photographic leather texture now -- a 1px dark
    // shadow keeps them legible, and reserved titles use bright ink (dimText
    // simply vanished into the crinkle).
    g.setFont(VintageLookAndFeel::condensedFont(17.0f));
    auto titleStrip = getLocalBounds().reduced(16, 6).removeFromTop(titleH_);
    g.setColour(juce::Colours::black.withAlpha(0.65f * alpha));
    g.drawText(title_.toUpperCase(), titleStrip.translated(1, 1), juce::Justification::topLeft);
    g.setColour(VintageLookAndFeel::capText.withMultipliedAlpha(reserved_ ? 0.78f : 1.0f));
    g.drawText(title_.toUpperCase(), titleStrip, juce::Justification::topLeft);
    if (spine_) {
        g.setColour(VintageLookAndFeel::brassTrim.withMultipliedAlpha(alpha));
        const int w = juce::jmin(titleStrip.getWidth(), 8 * title_.length());
        g.fillRect(titleStrip.getX(), titleStrip.getY() + titleH_ - 3, w, 2);
    }
}
