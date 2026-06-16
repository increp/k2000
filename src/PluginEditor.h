#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "gui/SummitLookAndFeel.h"
#include "gui/LabeledKnob.h"
#include "gui/Section.h"
#include "gui/ParamBinder.h"

class K2000AudioProcessorEditor : public juce::AudioProcessorEditor {
public:
    explicit K2000AudioProcessorEditor(K2000AudioProcessor& p);
    ~K2000AudioProcessorEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    K2000AudioProcessor& processorRef;
    SummitLookAndFeel    lnf_;
    ParamBinder          binder_{ processorRef.apvts() };

    // --- Top bar ---
    juce::Label    title_;
    juce::Label    editLayerLabel_;
    juce::ComboBox editLayerCombo_;
    int            editLayer_ = 0;
    LabeledKnob    masterGain_{ "Gain" };

    // --- Signal row ---
    Section sourceSection_{ "VAST Source / DSP", /*spine*/ false };
    juce::ComboBox oscWave_, algo_;
    juce::Label    oscWaveLbl_, algoLbl_;
    LabeledKnob    oscCoarse_{ "Coarse" }, oscFine_{ "Fine" },
                   shaperDrive_{ "Drive" }, shaperMix_{ "Mix" };

    Section mixerSection_{ "Mixer", /*spine*/ true, /*reserved*/ true };

    Section filterSection_{ "Filter", /*spine*/ true };
    juce::ComboBox filterType_;
    juce::Label    filterTypeLbl_;
    LabeledKnob    filterCutoff_{ "Cutoff" }, filterRes_{ "Reso" };

    Section driveSection_{ "Drive", /*spine*/ true, /*reserved*/ true };
    Section ampSection_{ "Amp", /*spine*/ true, /*reserved*/ true };

    // --- Modulation row ---
    Section ampEnvSection_{ "Amp Env", /*spine*/ true };
    LabeledKnob ampA_{ "A" }, ampD_{ "D" }, ampS_{ "S" }, ampR_{ "R" };

    Section modEnvSection_{ "Mod Envs", /*spine*/ true, /*reserved*/ true };
    Section lfoSection_{ "LFO 1-4", /*spine*/ true, /*reserved*/ true };
    Section modMatrixSection_{ "Mod Matrix", /*spine*/ true, /*reserved*/ true };
    Section fxSection_{ "FX Chains", /*spine*/ false, /*reserved*/ true };

    // --- Routing strip ---
    Section routingSection_{ "Layer Routing", /*spine*/ false };
    juce::ToggleButton enable_;
    juce::Label        enableLbl_;
    LabeledKnob        keyLo_{ "Key Lo" }, keyHi_{ "Key Hi" },
                       velLo_{ "Vel Lo" }, velHi_{ "Vel Hi" }, level_{ "Level" };
    juce::ComboBox     channel_;
    juce::Label        channelLbl_;

    void buildStaticControls();   // combos' item lists, labels, child attach (once)
    void bindLayer(int layer);    // (re)bind every per-layer control via binder_

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(K2000AudioProcessorEditor)
};
