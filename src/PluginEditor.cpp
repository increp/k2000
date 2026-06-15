#include "PluginEditor.h"
#include "params/Parameters.h"
#include "dsp/AlgorithmLibrary.h"

K2000AudioProcessorEditor::K2000AudioProcessorEditor(K2000AudioProcessor& p)
    : juce::AudioProcessorEditor(&p), processorRef(p) {
    addSlider(oscCoarse, "Coarse", params::layerIds(0).oscCoarse);
    addSlider(oscFine,   "Fine",   params::layerIds(0).oscFine);
    addSlider(svfCutoff, "Cutoff",     params::layerIds(0).filterCutoff);
    addSlider(svfRes,    "Resonance",  params::layerIds(0).filterResonance);
    addSlider(wsDrive,   "Drive", params::layerIds(0).shaperDrive);
    addSlider(wsMix,     "Mix",   params::layerIds(0).shaperMix);
    addSlider(ampA,      "A", params::layerIds(0).ampAttack);
    addSlider(ampD,      "D", params::layerIds(0).ampDecay);
    addSlider(ampS,      "S", params::layerIds(0).ampSustain);
    addSlider(ampR,      "R", params::layerIds(0).ampRelease);
    addSlider(masterGain,"Gain", params::masterGain);

    addCombo(oscWave, "Wave",       params::layerIds(0).oscWaveform,
             juce::StringArray{"Saw", "Square", "Triangle", "Sine"});
    addCombo(svfType, "Filter",     params::layerIds(0).filterType,
             juce::StringArray{"LP", "HP", "BP", "Notch"});

    juce::StringArray algoItems;
    for (std::size_t i = 0; i < AlgorithmLibrary::count(); ++i)
        algoItems.add(AlgorithmLibrary::byIndex(i).displayName);
    addCombo(algo, "Algo", params::layerIds(0).algorithm, algoItems);

    setSize(720, 360);
}

void K2000AudioProcessorEditor::addSlider(LabeledSlider& ls,
                                          juce::StringRef label,
                                          juce::StringRef paramId) {
    ls.label.setText(label, juce::dontSendNotification);
    ls.label.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(ls.label);
    addAndMakeVisible(ls.slider);
    ls.attach = std::make_unique<SliderAtt>(processorRef.apvts(), paramId, ls.slider);
}

void K2000AudioProcessorEditor::addCombo(LabeledCombo& lc,
                                         juce::StringRef label,
                                         juce::StringRef paramId,
                                         const juce::StringArray& items) {
    lc.label.setText(label, juce::dontSendNotification);
    lc.label.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(lc.label);
    lc.combo.addItemList(items, 1);
    addAndMakeVisible(lc.combo);
    lc.attach = std::make_unique<ComboAtt>(processorRef.apvts(), paramId, lc.combo);
}

void K2000AudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour::fromRGB(28, 28, 32));
    g.setColour(juce::Colours::white);
    g.setFont(16.0f);
    g.drawText("k2000 — v2", 12, 8, 200, 20, juce::Justification::left);
}

void K2000AudioProcessorEditor::resized() {
    auto area = getLocalBounds().reduced(12).withTrimmedTop(28);
    const int rowH = area.getHeight() / 4;

    auto layoutRow = [&](juce::Rectangle<int> row,
                         std::initializer_list<std::pair<juce::Component*, juce::Component*>> cells) {
        const int n = int(cells.size());
        const int w = row.getWidth() / n;
        int x = row.getX();
        for (auto& [labelC, knobC] : cells) {
            labelC->setBounds(x, row.getY(), w, 18);
            knobC->setBounds(x, row.getY() + 18, w, row.getHeight() - 18);
            x += w;
        }
    };

    layoutRow(area.removeFromTop(rowH),
              {{&oscWave.label, &oscWave.combo},
               {&oscCoarse.label, &oscCoarse.slider},
               {&oscFine.label, &oscFine.slider}});

    layoutRow(area.removeFromTop(rowH),
              {{&svfType.label, &svfType.combo},
               {&svfCutoff.label, &svfCutoff.slider},
               {&svfRes.label, &svfRes.slider},
               {&wsDrive.label, &wsDrive.slider},
               {&wsMix.label, &wsMix.slider}});

    layoutRow(area.removeFromTop(rowH),
              {{&ampA.label, &ampA.slider},
               {&ampD.label, &ampD.slider},
               {&ampS.label, &ampS.slider},
               {&ampR.label, &ampR.slider}});

    layoutRow(area.removeFromTop(rowH),
              {{&algo.label, &algo.combo},
               {&masterGain.label, &masterGain.slider}});
}
