#pragma once
#include <array>
#include <memory>
#include <vector>
#include "dsp/Oscillator.h"
#include "dsp/Envelope.h"
#include "dsp/DSPBlock.h"
#include "params/ParamSnapshot.h"

class Voice {
public:
    Voice();
    ~Voice() = default;

    void prepare(double sampleRate, int maxBlockSize);
    void reset();

    void noteOn(int midiNote, float velocity);
    void noteOff();
    bool isActive() const;
    int  currentNote() const { return note_; }

    // RT-safe. Renders into `out`, additively (caller pre-zeroes).
    void render(float* out, int numSamples, const ParamSnapshot& snapshot);

private:
    Oscillator osc_;
    std::array<std::unique_ptr<DSPBlock>, 2> slots_;
    Envelope amp_;

    int note_ = -1;
    float velocity_ = 0.0f;
    double sampleRate_ = 44100.0;

    std::vector<float> scratch_;  // preallocated in prepare(); used in render()

    static float midiToHz(int note);
};
