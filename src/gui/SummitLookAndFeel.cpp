#include "SummitLookAndFeel.h"

const juce::Colour SummitLookAndFeel::panelBg    = juce::Colour::fromRGB(28, 28, 32);
const juce::Colour SummitLookAndFeel::moduleBg   = juce::Colour::fromRGB(38, 38, 44);
const juce::Colour SummitLookAndFeel::moduleEdge = juce::Colour::fromRGB(67, 67, 77);
const juce::Colour SummitLookAndFeel::spineEdge  = juce::Colour::fromRGB(91, 108, 255);
const juce::Colour SummitLookAndFeel::knobBody   = juce::Colour::fromRGB(51, 51, 59);
const juce::Colour SummitLookAndFeel::knobRing   = juce::Colour::fromRGB(174, 184, 255);
const juce::Colour SummitLookAndFeel::textBright = juce::Colour::fromRGB(207, 210, 218);
const juce::Colour SummitLookAndFeel::textDim    = juce::Colour::fromRGB(120, 124, 134);

SummitLookAndFeel::SummitLookAndFeel() {
    setColour(juce::ResizableWindow::backgroundColourId, panelBg);
    setColour(juce::Slider::textBoxTextColourId,        textBright);
    setColour(juce::Slider::textBoxOutlineColourId,     juce::Colours::transparentBlack);
    setColour(juce::ComboBox::backgroundColourId,       knobBody);
    setColour(juce::ComboBox::textColourId,             textBright);
    setColour(juce::ComboBox::outlineColourId,          moduleEdge);
    setColour(juce::Label::textColourId,                textBright);
    setColour(juce::ToggleButton::tickColourId,         knobRing);
    setColour(juce::ToggleButton::tickDisabledColourId, moduleEdge);
}

void SummitLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPos, float startAngle, float endAngle,
                                         juce::Slider&) {
    const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(4.0f);
    const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    if (radius < 2.0f)
        return;  // too small to draw anything meaningful (avoids negative-size fillEllipse)
    const auto centre = bounds.getCentre();
    const float angle = startAngle + sliderPos * (endAngle - startAngle);

    // Body
    g.setColour(knobBody);
    g.fillEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);

    // Track arc (full sweep, dim) + value arc (to current pos, bright)
    const float lineW = juce::jmax(2.0f, radius * 0.12f);
    const float arcR = radius - lineW;
    juce::Path track, value;
    track.addCentredArc(centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    value.addCentredArc(centre.x, centre.y, arcR, arcR, 0.0f, startAngle, angle, true);
    g.setColour(moduleEdge);
    g.strokePath(track, juce::PathStrokeType(lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(knobRing);
    g.strokePath(value, juce::PathStrokeType(lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Pointer
    juce::Point<float> tip(centre.x + std::sin(angle) * (radius - lineW),
                           centre.y - std::cos(angle) * (radius - lineW));
    g.setColour(textBright);
    g.drawLine({ centre, tip }, juce::jmax(1.5f, radius * 0.08f));
}

// A combo's arrow gutter, narrower than the V4 default's 30 px so compact cells
// keep room for their text.
static constexpr int kComboArrowZone = 18;

juce::Font SummitLookAndFeel::getComboBoxFont(juce::ComboBox& box) {
    return juce::Font(juce::FontOptions(juce::jmin(13.0f, (float) box.getHeight() * 0.75f)));
}

void SummitLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label) {
    label.setBounds(6, 1, box.getWidth() - (kComboArrowZone + 4), box.getHeight() - 2);
    label.setFont(getComboBoxFont(box));
}

void SummitLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool,
                                     int, int, int, int, juce::ComboBox& box) {
    const auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat();
    g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle(bounds, 3.0f);
    g.setColour(box.findColour(juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 3.0f, 1.0f);

    juce::Rectangle<int> arrow(width - kComboArrowZone, 0, kComboArrowZone - 4, height);
    juce::Path p;
    p.startNewSubPath((float) arrow.getX() + 3.0f,    (float) arrow.getCentreY() - 2.0f);
    p.lineTo         ((float) arrow.getCentreX(),     (float) arrow.getCentreY() + 3.0f);
    p.lineTo         ((float) arrow.getRight() - 3.0f, (float) arrow.getCentreY() - 2.0f);
    g.setColour(box.findColour(juce::ComboBox::textColourId).withAlpha(box.isEnabled() ? 0.9f : 0.3f));
    g.strokePath(p, juce::PathStrokeType(2.0f));
}
