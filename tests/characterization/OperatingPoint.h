#pragma once
#include <juce_core/juce_core.h>

// chz -- filter CHaracteriZation layer. One OperatingPoint describes a single
// measurement configuration; it is the shared schema for L1, L2, and the CSV
// column header, so a fingerprint row and (later, sub-project #2) an external
// capture row are column-aligned.
namespace chz {

enum class Mode   { LP12, LP24, BP, HP, Notch };
enum class OsMode { Live, Render };

struct OperatingPoint {
    Mode   mode           = Mode::LP24;
    double cutoffHz       = 1000.0;
    double resonance      = 0.0;     // 0 .. 1
    double drive          = 0.0;     // 0 .. 1
    int    osFactor       = 1;       // 1, 2, 4, 8
    OsMode osMode         = OsMode::Live;
    double hostSampleRate = 96000.0;
};

inline juce::String modeName(Mode m) {
    switch (m) { case Mode::LP12: return "LP12"; case Mode::LP24: return "LP24";
                 case Mode::BP: return "BP"; case Mode::HP: return "HP";
                 case Mode::Notch: return "Notch"; }
    return "?";
}
inline juce::String osModeName(OsMode m) { return m == OsMode::Live ? "live" : "render"; }

} // namespace chz
