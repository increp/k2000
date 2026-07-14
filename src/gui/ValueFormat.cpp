#include "ValueFormat.h"

namespace {
juce::String cleaned(const juce::String& t) {
    return t.trim().toLowerCase().removeCharacters(" ");
}
double num(const juce::String& t) {
    return t.retainCharacters("0123456789.+-").getDoubleValue();
}
} // namespace

juce::String vfmt::text(Fmt f, double v) {
    switch (f) {
        case Fmt::Pct:  return juce::String(juce::roundToInt(v * 100.0)) + "%";
        case Fmt::St: {
            const int n = juce::roundToInt(v);
            return juce::String(n > 0 ? "+" : "") + juce::String(n) + " st";
        }
        case Fmt::Ct: {
            const int n = juce::roundToInt(v);
            return juce::String(n > 0 ? "+" : "") + juce::String(n) + " ct";
        }
        case Fmt::Hz:
            return v < 999.5 ? juce::String(juce::roundToInt(v)) + " Hz"
                             : (v < 1000.0 ? juce::String("1000 Hz")
                                           : juce::String(v / 1000.0, 2) + " kHz");
        case Fmt::HzOff:   return v <= 0.0 ? juce::String("Off") : text(Fmt::Hz, v);
        case Fmt::Oct:     return juce::String(v > 0.0 ? "+" : "") + juce::String(v, 2) + " oct";
        case Fmt::EnvTime: return v < 1.0 ? juce::String(juce::roundToInt(v * 1000.0)) + " ms"
                                          : juce::String(v, 2) + " s";
        case Fmt::Db:      return juce::String(v, 1) + " dB";
        case Fmt::Plain2:  return juce::String(v, 2);
    }
    return juce::String(v);
}

double vfmt::value(Fmt f, const juce::String& raw) {
    const auto t = cleaned(raw);
    switch (f) {
        case Fmt::Pct:
            return num(t) / 100.0;
        case Fmt::HzOff:
            if (t == "off") return 0.0;
            [[fallthrough]];
        case Fmt::Hz:
            return num(t) * (t.contains("k") ? 1000.0 : 1.0);
        case Fmt::EnvTime:
            return num(t) * (t.endsWith("ms") ? 0.001 : 1.0);
        default:
            return num(t);
    }
}

void vfmt::apply(juce::Slider& s, Fmt f) {
    s.textFromValueFunction = [f](double v) { return text(f, v); };
    s.valueFromTextFunction = [f](const juce::String& t) { return value(f, t); };
    s.updateText();
}
