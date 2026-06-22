#pragma once
#include <cstddef>
#include <new>

// Abstract per-voice, stereo, heap-free spine filter. One instance configures
// shared params (on the Layer); per-voice integrator state lives in State.
// Concrete models define their own State subtype.
//
// State lives in CALLER-PROVIDED memory via constructState() (placement-new) so a
// live model switch never allocates on the audio thread. makeState() is a non-RT
// convenience that heap-allocates + constructs (used by tests and prepare-time code).
class FilterModel {
public:
    struct State { virtual ~State() = default; };

    virtual ~FilterModel() = default;
    virtual void prepare(double sampleRate) noexcept = 0;   // cheap; no heap

    // In-place lifecycle. INVARIANT: heap-free + RT-safe (audio-thread callable).
    // constructState placement-news into mem (>= stateSize() bytes, >= stateAlign()).
    virtual std::size_t stateSize()  const noexcept = 0;
    virtual std::size_t stateAlign() const noexcept = 0;
    virtual State* constructState(void* mem) const = 0;
    virtual void   destroyState(State* s) const noexcept { if (s) s->~State(); }

    // Heap convenience (NON-RT): allocate + construct. Wrappable in unique_ptr<State>;
    // unique_ptr's delete runs ~State() then ::operator delete, matching this new.
    State* makeState() const { return constructState(::operator new(stateSize())); }

    virtual void reset(State& s) const noexcept = 0;
    virtual void setCommon(float cutoffHz, float resonance, float drive) noexcept = 0;
    virtual void processStereo(State& s, float* left, float* right, int numSamples) const noexcept = 0;
};
