#include <array>
#include "Parameters.h"
#include "../dsp/AlgorithmLibrary.h"
#include "../dsp/spine/FilterModelLibrary.h"
#include "../util/Utf8.h"

namespace params {

using APVTS = juce::AudioProcessorValueTreeState;
using FloatParam  = juce::AudioParameterFloat;
using ChoiceParam = juce::AudioParameterChoice;
using BoolParam   = juce::AudioParameterBool;

namespace {
juce::String pfx(int layer) { return "layer" + juce::String(layer) + "."; }

LayerIds buildIds(int layer) {
    const juce::String p = pfx(layer);
    LayerIds id;
    id.algorithm       = p + "algorithm";
    id.oscWaveform     = p + "osc.waveform";
    id.oscCoarse       = p + "osc.coarse";
    id.oscFine         = p + "osc.fine";
    id.filterType      = p + "filter.type";
    id.filterCutoff    = p + "filter.cutoff";
    id.filterResonance = p + "filter.resonance";
    id.shaperDrive     = p + "shaper.drive";
    id.shaperMix       = p + "shaper.mix";
    id.ampAttack       = p + "amp.attack";
    id.ampDecay        = p + "amp.decay";
    id.ampSustain      = p + "amp.sustain";
    id.ampRelease      = p + "amp.release";
    id.enable          = p + "enable";
    id.keyLo           = p + "keyLo";
    id.keyHi           = p + "keyHi";
    id.velLo           = p + "velLo";
    id.velHi           = p + "velHi";
    id.channel         = p + "channel";
    id.level           = p + "level";
    id.spineModel      = p + "spine.filterModel";
    id.spineSeparation = p + "spine.separation";
    id.spineSlope      = p + "spine.slope";
    id.spineDrive      = p + "spine.drive";
    id.spineOutput     = p + "spine.output";
    return id;
}

const std::array<LayerIds, kNumLayers>& idTable() {
    static const std::array<LayerIds, kNumLayers> t = [] {
        std::array<LayerIds, kNumLayers> a;
        for (int i = 0; i < kNumLayers; ++i) a[(std::size_t) i] = buildIds(i);
        return a;
    }();
    return t;
}

juce::StringArray channelChoices() {
    juce::StringArray s; s.add("Omni");
    for (int c = 1; c <= 16; ++c) s.add(juce::String(c));
    return s;
}

float raw(const APVTS& apvts, const juce::String& id) {
    auto* p = apvts.getRawParameterValue(id);
    jassert(p != nullptr);
    return p->load();
}
}  // namespace

const LayerIds& layerIds(int layer) { return idTable()[(std::size_t) layer]; }

juce::StringArray algoNames() {
    juce::StringArray s;
    for (std::size_t i = 0; i < AlgorithmLibrary::count(); ++i)
        s.add(util::u8(AlgorithmLibrary::byIndex(i).displayName));
    return s;
}

juce::StringArray algoNamesSpine() { return FilterModelLibrary::names(); }

APVTS::ParameterLayout createLayout() {
    APVTS::ParameterLayout layout;

    for (int i = 0; i < kNumLayers; ++i) {
        const LayerIds& id = layerIds(i);

        layout.add(std::make_unique<ChoiceParam>(juce::ParameterID{id.algorithm, 1},
            "Algorithm " + juce::String(i), algoNames(), 0));
        layout.add(std::make_unique<ChoiceParam>(juce::ParameterID{id.oscWaveform, 1},
            "Osc Waveform " + juce::String(i),
            juce::StringArray{"Saw", "Square", "Triangle", "Sine"}, 0));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.oscCoarse, 1},
            "Osc Coarse " + juce::String(i),
            juce::NormalisableRange<float>{-24.0f, 24.0f, 1.0f}, 0.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.oscFine, 1},
            "Osc Fine " + juce::String(i),
            juce::NormalisableRange<float>{-100.0f, 100.0f, 0.1f}, 0.0f));
        layout.add(std::make_unique<ChoiceParam>(juce::ParameterID{id.filterType, 1},
            "Filter Type " + juce::String(i),
            juce::StringArray{"LP", "HP", "BP", "Notch"}, 0));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.filterCutoff, 1},
            "Filter Cutoff " + juce::String(i),
            juce::NormalisableRange<float>{20.0f, 20000.0f, 0.0f, 0.25f}, 1000.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.filterResonance, 1},
            "Filter Resonance " + juce::String(i),
            juce::NormalisableRange<float>{0.0f, 1.0f, 0.0f}, 0.2f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.shaperDrive, 1},
            "Drive " + juce::String(i),
            juce::NormalisableRange<float>{0.0f, 1.0f, 0.0f}, 0.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.shaperMix, 1},
            "Drive Mix " + juce::String(i),
            juce::NormalisableRange<float>{0.0f, 1.0f, 0.0f}, 1.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.ampAttack, 1},
            "Attack " + juce::String(i),
            juce::NormalisableRange<float>{0.001f, 5.0f, 0.0f, 0.3f}, 0.005f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.ampDecay, 1},
            "Decay " + juce::String(i),
            juce::NormalisableRange<float>{0.001f, 5.0f, 0.0f, 0.3f}, 0.1f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.ampSustain, 1},
            "Sustain " + juce::String(i),
            juce::NormalisableRange<float>{0.0f, 1.0f, 0.0f}, 0.8f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.ampRelease, 1},
            "Release " + juce::String(i),
            juce::NormalisableRange<float>{0.001f, 5.0f, 0.0f, 0.3f}, 0.2f));

        // Routing. layer0 enabled by default, others off -> v3 presets sound the same.
        layout.add(std::make_unique<BoolParam>(juce::ParameterID{id.enable, 1},
            "Layer " + juce::String(i) + " Enable", i == 0));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.keyLo, 1},
            "Key Low " + juce::String(i),
            juce::NormalisableRange<float>{0.0f, 127.0f, 1.0f}, 0.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.keyHi, 1},
            "Key High " + juce::String(i),
            juce::NormalisableRange<float>{0.0f, 127.0f, 1.0f}, 127.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.velLo, 1},
            "Vel Low " + juce::String(i),
            juce::NormalisableRange<float>{1.0f, 127.0f, 1.0f}, 1.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.velHi, 1},
            "Vel High " + juce::String(i),
            juce::NormalisableRange<float>{1.0f, 127.0f, 1.0f}, 127.0f));
        layout.add(std::make_unique<ChoiceParam>(juce::ParameterID{id.channel, 1},
            "MIDI Channel " + juce::String(i), channelChoices(), 0));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.level, 1},
            "Level " + juce::String(i),
            juce::NormalisableRange<float>{-60.0f, 6.0f, 0.0f}, 0.0f));

        layout.add(std::make_unique<ChoiceParam>(juce::ParameterID{id.spineModel, 1},
            "Spine Filter " + juce::String(i), algoNamesSpine(), 0));
        layout.add(std::make_unique<ChoiceParam>(juce::ParameterID{id.spineSlope, 1},
            "Spine Slope " + juce::String(i), juce::StringArray{"12 dB", "24 dB"}, 1));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.spineSeparation, 1},
            "Spine Separation " + juce::String(i),
            juce::NormalisableRange<float>{-2.0f, 2.0f, 0.0f}, 0.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.spineDrive, 1},
            "Spine Drive " + juce::String(i),
            juce::NormalisableRange<float>{0.0f, 1.0f, 0.0f}, 0.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.spineOutput, 1},
            "Spine Output " + juce::String(i),
            juce::NormalisableRange<float>{-24.0f, 24.0f, 0.0f}, 0.0f));
    }

    layout.add(std::make_unique<FloatParam>(juce::ParameterID{masterGain, 1},
        "Master Gain", juce::NormalisableRange<float>{-60.0f, 6.0f, 0.0f}, 0.0f));

    return layout;
}

ParamSnapshot snapshot(const APVTS& apvts, int layer) {
    const LayerIds& id = layerIds(layer);
    ParamSnapshot s;
    s.oscWaveform  = (int) raw(apvts, id.oscWaveform);
    s.oscCoarse    = raw(apvts, id.oscCoarse);
    s.oscFine      = raw(apvts, id.oscFine);
    s.svfType      = (int) raw(apvts, id.filterType);
    s.svfCutoffHz  = raw(apvts, id.filterCutoff);
    s.svfResonance = raw(apvts, id.filterResonance);
    s.wsDrive      = raw(apvts, id.shaperDrive);
    s.wsMix        = raw(apvts, id.shaperMix);
    s.ampAttackS   = raw(apvts, id.ampAttack);
    s.ampDecayS    = raw(apvts, id.ampDecay);
    s.ampSustain   = raw(apvts, id.ampSustain);
    s.ampReleaseS  = raw(apvts, id.ampRelease);
    s.algorithmId        = (int) raw(apvts, id.algorithm);
    s.masterGainDb       = raw(apvts, masterGain);
    s.spineModel         = (int) raw(apvts, id.spineModel);
    s.spineSeparationOct = raw(apvts, id.spineSeparation);
    s.spineSlope         = (int) raw(apvts, id.spineSlope);
    s.spineDrive         = raw(apvts, id.spineDrive);
    s.spineOutputDb      = raw(apvts, id.spineOutput);
    return s;
}

LayerRouting routing(const APVTS& apvts, int layer, float& levelGainOut) {
    const LayerIds& id = layerIds(layer);
    LayerRouting r;
    r.enable  = raw(apvts, id.enable) >= 0.5f;
    r.keyLo   = (int) raw(apvts, id.keyLo);
    r.keyHi   = (int) raw(apvts, id.keyHi);
    r.velLo   = (int) raw(apvts, id.velLo);
    r.velHi   = (int) raw(apvts, id.velHi);
    r.channel = (int) raw(apvts, id.channel);  // 0 = Omni, else 1..16
    levelGainOut = juce::Decibels::decibelsToGain(raw(apvts, id.level));
    return r;
}

} // namespace params
