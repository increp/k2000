#pragma once
#include <memory>
#include "ParamSnapshot.h"

// Abstract base for swappable per-voice processing units (VAST blocks).
// See docs/architecture/dsp-block-interface.md for the rationale behind
// each method.
//
// As of v2, blocks separate configuration from per-voice runtime state:
// - Configuration (sample rate, cutoff, mode, recomputed coefficients)
//   lives on the block instance, owned by the Layer.
// - Per-voice state (filter integrators, envelope phase, etc.) lives in
//   a block-specific VoiceState struct, owned by the Voice.
//
// Each concrete block defines its own VoiceState type and a factory.
class DSPBlock {
public:
    // Marker base — concrete blocks define their own VoiceState struct
    // inheriting from this. The runtime contract is: Voice holds a
    // unique_ptr<VoiceState> per slot, allocated at prepareToPlay, and
    // passes the reference to every process() call.
    struct VoiceState {
        virtual ~VoiceState() = default;
    };

    virtual ~DSPBlock() = default;

    // Allocate-OK. Called from prepareToPlay.
    virtual void prepare(double sampleRate, int maxBlockSize) = 0;

    // Allocate-OK. Returns a fresh VoiceState for this block. Called once
    // per voice during prepareToPlay (Voice stores them; never RT-allocates).
    virtual std::unique_ptr<VoiceState> makeVoiceState() const = 0;

    // RT-safe. Called on note-on / voice-steal to clear voice-local state.
    virtual void resetVoice(VoiceState& state) = 0;

    // RT-safe. Process numSamples in-place, mono, using the supplied voice state.
    virtual void process(VoiceState& state, float* buffer, int numSamples) = 0;

    // RT-safe. Called once per audio block before process(). Updates shared
    // configuration (cutoff, drive, etc.) — never per-voice state.
    virtual void updateParameters(const ParamSnapshot& snapshot) = 0;
};
