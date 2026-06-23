#pragma once
#include "FilterModel.h"
#include "cmajor/MoogLadderAdapter.h"

// Moog transistor-ladder spine filter (Spec 1): a fused Cmajor ladder behind an
// in-place adapter; stereo = two mono adapters sharing block-set params. Mode/slope
// are model-specific setters (not on the FilterModel base), per HuggettFilter.
class MoogLadder : public FilterModel {
public:
    enum class Slope { db12, db24 };

    struct VoiceState : public FilterModel::State {
        MoogLadderAdapter l, r;
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
    void setSeparation(float) noexcept { /* no analog in a single ladder */ }

    void processStereo(State& s, float* left, float* right, int numSamples) const noexcept override;

private:
    double sampleRate_ = 48000.0;
    float  cutoffHz_ = 1000.0f, resonance_ = 0.0f, drive_ = 0.0f;
    Slope  slope_ = Slope::db24;
};
