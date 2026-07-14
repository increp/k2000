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
