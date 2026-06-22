#pragma once
#include "NlSvfCell.h"
#include "DcBlocker.h"

// Fixed, always-available HP-only Huggett stage that runs in the spine BEFORE the
// selectable model (mirrors the Summit HP->LP series routing). Clean (no drive) —
// the drive shaping lives in the main filter. Not a FilterModel (not swappable).
class HuggettHpStage {
public:
    enum class Slope { db12, db24 };

    struct State {
        NlSvfCell a, b;
        DcBlocker dc;
    };

    void prepare(double sampleRate) noexcept { sampleRate_ = sampleRate; }
    State* makeState() const;                 // heap convenience (prepare-time / tests)
    std::size_t stateSize() const noexcept { return sizeof(State); }
    State* constructState(void* mem) const;   // placement-new; RT-safe
    void reset(State& s) const noexcept;

    void setParams(float cutoffHz, float resonance, Slope slope) noexcept {
        cutoffHz_ = cutoffHz; resonance_ = resonance; slope_ = slope;
    }
    void processStereo(State& s, float* left, float* right, int numSamples) const noexcept;

private:
    double sampleRate_ = 44100.0;
    float  cutoffHz_ = 20.0f, resonance_ = 0.0f;
    Slope  slope_ = Slope::db12;
};
