#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// Lightweight description of a single parameter exposed by a DSPBlock.
// Blocks declare their parameters via getParamSpecs(); the processor
// namespaces them by slot when registering them in the APVTS.
struct ParamSpec {
    juce::String id;                             // unscoped, e.g. "cutoff"
    juce::String label;                          // user-visible
    juce::NormalisableRange<float> range;
    float defaultValue;
};
