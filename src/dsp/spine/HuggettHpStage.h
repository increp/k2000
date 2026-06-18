#pragma once
#include "NlSvfCell.h"
#include "AsymSaturator.h"
#include "DcBlocker.h"

// Fixed, always-available HP-only Huggett stage that runs in the spine BEFORE the
// selectable model (mirrors the Summit HP->LP series routing). Own pre-drive at a
// lighter voicing than the main filter. Not a FilterModel (not swappable).
class HuggettHpStage {
public:
    enum class Slope { db12, db24 };

    struct State {
        NlSvfCell a, b;
        AsymSaturator::State pre;
        DcBlocker dc;
    };

    void prepare(double sampleRate) noexcept { sampleRate_ = sampleRate; }
    State* makeState() const;                 // prepare-time alloc only
    void reset(State& s) const noexcept;

    void setParams(float cutoffHz, float resonance, Slope slope, float drive01) noexcept {
        cutoffHz_ = cutoffHz; resonance_ = resonance; slope_ = slope; drive_ = drive01;
    }
    void processStereo(State& s, float* left, float* right, int numSamples) const noexcept;

private:
    static constexpr float kHpDriveDb = 24.0f;   // CALIB
    static constexpr float kHpBias    = 0.10f;   // CALIB (lighter than the main filter)
    double sampleRate_ = 44100.0;
    float  cutoffHz_ = 20.0f, resonance_ = 0.0f, drive_ = 0.0f;
    Slope  slope_ = Slope::db12;
};
