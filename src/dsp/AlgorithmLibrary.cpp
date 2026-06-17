#include "AlgorithmLibrary.h"
#include <array>
#include <cstring>

namespace {
constexpr Algorithm make(const char* id, const char* name,
                         std::size_t n, BlockTypeId a, BlockTypeId b) {
    Algorithm alg;
    alg.id = id; alg.displayName = name; alg.slotCount = n;
    alg.blockTypePerSlot[0] = a; alg.blockTypePerSlot[1] = b;
    return alg;
}

// APPEND-ONLY. Do not reorder existing entries (choice index is serialised).
// v5: SvfFilter retired from graph to always-on spine. Former filter-bearing
// entries become shaper-only (or empty); ids/order preserved for ADR-0008.
const std::array<Algorithm, 4> kAlgorithms = {{
    make("filter_then_shaper", "Shaper",   1, BlockTypeId::Waveshaper, BlockTypeId::None),
    make("shaper_then_filter", "Shaper",   1, BlockTypeId::Waveshaper, BlockTypeId::None),
    make("filter_only",        "Passthru", 0, BlockTypeId::None,       BlockTypeId::None),
    make("thru",               "Thru",     0, BlockTypeId::None,       BlockTypeId::None),
}};
}  // namespace

namespace AlgorithmLibrary {
std::size_t count() { return kAlgorithms.size(); }

const Algorithm& byIndex(std::size_t i) {
    return kAlgorithms[i < kAlgorithms.size() ? i : 0];
}

std::size_t indexOfId(const char* id) {
    for (std::size_t i = 0; i < kAlgorithms.size(); ++i)
        if (std::strcmp(kAlgorithms[i].id, id) == 0) return i;
    return 0;
}
}  // namespace AlgorithmLibrary
