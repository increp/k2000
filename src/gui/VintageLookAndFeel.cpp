#include "VintageLookAndFeel.h"
#include "BinaryData.h"
#include <cmath>
#include <vector>

const juce::Colour VintageLookAndFeel::windowBg      = juce::Colour::fromRGB(24, 23, 26);
const juce::Colour VintageLookAndFeel::creamPanel    = juce::Colour::fromRGB(214, 208, 194);
const juce::Colour VintageLookAndFeel::creamText     = juce::Colour::fromRGB(42, 38, 32);
const juce::Colour VintageLookAndFeel::charcoalPanel = juce::Colour::fromRGB(34, 33, 36);
const juce::Colour VintageLookAndFeel::charcoalWell  = juce::Colour::fromRGB(26, 25, 28);
const juce::Colour VintageLookAndFeel::panelEdge     = juce::Colour::fromRGB(20, 19, 22);
const juce::Colour VintageLookAndFeel::woodRail      = juce::Colour::fromRGB(107, 74, 50);
const juce::Colour VintageLookAndFeel::capText       = juce::Colour::fromRGB(226, 224, 218);
const juce::Colour VintageLookAndFeel::dimText       = juce::Colour::fromRGB(141, 138, 130);
const juce::Colour VintageLookAndFeel::brassTrim     = juce::Colour::fromRGB(183, 155, 94);
const juce::Colour VintageLookAndFeel::amberLed      = juce::Colour::fromRGB(232, 161, 60);
const juce::Colour VintageLookAndFeel::ledRed        = juce::Colour::fromRGB(216, 69, 44);

namespace {
// Per-pixel brightness jitter around a base colour — the "grain" for cream plates.
juce::Image makeSpeckle(juce::Colour base, int size, float amount, juce::int64 seed) {
    juce::Image img(juce::Image::RGB, size, size, false);
    juce::Random rng(seed);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            const float d = (rng.nextFloat() - 0.5f) * 2.0f * amount;
            img.setPixelAt(x, y, d >= 0.0f ? base.brighter(d) : base.darker(-d));
        }
    return img;
}

// Vertical grain for the side rails: smooth per-column tone wander + fine noise.
juce::Image makeWoodGrain(juce::Colour base, int w, int h, juce::int64 seed) {
    juce::Image img(juce::Image::RGB, w, h, false);
    juce::Random rng(seed);
    std::vector<float> colTone((size_t) w, 0.0f);
    float t = 0.0f;
    for (int x = 0; x < w; ++x) {
        t = 0.85f * t + 0.15f * (rng.nextFloat() - 0.5f);
        colTone[(size_t) x] = t * 0.5f + 0.08f * std::sin((float) x * 0.9f);
    }
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const float d = colTone[(size_t) x] + (rng.nextFloat() - 0.5f) * 0.06f;
            img.setPixelAt(x, y, d >= 0.0f ? base.brighter(d) : base.darker(-d));
        }
    return img;
}
} // namespace

juce::Typeface::Ptr VintageLookAndFeel::condensedTypeface() {
    static juce::Typeface::Ptr t = juce::Typeface::createSystemTypefaceFor(
        BinaryData::BarlowCondensedMedium_ttf, (size_t) BinaryData::BarlowCondensedMedium_ttfSize);
    return t;
}

juce::Font VintageLookAndFeel::condensedFont(float height) {
    return juce::Font(juce::FontOptions(condensedTypeface()).withHeight(height));
}

const juce::Image& VintageLookAndFeel::creamTexture() {
    static juce::Image img = makeSpeckle(creamPanel, 256, 0.045f, 0x5EED01);
    return img;
}

const juce::Image& VintageLookAndFeel::woodTexture() {
    static juce::Image img = makeWoodGrain(woodRail, 64, 512, 0x5EED02);
    return img;
}

VintageLookAndFeel::VintageLookAndFeel() {
    setColour(juce::ResizableWindow::backgroundColourId,       windowBg);
    setColour(juce::Label::textColourId,                       capText);
    setColour(juce::Slider::textBoxTextColourId,               capText);
    setColour(juce::Slider::textBoxBackgroundColourId,         charcoalWell);
    setColour(juce::Slider::textBoxOutlineColourId,            panelEdge);
    setColour(juce::ComboBox::backgroundColourId,              charcoalWell);
    setColour(juce::ComboBox::textColourId,                    capText);
    setColour(juce::ComboBox::outlineColourId,                 panelEdge);
    setColour(juce::ComboBox::arrowColourId,                   dimText);
    setColour(juce::PopupMenu::backgroundColourId,             charcoalPanel);
    setColour(juce::PopupMenu::textColourId,                   capText);
    setColour(juce::PopupMenu::highlightedBackgroundColourId,  brassTrim.withAlpha(0.35f));
    setColour(juce::PopupMenu::highlightedTextColourId,        juce::Colours::white);
    setColour(juce::ToggleButton::textColourId,                capText);
    setColour(juce::ToggleButton::tickColourId,                amberLed);
    setColour(juce::ToggleButton::tickDisabledColourId,        panelEdge);
    setColour(juce::TextButton::buttonColourId,                charcoalWell);
    setColour(juce::TextButton::textColourOffId,               capText);
}

juce::Font VintageLookAndFeel::getLabelFont(juce::Label& label) {
    // Floor small label text (value readouts, cell captions) at 16 logical px —
    // the canvas often renders below 1:1 on DPI-scaled screens, so tiny label
    // fonts drop under legibility (user acceptance feedback, 2026-07-10).
    return condensedFont(juce::jmax(16.0f, label.getFont().getHeight()));
}

void VintageLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                          float sliderPos, float startAngle, float endAngle,
                                          juce::Slider&) {
    const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(4.0f);
    const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    if (radius < 4.0f)
        return;
    const auto centre = bounds.getCentre();
    const float angle = startAngle + sliderPos * (endAngle - startAngle);
    const float bodyR = radius * 0.72f;

    // Tick ring along the sweep, outside the body (major tick every 5th).
    g.setColour(dimText.withAlpha(0.8f));
    for (int i = 0; i <= 10; ++i) {
        const float a = startAngle + (endAngle - startAngle) * (float) i / 10.0f;
        const juce::Point<float> outer(centre.x + std::sin(a) * radius,
                                       centre.y - std::cos(a) * radius);
        const juce::Point<float> inner(centre.x + std::sin(a) * (bodyR + 3.0f),
                                       centre.y - std::cos(a) * (bodyR + 3.0f));
        g.drawLine({ inner, outer }, (i % 5 == 0) ? 1.6f : 1.0f);
    }

    // Metallic rim.
    juce::ColourGradient rim(juce::Colour(0xFF8E8B84), centre.x - bodyR, centre.y - bodyR,
                             juce::Colour(0xFF2B2A27), centre.x + bodyR, centre.y + bodyR, false);
    g.setGradientFill(rim);
    g.fillEllipse(centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);

    // Dark body with a top-left sheen.
    const float innerR = bodyR - 2.5f;
    juce::ColourGradient sheen(juce::Colour(0xFF3A3936), centre.x - innerR * 0.6f, centre.y - innerR * 0.8f,
                               juce::Colour(0xFF141312), centre.x + innerR * 0.5f, centre.y + innerR, false);
    g.setGradientFill(sheen);
    g.fillEllipse(centre.x - innerR, centre.y - innerR, innerR * 2.0f, innerR * 2.0f);

    // Pointer.
    const juce::Point<float> tip(centre.x + std::sin(angle) * innerR * 0.92f,
                                 centre.y - std::cos(angle) * innerR * 0.92f);
    const juce::Point<float> tail(centre.x + std::sin(angle) * innerR * 0.35f,
                                  centre.y - std::cos(angle) * innerR * 0.35f);
    g.setColour(capText);
    g.drawLine({ tail, tip }, juce::jmax(2.0f, innerR * 0.10f));
}

// --- compact combo (carried over from SummitLookAndFeel, new palette) ---

static constexpr int kComboArrowZone = 18;

juce::Font VintageLookAndFeel::getComboBoxFont(juce::ComboBox& box) {
    return condensedFont(juce::jmin(15.0f, (float) box.getHeight() * 0.78f));
}

void VintageLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label) {
    label.setBounds(6, 1, box.getWidth() - (kComboArrowZone + 4), box.getHeight() - 2);
    label.setFont(getComboBoxFont(box));
}

void VintageLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool,
                                      int, int, int, int, juce::ComboBox& box) {
    const auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat();
    g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle(bounds, 3.0f);
    g.setColour(box.findColour(juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 3.0f, 1.0f);

    const juce::Rectangle<int> arrow(width - kComboArrowZone, 0, kComboArrowZone - 4, height);
    juce::Path p;
    p.startNewSubPath((float) arrow.getX() + 3.0f,     (float) arrow.getCentreY() - 2.0f);
    p.lineTo         ((float) arrow.getCentreX(),      (float) arrow.getCentreY() + 3.0f);
    p.lineTo         ((float) arrow.getRight() - 3.0f, (float) arrow.getCentreY() - 2.0f);
    g.setColour(box.findColour(juce::ComboBox::textColourId).withAlpha(box.isEnabled() ? 0.9f : 0.3f));
    g.strokePath(p, juce::PathStrokeType(2.0f));
}

// --- chassis primitives ---

void VintageLookAndFeel::fillCream(juce::Graphics& g, juce::Rectangle<int> area) {
    g.setTiledImageFill(creamTexture(), 0, 0, 1.0f);
    g.fillRect(area);
    juce::ColourGradient shade(juce::Colours::white.withAlpha(0.05f), 0.0f, (float) area.getY(),
                               juce::Colours::black.withAlpha(0.06f),  0.0f, (float) area.getBottom(),
                               false);
    g.setGradientFill(shade);
    g.fillRect(area);
}

void VintageLookAndFeel::fillWood(juce::Graphics& g, juce::Rectangle<int> area) {
    g.setTiledImageFill(woodTexture(), 0, 0, 1.0f);
    g.fillRect(area);
    g.setColour(juce::Colours::black.withAlpha(0.35f));
    g.drawRect(area, 1);
}

void VintageLookAndFeel::drawScrew(juce::Graphics& g, float cx, float cy, float r) {
    juce::ColourGradient grad(juce::Colour(0xFF9A9890), cx - r * 0.5f, cy - r * 0.5f,
                              juce::Colour(0xFF3E3C38), cx + r, cy + r, true);
    g.setGradientFill(grad);
    g.fillEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f);
    g.setColour(juce::Colour(0xFF23211E));
    g.drawEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f, 1.0f);
    // Slot, rotated pseudo-randomly by position so screws don't all align.
    const float rot = std::fmod(cx * 12.9898f + cy * 78.233f, juce::MathConstants<float>::pi);
    g.saveState();
    g.addTransform(juce::AffineTransform::rotation(rot, cx, cy));
    g.drawLine(cx - r * 0.55f, cy, cx + r * 0.55f, cy, 1.4f);
    g.restoreState();
}

void VintageLookAndFeel::drawRecessedWell(juce::Graphics& g, juce::Rectangle<float> r, float corner) {
    g.setColour(charcoalWell);
    g.fillRoundedRectangle(r, corner);
    g.setColour(juce::Colours::black.withAlpha(0.55f));
    g.drawRoundedRectangle(r.reduced(0.5f), corner, 1.0f);
    g.setColour(juce::Colours::white.withAlpha(0.05f));   // bottom lip catch-light
    g.drawLine(r.getX() + corner, r.getBottom() - 0.5f, r.getRight() - corner, r.getBottom() - 0.5f, 1.0f);
}
