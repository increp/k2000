#pragma once
#include <array>
#include <cstddef>

// Stable identifier for the DSP block type sitting in each slot.
// New entries appended to the end as block types are added in v5+.
enum class BlockTypeId : int {
    None        = 0,
    SvfFilter   = 1,
    Waveshaper  = 2,
};

// Passive data describing a per-voice DSP topology.
// At v2 we ship one algorithm (Algorithm::v1Fixed). v3 introduces a library
// and a selection mechanism; v4 may extend the struct to carry routing
// metadata for non-linear topologies.
struct Algorithm {
    static constexpr std::size_t kMaxSlots = 4;

    std::size_t slotCount = 0;
    std::array<BlockTypeId, kMaxSlots> blockTypePerSlot {};

    // v1's fixed chain: osc → SVF filter → Waveshaper → amp.
    static Algorithm v1Fixed() {
        Algorithm a;
        a.slotCount = 2;
        a.blockTypePerSlot[0] = BlockTypeId::SvfFilter;
        a.blockTypePerSlot[1] = BlockTypeId::Waveshaper;
        return a;
    }
};
