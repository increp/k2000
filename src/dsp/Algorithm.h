#pragma once
#include <array>
#include <cstddef>

// Stable identifier for the DSP block type sitting in a slot.
// New entries appended to the end as block types are added in v5+.
enum class BlockTypeId : int {
    None        = 0,
    SvfFilter   = 1,
    Waveshaper  = 2,
};

// Number of BlockTypeId values (used to size per-type arrays indexed by the
// enum value). Bump when a new BlockTypeId is appended.
inline constexpr std::size_t kNumBlockTypes = 3;

// Passive data describing a per-voice DSP topology: an ordered list of block
// TYPES. Order is the array order (blockTypePerSlot[0..slotCount)). The Voice
// walks the list, processing through the Layer's palette block for each type.
struct Algorithm {
    static constexpr std::size_t kMaxSlots = 4;

    const char* id          = "";   // stable, serialised via the choice index
    const char* displayName = "";   // shown in the layer.algorithm combo
    std::size_t slotCount   = 0;
    std::array<BlockTypeId, kMaxSlots> blockTypePerSlot {};

    // Retained only so the v2 Layer keeps compiling until Task 3 rewrites it.
    // Equivalent to AlgorithmLibrary entry 0 (filter_then_shaper). Removed in Task 3.
    static Algorithm v1Fixed() {
        Algorithm a;
        a.id = "filter_then_shaper"; a.displayName = "Filter -> Shaper"; a.slotCount = 2;
        a.blockTypePerSlot[0] = BlockTypeId::SvfFilter;
        a.blockTypePerSlot[1] = BlockTypeId::Waveshaper;
        return a;
    }
};
