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
    os_ = std::make_unique<VoiceOversampler>();
    os_->prepare(maxBlock);
    os_->setFactor(osFactor_);
    osMono_.assign((size_t) maxBlock * VoiceOversampler::kMaxFactor, 0.0f);
    osL_.assign  ((size_t) maxBlock * VoiceOversampler::kMaxFactor, 0.0f);
    osR_.assign  ((size_t) maxBlock * VoiceOversampler::kMaxFactor, 0.0f);

    osc1_.prepare(sr); osc2_.prepare(sr); osc3_.prepare(sr);  // base rate (already band-limited)
    amp_.prepare(sr);          // base rate
    scratch_.assign(maxBlock, 0.0f);
    oscScratch_.assign(maxBlock, 0.0f);
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
    osc1_.reset(); osc2_.reset(); osc3_.reset();
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
    osc1_.reset(); osc2_.reset(); osc3_.reset();
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

    amp_.setParameters(s.ampAttackS, s.ampDecayS, s.ampSustain, s.ampReleaseS);

    jassert(numSamples <= (int) scratch_.size());

    // --- Base rate: three VCOs, each independently tuned/blended, summed via
    //     their Mixer level into one mono buffer -----------------------------
    const float tune1 = s.osc1Coarse + s.osc1Fine * 0.01f;
    const float hz1 = midiToHz(note_) * std::pow(2.0f, tune1 / 12.0f);
    osc1_.setBlend(s.osc1BlendSine, s.osc1BlendTriangle, s.osc1BlendSaw, s.osc1BlendPulse);
    osc1_.setPulseDuty(s.osc1PulseDuty);
    osc1_.setFrequency(hz1);
    osc1_.processBlock(scratch_.data(), numSamples);
    for (int i = 0; i < numSamples; ++i) scratch_[i] *= s.mixerOsc1Level;

    const float tune2 = s.osc2Coarse + s.osc2Fine * 0.01f;
    const float hz2 = midiToHz(note_) * std::pow(2.0f, tune2 / 12.0f);
    osc2_.setBlend(s.osc2BlendSine, s.osc2BlendTriangle, s.osc2BlendSaw, s.osc2BlendPulse);
    osc2_.setPulseDuty(s.osc2PulseDuty);
    osc2_.setFrequency(hz2);
    osc2_.processBlock(oscScratch_.data(), numSamples);
    for (int i = 0; i < numSamples; ++i) scratch_[i] += oscScratch_[i] * s.mixerOsc2Level;

    const float tune3 = s.osc3Coarse + s.osc3Fine * 0.01f;
    const float hz3 = midiToHz(note_) * std::pow(2.0f, tune3 / 12.0f);
    osc3_.setBlend(s.osc3BlendSine, s.osc3BlendTriangle, s.osc3BlendSaw, s.osc3BlendPulse);
    osc3_.setPulseDuty(s.osc3PulseDuty);
    osc3_.setFrequency(hz3);
    osc3_.processBlock(oscScratch_.data(), numSamples);
    for (int i = 0; i < numSamples; ++i) scratch_[i] += oscScratch_[i] * s.mixerOsc3Level;

    // --- Upsample osc to oversampled domain ------------------------------------
    const int nOs = numSamples * osFactor_;
    os_->processMonoUp(scratch_.data(), numSamples, osMono_.data());

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
                         layer_->spineModel(), s.spineModelFadeMs, hz1,
                         osL_.data(), osR_.data(), nOs);

    // --- Downsample back to base rate ------------------------------------------
    os_->processStereoDown(osL_.data(), osR_.data(), numSamples,
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
