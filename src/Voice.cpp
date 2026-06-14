#include "Voice.h"
#include "Layer.h"
#include <cmath>

Voice::Voice() = default;

void Voice::prepare(double sr, int maxBlock) {
    sampleRate_ = sr;
    osc_.prepare(sr);
    amp_.prepare(sr);
    scratch_.assign(maxBlock, 0.0f);  // allocate once, RT-safe henceforth

    // Pre-allocate per-slot VoiceState according to the Layer's algorithm.
    if (layer_) {
        const auto& alg = layer_->algorithm();
        for (std::size_t i = 0; i < alg.slotCount; ++i)
            slotStates_[i] = layer_->slot(i).makeVoiceState();
    }
    reset();
}

void Voice::reset() {
    osc_.reset();
    amp_.reset();
    if (layer_) {
        const auto& alg = layer_->algorithm();
        for (std::size_t i = 0; i < alg.slotCount; ++i)
            if (slotStates_[i]) layer_->slot(i).resetVoice(*slotStates_[i]);
    }
    note_ = -1;
    velocity_ = 0.0f;
}

void Voice::noteOn(int midiNote, float velocity) {
    note_ = midiNote;
    velocity_ = velocity;
    osc_.reset();
    amp_.reset();
    if (layer_) {
        const auto& alg = layer_->algorithm();
        for (std::size_t i = 0; i < alg.slotCount; ++i)
            if (slotStates_[i]) layer_->slot(i).resetVoice(*slotStates_[i]);
    }
    amp_.noteOn();
}

void Voice::noteOff() {
    amp_.noteOff();
}

bool Voice::isActive() const {
    return amp_.isActive();
}

float Voice::midiToHz(int note) {
    return 440.0f * std::pow(2.0f, (note - 69) / 12.0f);
}

void Voice::render(float* out, int numSamples) {
    if (!isActive() || !layer_) return;

    const auto& s   = layer_->snapshot();
    const auto& alg = layer_->algorithm();

    // Apply parameter snapshot to the per-voice sub-components. (Slot blocks
    // are configured once per block by Layer::updateParameters.)
    const float tune = s.oscCoarse + s.oscFine * 0.01f;
    const float hz = midiToHz(note_) * std::pow(2.0f, tune / 12.0f);
    osc_.setFrequency(hz);
    osc_.setWaveform(static_cast<Oscillator::Waveform>(s.oscWaveform));
    amp_.setParameters(s.ampAttackS, s.ampDecayS, s.ampSustain, s.ampReleaseS);

    jassert(numSamples <= (int) scratch_.size());
    float* tmp = scratch_.data();
    osc_.processBlock(tmp, numSamples);

    for (std::size_t i = 0; i < alg.slotCount; ++i)
        layer_->slot(i).process(*slotStates_[i], tmp, numSamples);

    for (int i = 0; i < numSamples; ++i)
        out[i] += tmp[i] * amp_.nextSample() * velocity_;
}
