#include "PluginEditor.h"
#include "params/Parameters.h"
#include "dsp/AlgorithmLibrary.h"

K2000AudioProcessorEditor::K2000AudioProcessorEditor(K2000AudioProcessor& p)
    : juce::AudioProcessorEditor(&p), processorRef(p) {

    // masterGain is not per-layer — bind it once here
    addSlider(masterGain, "Gain", params::masterGain);

    // Edit-layer selector (editor-local state, not APVTS)
    editLayerLabel.setText("Edit Layer", juce::dontSendNotification);
    editLayerLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(editLayerLabel);

    for (int i = 0; i < params::kNumLayers; ++i)
        editLayerCombo.addItem("Layer " + juce::String(i), i + 1);
    editLayerCombo.setSelectedId(1, juce::dontSendNotification);
    addAndMakeVisible(editLayerCombo);

    editLayerCombo.onChange = [this] {
        editLayer_ = editLayerCombo.getSelectedId() - 1;
        bindLayer(editLayer_);
    };

    // Enable toggle label (visible once; button is added in bindLayer)
    enableLabel.setText("Enable", juce::dontSendNotification);
    enableLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(enableLabel);
    addAndMakeVisible(enableButton);

    // Bind all per-layer controls to layer 0
    bindLayer(0);

    setSize(720, 600);
}

// ---------------------------------------------------------------------------
// Bind all per-layer controls (DSP + routing) to the given layer.
// Recreating an attachment (via unique_ptr assignment) destroys the old one
// first, which detaches it from the APVTS before the new one is created.
// ---------------------------------------------------------------------------
void K2000AudioProcessorEditor::bindLayer(int layer) {
    const auto& ids = params::layerIds(layer);

    // --- DSP controls ---
    addSlider(oscCoarse, "Coarse",    ids.oscCoarse);
    addSlider(oscFine,   "Fine",      ids.oscFine);
    addSlider(svfCutoff, "Cutoff",    ids.filterCutoff);
    addSlider(svfRes,    "Resonance", ids.filterResonance);
    addSlider(wsDrive,   "Drive",     ids.shaperDrive);
    addSlider(wsMix,     "Mix",       ids.shaperMix);
    addSlider(ampA,      "A",         ids.ampAttack);
    addSlider(ampD,      "D",         ids.ampDecay);
    addSlider(ampS,      "S",         ids.ampSustain);
    addSlider(ampR,      "R",         ids.ampRelease);

    addCombo(oscWave, "Wave",   ids.oscWaveform,
             juce::StringArray{"Saw", "Square", "Triangle", "Sine"});
    addCombo(svfType, "Filter", ids.filterType,
             juce::StringArray{"LP", "HP", "BP", "Notch"});

    juce::StringArray algoItems;
    for (std::size_t i = 0; i < AlgorithmLibrary::count(); ++i)
        algoItems.add(AlgorithmLibrary::byIndex(i).displayName);
    addCombo(algo, "Algo", ids.algorithm, algoItems);

    // --- Routing strip ---
    // Enable toggle
    enableAttach.reset();
    enableAttach = std::make_unique<ButtonAtt>(processorRef.apvts(), ids.enable, enableButton);

    // Key / vel / level sliders
    addSlider(keyLo,  "Key Lo",  ids.keyLo);
    addSlider(keyHi,  "Key Hi",  ids.keyHi);
    addSlider(velLo,  "Vel Lo",  ids.velLo);
    addSlider(velHi,  "Vel Hi",  ids.velHi);
    addSlider(level,  "Level",   ids.level);

    // Channel combo: "Omni", "1" .. "16"
    juce::StringArray chanItems{"Omni"};
    for (int ch = 1; ch <= 16; ++ch)
        chanItems.add(juce::String(ch));
    addCombo(channel, "Channel", ids.channel, chanItems);
}

// ---------------------------------------------------------------------------
// Helpers — add a component the first time; subsequent calls just rebind.
// ---------------------------------------------------------------------------
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
    lc.combo.clear(juce::dontSendNotification);
    lc.combo.addItemList(items, 1);
    addAndMakeVisible(lc.combo);
    lc.attach = std::make_unique<ComboAtt>(processorRef.apvts(), paramId, lc.combo);
}

void K2000AudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour::fromRGB(28, 28, 32));
    g.setColour(juce::Colours::white);
    g.setFont(16.0f);
    // Derived from the build version so it never goes stale (see memory:
    // release_version_surface). Plain ASCII to avoid font/encoding issues.
    g.drawText(juce::String("k2000  v") + JucePlugin_VersionString,
               12, 8, 240, 20, juce::Justification::left);
}

void K2000AudioProcessorEditor::resized() {
    auto area = getLocalBounds().reduced(12).withTrimmedTop(28);
    const int rowH = 90;

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

    // Row 0: edit-layer selector (flat strip, 30px tall)
    {
        auto row = area.removeFromTop(30);
        const int w = 120;
        editLayerLabel.setBounds(row.getX(), row.getY(), w, row.getHeight());
        editLayerCombo.setBounds(row.getX() + w, row.getY(), w, row.getHeight());
    }
    area.removeFromTop(4); // spacer

    // Row 1: oscillator
    layoutRow(area.removeFromTop(rowH),
              {{&oscWave.label, &oscWave.combo},
               {&oscCoarse.label, &oscCoarse.slider},
               {&oscFine.label, &oscFine.slider}});

    // Row 2: filter + shaper
    layoutRow(area.removeFromTop(rowH),
              {{&svfType.label, &svfType.combo},
               {&svfCutoff.label, &svfCutoff.slider},
               {&svfRes.label, &svfRes.slider},
               {&wsDrive.label, &wsDrive.slider},
               {&wsMix.label, &wsMix.slider}});

    // Row 3: envelope
    layoutRow(area.removeFromTop(rowH),
              {{&ampA.label, &ampA.slider},
               {&ampD.label, &ampD.slider},
               {&ampS.label, &ampS.slider},
               {&ampR.label, &ampR.slider}});

    // Row 4: algo + master gain
    layoutRow(area.removeFromTop(rowH),
              {{&algo.label, &algo.combo},
               {&masterGain.label, &masterGain.slider}});

    // Row 5: routing strip — full-height row so the rotary knobs actually render
    // (LabeledSlider is a rotary + textbox; a short box draws nothing).
    area.removeFromTop(6); // spacer
    {
        auto row = area.removeFromTop(rowH);
        const int cellW = row.getWidth() / 7;
        int x = row.getX();

        // Enable toggle — give it a real, clickable square
        enableLabel.setBounds(x, row.getY(), cellW, 18);
        enableButton.setBounds(x + cellW / 2 - 14, row.getY() + 28, 28, 28);
        x += cellW;

        auto placeSlider = [&](LabeledSlider& ls) {
            ls.label.setBounds(x, row.getY(), cellW, 18);
            ls.slider.setBounds(x, row.getY() + 18, cellW, row.getHeight() - 18);
            x += cellW;
        };
        placeSlider(keyLo);
        placeSlider(keyHi);
        placeSlider(velLo);
        placeSlider(velHi);
        placeSlider(level);

        // Channel combo
        channel.label.setBounds(x, row.getY(), cellW, 18);
        channel.combo.setBounds(x, row.getY() + 18, cellW, 26);
    }
}
