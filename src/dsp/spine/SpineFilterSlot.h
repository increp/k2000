#pragma once
#include <vector>
#include <cstddef>
#include "FilterModel.h"
#include "HuggettHpStage.h"
#include "SpineState.h"

// Per-voice spine STATE holder. Owns this voice's filter State in fixed in-place
// storage (no heap after prepare). Two buffers exist for the live model crossfade
// (Task 5); Task 2 uses only buffer 0. The active model is supplied per call from
// the voice's current Layer, so a reassigned voice filters through the right model.
class SpineFilterSlot {
public:
    SpineFilterSlot() = default;
    ~SpineFilterSlot();                                            // destroys placement-constructed states
    SpineFilterSlot(const SpineFilterSlot&) = delete;             // state_ points into buf_ — non-relocatable
    SpineFilterSlot& operator=(const SpineFilterSlot&) = delete;

    void prepare(double sampleRate, int maxBlockSize,
                 const FilterModel* modelForState, const HuggettHpStage* hpForState);
    void reset(const FilterModel* model, const HuggettHpStage* hp) noexcept;
    void processStereo(const HuggettHpStage* hp, bool hpEnabled,
                       const FilterModel* current, float fadeMs,
                       float* left, float* right, int numSamples) noexcept;
    void bind(const FilterModel* model, const HuggettHpStage* hp) noexcept;  // note-start; snap, no fade

private:
    void beginFade(const FilterModel* target, float fadeMs) noexcept;

    alignas(kSpineStateAlign) std::byte buf_[2][kMaxSpineStateBytes];
    FilterModel::State* state_[2] = {nullptr, nullptr};
    const FilterModel*  model_[2] = {nullptr, nullptr};
    int    active_ = 0;
    int    fadePos_ = 0;        // 0 = steady; 1..fadeLen_ = samples into the current fade
    int    fadeLen_ = 0;        // captured at fade-begin
    const FilterModel* pending_ = nullptr;   // coalesce depth-1
    double sampleRate_ = 44100.0;
    std::vector<float> scratchL_, scratchR_;   // sized at prepare; used by the fade (Task 5)

    alignas(kSpineStateAlign) std::byte hpBuf_[kSpineHpStateBytes];
    HuggettHpStage::State* hpState_ = nullptr;
};
