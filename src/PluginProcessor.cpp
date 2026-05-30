#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "dsp/DSPBlock.h"

K2000AudioProcessor::K2000AudioProcessor()
    : juce::AudioProcessor(BusesProperties()
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)) {}

void K2000AudioProcessor::prepareToPlay(double, int) {}
void K2000AudioProcessor::releaseResources() {}

bool K2000AudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo()
        || out == juce::AudioChannelSet::mono();
}

void K2000AudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                       juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
}

juce::AudioProcessorEditor* K2000AudioProcessor::createEditor() {
    return new K2000AudioProcessorEditor(*this);
}

void K2000AudioProcessor::getStateInformation(juce::MemoryBlock&) {}
void K2000AudioProcessor::setStateInformation(const void*, int) {}

// JUCE plugin entry point
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new K2000AudioProcessor();
}
