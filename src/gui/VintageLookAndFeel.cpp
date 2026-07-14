#include "VintageLookAndFeel.h"
#include "BinaryData.h"
#include <cmath>
#include <vector>

const juce::Colour VintageLookAndFeel::windowBg      = juce::Colour::fromRGB(24, 23, 26);
const juce::Colour VintageLookAndFeel::creamPanel    = juce::Colour::fromRGB(196, 198, 201);  // brushed aluminum
const juce::Colour VintageLookAndFeel::creamText     = juce::Colour::fromRGB(34, 36, 40);
const juce::Colour VintageLookAndFeel::charcoalPanel = juce::Colour::fromRGB(34, 33, 36);
const juce::Colour VintageLookAndFeel::charcoalWell  = juce::Colour::fromRGB(26, 25, 28);
const juce::Colour VintageLookAndFeel::panelEdge     = juce::Colour::fromRGB(20, 19, 22);
const juce::Colour VintageLookAndFeel::woodRail      = juce::Colour::fromRGB(122, 60, 40);  // rich redwood
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
        t = 0.80f * t + 0.20f * (rng.nextFloat() - 0.5f);
        // strong ropy grain: fast streaks + slow dark figure bands
        colTone[(size_t) x] = t * 0.9f
                            + 0.14f * std::sin((float) x * 0.9f)
                            + 0.10f * std::sin((float) x * 0.23f + 1.7f);
    }
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            // slight along-grain wobble so streaks aren't ruler-straight
            const float wob = 0.05f * std::sin((float) y * 0.045f + (float) x * 0.6f);
            const float d = colTone[(size_t) x] + wob + (rng.nextFloat() - 0.5f) * 0.05f;
            img.setPixelAt(x, y, d >= 0.0f ? base.brighter(d) : base.darker(-d));
        }
    return img;
}
} // namespace

namespace {
juce::Image loadAsset(const char* data, int size) {
    return juce::ImageFileFormat::loadFrom(data, (size_t) size);
}
// Draw a crop of `src` covering `dest` at matching aspect, band chosen by `seed`
// so different plates/panels/rails sample different regions of the photograph.
void drawPhotoCrop(juce::Graphics& g, const juce::Image& src,
                   juce::Rectangle<float> dest, juce::uint32 seed) {
    const float destAR = dest.getWidth() / juce::jmax(1.0f, dest.getHeight());
    float sw = (float) src.getWidth(), sh = (float) src.getHeight();
    float cw = sw, ch = sh;
    if (cw / ch > destAR) cw = ch * destAR; else ch = cw / destAR;
    const float maxX = sw - cw, maxY = sh - ch;
    const float fx = (float) ((seed * 2654435761u) % 1000u) / 1000.0f;
    const float fy = (float) ((seed * 40503u + 12345u) % 1000u) / 1000.0f;
    g.drawImage(src, dest.getX(), dest.getY(), (int) dest.getWidth(), (int) dest.getHeight(),
                (int) (fx * maxX), (int) (fy * maxY), (int) cw, (int) ch);
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

namespace {
// Horizontal brushed-metal: smooth per-row tone wander (the brushing streaks)
// plus faint per-pixel grit. Rows, not columns -- plates are brushed lengthwise.
juce::Image makeBrushedMetal(juce::Colour base, int w, int h, juce::int64 seed) {
    juce::Image img(juce::Image::RGB, w, h, false);
    juce::Random rng(seed);

    // low-frequency patina blotches: coarse value grid, bilinearly upsampled
    constexpr int bw = 16, bh = 4;
    float blotch[bh + 1][bw + 1];
    for (int by = 0; by <= bh; ++by)
        for (int bx = 0; bx <= bw; ++bx)
            blotch[by][bx] = (rng.nextFloat() - 0.5f) * 0.055f;

    float t = 0.0f;
    for (int y = 0; y < h; ++y) {
        // long-correlated row tone = the lengthwise brushing
        t = 0.86f * t + 0.14f * (rng.nextFloat() - 0.5f);
        const float rowTone = t * 0.20f;
        const float fy = (float) y / (float) h * bh;
        const int   iy = juce::jmin((int) fy, bh - 1);
        const float ry = fy - (float) iy;
        for (int x = 0; x < w; ++x) {
            const float fx = (float) x / (float) w * bw;
            const int   ix = juce::jmin((int) fx, bw - 1);
            const float rx = fx - (float) ix;
            const float pat = juce::jmap(ry,
                juce::jmap(rx, blotch[iy][ix],     blotch[iy][ix + 1]),
                juce::jmap(rx, blotch[iy + 1][ix], blotch[iy + 1][ix + 1]));
            const float d = rowTone + pat + (rng.nextFloat() - 0.5f) * 0.045f;
            img.setPixelAt(x, y, d >= 0.0f ? base.brighter(d) : base.darker(-d));
        }
    }

    // sparse horizontal micro-scratches (bright and dark hairlines)
    for (int i = 0; i < 26; ++i) {
        const int y = rng.nextInt(h);
        const int x0 = rng.nextInt(w);
        const int len = 12 + rng.nextInt(90);
        const float d = (rng.nextBool() ? 1.0f : -1.0f) * (0.06f + rng.nextFloat() * 0.10f);
        for (int x = x0; x < juce::jmin(w, x0 + len); ++x) {
            auto c = img.getPixelAt(x, y);
            img.setPixelAt(x, y, d >= 0.0f ? c.brighter(d) : c.darker(-d));
        }
    }
    // micro-dings: tiny dark pits with a bright lower lip
    for (int i = 0; i < 18; ++i) {
        const int x = rng.nextInt(w), y = rng.nextInt(h - 2);
        img.setPixelAt(x, y,     img.getPixelAt(x, y).darker(0.35f));
        img.setPixelAt(x, y + 1, img.getPixelAt(x, y + 1).brighter(0.25f));
    }
    return img;
}
} // namespace

const juce::Image& VintageLookAndFeel::creamTexture() {
    static juce::Image img = [] {
        auto photo = loadAsset(BinaryData::PlateAluminum_jpg, BinaryData::PlateAluminum_jpgSize);
        return photo.isValid() ? photo : makeBrushedMetal(creamPanel, 512, 128, 0x5EED01);
    }();
    return img;
}

const juce::Image& VintageLookAndFeel::panelTexture() {
    static juce::Image img = [] {
        auto photo = loadAsset(BinaryData::PanelBlack_jpg, BinaryData::PanelBlack_jpgSize);
        return photo.isValid() ? photo : makeSpeckle(charcoalPanel, 128, 0.055f, 0x5EED03);
    }();
    return img;
}

const juce::Image& VintageLookAndFeel::woodTexture() {
    // Photographic rosewood (CC0, assets/textures/RosewoodVeneer.jpg), darkened
    // toward the palette's deep redwood. Rails are vertical, so if the photo's
    // grain runs horizontally we rotate it 90 degrees at load (checked visually).
    static juce::Image img = [] {
        auto photo = loadAsset(BinaryData::RailRedwood_jpg, BinaryData::RailRedwood_jpgSize);
        return photo.isValid() ? photo : makeWoodGrain(woodRail, 64, 512, 0x5EED02);
    }();
    return img;
}

VintageLookAndFeel::VintageLookAndFeel() {
    setColour(juce::ResizableWindow::backgroundColourId,       windowBg);
    setColour(juce::Label::textColourId,                       capText);
    setColour(juce::Slider::textBoxTextColourId,               capText);
    setColour(juce::Slider::rotarySliderOutlineColourId,       juce::Colour::fromRGB(178, 175, 167));
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
                                          juce::Slider& slider) {
    const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(4.0f);
    const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    if (radius < 4.0f)
        return;
    const auto centre = bounds.getCentre();
    const float angle = startAngle + sliderPos * (endAngle - startAngle);
    const float bodyR = radius * 0.80f;

    // Tick ring along the sweep, outside the body (major tick every 5th).
    // Colour is per-slider so knobs on aluminum plates can use dark ticks.
    g.setColour(slider.findColour(juce::Slider::rotarySliderOutlineColourId).withAlpha(0.8f));
    for (int i = 0; i <= 10; ++i) {
        const float a = startAngle + (endAngle - startAngle) * (float) i / 10.0f;
        const juce::Point<float> outer(centre.x + std::sin(a) * radius,
                                       centre.y - std::cos(a) * radius);
        const juce::Point<float> inner(centre.x + std::sin(a) * (bodyR + 3.0f),
                                       centre.y - std::cos(a) * (bodyR + 3.0f));
        g.drawLine({ inner, outer }, (i % 5 == 0) ? 1.6f : 1.0f);
    }

    static juce::Image sprite = loadAsset(BinaryData::KnobBlack_png, BinaryData::KnobBlack_pngSize);
    if (sprite.isValid()) {
        // photographic knob body (static -- lighting stays put), live pointer on top.
        // Dark sprite on dark leather needs separation: a grounded shadow below
        // and a thin top catch-light halo around the skirt.
        g.setColour(juce::Colours::black.withAlpha(0.55f));
        g.fillEllipse(centre.x - bodyR * 1.04f, centre.y - bodyR * 0.90f + 3.0f,
                      bodyR * 2.08f, bodyR * 2.0f);
        g.drawImage(sprite, juce::Rectangle<float>(centre.x - bodyR, centre.y - bodyR,
                                                   bodyR * 2.0f, bodyR * 2.0f),
                    juce::RectanglePlacement::centred);
        juce::Path halo;
        halo.addCentredArc(centre.x, centre.y, bodyR * 0.99f, bodyR * 0.99f, 0.0f,
                           -2.6f, 0.9f, true);   // top-left arc
        g.setColour(juce::Colours::white.withAlpha(0.22f));
        g.strokePath(halo, juce::PathStrokeType(1.4f));
        // pointer runs from hub to the metal cap's edge (cap ~= 0.72 of the sprite)
        const float capR = bodyR * 0.72f;
        const juce::Point<float> tip(centre.x + std::sin(angle) * capR * 0.96f,
                                     centre.y - std::cos(angle) * capR * 0.96f);
        const juce::Point<float> tail(centre.x + std::sin(angle) * capR * 0.10f,
                                      centre.y - std::cos(angle) * capR * 0.10f);
        g.setColour(juce::Colour(0xFFE8E3D2).withAlpha(0.92f));
        g.drawLine({ tail, tip }, juce::jmax(2.0f, capR * 0.13f));
    } else {
        // fallback: drawn body
        juce::ColourGradient rim(juce::Colour(0xFF8E8B84), centre.x - bodyR, centre.y - bodyR,
                                 juce::Colour(0xFF2B2A27), centre.x + bodyR, centre.y + bodyR, false);
        g.setGradientFill(rim);
        g.fillEllipse(centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);
        const float innerR = bodyR - 2.5f;
        const juce::Point<float> tip(centre.x + std::sin(angle) * innerR * 0.92f,
                                     centre.y - std::cos(angle) * innerR * 0.92f);
        const juce::Point<float> tail(centre.x + std::sin(angle) * innerR * 0.35f,
                                      centre.y - std::cos(angle) * innerR * 0.35f);
        g.setColour(capText);
        g.drawLine({ tail, tip }, juce::jmax(2.0f, innerR * 0.10f));
    }
}

void VintageLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                                          float sliderPos, float minSliderPos, float maxSliderPos,
                                          juce::Slider::SliderStyle style, juce::Slider& slider) {
    if (style != juce::Slider::LinearVertical && style != juce::Slider::LinearHorizontal) {
        juce::LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos,
                                               minSliderPos, maxSliderPos, style, slider);
        return;
    }
    const bool vertical = (style == juce::Slider::LinearVertical);
    const auto area = juce::Rectangle<int>(x, y, width, height).toFloat();

    // Tiny-bounds early-out (same idiom as drawRotarySlider's radius guard):
    // below this there is no room for a track slot + cap, and the cap's
    // jlimit clamp would invert its limits.
    const float across = vertical ? area.getWidth() : area.getHeight();
    const float along  = vertical ? area.getHeight() : area.getWidth();
    if (across < 12.0f || along < 20.0f)
        return;

    // Recessed track slot through the middle -- same well vocabulary as the
    // value boxes and the blank Stage-3 plates.
    const float slotW = 8.0f;
    const auto track = vertical
        ? juce::Rectangle<float>(area.getCentreX() - slotW * 0.5f, area.getY() + 2.0f,
                                 slotW, area.getHeight() - 4.0f)
        : juce::Rectangle<float>(area.getX() + 2.0f, area.getCentreY() - slotW * 0.5f,
                                 area.getWidth() - 4.0f, slotW);
    drawRecessedWell(g, track, 3.0f);

    // Brushed-metal cap, lit from above like the knob sprite (static lighting;
    // the cap only translates). Grip line runs across the travel direction.
    const float capAcross = vertical ? juce::jmin(area.getWidth() - 2.0f, 26.0f)
                                     : juce::jmin(area.getHeight() - 2.0f, 20.0f);
    const float capAlong = 15.0f;
    const auto cap = vertical
        ? juce::Rectangle<float>(capAcross, capAlong).withCentre(
              { area.getCentreX(),
                juce::jlimit(area.getY() + capAlong * 0.5f,
                             area.getBottom() - capAlong * 0.5f, sliderPos) })
        : juce::Rectangle<float>(capAlong, capAcross).withCentre(
              { juce::jlimit(area.getX() + capAlong * 0.5f,
                             area.getRight() - capAlong * 0.5f, sliderPos),
                area.getCentreY() });

    g.setColour(juce::Colours::black.withAlpha(0.45f));   // seat shadow
    g.fillRoundedRectangle(cap.translated(0.0f, 1.5f), 2.5f);
    juce::ColourGradient metal(juce::Colour(0xFF8E8B84), cap.getX(), cap.getY(),
                               juce::Colour(0xFF3A3936), cap.getX(), cap.getBottom(), false);
    g.setGradientFill(metal);
    g.fillRoundedRectangle(cap, 2.5f);
    g.setColour(panelEdge);
    g.drawRoundedRectangle(cap.reduced(0.5f), 2.5f, 1.0f);
    g.setColour(juce::Colours::black.withAlpha(0.6f));    // grip slot
    if (vertical)
        g.drawLine(cap.getX() + 3.0f, cap.getCentreY(), cap.getRight() - 3.0f, cap.getCentreY(), 1.5f);
    else
        g.drawLine(cap.getCentreX(), cap.getY() + 3.0f, cap.getCentreX(), cap.getBottom() - 3.0f, 1.5f);
    g.setColour(juce::Colours::white.withAlpha(0.25f));   // catch-light beside the grip
    if (vertical)
        g.drawLine(cap.getX() + 3.0f, cap.getCentreY() + 1.5f, cap.getRight() - 3.0f, cap.getCentreY() + 1.5f, 1.0f);
    else
        g.drawLine(cap.getCentreX() + 1.5f, cap.getY() + 3.0f, cap.getCentreX() + 1.5f, cap.getBottom() - 3.0f, 1.0f);
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
    drawPhotoCrop(g, creamTexture(), area.toFloat(),
                  (juce::uint32) (area.getY() * 7 + area.getX()));
}

void VintageLookAndFeel::fillModulePanel(juce::Graphics& g, juce::Rectangle<float> area,
                                         float corner, float alpha) {
    g.saveState();
    juce::Path clip;
    clip.addRoundedRectangle(area, corner);
    g.reduceClipRegion(clip);
    g.setOpacity(alpha);
    drawPhotoCrop(g, panelTexture(), area,
                  (juce::uint32) ((int) area.getX() * 31 + (int) area.getY() * 17));
    g.restoreState();
}

void VintageLookAndFeel::fillWood(juce::Graphics& g, juce::Rectangle<int> area) {
    drawPhotoCrop(g, woodTexture(), area.toFloat(),
                  (juce::uint32) (area.getX() * 13 + 5));
    // inner shading so the rail reads as a raised cheek, not wallpaper
    juce::ColourGradient edge(juce::Colours::black.withAlpha(0.35f), (float) area.getX(), 0.0f,
                              juce::Colours::transparentBlack, (float) area.getX() + 6.0f, 0.0f, false);
    g.setGradientFill(edge);
    g.fillRect(area.removeFromLeft(6));
}

void VintageLookAndFeel::drawScrew(juce::Graphics& g, float cx, float cy, float r, bool onDark) {
    static juce::Image sprite = loadAsset(BinaryData::ScrewHead_png, BinaryData::ScrewHead_pngSize);
    // Pre-brightened copy for dark leather panels, where the aged-nickel head
    // otherwise disappears (user acceptance feedback, 2026-07-11).
    static juce::Image spriteBright = [] {
        juce::Image b = sprite.createCopy();
        for (int y = 0; y < b.getHeight(); ++y)
            for (int x = 0; x < b.getWidth(); ++x) {
                auto c = b.getPixelAt(x, y);
                b.setPixelAt(x, y, c.brighter(0.55f).withAlpha(c.getFloatAlpha()));
            }
        return b;
    }();
    const juce::Image& head = (onDark && spriteBright.isValid()) ? spriteBright : sprite;
    // seat shadow ring for separation
    g.setColour(juce::Colours::black.withAlpha(onDark ? 0.65f : 0.35f));
    g.fillEllipse(cx - r * 1.3f + 0.8f, cy - r * 1.3f + 1.2f, r * 2.6f, r * 2.6f);
    if (onDark) {   // faint raised-lip catch-light so the head pops off the leather
        g.setColour(juce::Colours::white.withAlpha(0.18f));
        g.drawEllipse(cx - r * 1.08f, cy - r * 1.08f, r * 2.16f, r * 2.16f, 1.0f);
    }
    if (head.isValid()) {
        // slot-angle jitter only (small, so the baked top-left light stays honest)
        const float rot = std::fmod(cx * 12.9898f + cy * 78.233f, 0.6f) - 0.3f;
        const float box = r * 2.15f;
        g.saveState();
        g.addTransform(juce::AffineTransform::rotation(rot, cx, cy));
        g.drawImage(head, juce::Rectangle<float>(cx - box * 0.5f, cy - box * 0.5f, box, box),
                    juce::RectanglePlacement::centred);
        g.restoreState();
        return;
    }
    // fallback: simple drawn head
    juce::ColourGradient rim(juce::Colour(0xFFDDDBD3), cx - r * 0.8f, cy - r * 0.8f,
                             juce::Colour(0xFF232220), cx + r * 0.7f, cy + r * 0.8f, false);
    g.setGradientFill(rim);
    g.fillEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f);
}

void VintageLookAndFeel::drawRecessedWell(juce::Graphics& g, juce::Rectangle<float> r, float corner) {
    g.setColour(charcoalWell);
    g.fillRoundedRectangle(r, corner);
    g.setColour(juce::Colours::black.withAlpha(0.55f));
    g.drawRoundedRectangle(r.reduced(0.5f), corner, 1.0f);
    g.setColour(juce::Colours::white.withAlpha(0.05f));   // bottom lip catch-light
    g.drawLine(r.getX() + corner, r.getBottom() - 0.5f, r.getRight() - corner, r.getBottom() - 0.5f, 1.0f);
}
