#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include "ParamSpec.h"
#include "../params/ParamSnapshot.h"

// Abstract base for swappable per-voice processing units (VAST blocks).
// See docs/architecture/dsp-block-interface.md for the rationale behind
// each method.
class DSPBlock {
public:
    virtual ~DSPBlock() = default;

    // Allocate-OK. Called from prepareToPlay.
    virtual void prepare(double sampleRate, int maxBlockSize) = 0;

    // RT-safe. Called on note-on / voice-steal to clear state.
    virtual void reset() = 0;

    // RT-safe. Process numSamples in-place, mono.
    virtual void process(float* buffer, int numSamples) = 0;

    // Stable identifier for preset serialisation, e.g. "svf_filter".
    virtual juce::String getTypeId() const = 0;

    // Parameter descriptors. The processor namespaces these by slot.
    virtual std::vector<ParamSpec> getParamSpecs() const = 0;

    // RT-safe. Called once per audio block before process().
    virtual void updateParameters(const ParamSnapshot& snapshot) = 0;
};
