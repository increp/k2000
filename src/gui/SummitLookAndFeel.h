#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// The single source of the dark Summit aesthetic: palette + knob rendering.
// Colour IDs used across the panel are also exposed as named constants so the
// editor frames/dims sections with the same palette.
class SummitLookAndFeel : public juce::LookAndFeel_V4 {
public:
    SummitLookAndFeel();

    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height,
                          float sliderPosProportional,
                          float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider&) override;

    // Shared palette (also used directly by Section/editor painting).
    static const juce::Colour panelBg;     // window background
    static const juce::Colour moduleBg;    // section fill
    static const juce::Colour moduleEdge;  // section border (live)
    static const juce::Colour spineEdge;   // section border (constant-spine accent)
    static const juce::Colour knobBody;    // knob fill
    static const juce::Colour knobRing;    // knob track / pointer
    static const juce::Colour textBright;  // captions / titles
    static const juce::Colour textDim;     // reserved/dimmed text
};
