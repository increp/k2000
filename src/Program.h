#pragma once
#include <array>
#include <cstddef>
#include "Layer.h"
#include "LayerRouting.h"
#include "params/Parameters.h"  // for params::kNumLayers

// A Program holds kNumLayers LayerSlots. Each slot = a Layer (DSP config) plus
// its routing (which notes play it) and is the unit the VoiceManager allocates
// against. v4 fully parameterizes 2 layers; the structures are generic over the
// count. See ADR 0009.
struct LayerSlot {
    Layer layer;
    LayerRouting routing;
};

class Program {
public:
    Program() = default;

    void prepare(double sampleRate, int maxBlockSize) {
        for (auto& s : slots_) s.layer.prepare(sampleRate, maxBlockSize);
    }

    static constexpr std::size_t numLayers() { return params::kNumLayers; }
    LayerSlot&       slot(std::size_t i)       { return slots_[i]; }
    const LayerSlot& slot(std::size_t i) const { return slots_[i]; }

private:
    std::array<LayerSlot, params::kNumLayers> slots_;
};
