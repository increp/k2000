#include "SpineFilterSlot.h"
#include <algorithm>
#include <cmath>

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
    fadePos_ = 0;
    fadeLen_ = 0;
    pending_ = nullptr;

    if (modelForState) { state_[0] = modelForState->constructState(buf_[0]); model_[0] = modelForState; }
    if (hpForState)    { hpState_  = hpForState->constructState(hpBuf_); }
}

void SpineFilterSlot::reset(const FilterModel* model, const HuggettHpStage* hp) noexcept {
    if (model && state_[active_]) model->reset(*state_[active_]);
    if (hp    && hpState_)        hp->reset(*hpState_);
}

void SpineFilterSlot::beginFade(const FilterModel* target, float fadeMs) noexcept {
    const int other = 1 - active_;
    if (state_[other]) { model_[other]->destroyState(state_[other]); state_[other] = nullptr; }
    state_[other] = target->constructState(buf_[other]);
    model_[other] = target;
    const float ms = std::clamp(fadeMs, kMinModelFadeMs, kMaxModelFadeMs);
    fadeLen_ = std::max(1, (int) std::lround(ms * sampleRate_ / 1000.0));
    fadePos_ = 1;
    pending_ = nullptr;
}

void SpineFilterSlot::bind(const FilterModel* model, const HuggettHpStage* hp) noexcept {
    const int other = 1 - active_;
    if (state_[other]) { model_[other]->destroyState(state_[other]); state_[other] = nullptr; model_[other] = nullptr; }
    fadePos_ = 0; pending_ = nullptr;
    if (model != model_[active_]) {
        if (state_[active_]) model_[active_]->destroyState(state_[active_]);
        state_[active_] = model ? model->constructState(buf_[active_]) : nullptr;
        model_[active_] = model;
    }
    if (model && state_[active_]) model->reset(*state_[active_]);
    if (hp && hpState_)           hp->reset(*hpState_);
}

void SpineFilterSlot::processStereo(const HuggettHpStage* hp, bool hpEnabled,
                                    const FilterModel* current, float fadeMs,
                                    float* l, float* r, int n) noexcept {
    if (hpEnabled && hp && hpState_) hp->processStereo(*hpState_, l, r, n);
    if (current == nullptr) return;

    // switch detection
    if (fadePos_ == 0) {
        if (current != model_[active_]) beginFade(current, fadeMs);
    } else {
        const int other = 1 - active_;
        pending_ = (current != model_[other]) ? current : nullptr;   // coalesce depth-1
    }

    // steady
    if (fadePos_ == 0) {
        if (state_[active_]) model_[active_]->processStereo(*state_[active_], l, r, n);
        return;
    }

    // fading: NEW in place on (l,r); OLD on a scratch copy of the input
    const int other = 1 - active_;
    std::copy(l, l + n, scratchL_.data());
    std::copy(r, r + n, scratchR_.data());
    model_[other ]->processStereo(*state_[other ], l, r, n);
    model_[active_]->processStereo(*state_[active_], scratchL_.data(), scratchR_.data(), n);

    constexpr float kHalfPi = 1.57079632679f;
    for (int i = 0; i < n; ++i) {
        const float p    = std::min(1.0f, (float) fadePos_ / (float) fadeLen_);
        const float gOld = std::cos(p * kHalfPi);
        const float gNew = std::sin(p * kHalfPi);
        l[i] = gNew * l[i] + gOld * scratchL_[i];
        r[i] = gNew * r[i] + gOld * scratchR_[i];
        if (fadePos_ < fadeLen_) ++fadePos_;
    }

    // completion at block end
    if (fadePos_ >= fadeLen_) {
        model_[active_]->destroyState(state_[active_]);
        state_[active_] = nullptr;
        active_ = other;
        fadePos_ = 0;
        if (pending_ && pending_ != model_[active_]) {
            const FilterModel* next = pending_;
            pending_ = nullptr;
            beginFade(next, fadeMs);
        } else {
            pending_ = nullptr;
        }
    }
}
