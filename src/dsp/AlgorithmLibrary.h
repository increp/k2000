#pragma once
#include <cstddef>
#include "Algorithm.h"

// The v3 algorithm library: a fixed, APPEND-ONLY set of algorithms built from
// the {filter, shaper} palette. Append-only ordering keeps the layer.algorithm
// choice index preset-stable. See ADR 0008.
namespace AlgorithmLibrary {
    std::size_t      count();
    const Algorithm& byIndex(std::size_t i);   // clamps out-of-range to 0
    std::size_t      indexOfId(const char* id); // returns 0 if not found
}
