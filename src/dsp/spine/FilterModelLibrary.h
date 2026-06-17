#pragma once
#include <juce_core/juce_core.h>
#include <memory>
#include <cstddef>
#include "FilterModel.h"

// Append-only registry of spine filter models. Index is the stable serialised
// id (never reorder; append only) — the AlgorithmLibrary/ADR-0008 idiom.
namespace FilterModelLibrary {
    std::size_t          count();
    juce::String         id(std::size_t i);          // stable string id, e.g. "huggett"
    juce::StringArray    names();                     // display names (UTF-8, for UI/param)
    std::unique_ptr<FilterModel> create(std::size_t i); // factory (prepare-time alloc)
}
