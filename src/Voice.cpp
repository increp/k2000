#include "Voice.h"
#include "Layer.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <cmath>

// The per-block-type loops below start at t=1 to skip BlockTypeId::None.
static_assert((int) BlockTypeId::None == 0, "voice state iteration skips index 0 == None");

Voice::Voice() = default;

void Voice::prepare(double sr, int maxBlock) {
    sampleRate_ = sr;
    osc_.prepare(sr);
    amp_.prepare(sr);
    scratch_.assign(maxBlock, 0.0f);
    scratchR_.assign(maxBlock, 0.0f);
    spine_.prepare(sr, layer_ ? layer_->spineModel() : nullptr);

    // One VoiceState per palette block type.
    if (layer_) {
        for (int t = 1; t < (int) kNumBlockTypes; ++t)
            if (layer_->hasBlock((BlockTypeId) t))
                blockStates_[t] = layer_->block((BlockTypeId) t).makeVoiceState();
    }
    reset();
}

void Voice::reset() {
    osc_.reset();
    amp_.reset();
    spine_.reset();
    if (layer_)
        for (int t = 1; t < (int) kNumBlockTypes; ++t)
            if (blockStates_[t]) layer_->block((BlockTypeId) t).resetVoice(*blockStates_[t]);
    note_ = -1;
    velocity_ = 0.0f;
}

void Voice::noteOn(int midiNote, float velocity) {
    note_ = midiNote;
    velocity_ = velocity;
    osc_.reset();
    amp_.reset();
    spine_.reset();
    if (layer_)
        for (int t = 1; t < (int) kNumBlockTypes; ++t)
            if (blockStates_[t]) layer_->block((BlockTypeId) t).resetVoice(*blockStates_[t]);
    amp_.noteOn();
}

void Voice::noteOff() { amp_.noteOff(); }
bool Voice::isActive() const { return amp_.isActive(); }

float Voice::midiToHz(int note) {
    return 440.0f * std::pow(2.0f, (note - 69) / 12.0f);
}

void Voice::render(float* outL, float* outR, int numSamples) {
    if (!isActive() || !layer_) return;

    const auto& s   = layer_->snapshot();
    const auto& alg = layer_->activeAlgorithm();

    const float tune = s.oscCoarse + s.oscFine * 0.01f;
    const float hz = midiToHz(note_) * std::pow(2.0f, tune / 12.0f);
    osc_.setFrequency(hz);
    osc_.setWaveform(static_cast<Oscillator::Waveform>(s.oscWaveform));
    amp_.setParameters(s.ampAttackS, s.ampDecayS, s.ampSustain, s.ampReleaseS);

    jassert(numSamples <= (int) scratch_.size());
    float* tmpL = scratch_.data();
    float* tmpR = scratchR_.data();
    osc_.processBlock(tmpL, numSamples);

    for (std::size_t i = 0; i < alg.slotCount; ++i) {
        const BlockTypeId t = alg.blockTypePerSlot[i];
        layer_->block(t).process(*blockStates_[(int) t], tmpL, numSamples);
    }
    // Mono graph -> stereo spine input (dual mono; L/R diverge in later phases).
    std::copy(tmpL, tmpL + numSamples, tmpR);
    spine_.processStereo(tmpL, tmpR, numSamples);

    const float lvl = layer_->level();
    const float spineOut = juce::Decibels::decibelsToGain(s.spineOutputDb);
    for (int i = 0; i < numSamples; ++i) {
        const float env = amp_.nextSample() * velocity_ * lvl * spineOut;
        outL[i] += tmpL[i] * env;
        outR[i] += tmpR[i] * env;
    }
}
