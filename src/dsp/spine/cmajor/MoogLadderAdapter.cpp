#include "MoogLadderAdapter.h"
#include "generated/MoogLadder.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <cstring>
#include <new>

using Generated = MoogLadder;
static_assert(sizeof(Generated)  <= 512, "MoogLadder generated state exceeds MoogLadderAdapter::kGenBytes — bump it");
static_assert(alignof(Generated) <= 16,   "MoogLadder generated state over-aligned for the adapter buffer");

namespace { Generated* gen(unsigned char* s) { return reinterpret_cast<Generated*>(s); } }

MoogLadderAdapter::MoogLadderAdapter() noexcept { new (storage_) Generated(); }
MoogLadderAdapter::~MoogLadderAdapter() { gen(storage_)->~Generated(); }

void MoogLadderAdapter::prepare(double sr) noexcept {
    auto* d = gen(storage_);
    const double maxF = d->getMaxFrequency();
    jassert(sr <= maxF);                 // spine is base-rate; >192 kHz is exotic and degrades, not crashes
    d->initialise(0, sr <= maxF ? sr : maxF);
}
void MoogLadderAdapter::reset() noexcept { gen(storage_)->reset(); }
void MoogLadderAdapter::setCutoff(float hz) noexcept { gen(storage_)->addEvent_cutoffHz(hz); }

void MoogLadderAdapter::setParams(float c, float r, float dr, int slope, int mode) noexcept {
    auto* d = gen(storage_);
    d->addEvent_cutoffHz(c);
    d->addEvent_resonance(r);
    d->addEvent_drive(dr);
    d->addEvent_slope((int32_t) slope);
    d->addEvent_mode((int32_t) mode);
}

void MoogLadderAdapter::setBass(float amount, int wave, int octave) noexcept {
    auto* d = gen(storage_);
    d->addEvent_bassAmount(amount);
    d->addEvent_bassWave((int32_t) wave);
    d->addEvent_bassOctave((int32_t) octave);
}
void MoogLadderAdapter::setFundamental(float hz) noexcept { gen(storage_)->addEvent_fundamentalHz(hz); }
void MoogLadderAdapter::noteReset() noexcept { gen(storage_)->addEvent_noteReset(); }

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
