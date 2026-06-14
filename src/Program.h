#pragma once
#include "Layer.h"

// Container for 1..N Layers. v2 always has exactly one. v4 introduces
// Layer/Split/Dual modes and multiple Layers; for now Program is a thin
// pass-through so that PluginProcessor talks to a Program rather than
// directly to a Layer (clean v4 extension point).

class Program {
public:
    Program() = default;

    void prepare(double sampleRate, int maxBlockSize) {
        layer_.prepare(sampleRate, maxBlockSize);
    }

    Layer& layer() { return layer_; }
    const Layer& layer() const { return layer_; }

private:
    Layer layer_;
};
