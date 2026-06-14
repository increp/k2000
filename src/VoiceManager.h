#pragma once
#include <array>
#include "Voice.h"

class Layer;  // forward

class VoiceManager {
public:
    static constexpr int kNumVoices = 8;

    // Bind every voice to a Layer. Must be called before prepare().
    void setLayer(Layer* layer);

    void prepare(double sampleRate, int maxBlockSize);

    // RT-safe. Process MIDI events at their exact sample positions and
    // mix voice outputs additively into `out` (mono). Voices read their
    // parameters from the bound Layer's snapshot.
    void renderBlock(float* out, int numSamples,
                     const juce::MidiBuffer& midi);

    void allNotesOff();

private:
    Layer* layer_ = nullptr;  // non-owning
    std::array<Voice, kNumVoices> voices_;

    // Steal strategy: prefer an inactive voice; otherwise the oldest active.
    int pickVoiceFor(int midiNote);
    void noteOn(int midiNote, float velocity);
    void noteOff(int midiNote);

    // Per-voice age counter (incremented on noteOn for ordering)
    std::array<int, kNumVoices> voiceAge_{};
    int ageCounter_ = 0;
};
