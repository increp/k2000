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
