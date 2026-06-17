#include "SpineFilterSlot.h"

void SpineFilterSlot::prepare(double, const FilterModel* active) {
    model_ = active;
    state_.reset(active ? active->makeState() : nullptr);  // prepare-time alloc only
}

void SpineFilterSlot::reset() noexcept {
    if (model_ && state_) model_->reset(*state_);
}

void SpineFilterSlot::processStereo(float* left, float* right, int n) noexcept {
    if (model_ && state_) model_->processStereo(*state_, left, right, n);
}
