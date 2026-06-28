#pragma once
#include <array>
#include <memory>
#include <vector>
#include "dsp/Oscillator.h"
#include "dsp/Envelope.h"
#include "dsp/DSPBlock.h"
#include "dsp/Algorithm.h"
#include "dsp/spine/SpineFilterSlot.h"
#include "dsp/VoiceOversampler.h"

class Layer;  // forward

class Voice {
public:
    Voice();
    ~Voice() = default;

    // Bind this voice to a Layer. Must be called before prepare(), which sizes
    // per-block-type state from the Layer's palette.
    void setLayer(Layer* layer) { layer_ = layer; }

    void prepare(double sampleRate, int maxBlockSize, int osFactor = 1);
    void reset();

    void noteOn(int midiNote, float velocity);
    void noteOff();
    bool isActive() const;
    int  currentNote() const { return note_; }

    // RT-safe. Renders additively into outL and outR. Walks the Layer's active
    // algorithm, processing through the palette block for each block type,
    // then routes through the Layer's spine filter (dual-mono to stereo).
    void render(float* outL, float* outR, int numSamples);

private:
    Layer* layer_ = nullptr;  // non-owning
    Oscillator osc_;
    Envelope amp_;

    // Per-block-TYPE voice-local state (indexed by BlockTypeId value).
    std::array<std::unique_ptr<DSPBlock::VoiceState>, kNumBlockTypes> blockStates_;

    int note_ = -1;
    float velocity_ = 0.0f;
    double sampleRate_ = 44100.0;
    std::vector<float> scratch_;
    std::vector<float> scratchR_;
    SpineFilterSlot spine_;

    VoiceOversampler os_;
    int osFactor_ = 1;
    std::vector<float> osMono_, osL_, osR_;   // oversampled-domain scratch

    static float midiToHz(int note);
};
