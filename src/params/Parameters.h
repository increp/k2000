#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "ParamSnapshot.h"
#include "../LayerRouting.h"

namespace params {

inline constexpr int kNumLayers = 2;

// Per-layer parameter IDs. Built once into a static table (juce::Strings), so
// snapshot()/routing() read via stable ids with no per-block string building.
struct LayerIds {
    juce::String algorithm, oscWaveform, oscCoarse, oscFine,
                 filterType, filterCutoff, filterResonance,
                 shaperDrive, shaperMix,
                 ampAttack, ampDecay, ampSustain, ampRelease,
                 enable, keyLo, keyHi, velLo, velHi, channel, level;
};

// Returns a reference to the (statically built) ids for the given layer.
const LayerIds& layerIds(int layer);

inline constexpr auto masterGain = "master.gain";

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

// DSP params for one layer → snapshot (RT-safe: reads cached atomics by id).
ParamSnapshot snapshot(const juce::AudioProcessorValueTreeState& apvts, int layer);

// Routing params for one layer → LayerRouting; also returns the layer's output
// level as a linear gain via the out-param.
LayerRouting routing(const juce::AudioProcessorValueTreeState& apvts, int layer,
                     float& levelGainOut);

} // namespace params
