#pragma once
#include <memory>
#include "FilterModel.h"

// Per-voice spine STATE holder. It owns this voice's filter State but does NOT
// cache the model: the active model is supplied per call from the voice's
// current Layer. That keeps a voice reassigned across Layers filtering through
// the correct Layer's model, and means there is no cached model pointer to
// dangle if a Layer rebuilds its model. The State's concrete type is fixed by
// the model passed to prepare(); supplying a different model TYPE later is
// undefined (one model type today; multi-type arrives with the Plan 3 hot-swap).
class SpineFilterSlot {
public:
    void prepare(double sampleRate, const FilterModel* modelForState);
    void reset(const FilterModel* model) noexcept;
    void processStereo(const FilterModel* model, float* left, float* right, int numSamples) noexcept;

private:
    std::unique_ptr<FilterModel::State> state_;
};
