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

// v5 retired the SvfFilter from the per-voice graph to the always-on Summit spine,
// which collapsed the four legacy algorithms into two real behaviours (a waveshaper
// or nothing). Trimmed to those two (2026-06-21) — preset index-compat deliberately
// dropped (no installed base). v6's dynamic graph will supersede this selector.
const std::array<Algorithm, 2> kAlgorithms = {{
    make("shaper", "Shaper", 1, BlockTypeId::Waveshaper, BlockTypeId::None),
    make("thru",   "Thru",   0, BlockTypeId::None,       BlockTypeId::None),
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
