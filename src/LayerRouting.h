#pragma once

// Per-layer routing: decides whether a note plays this layer. Owned by a
// Program's LayerSlot, set each block from the layer{i}.* routing params.
// channel == 0 means Omni; 1..16 means that MIDI channel only. See ADR 0009.
struct LayerRouting {
    bool enable = false;
    int  keyLo = 0, keyHi = 127;    // inclusive MIDI note range
    int  velLo = 1, velHi = 127;    // inclusive velocity range
    int  channel = 0;               // 0 = Omni, else 1..16

    bool matches(int note, int velocity, int midiChannel) const {
        return enable
            && note >= keyLo && note <= keyHi
            && velocity >= velLo && velocity <= velHi
            && (channel == 0 || channel == midiChannel);
    }
};
