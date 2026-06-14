#include "Parameters.h"
#include "../dsp/AlgorithmLibrary.h"

namespace params {

using APVTS = juce::AudioProcessorValueTreeState;

juce::AudioProcessorValueTreeState::ParameterLayout createLayout() {
    APVTS::ParameterLayout layout;

    using FloatParam  = juce::AudioParameterFloat;
    using ChoiceParam = juce::AudioParameterChoice;

    layout.add(std::make_unique<ChoiceParam>(
        juce::ParameterID{id::oscWaveform, 1},
        "Osc Waveform",
        juce::StringArray{"Saw", "Square", "Triangle", "Sine"}, 0));

    layout.add(std::make_unique<FloatParam>(
        juce::ParameterID{id::oscCoarse, 1}, "Osc Coarse",
        juce::NormalisableRange<float>{-24.0f, 24.0f, 1.0f}, 0.0f));

    layout.add(std::make_unique<FloatParam>(
        juce::ParameterID{id::oscFine, 1}, "Osc Fine",
        juce::NormalisableRange<float>{-100.0f, 100.0f, 0.1f}, 0.0f));

    layout.add(std::make_unique<ChoiceParam>(
        juce::ParameterID{id::svfType, 1},
        "Filter Type",
        juce::StringArray{"LP", "HP", "BP", "Notch"}, 0));

    layout.add(std::make_unique<FloatParam>(
        juce::ParameterID{id::svfCutoff, 1}, "Filter Cutoff",
        juce::NormalisableRange<float>{20.0f, 20000.0f, 0.0f, 0.25f},
        1000.0f));

    layout.add(std::make_unique<FloatParam>(
        juce::ParameterID{id::svfResonance, 1}, "Filter Resonance",
        juce::NormalisableRange<float>{0.0f, 1.0f, 0.0f}, 0.2f));

    layout.add(std::make_unique<FloatParam>(
        juce::ParameterID{id::wsDrive, 1}, "Drive",
        juce::NormalisableRange<float>{0.0f, 1.0f, 0.0f}, 0.0f));

    layout.add(std::make_unique<FloatParam>(
        juce::ParameterID{id::wsMix, 1}, "Drive Mix",
        juce::NormalisableRange<float>{0.0f, 1.0f, 0.0f}, 1.0f));

    layout.add(std::make_unique<FloatParam>(
        juce::ParameterID{id::ampAttack, 1}, "Attack",
        juce::NormalisableRange<float>{0.001f, 5.0f, 0.0f, 0.3f}, 0.005f));

    layout.add(std::make_unique<FloatParam>(
        juce::ParameterID{id::ampDecay, 1}, "Decay",
        juce::NormalisableRange<float>{0.001f, 5.0f, 0.0f, 0.3f}, 0.1f));

    layout.add(std::make_unique<FloatParam>(
        juce::ParameterID{id::ampSustain, 1}, "Sustain",
        juce::NormalisableRange<float>{0.0f, 1.0f, 0.0f}, 0.8f));

    layout.add(std::make_unique<FloatParam>(
        juce::ParameterID{id::ampRelease, 1}, "Release",
        juce::NormalisableRange<float>{0.001f, 5.0f, 0.0f, 0.3f}, 0.2f));

    layout.add(std::make_unique<FloatParam>(
        juce::ParameterID{id::masterGain, 1}, "Master Gain",
        juce::NormalisableRange<float>{-60.0f, 6.0f, 0.0f}, 0.0f));

    juce::StringArray algoNames;
    for (std::size_t i = 0; i < AlgorithmLibrary::count(); ++i)
        algoNames.add(AlgorithmLibrary::byIndex(i).displayName);
    layout.add(std::make_unique<ChoiceParam>(
        juce::ParameterID{id::algorithm, 1}, "Algorithm", algoNames, 0));

    return layout;
}

// Safe to deref unconditionally: callers only pass params::id:: constants,
// all of which createLayout() registers, so the lookup never returns null.
// jassert guards against a future param added to id:: but missed in the layout.
static float raw(const APVTS& apvts, juce::StringRef id) {
    auto* p = apvts.getRawParameterValue(id);
    jassert(p != nullptr);
    return p->load();
}

ParamSnapshot snapshot(const APVTS& apvts) {
    ParamSnapshot s;
    s.oscWaveform   = (int) raw(apvts, id::oscWaveform);
    s.oscCoarse     = raw(apvts, id::oscCoarse);
    s.oscFine       = raw(apvts, id::oscFine);
    s.svfType       = (int) raw(apvts, id::svfType);
    s.svfCutoffHz   = raw(apvts, id::svfCutoff);
    s.svfResonance  = raw(apvts, id::svfResonance);
    s.wsDrive       = raw(apvts, id::wsDrive);
    s.wsMix         = raw(apvts, id::wsMix);
    s.ampAttackS    = raw(apvts, id::ampAttack);
    s.ampDecayS     = raw(apvts, id::ampDecay);
    s.ampSustain    = raw(apvts, id::ampSustain);
    s.ampReleaseS   = raw(apvts, id::ampRelease);
    s.masterGainDb  = raw(apvts, id::masterGain);
    s.algorithmId   = (int) raw(apvts, id::algorithm);
    return s;
}

} // namespace params
