#pragma once
#include <memory>
#include "FilterModel.h"
#include "HuggettHpStage.h"

// Per-voice spine STATE holder. It owns this voice's filter State but does NOT
// cache the model: the active model is supplied per call from the voice's
// current Layer. That keeps a voice reassigned across Layers filtering through
// the correct Layer's model, and means there is no cached model pointer to
// dangle if a Layer rebuilds its model. The State's concrete type is fixed by
// the model passed to prepare(); supplying a different model TYPE later is
// undefined (one model type today; multi-type arrives with the Plan 3 hot-swap).
// Also holds the HP pre-stage state (Task 8): the HP runs BEFORE the model when
// hpEnabled is true.
class SpineFilterSlot {
public:
    void prepare(double sampleRate, const FilterModel* modelForState,
                 const HuggettHpStage* hpForState);
    void reset(const FilterModel* model, const HuggettHpStage* hp) noexcept;
    void processStereo(const HuggettHpStage* hp, bool hpEnabled,
                       const FilterModel* model, float* left, float* right, int numSamples) noexcept;

private:
    std::unique_ptr<FilterModel::State>    state_;
    std::unique_ptr<HuggettHpStage::State> hpState_;
};
