#pragma once
#include <array>
#include <juce_audio_basics/juce_audio_basics.h>   // juce::MidiBuffer
#include "Voice.h"

class Program;  // forward

class VoiceManager {
public:
    static constexpr int kNumVoices = 64;

    // Bind to the Program whose layers these voices play. Call before prepare().
    void setProgram(Program* program);

    void prepare(double sampleRate, int maxBlockSize, int osFactor = 1);

    // RT-safe. Process MIDI at sample positions and mix voices into outL/outR (stereo).
    void renderBlock(float* outL, float* outR, int numSamples, const juce::MidiBuffer& midi);

    void allNotesOff();

private:
    Program* program_ = nullptr;  // non-owning
    std::array<Voice, kNumVoices> voices_;

    // Per-voice: which Program slot it is currently playing (-1 = none).
    std::array<int, kNumVoices> voiceSlot_{};
    std::array<int, kNumVoices> voiceAge_{};
    int ageCounter_ = 0;

    int  pickVoice();                                   // free, else oldest
    void noteOn(int note, float velocity, int channel);
    void noteOff(int note, int channel);
};
