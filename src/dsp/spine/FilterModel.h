#pragma once
#include <cstddef>

// Abstract per-voice, stereo, heap-free spine filter. One instance configures
// shared params (on the Layer); per-voice integrator state lives in State.
// Concrete models define their own State subtype.
class FilterModel {
public:
    struct State { virtual ~State() = default; };

    virtual ~FilterModel() = default;
    virtual void prepare(double sampleRate) noexcept = 0;   // cheap; no heap
    virtual State* makeState() const = 0;                   // allocate-OK (prepare time)
    virtual void reset(State& s) const noexcept = 0;

    // Common core, set per block from the spine's common params.
    virtual void setCommon(float cutoffHz, float resonance, float drive) noexcept = 0;

    // Process numSamples of stereo audio in place using this voice's state.
    virtual void processStereo(State& s, float* left, float* right, int numSamples) const noexcept = 0;
};
