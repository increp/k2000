#pragma once
#include <cstddef>
#include <cstdint>

// Per-voice spine state storage budget + crossfade constants (register Q17/Q18).
// kMaxSpineStateBytes is GOVERNED: a per-model static_assert (see FilterModelLibrary.cpp
// and the test target) fails the build if a model's State exceeds it. On overflow a
// reviewer bumps this (cost: 2 * delta * voices of RAM) or slims/rejects the model.
// Measured: HuggettFilter::VoiceState ~= 176 B (vptr + 2*NlSvfCell(72) + DcBlocker(20)).
// MoogLadder::VoiceState = 1040 B (vptr-align + 2*MoogLadderAdapter(512 B each, kGenBytes=512)).
// 1152 B gives headroom for the Moog 4-pole and future models (v5.2, Q18).
inline constexpr std::size_t kMaxSpineStateBytes = 1152;
inline constexpr std::size_t kSpineStateAlign    = alignof(std::max_align_t);
inline constexpr std::size_t kSpineHpStateBytes  = 256;   // HuggettHpStage::State ~= 168 B

inline constexpr float kDefaultModelFadeMs = 25.0f;  // CALIB (default for spine.modelFadeMs)
inline constexpr float kMinModelFadeMs     = 2.0f;   // floor keeps every switch click-free
inline constexpr float kMaxModelFadeMs     = 100.0f;
