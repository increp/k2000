#pragma once
#include <memory>
#include "FilterModel.h"

// Per-voice spine holder. Points at the Layer-owned active FilterModel and owns
// this voice's State for it. (A later plan adds a second outgoing slot + crossfade.)
class SpineFilterSlot {
public:
    // active is owned by the Layer and must outlive this slot.
    void prepare(double sampleRate, const FilterModel* active);
    void reset() noexcept;
    void processStereo(float* left, float* right, int numSamples) noexcept;

private:
    const FilterModel* model_ = nullptr;
    std::unique_ptr<FilterModel::State> state_;
};
