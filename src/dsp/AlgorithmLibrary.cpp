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
const std::array<Algorithm, 4> kAlgorithms = {{
    make("filter_then_shaper", "Filter \xE2\x86\x92 Shaper", 2, BlockTypeId::SvfFilter,  BlockTypeId::Waveshaper),
    make("shaper_then_filter", "Shaper \xE2\x86\x92 Filter", 2, BlockTypeId::Waveshaper, BlockTypeId::SvfFilter),
    make("filter_only",        "Filter only",      1, BlockTypeId::SvfFilter,  BlockTypeId::None),
    make("thru",               "Thru",             0, BlockTypeId::None,       BlockTypeId::None),
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
