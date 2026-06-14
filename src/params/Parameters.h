#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "ParamSnapshot.h"

namespace params {

// Stable string IDs. Used in APVTS, in preset state, and in GUI attachments.
// v2: Layer-scoped params live under the `layer.*` namespace so v4's
// multi-Layer Programs can become `layer[0].*`, `layer[1].*` additively.
// master.gain stays top-level (it sits downstream of the Program mix).
// A v1→v2 migration shim (see PluginProcessor::setStateInformation) rewrites
// old flat IDs on load.
namespace id {
    inline constexpr auto oscWaveform   = "layer.osc.waveform";
    inline constexpr auto oscCoarse     = "layer.osc.coarse";
    inline constexpr auto oscFine       = "layer.osc.fine";

    inline constexpr auto svfType       = "layer.slot0.type";
    inline constexpr auto svfCutoff     = "layer.slot0.cutoff";
    inline constexpr auto svfResonance  = "layer.slot0.resonance";

    inline constexpr auto wsDrive       = "layer.slot1.drive";
    inline constexpr auto wsMix         = "layer.slot1.mix";

    inline constexpr auto ampAttack     = "layer.amp.attack";
    inline constexpr auto ampDecay      = "layer.amp.decay";
    inline constexpr auto ampSustain    = "layer.amp.sustain";
    inline constexpr auto ampRelease    = "layer.amp.release";

    inline constexpr auto masterGain    = "master.gain";

    inline constexpr auto algorithm     = "layer.algorithm";
}

// Build the parameter layout. Called from PluginProcessor's APVTS constructor.
juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

// Read the current values from the APVTS into a snapshot. RT-safe — reads
// each parameter's atomic raw-value pointer (no locks, no allocation).
ParamSnapshot snapshot(const juce::AudioProcessorValueTreeState& apvts);

} // namespace params
