#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// Instrument-style value-box text (spec v5.33 Sec 4): formatter/parser pairs
// installed on sliders as textFromValueFunction / valueFromTextFunction.
// Parsers accept the unit suffix in any case/spacing; a bare number means the
// family's display unit (percent for Pct, seconds for EnvTime, Hz for Hz...).
namespace vfmt {

enum class Fmt {
    Pct,      // 0..1 -> "48%"            (blend weights, duty, mixer levels)
    St,       // semitones -> "+7 st"     (VCO coarse)
    Ct,       // cents -> "-12 ct"        (VCO fine)
    Hz,       // "250 Hz" / "2.50 kHz"    (filter cutoff)
    HzOff,    // 0 -> "Off", else Hz      (HP pre-filter: 0 == bypassed)
    Oct,      // "+1.50 oct"              (separation)
    EnvTime,  // "5 ms" / "1.20 s"        (amp envelope A/D/R)
    Db,       // "-9.0 dB"                (master gain, layer level)
    Plain2,   // "0.20"                   (resonance-class 0..1 knobs)
};

juce::String text(Fmt, double value);
double value(Fmt, const juce::String& textIn);

// Install the pair on a slider and refresh its text box.
void apply(juce::Slider&, Fmt);

} // namespace vfmt
