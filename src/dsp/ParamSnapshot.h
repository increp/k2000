#pragma once

// Plain-old-data snapshot of the v1 parameter state.
// Built once per audio block on the audio thread by reading the APVTS's
// atomic raw-value pointers, then passed by const ref to Voice/blocks.
// Adding new params here is a single-source-of-truth change: extend this
// struct, the Parameters layout, and any consumer that needs the value.
struct ParamSnapshot {
    // Oscillator (retired -- kept until Task 4 of the three-VCO-blend plan
    // removes it, since removing it here would break every test file that
    // still constructs a ParamSnapshot directly)
    int   oscWaveform   = 0;   // 0=saw 1=square 2=triangle 3=sine
    float oscCoarse     = 0.0f; // semitones
    float oscFine       = 0.0f; // cents

    // VCO1/2/3: coarse/fine tuning (semitones/cents) + proportional 4-way
    // waveform blend (each 0..1, combined as a weighted average -- see
    // Oscillator::processSample()) + pulse duty cycle (0.01..0.99).
    // All three default to unison pitch + 100% saw; only mixerOscNLevel
    // differs between them (VCO1 audible, VCO2/3 silent) so a fresh patch
    // sounds identical to today's single-oscillator saw default.
    float osc1Coarse = 0.0f, osc1Fine = 0.0f;
    float osc1BlendSine = 0.0f, osc1BlendTriangle = 0.0f, osc1BlendSaw = 1.0f, osc1BlendPulse = 0.0f;
    float osc1PulseDuty = 0.5f;
    float osc2Coarse = 0.0f, osc2Fine = 0.0f;
    float osc2BlendSine = 0.0f, osc2BlendTriangle = 0.0f, osc2BlendSaw = 1.0f, osc2BlendPulse = 0.0f;
    float osc2PulseDuty = 0.5f;
    float osc3Coarse = 0.0f, osc3Fine = 0.0f;
    float osc3BlendSine = 0.0f, osc3BlendTriangle = 0.0f, osc3BlendSaw = 1.0f, osc3BlendPulse = 0.0f;
    float osc3PulseDuty = 0.5f;

    // Mixer: linear gain (0..1) balancing the three VCOs before they sum
    // into the algorithm-block graph.
    float mixerOsc1Level = 1.0f, mixerOsc2Level = 0.0f, mixerOsc3Level = 0.0f;

    // Filter block (layer.filter.*)
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
    float spineModelFadeMs   = 25.0f;   // global: spine.modelFadeMs (2..100 ms)

    // HP pre-filter (always-available, before the main model).
    // No enable flag: cutoff == 0 (knob at bottom) = OFF/bypassed; cutoff > 0 engages it.
    float hpCutoffHz    = 0.0f;   // 0 = off
    float hpResonance   = 0.0f;
    int   hpSlope       = 0;      // 0=12 dB, 1=24 dB
    // Main Huggett post-filter drive (Huggett bank)
    float huggettPostDrive = 0.0f;
    // Main Huggett routing (Huggett bank): index into HuggettFilter::Routing.
    // 0=LP 1=BP 2=HP, 3..5 series LP->HP/LP->BP/HP->BP, 6..8 parallel LP+HP/LP+BP/HP+BP,
    // 9..11 parallel LP+LP/BP+BP/HP+HP.
    int huggettRouting = 0;

    // Moog bank (spine.moog.*)
    int   moogMode       = 0;   // 0=LP 1=BP 2=HP
    float moogBassAmount = 0.0f;
    int   moogBassWave   = 0;   // 0=Sine 1=Triangle 2=Saw
    int   moogBassOctave = 0;   // 0=unison 1=-1oct 2=-2oct
};
