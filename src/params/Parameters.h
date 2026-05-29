#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "ParamSnapshot.h"

namespace params {

// Stable string IDs. Used in APVTS, in preset state, and in GUI attachments.
namespace id {
    inline constexpr auto oscWaveform   = "osc.waveform";
    inline constexpr auto oscCoarse     = "osc.coarse";
    inline constexpr auto oscFine       = "osc.fine";

    inline constexpr auto svfType       = "slot0.type";
    inline constexpr auto svfCutoff     = "slot0.cutoff";
    inline constexpr auto svfResonance  = "slot0.resonance";

    inline constexpr auto wsDrive       = "slot1.drive";
    inline constexpr auto wsMix         = "slot1.mix";

    inline constexpr auto ampAttack     = "amp.attack";
    inline constexpr auto ampDecay      = "amp.decay";
    inline constexpr auto ampSustain    = "amp.sustain";
    inline constexpr auto ampRelease    = "amp.release";

    inline constexpr auto masterGain    = "master.gain";
}

// Build the parameter layout. Called from PluginProcessor's APVTS constructor.
juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

// Read the current values from the APVTS into a snapshot. RT-safe — reads
// each parameter's atomic raw-value pointer (no locks, no allocation).
ParamSnapshot snapshot(const juce::AudioProcessorValueTreeState& apvts);

} // namespace params
