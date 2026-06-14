#pragma once
#include <array>
#include <memory>
#include "dsp/Algorithm.h"
#include "dsp/DSPBlock.h"
#include "params/ParamSnapshot.h"

// Configuration container. Owns the algorithm topology, the DSP block
// instances laid out per algorithm slot, and the per-Layer parameter
// snapshot. Does not by itself play notes — Voices hold a pointer to
// their Layer and walk it during render.
//
// v2: there is exactly one Layer, owned by Program. v4 introduces
// multi-Layer Programs.

class Layer {
public:
    Layer();
    ~Layer() = default;

    void prepare(double sampleRate, int maxBlockSize);

    // RT-safe.
    void updateParameters(const ParamSnapshot& snapshot);

    // Read-only access for Voices walking the algorithm.
    const Algorithm& algorithm() const { return algorithm_; }
    DSPBlock& slot(std::size_t i) { return *slots_[i]; }
    const DSPBlock& slot(std::size_t i) const { return *slots_[i]; }

    const ParamSnapshot& snapshot() const { return snapshot_; }

private:
    Algorithm algorithm_ = Algorithm::v1Fixed();
    std::array<std::unique_ptr<DSPBlock>, Algorithm::kMaxSlots> slots_;
    ParamSnapshot snapshot_;
};
