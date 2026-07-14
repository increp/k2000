# VCO Rows + Osc Blend Mixer (GUI Stage 2) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Version:** 5.34 (artifact) · **Date:** 2026-07-13 · **Spec:** `docs/superpowers/specs/2026-07-13-vco-mixer-gui-design.md` (v5.33) · **Branch:** `feat/gui-stage2`

**Goal:** Fill the three reserved VCO plates (4 blend faders + DUTY + COARSE/FINE + reserved WAVE PREVIEW well each) and the Osc Blend panel (3 level knobs), and switch every value box on the panel to instrument-style text with units — GUI-only, binding the 24 already-shipped params.

**Architecture:** Three new building blocks: `vfmt` (ValueFormat) formatter/parser pairs installed as JUCE `textFromValueFunction`/`valueFromTextFunction`; `LabeledFader` (caption above a vertical slider with a `%` box below — sibling of `LabeledKnob`); `VcoRow : Section` (owns one complete row's controls + the reserved scope well; the editor holds three and binds them per layer through the existing `ParamBinder`). `VintageLookAndFeel` gains its first `drawLinearSlider` (recessed track + brushed-metal cap). The editor's geography does not change — `VcoRow` replaces the empty `Section` placeholders in the same rectangles.

**Tech Stack:** JUCE (vendored `third_party/JUCE`, JUCE-8 `FontOptions` API), C++17, CMake, JUCE `UnitTest` suite, `k2000_panel_snapshot` offscreen render tool.

## Global Constraints

- **ALWAYS build `-j4`** — bare `-j` OOMs the JUCE compile (0-byte object → confusing link error).
- `build/` is the Release dir by convention; all commands below use it.
- **GUI-only:** no changes to params, DSP, processor, `ParamSnapshot`, or `ParamBinder` internals. The 24 params (`osc{1,2,3}.coarse/.fine/.blend.{sine,triangle,saw,pulse}/.blend.pulseDuty`, `mixer.osc{1,2,3}.level`) already exist in `src/params/Parameters.cpp` — Task 3/4 only *bind* them.
  **Amended 2026-07-13 (Task 3 discovery):** one ruled exception — `ParamBinder::bind(juce::Slider&)` now preserves caller-installed slider text functions across attachment construction (JUCE's `SliderParameterAttachment` ctor overwrites them with the parameter's own, which clobbered every `vfmt::apply` formatter on bind and re-bind). The spec's §4 formatting requirement governs; the detach-before-rebind contract is unchanged; sliders without custom formatters keep parameter text exactly as before. `clear()` additionally restores each slider's snapshotted caller functions, so a post-clear rebind can never mistake stale attachment lambdas for caller formatting (final-review hardening).
- **Suite goes 297 → 298 exactly once** (Task 1's `ValueFormatTests`). That task updates `docs/filter-validation/README.md` and `docs/franklin/test-catalog.json` in the same commit (banked lesson: CI Drift fails otherwise). No other task may change the test count.
- All new user-visible strings are pure ASCII (the `util::u8()` rule is not triggered; do not add non-ASCII text).
- Panel snapshots are judged **at 100% full-frame** — never from zoomed crops.
- Pixel constants in `VcoRow::resized()` and `drawLinearSlider` are **starting values**; the acceptance pass tunes them live. Do not bikeshed exact pixels during implementation.
- Commit messages end with: `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`

---

### Task 1: `vfmt` value formatting (TDD) + suite bookkeeping

**Files:**
- Create: `src/gui/ValueFormat.h`, `src/gui/ValueFormat.cpp`, `tests/ValueFormatTests.cpp`
- Modify: `tests/CMakeLists.txt` (k2000_tests: test file + source; k2000_panel_snapshot: source), `CMakeLists.txt:52` region (target_sources), `docs/filter-validation/README.md:30` (count 297 → 298), `docs/franklin/test-catalog.json` (one new entry)

**Interfaces:**
- Consumes: nothing (leaf utility; only `juce_gui_basics` for `juce::String`/`juce::Slider`).
- Produces (Tasks 2–4 rely on these exact names):
  - `enum class vfmt::Fmt { Pct, St, Ct, Hz, HzOff, Oct, EnvTime, Db, Plain2 };`
  - `juce::String vfmt::text(Fmt, double value);`
  - `double vfmt::value(Fmt, const juce::String& textIn);`
  - `void vfmt::apply(juce::Slider&, Fmt);` — installs the pair + refreshes the box.

- [ ] **Step 1: Write the failing test**

Create `tests/ValueFormatTests.cpp`:

```cpp
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
```

- [ ] **Step 2: Wire it into the test target and verify it fails to build**

In `tests/CMakeLists.txt`, inside `add_executable(k2000_tests ...)`: add `ValueFormatTests.cpp` after line 4 (`ParamSnapshotTests.cpp`), and add `../src/gui/ValueFormat.cpp` directly after the existing `../src/gui/ParamBinder.cpp` line.

Run: `cmake --build build --target k2000_tests -j4`
Expected: **FAIL** — `fatal error: ../src/gui/ValueFormat.h: No such file or directory` (and/or missing source). This is the red step.

- [ ] **Step 3: Write the implementation**

Create `src/gui/ValueFormat.h`:

```cpp
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
```

Create `src/gui/ValueFormat.cpp`:

```cpp
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
```

Note the `Hz` boundary: values in `[999.5, 1000)` (the default cutoff is stored as
`999.9999390`) must print `1000 Hz`, not `1000 kHz-style` or `1.00 kHz` — the explicit
middle branch handles exactly that band.

- [ ] **Step 4: Run the test, verify it passes**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | tee build/last-test-run.log | tail -3`
Expected: `Summary: 298 tests, 0 failed` and a `[PASS] ValueFormat` line earlier in the output.

- [ ] **Step 5: Wire the source into the plugin + snapshot targets**

- `CMakeLists.txt` `target_sources(k2000 ...)`: add `src/gui/ValueFormat.cpp` after `src/gui/ParamBinder.cpp`.
- `tests/CMakeLists.txt` `add_executable(k2000_panel_snapshot ...)`: add `../src/gui/ValueFormat.cpp` after `../src/gui/ParamBinder.cpp`.

Run: `cmake --build build --target k2000_Standalone k2000_panel_snapshot -j4`
Expected: clean build (the TU is compiled, not yet referenced).

- [ ] **Step 6: Suite-count bookkeeping (same commit — CI Drift gates this)**

- `docs/filter-validation/README.md` line 30: change ``Expected: `Summary: 297 tests, 0 failed` `` to ``Expected: `Summary: 298 tests, 0 failed` ``.
- `docs/franklin/test-catalog.json`: append to the `entries` array (key = `"<UnitTest name> / <beginTest string>"`, audited v2 format):

```json
{
  "key": "ValueFormat / formats and round-trips per family",
  "file": "tests/ValueFormatTests.cpp",
  "what": "Asserts exact display strings per formatting family (48%, +7 st, 2.50 kHz, Off, 5 ms, -9.0 dB, 0.80) and that value(text(v)) round-trips within display precision for every family.",
  "why": "Every value box on the Bernie panel routes through these formatter/parser pairs (spec v5.33 Sec 4); a drifted formatter mislabels what the user reads on every knob, and a non-inverse parser corrupts values typed into value boxes.",
  "deviationMeans": "A formatting family changed its display contract (units, precision, thresholds) or its parser stopped being the formatter's inverse — check whether a deliberate readout retune forgot to update the test, or a refactor broke text round-tripping.",
  "links": [],
  "compares": "vfmt::text() output strings against literal expected strings, and vfmt::value(vfmt::text(v)) against the original v with per-family tolerances — no golden file, the literals ARE the contract.",
  "succeedingMeans": "Panel readouts show exactly the documented instrument-style text, and typing a displayed value back into any value box reproduces the same parameter value."
}
```

Run: `tools/drift-check --ci --suite-log build/last-test-run.log 2>/dev/null || tools/drift-check --session`
Expected: `suite-count-claims` and `franklin-catalog` both OK (298 everywhere).

- [ ] **Step 7: Commit**

```bash
git add src/gui/ValueFormat.h src/gui/ValueFormat.cpp tests/ValueFormatTests.cpp \
        tests/CMakeLists.txt CMakeLists.txt \
        docs/filter-validation/README.md docs/franklin/test-catalog.json
git commit -m "feat(gui): vfmt instrument-style value formatting + round-trip test (suite 297->298)

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 2: Vintage linear-slider rendering + `LabeledFader`

**Files:**
- Create: `src/gui/LabeledFader.h`, `src/gui/LabeledFader.cpp`
- Modify: `src/gui/VintageLookAndFeel.h:16` region (declaration), `src/gui/VintageLookAndFeel.cpp` (implementation, after `drawRotarySlider` ends at line 258), `CMakeLists.txt` (target_sources), `tests/CMakeLists.txt` (k2000_panel_snapshot sources)

**Interfaces:**
- Consumes: `VintageLookAndFeel::drawRecessedWell(juce::Graphics&, juce::Rectangle<float>, float)`, `condensedFont(float)`, palette constants (all existing statics).
- Produces (Task 3 relies on):
  - `class LabeledFader : public juce::Component` with `explicit LabeledFader(const juce::String& caption);` and `juce::Slider& slider();`
  - `VintageLookAndFeel::drawLinearSlider(...)` override rendering `LinearVertical` and `LinearHorizontal` styles in the vintage vocabulary (other styles fall through to `LookAndFeel_V4`).

- [ ] **Step 1: Declare and implement `drawLinearSlider`**

In `src/gui/VintageLookAndFeel.h`, directly after the `drawRotarySlider` declaration (line 16), add:

```cpp
    // Vintage fader: recessed track slot + brushed-metal cap with a grip line.
    // Handles LinearVertical (blend faders) and LinearHorizontal (DUTY);
    // other styles fall through to the V4 default.
    void drawLinearSlider(juce::Graphics&, int x, int y, int width, int height,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          juce::Slider::SliderStyle, juce::Slider&) override;
```

In `src/gui/VintageLookAndFeel.cpp`, directly after the closing brace of `drawRotarySlider` (line 258), add:

```cpp
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
```

- [ ] **Step 2: Create `LabeledFader`**

Create `src/gui/LabeledFader.h`:

```cpp
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// A vertical fader with its caption above and value box below -- the Wave
// Recipe control (reference mockup, VCO 1 row). Sibling of LabeledKnob: the
// owner binds slider() through ParamBinder; LabeledFader owns no attachment.
class LabeledFader : public juce::Component {
public:
    explicit LabeledFader(const juce::String& caption);

    juce::Slider& slider() { return slider_; }

    void resized() override;

private:
    static constexpr int captionH_ = 18;

    juce::Slider slider_{ juce::Slider::LinearVertical,
                          juce::Slider::TextBoxBelow };
    juce::Label  caption_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LabeledFader)
};
```

Create `src/gui/LabeledFader.cpp`:

```cpp
#include "LabeledFader.h"
#include "VintageLookAndFeel.h"

LabeledFader::LabeledFader(const juce::String& caption) {
    slider_.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 18);
    addAndMakeVisible(slider_);

    caption_.setText(caption, juce::dontSendNotification);
    caption_.setJustificationType(juce::Justification::centred);
    caption_.setFont(VintageLookAndFeel::condensedFont(14.0f));
    addAndMakeVisible(caption_);
}

void LabeledFader::resized() {
    auto area = getLocalBounds();
    caption_.setBounds(area.removeFromTop(captionH_));
    slider_.setBounds(area);
}
```

- [ ] **Step 3: Wire into CMake**

- `CMakeLists.txt` `target_sources(k2000 ...)`: add `src/gui/LabeledFader.cpp` after `src/gui/LabeledKnob.cpp`.
- `tests/CMakeLists.txt` `add_executable(k2000_panel_snapshot ...)`: add `../src/gui/LabeledFader.cpp` after `../src/gui/LabeledKnob.cpp`.

(`k2000_tests` does NOT get these — GUI stays out of the suite.)

- [ ] **Step 4: Verify everything builds (nothing visible yet — that's expected)**

Run: `cmake --build build --target k2000_tests k2000_Standalone k2000_panel_snapshot -j4 && ./build/tests/k2000_tests 2>&1 | tee build/last-test-run.log | tail -1`
Expected: clean build; `Summary: 298 tests, 0 failed`. No panel change yet (no component uses a linear slider until Task 3) — the reviewer gate for this task is code-level.

- [ ] **Step 5: Commit**

```bash
git add src/gui/VintageLookAndFeel.h src/gui/VintageLookAndFeel.cpp \
        src/gui/LabeledFader.h src/gui/LabeledFader.cpp \
        CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(gui): vintage drawLinearSlider + LabeledFader control

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 3: `VcoRow` + editor integration (the three rows go live)

**Files:**
- Create: `src/gui/VcoRow.h`, `src/gui/VcoRow.cpp`
- Modify: `src/PluginEditor.h` (includes + members), `src/PluginEditor.cpp` (`buildStaticControls`, `bindLayer`, `layoutCanvas`), `CMakeLists.txt`, `tests/CMakeLists.txt` (snapshot target)

**Interfaces:**
- Consumes: `LabeledFader` (Task 2), `vfmt::apply`/`vfmt::Fmt` (Task 1), `Section` (existing: `Section(title, spine=false, reserved=false)`, `contentBounds()`, `paint`), `LabeledKnob`, `ParamBinder::bind(juce::Slider&, const juce::String&)`, `params::layerIds(int)` fields `osc{1,2,3}Coarse/Fine/BlendSine/BlendTriangle/BlendSaw/BlendPulse/PulseDuty`.
- Produces (Task 4 / Stage 3 rely on):
  - `class VcoRow : public Section` with `explicit VcoRow(const juce::String& title);`
  - Slider accessors: `sine() tri() saw() pulse() duty() coarse() fine()` → `juce::Slider&`
  - `juce::Rectangle<int> previewWellBounds() const;` (local coords — Stage 3 mounts the scope here)
  - Editor members `vco1_`, `vco2_`, `vco3_` replacing `vco1Section_`/`vco2Section_`/`vco3Section_`.

- [ ] **Step 1: Create `VcoRow`**

Create `src/gui/VcoRow.h`:

```cpp
#pragma once
#include "Section.h"
#include "LabeledFader.h"
#include "LabeledKnob.h"

// One complete Wave Recipe row (spec v5.33 Sec 3.1): four blend faders with a
// DUTY mini-slider under the Pulse end, COARSE/FINE knobs, and the reserved
// WAVE PREVIEW well Stage 3 fills. The editor owns three; all binding goes
// through ParamBinder via the slider accessors (VcoRow owns no attachments).
class VcoRow : public Section {
public:
    explicit VcoRow(const juce::String& title);

    juce::Slider& sine()   { return sine_.slider(); }
    juce::Slider& tri()    { return tri_.slider(); }
    juce::Slider& saw()    { return saw_.slider(); }
    juce::Slider& pulse()  { return pulse_.slider(); }
    juce::Slider& duty()   { return duty_; }
    juce::Slider& coarse() { return coarse_.slider(); }
    juce::Slider& fine()   { return fine_.slider(); }

    // Local coords; Stage 3 mounts the scope component here.
    juce::Rectangle<int> previewWellBounds() const { return previewWell_; }

    void resized() override;
    void paint(juce::Graphics&) override;

private:
    static constexpr int dutyStripH_ = 26;

    LabeledFader sine_{ "SINE" }, tri_{ "TRI" }, saw_{ "SAW" }, pulse_{ "PULSE" };
    juce::Label  dutyLbl_;
    juce::Slider duty_{ juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    LabeledKnob  coarse_{ "Coarse" }, fine_{ "Fine" };
    juce::Rectangle<int> previewWell_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VcoRow)
};
```

Create `src/gui/VcoRow.cpp`:

```cpp
#include "VcoRow.h"
#include "ValueFormat.h"
#include "VintageLookAndFeel.h"

VcoRow::VcoRow(const juce::String& title) : Section(title) {
    for (auto* f : { &sine_, &tri_, &saw_, &pulse_ }) {
        vfmt::apply(f->slider(), vfmt::Fmt::Pct);
        addAndMakeVisible(*f);
    }

    dutyLbl_.setText("DUTY", juce::dontSendNotification);
    dutyLbl_.setJustificationType(juce::Justification::centredLeft);
    dutyLbl_.setFont(VintageLookAndFeel::condensedFont(14.0f));
    addAndMakeVisible(dutyLbl_);
    duty_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 16);
    vfmt::apply(duty_, vfmt::Fmt::Pct);
    addAndMakeVisible(duty_);

    vfmt::apply(coarse_.slider(), vfmt::Fmt::St);
    vfmt::apply(fine_.slider(),   vfmt::Fmt::Ct);
    addAndMakeVisible(coarse_);
    addAndMakeVisible(fine_);
}

void VcoRow::resized() {
    auto c = contentBounds();

    // Right zone: reserved WAVE PREVIEW well over the pitch knobs.
    auto right = c.removeFromRight((int) ((float) c.getWidth() * 0.40f));
    c.removeFromRight(8);
    auto knobs = right.removeFromBottom(84);
    right.removeFromBottom(4);
    previewWell_ = right.reduced(2);
    const int kw = knobs.getWidth() / 2;
    coarse_.setBounds(knobs.removeFromLeft(kw));
    fine_.setBounds(knobs);

    // Left zone: DUTY strip under the faders (right-aligned so the slider ends
    // under the PULSE fader), then four equal fader columns above it.
    auto dutyStrip = c.removeFromBottom(dutyStripH_);
    const int fw = c.getWidth() / 4;
    sine_.setBounds(c.removeFromLeft(fw));
    tri_.setBounds(c.removeFromLeft(fw));
    saw_.setBounds(c.removeFromLeft(fw));
    pulse_.setBounds(c);
    auto d = dutyStrip.removeFromRight(fw * 2);
    dutyLbl_.setBounds(d.removeFromLeft(38));
    duty_.setBounds(d);
}

void VcoRow::paint(juce::Graphics& g) {
    Section::paint(g);
    VintageLookAndFeel::drawRecessedWell(g, previewWell_.toFloat(), 4.0f);
    g.setColour(VintageLookAndFeel::dimText.withAlpha(0.75f));
    g.setFont(VintageLookAndFeel::condensedFont(15.0f));
    g.drawText("WAVE PREVIEW", previewWell_, juce::Justification::centred);
}
```

- [ ] **Step 2: Swap the editor's three placeholder Sections for VcoRows**

`src/PluginEditor.h`:
- After `#include "gui/Section.h"` add `#include "gui/VcoRow.h"`.
- Replace the three placeholder members (lines 44–47):

```cpp
    // Left column: three VCO panels, empty until GUI Stage 2 fills them.
    Section vco1Section_{ "VCO 1", /*spine*/ false, /*reserved*/ true };
    Section vco2Section_{ "VCO 2", /*spine*/ false, /*reserved*/ true };
    Section vco3Section_{ "VCO 3", /*spine*/ false, /*reserved*/ true };
```

with:

```cpp
    // Left column: the three Wave Recipe rows (GUI Stage 2).
    VcoRow vco1_{ "VCO 1" }, vco2_{ "VCO 2" }, vco3_{ "VCO 3" };
```

`src/PluginEditor.cpp` — `buildStaticControls()`: replace the reserved-sections loop (lines 157–161):

```cpp
    // Reserved sections — visible (framed/dimmed) but no children yet.
    for (auto* s : { &vco1Section_, &vco2Section_, &vco3Section_,
                     &mixerSection_, &outputSection_,
                     &modEnvSection_, &lfoSection_, &modMatrixSection_, &fxSection_ })
        canvas_.addAndMakeVisible(*s);
```

with:

```cpp
    // The three live Wave Recipe rows (Stage 2).
    for (auto* r : { &vco1_, &vco2_, &vco3_ })
        canvas_.addAndMakeVisible(*r);

    // Reserved sections — visible (framed/dimmed) but no children yet.
    for (auto* s : { &mixerSection_, &outputSection_,
                     &modEnvSection_, &lfoSection_, &modMatrixSection_, &fxSection_ })
        canvas_.addAndMakeVisible(*s);
```

`src/PluginEditor.cpp` — `layoutCanvas()`: in the "Three equal VCO panels" block (lines 439–445), rename the three targets — `vco1Section_` → `vco1_`, `vco2Section_` → `vco2_`, `vco3Section_` → `vco3_` (bounds math unchanged; update the comment to "Three Wave Recipe rows (Stage 2).").

- [ ] **Step 3: Bind the 21 row params per layer**

`src/PluginEditor.cpp` — `bindLayer()`: after the `binder_.bind(shaperMix_.slider(), ids.shaperMix);` line, add:

```cpp
    // VCO rows (Stage 2): 7 controls per row, per layer.
    binder_.bind(vco1_.coarse(), ids.osc1Coarse);
    binder_.bind(vco1_.fine(),   ids.osc1Fine);
    binder_.bind(vco1_.sine(),   ids.osc1BlendSine);
    binder_.bind(vco1_.tri(),    ids.osc1BlendTriangle);
    binder_.bind(vco1_.saw(),    ids.osc1BlendSaw);
    binder_.bind(vco1_.pulse(),  ids.osc1BlendPulse);
    binder_.bind(vco1_.duty(),   ids.osc1PulseDuty);
    binder_.bind(vco2_.coarse(), ids.osc2Coarse);
    binder_.bind(vco2_.fine(),   ids.osc2Fine);
    binder_.bind(vco2_.sine(),   ids.osc2BlendSine);
    binder_.bind(vco2_.tri(),    ids.osc2BlendTriangle);
    binder_.bind(vco2_.saw(),    ids.osc2BlendSaw);
    binder_.bind(vco2_.pulse(),  ids.osc2BlendPulse);
    binder_.bind(vco2_.duty(),   ids.osc2PulseDuty);
    binder_.bind(vco3_.coarse(), ids.osc3Coarse);
    binder_.bind(vco3_.fine(),   ids.osc3Fine);
    binder_.bind(vco3_.sine(),   ids.osc3BlendSine);
    binder_.bind(vco3_.tri(),    ids.osc3BlendTriangle);
    binder_.bind(vco3_.saw(),    ids.osc3BlendSaw);
    binder_.bind(vco3_.pulse(),  ids.osc3BlendPulse);
    binder_.bind(vco3_.duty(),   ids.osc3PulseDuty);
```

- [ ] **Step 4: CMake wiring**

- `CMakeLists.txt` `target_sources(k2000 ...)`: add `src/gui/VcoRow.cpp` after `src/gui/Section.cpp`.
- `tests/CMakeLists.txt` `add_executable(k2000_panel_snapshot ...)`: add `../src/gui/VcoRow.cpp` after `../src/gui/Section.cpp`.

- [ ] **Step 5: Build, suite, snapshot — verify the rows are alive**

Run: `cmake --build build --target k2000_tests k2000_Standalone k2000_panel_snapshot -j4 && ./build/tests/k2000_tests 2>&1 | tee build/last-test-run.log | tail -1`
Expected: clean build; `Summary: 298 tests, 0 failed`.

Run: `./build/tests/k2000_panel_snapshot build/panel-stage2-rows.png && echo done`
Then **view the PNG at 100% full-frame**. Expected in each of the three rows:
- four vertical faders captioned SINE / TRI / SAW / PULSE; SAW's cap at the top reading `100%`, the others at the bottom reading `0%` (the default single-saw patch);
- a DUTY mini-slider under the SAW/PULSE end reading `50%`;
- a recessed WAVE PREVIEW well (dimmed caption) top-right with COARSE `0 st` and FINE `0 ct` knobs beneath it;
- the leather plate no longer dimmed (rows are live, not reserved).

- [ ] **Step 6: Close the zero-weight watch item (verification only — no code)**

Run: `grep -n "setBlend\|SmoothedValue" src/Voice.cpp src/Layer.cpp src/dsp/Oscillator.h src/dsp/Oscillator.cpp`
Expected: `Voice.cpp` lines ~90/98/106 pass `s.oscNBlend*` snapshot values straight into `setBlend(...)`; **no `SmoothedValue` matches anywhere in these files**. That means a fader at 0% delivers exact `0.0f` and the `Oscillator` zero-weight render skip stays effective. Record the finding in the commit message. If a `SmoothedValue` DOES appear in the blend path, **stop and flag it** in the task report (fixing is a DSP change — out of scope).

- [ ] **Step 7: Commit**

```bash
git add src/gui/VcoRow.h src/gui/VcoRow.cpp src/PluginEditor.h src/PluginEditor.cpp \
        CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(gui): three live VCO Wave Recipe rows with reserved preview wells

Verified: blend params reach Oscillator::setBlend unsmoothed (exact 0.0f
at fader bottom), so the zero-weight render skip stays effective.

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 4: Osc Blend mixer knobs + whole-panel readout formatting + version 5.5.0

**Files:**
- Modify: `src/PluginEditor.h` (mixer members + helper decl), `src/PluginEditor.cpp` (`buildStaticControls`, new `applyValueFormats`, `bindLayer`, `layoutCanvas`), `CMakeLists.txt:2` (VERSION)

**Interfaces:**
- Consumes: `vfmt::apply`/`vfmt::Fmt` (Task 1), `LabeledKnob`, `layoutCells` (existing lambda in `layoutCanvas`), `params::layerIds(int)` fields `mixerOsc1Level/mixerOsc2Level/mixerOsc3Level`, `params::masterGain`.
- Produces: editor members `mixVco1_`, `mixVco2_`, `mixVco3_`; private method `void applyValueFormats();` — the last GUI surface Stage 2 adds.

- [ ] **Step 1: Mixer knob members + formatting helper declaration**

`src/PluginEditor.h`:
- Replace the member `Section mixerSection_{ "Osc Blend", /*spine*/ true, /*reserved*/ true };` with:

```cpp
    Section mixerSection_{ "Osc Blend", /*spine*/ true };
    LabeledKnob mixVco1_{ "VCO 1" }, mixVco2_{ "VCO 2" }, mixVco3_{ "VCO 3" };
```

- After the `void updateModelVisibility();` declaration add:

```cpp
    void applyValueFormats();        // instrument-style value-box text (spec v5.33 §4)
```

- [ ] **Step 2: Build the mixer knobs and the formatting pass**

`src/PluginEditor.cpp` — in `buildStaticControls()`, directly after the VAST DSP block (after `vastDspSection_.addAndMakeVisible(algo_);`), add:

```cpp
    // Osc Blend mixer (Stage 2): three per-VCO level knobs, % readouts.
    for (auto* k : { &mixVco1_, &mixVco2_, &mixVco3_ }) {
        vfmt::apply(k->slider(), vfmt::Fmt::Pct);
        mixerSection_.addAndMakeVisible(*k);
    }
```

At the very end of `buildStaticControls()` add the call `applyValueFormats();`, and add the method after `bindLayer`'s definition:

```cpp
// Instrument-style value-box text on the pre-Stage-2 controls (spec v5.33 §4).
// VcoRow and the mixer knobs format their own sliders at construction; Key/Vel
// sliders already display integers (their params step by 1).
void K2000AudioProcessorEditor::applyValueFormats() {
    vfmt::apply(filterCutoff_.slider(),    vfmt::Fmt::Hz);
    vfmt::apply(hpCutoff_.slider(),        vfmt::Fmt::HzOff);
    vfmt::apply(filterRes_.slider(),       vfmt::Fmt::Plain2);
    vfmt::apply(hpReso_.slider(),          vfmt::Fmt::Plain2);
    vfmt::apply(spineSeparation_.slider(), vfmt::Fmt::Oct);
    vfmt::apply(spinePostDrive_.slider(),  vfmt::Fmt::Plain2);
    vfmt::apply(ampA_.slider(),            vfmt::Fmt::EnvTime);
    vfmt::apply(ampD_.slider(),            vfmt::Fmt::EnvTime);
    vfmt::apply(ampS_.slider(),            vfmt::Fmt::Plain2);
    vfmt::apply(ampR_.slider(),            vfmt::Fmt::EnvTime);
    vfmt::apply(level_.slider(),           vfmt::Fmt::Db);
    vfmt::apply(masterGain_,               vfmt::Fmt::Db);
}
```

Add `#include "gui/ValueFormat.h"` next to the other gui includes at the top of `PluginEditor.cpp` (note: `PluginEditor.h` does not need it — only the .cpp calls `vfmt`).

- [ ] **Step 3: Bind and lay out the mixer knobs**

`bindLayer()` — after the Task-3 block's last line (`binder_.bind(vco3_.duty(), ids.osc3PulseDuty);`), add:

```cpp
    binder_.bind(mixVco1_.slider(), ids.mixerOsc1Level);
    binder_.bind(mixVco2_.slider(), ids.mixerOsc2Level);
    binder_.bind(mixVco3_.slider(), ids.mixerOsc3Level);
```

`layoutCanvas()` — in the bottom-control-row block, after `layoutCells(vastDspSection_.contentBounds(), { { &algoLbl_, &algo_ } });`, add:

```cpp
        layoutCells(mixerSection_.contentBounds(),
                    { { nullptr, &mixVco1_ }, { nullptr, &mixVco2_ }, { nullptr, &mixVco3_ } });
```

- [ ] **Step 4: Version bump**

`CMakeLists.txt` line 2: `project(k2000 VERSION 5.4.0 LANGUAGES C CXX)` → `project(k2000 VERSION 5.5.0 LANGUAGES C CXX)` (release-surface rule; the header label derives from `JucePlugin_VersionString`).

The `version-claims` drift rule compares every `plugin SemVer X.Y.Z` claim in `docs/` to CMake — exactly three headers currently claim `5.4.0` and must change to `5.5.0` in this same commit:
- `docs/superpowers/specs/2026-06-29-filter-validation-internal-design.md:3`
- `docs/superpowers/specs/2026-07-01-device-characterization-core-design.md:3`
- `docs/superpowers/specs/2026-07-02-anti-drift-harness-design.md:3`

- [ ] **Step 5: Build, suite, snapshot — verify the finished Stage 2 panel**

Run: `cmake --build build --target k2000_tests k2000_Standalone k2000_panel_snapshot -j4 && ./build/tests/k2000_tests 2>&1 | tee build/last-test-run.log | tail -1`
Expected: clean build; `Summary: 298 tests, 0 failed`.

Run: `./build/tests/k2000_panel_snapshot build/panel-stage2-full.png && echo done`
View at 100% full-frame. Expected (fresh default state):
- OSC BLEND panel live with three knobs reading `100%` / `0%` / `0%`;
- header title `Bernie  v5.5.0`; OUTPUT box `-9.0 dB`;
- VCF boxes: Cutoff `1000 Hz`, Reso `0.20`, HP Cut `Off`, HP Reso `0.00`, Sep `0.00 oct`, Post Drv `0.00`;
- AMP ENV boxes: A `5 ms`, D `100 ms`, S `0.80`, R `200 ms`;
- footer Level `0.0 dB`, Key/Vel integers unchanged;
- no raw multi-decimal float anywhere on the panel.

- [ ] **Step 6: Full gate**

Run: `tools/drift-check --ci --suite-log build/last-test-run.log 2>/dev/null || tools/drift-check --session`
Expected: all checks OK (count claims 298, catalog covers 298, version-claims all 5.5.0 after Step 4's three doc updates).

- [ ] **Step 7: Commit**

```bash
git add src/PluginEditor.h src/PluginEditor.cpp CMakeLists.txt \
        docs/superpowers/specs/2026-06-29-filter-validation-internal-design.md \
        docs/superpowers/specs/2026-07-01-device-characterization-core-design.md \
        docs/superpowers/specs/2026-07-02-anti-drift-harness-design.md
git commit -m "feat(gui): Osc Blend mixer knobs + instrument-style readouts everywhere; v5.5.0

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## After the plan (session-level, not tasks)

1. **Local acceptance:** relaunch the Standalone for the user (`nohup build/k2000_artefacts/Release/Standalone/Bernie ...`) and iterate visually (sizing latitude is in the Global Constraints). Check live: layer 0↔1 rebind on all 24 new controls, Moog↔Huggett switching untouched, typed values (`2.5 kHz`, `48%`) parse, audible VCO blending/mix balancing, default patch sounds identical to the old single-saw voice.
2. **Windows CI smoke:** `gh workflow run build.yml --ref feat/gui-stage2`, download the artifact, hand over the inner `Bernie.vst3` DLL **with its SHA256** for the Ableton pass (the user's DAW copy is stale since PR #21).
3. PR to `main` (remember: `gh pr edit` is broken — use `gh api -X PATCH ...`; merge needs `--admin`).
