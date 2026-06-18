#pragma once
#include <array>
#include <memory>
#include <cstddef>
#include "dsp/Algorithm.h"
#include "dsp/AlgorithmLibrary.h"
#include "dsp/DSPBlock.h"
#include "dsp/spine/FilterModel.h"
#include "dsp/spine/SpineFilterSlot.h"
#include "dsp/spine/HuggettFilter.h"
#include "dsp/spine/FilterModelLibrary.h"
#include "dsp/spine/HuggettHpStage.h"
#include "params/ParamSnapshot.h"

// Configuration container. Owns a PALETTE — one DSP block instance per block
// type — and the currently selected algorithm (an ordered list of block types).
// Voices read the palette and the active algorithm during render. See ADR 0008.
class Layer {
public:
    Layer();
    ~Layer() = default;

    void prepare(double sampleRate, int maxBlockSize);

    // RT-safe. Configures each palette block and selects the active algorithm.
    void updateParameters(const ParamSnapshot& snapshot);

    const Algorithm& activeAlgorithm() const { return AlgorithmLibrary::byIndex(activeAlgorithmId_); }

    bool hasBlock(BlockTypeId t) const { return palette_[(int) t] != nullptr; }
    // Precondition: t is a real block type present in the palette (not None).
    // Algorithms only name palette types (enforced by AlgorithmLibrary tests).
    DSPBlock&       block(BlockTypeId t)       { jassert((int) t > 0 && palette_[(int) t] != nullptr); return *palette_[(int) t]; }
    const DSPBlock& block(BlockTypeId t) const { jassert((int) t > 0 && palette_[(int) t] != nullptr); return *palette_[(int) t]; }

    const ParamSnapshot& snapshot() const { return snapshot_; }

    void  setLevel(float linearGain) { level_ = linearGain; }
    float level() const { return level_; }

    const FilterModel* spineModel() const { return spineModel_.get(); }

    const HuggettHpStage* hpStage() const { return &hpStage_; }

private:
    // Indexed by BlockTypeId value; null where the type isn't in the palette.
    std::array<std::unique_ptr<DSPBlock>, kNumBlockTypes> palette_;
    std::size_t activeAlgorithmId_ = 0;
    ParamSnapshot snapshot_;
    float level_ = 1.0f;  // linear output gain, set each block from layer{i}.level

    std::unique_ptr<FilterModel> spineModel_;
    std::size_t spineModelId_ = SIZE_MAX;   // forces first build
    HuggettFilter* huggett_ = nullptr;      // non-owning view when model 0 is active
    HuggettHpStage hpStage_;               // fixed HP pre-stage (config shared; per-voice state in the slot)
    double sampleRate_ = 44100.0;
};
