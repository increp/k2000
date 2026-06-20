#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// The single source of the dark Summit aesthetic: palette + knob rendering.
// Colour IDs used across the panel are also exposed as named constants so the
// editor frames/dims sections with the same palette.
class SummitLookAndFeel : public juce::LookAndFeel_V4 {
public:
    SummitLookAndFeel();

    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height,
                          float sliderPos,
                          float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider&) override;

    // Compact combo rendering: the V4 default reserves a 30 px arrow gutter and a
    // 16 px font, which truncates panel combos ("12 dB"->"1...", "Huggett"->"Hugg...").
    // We use an 18 px arrow zone and a 13 px font so the text fits at our cell sizes.
    void drawComboBox(juce::Graphics&, int width, int height, bool isButtonDown,
                      int buttonX, int buttonY, int buttonW, int buttonH,
                      juce::ComboBox&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;

    // Shared palette (also used directly by Section/editor painting).
    static const juce::Colour panelBg;     // window background
    static const juce::Colour moduleBg;    // section fill
    static const juce::Colour moduleEdge;  // section border (live)
    static const juce::Colour spineEdge;   // section border (constant-spine accent)
    static const juce::Colour knobBody;    // knob fill
    static const juce::Colour knobRing;    // value arc + pointer (lit)
    static const juce::Colour textBright;  // captions / titles
    static const juce::Colour textDim;     // reserved/dimmed text
};
