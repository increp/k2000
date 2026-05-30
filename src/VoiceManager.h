#pragma once
#include <array>
#include "Voice.h"

class VoiceManager {
public:
    static constexpr int kNumVoices = 8;

    void prepare(double sampleRate, int maxBlockSize);

    // RT-safe. Process MIDI events at their exact sample positions and
    // mix voice outputs additively into `out` (mono).
    void renderBlock(float* out, int numSamples,
                     const juce::MidiBuffer& midi,
                     const ParamSnapshot& snapshot);

    void allNotesOff();

private:
    std::array<Voice, kNumVoices> voices_;

    // Steal strategy: prefer an inactive voice; otherwise the oldest active.
    int pickVoiceFor(int midiNote);
    void noteOn(int midiNote, float velocity);
    void noteOff(int midiNote);

    // Per-voice age counter (incremented on noteOn for ordering)
    std::array<int, kNumVoices> voiceAge_{};
    int ageCounter_ = 0;
};
