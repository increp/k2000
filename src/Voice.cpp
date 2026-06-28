#include "Voice.h"
#include "Layer.h"
#include "dsp/spine/SpineState.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <cmath>

// The per-block-type loops below start at t=1 to skip BlockTypeId::None.
static_assert((int) BlockTypeId::None == 0, "voice state iteration skips index 0 == None");

Voice::Voice() = default;

void Voice::prepare(double sr, int maxBlock, int osFactor) {
    sampleRate_ = sr;
    osFactor_   = (osFactor==2||osFactor==4||osFactor==8) ? osFactor : 1;
    const double inner = sr * (double) osFactor_;
    os_.prepare(maxBlock);
    os_.setFactor(osFactor_);
    osMono_.assign((size_t) maxBlock * VoiceOversampler::kMaxFactor, 0.0f);
    osL_.assign  ((size_t) maxBlock * VoiceOversampler::kMaxFactor, 0.0f);
    osR_.assign  ((size_t) maxBlock * VoiceOversampler::kMaxFactor, 0.0f);

    osc_.prepare(sr);          // base rate (already band-limited)
    amp_.prepare(sr);          // base rate
    scratch_.assign(maxBlock, 0.0f);
    scratchR_.assign(maxBlock, 0.0f);
    baseL_.assign(maxBlock, 0.0f);
    baseR_.assign(maxBlock, 0.0f);
    spine_.prepare(inner, maxBlock * osFactor_, layer_ ? layer_->spineModel() : nullptr,
                       layer_ ? layer_->hpStage()    : nullptr);

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
    spine_.reset(layer_ ? layer_->spineModel() : nullptr,
                 layer_ ? layer_->hpStage()    : nullptr);
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
    // bind() snaps the spine to the current layer's model with no fade; correct for note-start
    // (including stolen-voice reassignment where the layer changes between prepare and noteOn).
    spine_.bind(layer_ ? layer_->spineModel() : nullptr,
                layer_ ? layer_->hpStage()    : nullptr);
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
    osc_.setWaveform(static_cast<Oscillator::Waveform>(s.oscWaveform));
    amp_.setParameters(s.ampAttackS, s.ampDecayS, s.ampSustain, s.ampReleaseS);

    jassert(numSamples <= (int) scratch_.size());

    // --- Base rate: oscillator --------------------------------------------------
    osc_.setFrequency(hz);
    osc_.processBlock(scratch_.data(), numSamples);

    // --- Upsample osc to oversampled domain ------------------------------------
    const int nOs = numSamples * osFactor_;
    os_.processMonoUp(scratch_.data(), numSamples, osMono_.data());

    // --- Graph blocks run in the oversampled domain (prepared at osFactor_*sr) --
    for (std::size_t i = 0; i < alg.slotCount; ++i) {
        const BlockTypeId t = alg.blockTypePerSlot[i];
        layer_->block(t).process(*blockStates_[(int) t], osMono_.data(), nOs);
    }

    // --- Mono -> stereo copy; spine runs in oversampled domain -----------------
    // HP pre-filter is ON whenever its cutoff knob is off the 0 position.
    std::copy(osMono_.data(), osMono_.data() + nOs, osL_.data());
    std::copy(osMono_.data(), osMono_.data() + nOs, osR_.data());
    spine_.processStereo(layer_->hpStage(), s.hpCutoffHz > 0.0f,
                         layer_->spineModel(), s.spineModelFadeMs, hz,
                         osL_.data(), osR_.data(), nOs);

    // --- Downsample back to base rate ------------------------------------------
    os_.processStereoDown(osL_.data(), osR_.data(), numSamples,
                          baseL_.data(), baseR_.data());

    // --- Envelope/output at base rate ------------------------------------------
    const float lvl      = layer_->level();
    const float spineOut = juce::Decibels::decibelsToGain(s.spineOutputDb);
    for (int i = 0; i < numSamples; ++i) {
        const float env = amp_.nextSample() * velocity_ * lvl * spineOut;
        outL[i] += baseL_[i] * env;
        outR[i] += baseR_[i] * env;
    }
}
