#pragma once
#include <juce_core/juce_core.h>

// Plain-old-data snapshot of the v1 parameter state.
// Built once per audio block on the audio thread by reading the APVTS's
// atomic raw-value pointers, then passed by const ref to Voice/blocks.
// Adding new params here is a single-source-of-truth change: extend this
// struct, the Parameters layout, and any consumer that needs the value.
struct ParamSnapshot {
    // Oscillator
    int   oscWaveform   = 0;   // 0=saw 1=square 2=triangle 3=sine
    float oscCoarse     = 0.0f; // semitones
    float oscFine       = 0.0f; // cents

    // Filter block (layer.filter.*)
    int   svfType       = 0;   // 0=LP 1=HP 2=BP 3=Notch
    float svfCutoffHz   = 1000.0f;
    float svfResonance  = 0.2f;

    // Shaper block (layer.shaper.*)
    float wsDrive       = 0.0f;
    float wsMix         = 1.0f;

    // Amp envelope
    float ampAttackS    = 0.005f;
    float ampDecayS     = 0.1f;
    float ampSustain    = 0.8f;
    float ampReleaseS   = 0.2f;

    // Master
    float masterGainDb  = 0.0f;

    // Algorithm selection (index into AlgorithmLibrary)
    int algorithmId = 0;

    // Spine filter (layer.spine.*)
    int   spineModel         = 0;
    float spineSeparationOct = 0.0f;
    int   spineSlope         = 1;   // 0=12 dB, 1=24 dB
    float spineDrive         = 0.0f;
    float spineOutputDb      = 0.0f;

    // HP pre-filter (always-available, before the main model)
    int   hpEnable      = 0;      // 0=off 1=on
    float hpCutoffHz    = 20.0f;
    float hpResonance   = 0.0f;
    int   hpSlope       = 0;      // 0=12 dB, 1=24 dB
    // Main Huggett post-filter drive (Huggett bank)
    float huggettPostDrive = 0.0f;
};
