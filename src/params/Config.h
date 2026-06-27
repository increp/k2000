#pragma once

// Engine-wide configuration constants. Deliberately dependency-free (no JUCE) so
// core types like Program can read them without pulling in the parameter/plugin
// layer (Program once included all of Parameters.h — and thus juce_audio_processors
// — just for kNumLayers).
namespace params {

inline constexpr int kNumLayers = 2;

}  // namespace params
