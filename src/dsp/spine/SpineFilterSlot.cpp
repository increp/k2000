#include "SpineFilterSlot.h"

SpineFilterSlot::~SpineFilterSlot() {
    for (int i = 0; i < 2; ++i)
        if (state_[i]) model_[i]->destroyState(state_[i]);
    if (hpState_) hpState_->~State();
}

void SpineFilterSlot::prepare(double sr, int maxBlock,
                              const FilterModel* modelForState, const HuggettHpStage* hpForState) {
    sampleRate_ = sr;
    scratchL_.assign((size_t) maxBlock, 0.0f);
    scratchR_.assign((size_t) maxBlock, 0.0f);

    for (int i = 0; i < 2; ++i)
        if (state_[i]) { model_[i]->destroyState(state_[i]); state_[i] = nullptr; model_[i] = nullptr; }
    if (hpState_) { hpState_->~State(); hpState_ = nullptr; }
    active_ = 0;

    if (modelForState) { state_[0] = modelForState->constructState(buf_[0]); model_[0] = modelForState; }
    if (hpForState)    { hpState_  = hpForState->constructState(hpBuf_); }
}

void SpineFilterSlot::reset(const FilterModel* model, const HuggettHpStage* hp) noexcept {
    if (model && state_[active_]) model->reset(*state_[active_]);
    if (hp    && hpState_)        hp->reset(*hpState_);
}

void SpineFilterSlot::processStereo(const HuggettHpStage* hp, bool hpEnabled,
                                    const FilterModel* model, float* l, float* r, int n) noexcept {
    if (hpEnabled && hp && hpState_) hp->processStereo(*hpState_, l, r, n);
    if (model && state_[active_])    model->processStereo(*state_[active_], l, r, n);
}
