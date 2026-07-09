#include <array>
#include "Parameters.h"
#include "../dsp/AlgorithmLibrary.h"
#include "../dsp/spine/FilterModelLibrary.h"
#include "../dsp/spine/SpineState.h"
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
    id.osc1Coarse         = p + "osc1.coarse";
    id.osc1Fine           = p + "osc1.fine";
    id.osc1BlendSine      = p + "osc1.blend.sine";
    id.osc1BlendTriangle  = p + "osc1.blend.triangle";
    id.osc1BlendSaw       = p + "osc1.blend.saw";
    id.osc1BlendPulse     = p + "osc1.blend.pulse";
    id.osc1PulseDuty      = p + "osc1.blend.pulseDuty";
    id.osc2Coarse         = p + "osc2.coarse";
    id.osc2Fine           = p + "osc2.fine";
    id.osc2BlendSine      = p + "osc2.blend.sine";
    id.osc2BlendTriangle  = p + "osc2.blend.triangle";
    id.osc2BlendSaw       = p + "osc2.blend.saw";
    id.osc2BlendPulse     = p + "osc2.blend.pulse";
    id.osc2PulseDuty      = p + "osc2.blend.pulseDuty";
    id.osc3Coarse         = p + "osc3.coarse";
    id.osc3Fine           = p + "osc3.fine";
    id.osc3BlendSine      = p + "osc3.blend.sine";
    id.osc3BlendTriangle  = p + "osc3.blend.triangle";
    id.osc3BlendSaw       = p + "osc3.blend.saw";
    id.osc3BlendPulse     = p + "osc3.blend.pulse";
    id.osc3PulseDuty      = p + "osc3.blend.pulseDuty";
    id.mixerOsc1Level     = p + "mixer.osc1.level";
    id.mixerOsc2Level     = p + "mixer.osc2.level";
    id.mixerOsc3Level     = p + "mixer.osc3.level";
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
    id.spineHpCutoff    = p + "spine.hp.cutoff";
    id.spineHpResonance = p + "spine.hp.resonance";
    id.spineHpSlope     = p + "spine.hp.slope";
    id.spinePostDrive       = p + "spine.huggett.postDrive";
    id.spineHuggettRouting  = p + "spine.huggett.routing";
    id.spineMoogMode       = p + "spine.moog.mode";
    id.spineMoogBassAmount = p + "spine.moog.bassAmount";
    id.spineMoogBassWave   = p + "spine.moog.bassWave";
    id.spineMoogBassOctave = p + "spine.moog.bassOctave";
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

        // VCO1/2/3: coarse/fine tuning + 4-way proportional waveform blend + pulse duty.
        // All three default identically for coarse/fine/blend (unison pitch, 100% saw) --
        // only the Mixer level differs (VCO1 audible, VCO2/3 silent) so the default patch
        // sounds identical to today's single-oscillator saw patch.
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.osc1Coarse, 1},
            "VCO1 Coarse " + juce::String(i),
            juce::NormalisableRange<float>{-24.0f, 24.0f, 1.0f}, 0.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.osc1Fine, 1},
            "VCO1 Fine " + juce::String(i),
            juce::NormalisableRange<float>{-100.0f, 100.0f, 0.1f}, 0.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.osc1BlendSine, 1},
            "VCO1 Blend Sine " + juce::String(i),
            juce::NormalisableRange<float>{0.0f, 1.0f, 0.0f}, 0.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.osc1BlendTriangle, 1},
            "VCO1 Blend Triangle " + juce::String(i),
            juce::NormalisableRange<float>{0.0f, 1.0f, 0.0f}, 0.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.osc1BlendSaw, 1},
            "VCO1 Blend Saw " + juce::String(i),
            juce::NormalisableRange<float>{0.0f, 1.0f, 0.0f}, 1.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.osc1BlendPulse, 1},
            "VCO1 Blend Pulse " + juce::String(i),
            juce::NormalisableRange<float>{0.0f, 1.0f, 0.0f}, 0.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.osc1PulseDuty, 1},
            "VCO1 Pulse Duty " + juce::String(i),
            juce::NormalisableRange<float>{0.01f, 0.99f, 0.0f}, 0.5f));

        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.osc2Coarse, 1},
            "VCO2 Coarse " + juce::String(i),
            juce::NormalisableRange<float>{-24.0f, 24.0f, 1.0f}, 0.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.osc2Fine, 1},
            "VCO2 Fine " + juce::String(i),
            juce::NormalisableRange<float>{-100.0f, 100.0f, 0.1f}, 0.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.osc2BlendSine, 1},
            "VCO2 Blend Sine " + juce::String(i),
            juce::NormalisableRange<float>{0.0f, 1.0f, 0.0f}, 0.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.osc2BlendTriangle, 1},
            "VCO2 Blend Triangle " + juce::String(i),
            juce::NormalisableRange<float>{0.0f, 1.0f, 0.0f}, 0.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.osc2BlendSaw, 1},
            "VCO2 Blend Saw " + juce::String(i),
            juce::NormalisableRange<float>{0.0f, 1.0f, 0.0f}, 1.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.osc2BlendPulse, 1},
            "VCO2 Blend Pulse " + juce::String(i),
            juce::NormalisableRange<float>{0.0f, 1.0f, 0.0f}, 0.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.osc2PulseDuty, 1},
            "VCO2 Pulse Duty " + juce::String(i),
            juce::NormalisableRange<float>{0.01f, 0.99f, 0.0f}, 0.5f));

        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.osc3Coarse, 1},
            "VCO3 Coarse " + juce::String(i),
            juce::NormalisableRange<float>{-24.0f, 24.0f, 1.0f}, 0.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.osc3Fine, 1},
            "VCO3 Fine " + juce::String(i),
            juce::NormalisableRange<float>{-100.0f, 100.0f, 0.1f}, 0.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.osc3BlendSine, 1},
            "VCO3 Blend Sine " + juce::String(i),
            juce::NormalisableRange<float>{0.0f, 1.0f, 0.0f}, 0.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.osc3BlendTriangle, 1},
            "VCO3 Blend Triangle " + juce::String(i),
            juce::NormalisableRange<float>{0.0f, 1.0f, 0.0f}, 0.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.osc3BlendSaw, 1},
            "VCO3 Blend Saw " + juce::String(i),
            juce::NormalisableRange<float>{0.0f, 1.0f, 0.0f}, 1.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.osc3BlendPulse, 1},
            "VCO3 Blend Pulse " + juce::String(i),
            juce::NormalisableRange<float>{0.0f, 1.0f, 0.0f}, 0.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.osc3PulseDuty, 1},
            "VCO3 Pulse Duty " + juce::String(i),
            juce::NormalisableRange<float>{0.01f, 0.99f, 0.0f}, 0.5f));

        // Mixer: balances the three VCOs. VCO1 audible by default, VCO2/3 silent
        // (so a fresh patch sounds identical to today's single-oscillator default).
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.mixerOsc1Level, 1},
            "Mixer VCO1 Level " + juce::String(i),
            juce::NormalisableRange<float>{0.0f, 1.0f, 0.0f}, 1.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.mixerOsc2Level, 1},
            "Mixer VCO2 Level " + juce::String(i),
            juce::NormalisableRange<float>{0.0f, 1.0f, 0.0f}, 0.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.mixerOsc3Level, 1},
            "Mixer VCO3 Level " + juce::String(i),
            juce::NormalisableRange<float>{0.0f, 1.0f, 0.0f}, 0.0f));

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
            juce::NormalisableRange<float>{-4.0f, 4.0f, 0.0f}, 0.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.spineDrive, 1},
            "Spine Drive " + juce::String(i),
            juce::NormalisableRange<float>{0.0f, 1.0f, 0.0f}, 0.0f));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.spineOutput, 1},
            "Spine Output " + juce::String(i),
            juce::NormalisableRange<float>{-24.0f, 24.0f, 0.0f}, 0.0f));
        // HP pre-filter has no separate enable: cutoff at the knob's 0 position = OFF
        // (bypassed); any cutoff > 0 engages it. Range starts at 0 so the bottom of the
        // knob is the off position. Default 0 = off.
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.spineHpCutoff, 1},
            "Spine HP Cutoff " + juce::String(i),
            juce::NormalisableRange<float>{0.0f, 20000.0f, 0.0f, 0.25f}, 0.0f));
        // HP resonance capped at 0.15: the OTA HP self-oscillates too hot across
        // its full range, so the knob's full travel maps to 0..0.15 (knob max ==
        // what 15% used to give) for a musically useful range.
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.spineHpResonance, 1},
            "Spine HP Resonance " + juce::String(i),
            juce::NormalisableRange<float>{0.0f, 0.15f, 0.0f}, 0.0f));
        layout.add(std::make_unique<ChoiceParam>(juce::ParameterID{id.spineHpSlope, 1},
            "Spine HP Slope " + juce::String(i), juce::StringArray{"12 dB", "24 dB"}, 0));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.spinePostDrive, 1},
            "Spine Post Drive " + juce::String(i),
            juce::NormalisableRange<float>{0.0f, 1.0f, 0.0f}, 0.0f));
        layout.add(std::make_unique<ChoiceParam>(juce::ParameterID{id.spineHuggettRouting, 1},
            "Spine Routing " + juce::String(i),
            juce::StringArray{ "LP", "BP", "HP", "Notch",
                               util::u8("LP\xE2\x86\x92" "HP"), util::u8("LP\xE2\x86\x92" "BP"), util::u8("HP\xE2\x86\x92" "BP"),
                               "LP+HP", "LP+BP", "HP+BP", "LP+LP", "BP+BP", "HP+HP" }, 0));
        layout.add(std::make_unique<ChoiceParam>(juce::ParameterID{id.spineMoogMode, 1},
            "Moog Mode " + juce::String(i), juce::StringArray{"LP", "BP", "HP"}, 0));
        layout.add(std::make_unique<FloatParam>(juce::ParameterID{id.spineMoogBassAmount, 1},
            "Moog Bass " + juce::String(i),
            juce::NormalisableRange<float>{0.0f, 1.0f, 0.0f}, 0.0f));
        layout.add(std::make_unique<ChoiceParam>(juce::ParameterID{id.spineMoogBassWave, 1},
            "Moog Bass Wave " + juce::String(i),
            juce::StringArray{"Sine", "Triangle", "Saw"}, 0));
        layout.add(std::make_unique<ChoiceParam>(juce::ParameterID{id.spineMoogBassOctave, 1},
            "Moog Bass Octave " + juce::String(i),
            juce::StringArray{"0", "-1 oct", "-2 oct"}, 0));
    }

    layout.add(std::make_unique<FloatParam>(juce::ParameterID{masterGain, 1},
        "Master Gain", juce::NormalisableRange<float>{-60.0f, 6.0f, 0.0f}, -9.0f));
    layout.add(std::make_unique<FloatParam>(juce::ParameterID{spineModelFadeMs, 1},
        "Spine Model Fade",
        juce::NormalisableRange<float>{kMinModelFadeMs, kMaxModelFadeMs, 0.0f}, kDefaultModelFadeMs));

    return layout;
}

ParamSnapshot snapshot(const APVTS& apvts, int layer) {
    const LayerIds& id = layerIds(layer);
    ParamSnapshot s;
    s.oscWaveform  = (int) raw(apvts, id.oscWaveform);
    s.oscCoarse    = raw(apvts, id.oscCoarse);
    s.oscFine      = raw(apvts, id.oscFine);
    s.osc1Coarse        = raw(apvts, id.osc1Coarse);
    s.osc1Fine          = raw(apvts, id.osc1Fine);
    s.osc1BlendSine     = raw(apvts, id.osc1BlendSine);
    s.osc1BlendTriangle = raw(apvts, id.osc1BlendTriangle);
    s.osc1BlendSaw      = raw(apvts, id.osc1BlendSaw);
    s.osc1BlendPulse    = raw(apvts, id.osc1BlendPulse);
    s.osc1PulseDuty     = raw(apvts, id.osc1PulseDuty);
    s.osc2Coarse        = raw(apvts, id.osc2Coarse);
    s.osc2Fine          = raw(apvts, id.osc2Fine);
    s.osc2BlendSine     = raw(apvts, id.osc2BlendSine);
    s.osc2BlendTriangle = raw(apvts, id.osc2BlendTriangle);
    s.osc2BlendSaw      = raw(apvts, id.osc2BlendSaw);
    s.osc2BlendPulse    = raw(apvts, id.osc2BlendPulse);
    s.osc2PulseDuty     = raw(apvts, id.osc2PulseDuty);
    s.osc3Coarse        = raw(apvts, id.osc3Coarse);
    s.osc3Fine          = raw(apvts, id.osc3Fine);
    s.osc3BlendSine     = raw(apvts, id.osc3BlendSine);
    s.osc3BlendTriangle = raw(apvts, id.osc3BlendTriangle);
    s.osc3BlendSaw      = raw(apvts, id.osc3BlendSaw);
    s.osc3BlendPulse    = raw(apvts, id.osc3BlendPulse);
    s.osc3PulseDuty     = raw(apvts, id.osc3PulseDuty);
    s.mixerOsc1Level    = raw(apvts, id.mixerOsc1Level);
    s.mixerOsc2Level    = raw(apvts, id.mixerOsc2Level);
    s.mixerOsc3Level    = raw(apvts, id.mixerOsc3Level);
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
    s.spineModelFadeMs   = raw(apvts, spineModelFadeMs);
    s.spineModel         = (int) raw(apvts, id.spineModel);
    s.spineSeparationOct = raw(apvts, id.spineSeparation);
    s.spineSlope         = (int) raw(apvts, id.spineSlope);
    s.spineDrive         = raw(apvts, id.spineDrive);
    s.spineOutputDb      = raw(apvts, id.spineOutput);
    s.hpCutoffHz      = raw(apvts, id.spineHpCutoff);
    s.hpResonance     = raw(apvts, id.spineHpResonance);
    s.hpSlope         = (int) raw(apvts, id.spineHpSlope);
    s.huggettPostDrive = raw(apvts, id.spinePostDrive);
    s.huggettRouting = (int) raw(apvts, id.spineHuggettRouting);
    s.moogMode       = (int) raw(apvts, id.spineMoogMode);
    s.moogBassAmount =       raw(apvts, id.spineMoogBassAmount);
    s.moogBassWave   = (int) raw(apvts, id.spineMoogBassWave);
    s.moogBassOctave = (int) raw(apvts, id.spineMoogBassOctave);
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
