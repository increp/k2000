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

    LabeledSlider oscCoarse, oscFine,
                  svfCutoff, svfRes,
                  wsDrive, wsMix,
                  ampA, ampD, ampS, ampR,
                  masterGain;
    LabeledCombo  oscWave, svfType, algo;

    void addSlider(LabeledSlider& ls, juce::StringRef label, juce::StringRef paramId);
    void addCombo(LabeledCombo& lc, juce::StringRef label, juce::StringRef paramId,
                  const juce::StringArray& items);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(K2000AudioProcessorEditor)
};
