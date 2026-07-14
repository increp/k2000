#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// The single source of the vintage-hardware aesthetic (reference mockup:
// docs/superpowers/specs/assets/2026-07-09-bernie-vintage-reference.png):
// palette, embedded condensed typeface, startup-cached grain textures,
// chassis primitives (cream/wood plates, screws, recessed wells), and all
// control rendering. Replaces SummitLookAndFeel (deleted in the same arc).
class VintageLookAndFeel : public juce::LookAndFeel_V4 {
public:
    VintageLookAndFeel();

    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height,
                          float sliderPos,
                          float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider&) override;

    // Vintage fader: recessed track slot + brushed-metal cap with a grip line.
    // Handles LinearVertical (blend faders) and LinearHorizontal (DUTY);
    // other styles fall through to the V4 default.
    void drawLinearSlider(juce::Graphics&, int x, int y, int width, int height,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          juce::Slider::SliderStyle, juce::Slider&) override;

    // Compact combo rendering carried over from SummitLookAndFeel: the V4
    // default's 30 px arrow gutter + 16 px font truncates panel combos.
    void drawComboBox(juce::Graphics&, int width, int height, bool isButtonDown,
                      int buttonX, int buttonY, int buttonW, int buttonH,
                      juce::ComboBox&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;

    // Every label (captions, values, combo text) renders in the condensed face.
    juce::Font getLabelFont(juce::Label&) override;

    // --- palette (shared with Section and the editor's chrome painting) ---
    static const juce::Colour windowBg;      // area behind the module panels
    static const juce::Colour creamPanel;    // header/footer plate base
    static const juce::Colour creamText;     // dark text on cream
    static const juce::Colour charcoalPanel; // module panel fill
    static const juce::Colour charcoalWell;  // recessed wells (value boxes, blank meters)
    static const juce::Colour panelEdge;     // module borders / engraved lines
    static const juce::Colour woodRail;      // side-rail base
    static const juce::Colour capText;       // caps labels on charcoal
    static const juce::Colour dimText;       // reserved/dimmed
    static const juce::Colour brassTrim;     // spine-section label underline
    static const juce::Colour amberLed;      // indicator accents
    static const juce::Colour ledRed;        // LIMIT light when active

    // --- typography (embedded Barlow Condensed Medium, OFL) ---
    static juce::Font condensedFont(float height);

    // --- chassis primitives ---
    static void fillCream(juce::Graphics&, juce::Rectangle<int> area);
    // Textured module-panel fill (subtle leather-grain speckle on charcoal).
    static void fillModulePanel(juce::Graphics&, juce::Rectangle<float> area,
                                float corner, float alpha);
    static void fillWood(juce::Graphics&, juce::Rectangle<int> area);
    static void drawScrew(juce::Graphics&, float cx, float cy, float radius,
                          bool onDark = false);
    static void drawRecessedWell(juce::Graphics&, juce::Rectangle<float> area,
                                 float corner = 4.0f);

private:
    static juce::Typeface::Ptr condensedTypeface();
    static const juce::Image& creamTexture();   // built once, tiled thereafter
    static const juce::Image& panelTexture();
    static const juce::Image& woodTexture();
};
