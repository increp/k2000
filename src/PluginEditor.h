#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "gui/VintageLookAndFeel.h"
#include "gui/LabeledKnob.h"
#include "gui/Section.h"
#include "gui/ParamBinder.h"

class K2000AudioProcessorEditor : public juce::AudioProcessorEditor,
                                  private juce::Timer {
public:
    explicit K2000AudioProcessorEditor(K2000AudioProcessor& p);
    ~K2000AudioProcessorEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    K2000AudioProcessor& processorRef;
    VintageLookAndFeel   lnf_;

    // --- Header (cream chassis plate) ---
    juce::Label      title_;                  // version label (branding TBD later)
    juce::Label      editLayerLabel_;
    juce::ComboBox   editLayerCombo_;
    int              editLayer_ = 0;
    // Master gain is the header OUTPUT knob (reference mockup, top right).
    juce::Label      masterGainLbl_;
    juce::Slider     masterGain_{ juce::Slider::RotaryHorizontalVerticalDrag,
                                  juce::Slider::TextBoxBelow };
    juce::TextButton menuButton_{ juce::String::fromUTF8("\xE2\x8B\xAE") };

    // --- Main geography (reference mockup) ---
    // Left column: three VCO panels, empty until GUI Stage 2 fills them.
    Section vco1Section_{ "VCO 1", /*spine*/ false, /*reserved*/ true };
    Section vco2Section_{ "VCO 2", /*spine*/ false, /*reserved*/ true };
    Section vco3Section_{ "VCO 3", /*spine*/ false, /*reserved*/ true };

    juce::ComboBox algo_;
    juce::Label    algoLbl_;
    LabeledKnob    shaperDrive_{ "Drive" }, shaperMix_{ "Mix" };   // hidden, still bound

    Section mixerSection_{ "Osc Blend", /*spine*/ true, /*reserved*/ true };
    Section vastDspSection_{ "VAST DSP", /*spine*/ false };
    Section outputSection_{ "Output", /*spine*/ true, /*reserved*/ true };

    Section filterSection_{ "VCF", /*spine*/ true };
    Section filterEnvSection_{ "Filter Env", /*spine*/ true, /*reserved*/ true };
    LabeledKnob    filterCutoff_{ "Cutoff" }, filterRes_{ "Reso" };
    juce::ComboBox spineModel_, spineSlope_;
    juce::Label    spineModelLbl_, spineSlopeLbl_;
    juce::ComboBox spineRouting_;
    juce::Label    spineRoutingLbl_;
    LabeledKnob    spineSeparation_{ "Sep" };
    // HP pre-filter band (Layout B)
    juce::Label        hpSectionLbl_;
    juce::ComboBox     hpSlope_;
    juce::Label        hpSlopeLbl_;
    LabeledKnob        hpCutoff_{ "HP Cut" }, hpReso_{ "HP Reso" };
    LabeledKnob        spinePostDrive_{ "Post Drv" };
    // Moog-only controls (shown when spine model == Moog, hidden otherwise)
    juce::Label    moogModeLbl_, moogWaveLbl_, moogOctaveLbl_;
    juce::ComboBox moogMode_, moogWave_, moogOctave_;
    LabeledKnob    moogBass_{ "Bass" };

    Section ampSection_{ "Amp", /*spine*/ true, /*reserved*/ true };
    // Amp section: safety limiter controls (protected — NOT bound to APVTS)
    juce::Label        safetyLbl_;        // "Safety" caption
    juce::ToggleButton safetyLimiter_;    // protected enable (NOT bound to APVTS)
    juce::Label        limitIndicator_;   // lights when the limiter is reducing gain

    // --- Modulation row ---
    Section ampEnvSection_{ "Amp Env", /*spine*/ true };
    LabeledKnob ampA_{ "A" }, ampD_{ "D" }, ampS_{ "S" }, ampR_{ "R" };

    Section modEnvSection_{ "Mod Envs", /*spine*/ true, /*reserved*/ true };
    Section lfoSection_{ "LFO 1-4", /*spine*/ true, /*reserved*/ true };
    Section modMatrixSection_{ "Mod Matrix", /*spine*/ true, /*reserved*/ true };
    Section fxSection_{ "FX Chains", /*spine*/ false, /*reserved*/ true };

    // --- Routing strip ---
    juce::ToggleButton enable_;
    juce::Label        enableLbl_;
    LabeledKnob        keyLo_{ "Key Lo" }, keyHi_{ "Key Hi" },
                       velLo_{ "Vel Lo" }, velHi_{ "Vel Hi" }, level_{ "Level" };
    juce::ComboBox     channel_;
    juce::Label        channelLbl_;

    // Declared LAST so it is destroyed FIRST: its APVTS attachments reference the
    // control members above and must be detached while those controls are still alive.
    ParamBinder binder_{ processorRef.apvts() };

    void buildStaticControls();      // combos' item lists, labels, child attach (once)
    void bindLayer(int layer);       // (re)bind every per-layer control via binder_
    void updateModelVisibility();    // show/hide Moog vs Huggett model-specific controls
    void showOversamplingMenu();
    void timerCallback() override;
    juce::Rectangle<float> vuWellRect(int index) const;  // header blank VU plates (Stage 3)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(K2000AudioProcessorEditor)
};
