#include "PluginEditor.h"

K2000AudioProcessorEditor::K2000AudioProcessorEditor(K2000AudioProcessor& p)
    : juce::AudioProcessorEditor(&p), processorRef(p) {
    setSize(600, 300);
}

void K2000AudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colours::black);
    g.setColour(juce::Colours::white);
    g.setFont(20.0f);
    g.drawFittedText("k2000 — v1 scaffold", getLocalBounds(),
                     juce::Justification::centred, 1);
}

void K2000AudioProcessorEditor::resized() {}
