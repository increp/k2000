#pragma once
#include <array>
#include <memory>
#include <vector>
#include "dsp/Oscillator.h"
#include "dsp/Envelope.h"
#include "dsp/DSPBlock.h"
#include "dsp/Algorithm.h"

class Layer;  // forward

class Voice {
public:
    Voice();
    ~Voice() = default;

    // Bind this voice to a Layer. Must be called before prepare(), which
    // sizes per-slot state from the Layer's algorithm.
    void setLayer(Layer* layer) { layer_ = layer; }

    void prepare(double sampleRate, int maxBlockSize);
    void reset();

    void noteOn(int midiNote, float velocity);
    void noteOff();
    bool isActive() const;
    int  currentNote() const { return note_; }

    // RT-safe. Renders additively into `out` (caller pre-zeroes). Reads its
    // Layer's algorithm, block instances, and ParamSnapshot.
    void render(float* out, int numSamples);

private:
    Layer* layer_ = nullptr;  // non-owning
    Oscillator osc_;
    Envelope amp_;

    // Per-slot voice-local state (filter integrators, etc.). Allocated in
    // prepare() from the Layer's algorithm; the slot block owns the
    // rendering logic, the Voice owns the state.
    std::array<std::unique_ptr<DSPBlock::VoiceState>, Algorithm::kMaxSlots> slotStates_;

    int note_ = -1;
    float velocity_ = 0.0f;
    double sampleRate_ = 44100.0;

    std::vector<float> scratch_;  // preallocated in prepare(); used in render()

    static float midiToHz(int note);
};
