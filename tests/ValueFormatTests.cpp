#include <juce_gui_basics/juce_gui_basics.h>
#include "../src/gui/ValueFormat.h"

// Round-trip and exact-format checks for the instrument-style value-box text
// (spec v5.33 Sec 4). Free functions only -- no Slider/MessageManager needed.
class ValueFormatTest : public juce::UnitTest {
public:
    ValueFormatTest() : juce::UnitTest("ValueFormat") {}

    void runTest() override {
        beginTest("formats and round-trips per family");
        using vfmt::Fmt;

        // Pct: 0..1 shown as integer percent; bare numbers parse as percent.
        expectEquals(vfmt::text(Fmt::Pct, 0.48), juce::String("48%"));
        expectEquals(vfmt::text(Fmt::Pct, 1.0),  juce::String("100%"));
        expectWithinAbsoluteError(vfmt::value(Fmt::Pct, "48%"), 0.48, 1e-6);
        expectWithinAbsoluteError(vfmt::value(Fmt::Pct, "48"),  0.48, 1e-6);

        // St / Ct: signed integers with unit.
        expectEquals(vfmt::text(Fmt::St, 7.0),   juce::String("+7 st"));
        expectEquals(vfmt::text(Fmt::St, 0.0),   juce::String("0 st"));
        expectEquals(vfmt::text(Fmt::Ct, -12.0), juce::String("-12 ct"));
        expectWithinAbsoluteError(vfmt::value(Fmt::St, "+7 st"),  7.0,  1e-6);
        expectWithinAbsoluteError(vfmt::value(Fmt::Ct, "-12 ct"), -12.0, 1e-6);

        // Hz: integer Hz below 1 kHz, two-decimal kHz above; parses both + bare.
        expectEquals(vfmt::text(Fmt::Hz, 250.0),    juce::String("250 Hz"));
        expectEquals(vfmt::text(Fmt::Hz, 999.9999), juce::String("1000 Hz"));
        expectEquals(vfmt::text(Fmt::Hz, 2500.0),   juce::String("2.50 kHz"));
        expectWithinAbsoluteError(vfmt::value(Fmt::Hz, "2.5 kHz"), 2500.0, 1e-3);
        expectWithinAbsoluteError(vfmt::value(Fmt::Hz, "250 Hz"),  250.0,  1e-3);
        expectWithinAbsoluteError(vfmt::value(Fmt::Hz, "250"),     250.0,  1e-3);

        // HzOff: 0 is the bypass position (HP pre-filter contract).
        expectEquals(vfmt::text(Fmt::HzOff, 0.0),   juce::String("Off"));
        expectEquals(vfmt::text(Fmt::HzOff, 120.0), juce::String("120 Hz"));
        expectWithinAbsoluteError(vfmt::value(Fmt::HzOff, "Off"), 0.0,   1e-6);
        expectWithinAbsoluteError(vfmt::value(Fmt::HzOff, "off"), 0.0,   1e-6);
        expectWithinAbsoluteError(vfmt::value(Fmt::HzOff, "120"), 120.0, 1e-3);

        // Oct: signed two-decimals (separation knob).
        expectEquals(vfmt::text(Fmt::Oct, 1.5),  juce::String("+1.50 oct"));
        expectEquals(vfmt::text(Fmt::Oct, 0.0),  juce::String("0.00 oct"));
        expectWithinAbsoluteError(vfmt::value(Fmt::Oct, "-2.25 oct"), -2.25, 1e-6);

        // EnvTime: ms below 1 s, seconds above; parses ms/s suffix, bare = seconds.
        expectEquals(vfmt::text(Fmt::EnvTime, 0.005), juce::String("5 ms"));
        expectEquals(vfmt::text(Fmt::EnvTime, 1.2),   juce::String("1.20 s"));
        expectWithinAbsoluteError(vfmt::value(Fmt::EnvTime, "250 ms"), 0.25, 1e-6);
        expectWithinAbsoluteError(vfmt::value(Fmt::EnvTime, "1.2 s"),  1.2,  1e-6);
        expectWithinAbsoluteError(vfmt::value(Fmt::EnvTime, "0.5"),    0.5,  1e-6);

        // Db / Plain2.
        expectEquals(vfmt::text(Fmt::Db, -9.0),   juce::String("-9.0 dB"));
        expectEquals(vfmt::text(Fmt::Plain2, 0.8), juce::String("0.80"));
        expectWithinAbsoluteError(vfmt::value(Fmt::Db, "-9.0 dB"), -9.0, 1e-6);
        expectWithinAbsoluteError(vfmt::value(Fmt::Plain2, "0.80"), 0.8, 1e-6);

        // Round-trip law: value(text(v)) stays within display precision.
        const struct { Fmt f; double v; double tol; } rt[] = {
            { Fmt::Pct, 0.37, 0.005 },  { Fmt::St, -24.0, 0.5 },  { Fmt::Ct, 99.0, 0.5 },
            { Fmt::Hz, 440.0, 0.5 },    { Fmt::Hz, 12345.0, 6.0 }, { Fmt::HzOff, 0.0, 1e-9 },
            { Fmt::Oct, -3.75, 0.005 }, { Fmt::EnvTime, 0.123, 5e-4 }, { Fmt::EnvTime, 4.2, 0.005 },
            { Fmt::Db, 5.5, 0.05 },     { Fmt::Plain2, 0.15, 0.005 },
        };
        for (const auto& c : rt)
            expectWithinAbsoluteError(vfmt::value(c.f, vfmt::text(c.f, c.v)), c.v, c.tol);
    }
};

static ValueFormatTest valueFormatTestInstance;
