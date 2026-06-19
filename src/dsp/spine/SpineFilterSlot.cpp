#include "SpineFilterSlot.h"

void SpineFilterSlot::prepare(double, const FilterModel* modelForState,
                              const HuggettHpStage* hpForState) {
    state_.reset(modelForState ? modelForState->makeState() : nullptr);   // prepare-time alloc only
    hpState_.reset(hpForState  ? hpForState->makeState()   : nullptr);
}

void SpineFilterSlot::reset(const FilterModel* model, const HuggettHpStage* hp) noexcept {
    if (model && state_)   model->reset(*state_);
    if (hp    && hpState_) hp->reset(*hpState_);
}

void SpineFilterSlot::processStereo(const HuggettHpStage* hp, bool hpEnabled,
                                    const FilterModel* model, float* l, float* r, int n) noexcept {
    if (hpEnabled && hp && hpState_) hp->processStereo(*hpState_, l, r, n);
    if (model && state_) model->processStereo(*state_, l, r, n);
}
