#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class K2000AudioProcessorEditor : public juce::AudioProcessorEditor {
public:
    explicit K2000AudioProcessorEditor(K2000AudioProcessor& p);
    ~K2000AudioProcessorEditor() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    K2000AudioProcessor& processorRef;

    using APVTS = juce::AudioProcessorValueTreeState;
    using SliderAtt  = APVTS::SliderAttachment;
    using ComboAtt   = APVTS::ComboBoxAttachment;
    using ButtonAtt  = APVTS::ButtonAttachment;

    struct LabeledSlider {
        juce::Label  label;
        juce::Slider slider{ juce::Slider::RotaryHorizontalVerticalDrag,
                             juce::Slider::TextBoxBelow };
        std::unique_ptr<SliderAtt> attach;
    };
    struct LabeledCombo {
        juce::Label    label;
        juce::ComboBox combo;
        std::unique_ptr<ComboAtt> attach;
    };

    // DSP controls (per-layer, rebound by bindLayer)
    LabeledSlider oscCoarse, oscFine,
                  svfCutoff, svfRes,
                  wsDrive, wsMix,
                  ampA, ampD, ampS, ampR,
                  masterGain;
    LabeledCombo  oscWave, svfType, algo;

    // Routing strip (per-layer, rebound by bindLayer)
    juce::Label         enableLabel;
    juce::ToggleButton  enableButton;
    std::unique_ptr<ButtonAtt> enableAttach;

    LabeledSlider keyLo, keyHi, velLo, velHi, level;
    LabeledCombo  channel;

    // Edit-layer selector (editor-local, not an APVTS param)
    juce::Label    editLayerLabel;
    juce::ComboBox editLayerCombo;
    int editLayer_ = 0;

    void addSlider(LabeledSlider& ls, juce::StringRef label, juce::StringRef paramId);
    void addCombo(LabeledCombo& lc, juce::StringRef label, juce::StringRef paramId,
                  const juce::StringArray& items);
    void bindLayer(int layer);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(K2000AudioProcessorEditor)
};
