#include "MoogLadderAdapter.h"
#include "generated/MoogLadder.h"
#include <algorithm>
#include <cstring>
#include <new>

using Generated = MoogLadder;
static_assert(sizeof(Generated)  <= 512, "MoogLadder generated state exceeds MoogLadderAdapter::kGenBytes — bump it");
static_assert(alignof(Generated) <= 16,   "MoogLadder generated state over-aligned for the adapter buffer");

namespace { Generated* gen(unsigned char* s) { return reinterpret_cast<Generated*>(s); } }

MoogLadderAdapter::MoogLadderAdapter() noexcept { new (storage_) Generated(); }
MoogLadderAdapter::~MoogLadderAdapter() { gen(storage_)->~Generated(); }

void MoogLadderAdapter::prepare(double sr) noexcept { gen(storage_)->initialise(0, sr); }
void MoogLadderAdapter::reset() noexcept { gen(storage_)->reset(); }
void MoogLadderAdapter::setCutoff(float hz) noexcept { gen(storage_)->addEvent_cutoffHz(hz); }

void MoogLadderAdapter::process(float* mono, int numSamples) noexcept {
    auto* d = gen(storage_);
    const int cap = (int) Generated::maxFramesPerBlock;
    int i = 0;
    while (i < numSamples) {
        const int n = std::min(numSamples - i, cap);
        std::memcpy(d->cmajIO.in.elements, &mono[i], (size_t) n * sizeof(float));
        d->advance(n);
        std::memcpy(&mono[i], &d->cmajIO.out, (size_t) n * sizeof(float));
        i += n;
    }
}
