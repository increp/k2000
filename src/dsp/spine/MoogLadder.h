#pragma once
#include "FilterModel.h"
#include "cmajor/MoogLadderAdapter.h"

// Moog transistor-ladder spine filter (Spec 1): a fused Cmajor ladder behind an
// in-place adapter; stereo = two mono adapters sharing block-set params. Mode/slope
// are model-specific setters (not on the FilterModel base), per HuggettFilter.
class MoogLadder : public FilterModel {
public:
    enum class Slope { db12, db24 };
    enum class Mode  { LP = 0, BP = 1, HP = 2 };

    struct VoiceState : public FilterModel::State {
        MoogLadderAdapter l, r;
        float fundamentalHz = 0.0f;   // played-note Hz (per-voice; re-forwarded each block)
    };

    void prepare(double sampleRate) noexcept override { sampleRate_ = sampleRate; }
    std::size_t stateSize()  const noexcept override { return sizeof(VoiceState); }
    std::size_t stateAlign() const noexcept override { return alignof(VoiceState); }
    FilterModel::State* constructState(void* mem) const override;
    void reset(State& s) const noexcept override;

    void setCommon(float cutoffHz, float resonance, float drive) noexcept override {
        cutoffHz_ = cutoffHz; resonance_ = resonance; drive_ = drive;
    }
    void setSlope(Slope s) noexcept { slope_ = s; }
    void setMode(Mode m)   noexcept { mode_  = m; }
    void setSeparation(float) noexcept { /* no analog in a single ladder */ }

    // Task 6: played-note sub-osc "bass voice". setBass is model-wide (amount/wave/
    // octave shared by both lanes); setFundamental is per-voice (writes both lanes of
    // the given state). Octave convention: 0=fundamental, 1=-1oct, 2=-2oct.
    void setBass(float amount, int wave, int octave) noexcept {
        bassAmount_ = amount; bassWave_ = wave; bassOctave_ = octave;
    }
    void setFundamental(State& s, float hz) const noexcept;

    void processStereo(State& s, float* left, float* right, int numSamples) const noexcept override;

private:
    double sampleRate_ = 48000.0;
    float  cutoffHz_ = 1000.0f, resonance_ = 0.0f, drive_ = 0.0f;
    Slope  slope_ = Slope::db24;
    Mode   mode_  = Mode::LP;
    float  bassAmount_ = 0.0f;
    int    bassWave_   = 0;
    int    bassOctave_ = 0;
};
