#include "SpineFilterSlot.h"

void SpineFilterSlot::prepare(double, const FilterModel* modelForState) {
    state_.reset(modelForState ? modelForState->makeState() : nullptr);  // prepare-time alloc only
}

void SpineFilterSlot::reset(const FilterModel* model) noexcept {
    if (model && state_) model->reset(*state_);
}

void SpineFilterSlot::processStereo(const FilterModel* model, float* left, float* right, int n) noexcept {
    if (model && state_) model->processStereo(*state_, left, right, n);
}
