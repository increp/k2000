#include "Voice.h"
#include "dsp/blocks/SVFFilter.h"
#include "dsp/blocks/Waveshaper.h"
#include <cmath>

Voice::Voice() {
    slots_[0] = std::make_unique<SVFFilter>();
    slots_[1] = std::make_unique<Waveshaper>();
}

void Voice::prepare(double sr, int maxBlock) {
    sampleRate_ = sr;
    osc_.prepare(sr);
    amp_.prepare(sr);
    for (size_t i = 0; i < slots_.size(); ++i) {
        slots_[i]->prepare(sr, maxBlock);
        slotStates_[i] = slots_[i]->makeVoiceState();
    }
    scratch_.assign(maxBlock, 0.0f);  // allocate once, RT-safe henceforth
    reset();
}

void Voice::reset() {
    osc_.reset();
    amp_.reset();
    for (size_t i = 0; i < slots_.size(); ++i)
        if (slotStates_[i]) slots_[i]->resetVoice(*slotStates_[i]);
    note_ = -1;
    velocity_ = 0.0f;
}

void Voice::noteOn(int midiNote, float velocity) {
    note_ = midiNote;
    velocity_ = velocity;
    osc_.reset();
    amp_.reset();
    for (size_t i = 0; i < slots_.size(); ++i)
        if (slotStates_[i]) slots_[i]->resetVoice(*slotStates_[i]);
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

void Voice::render(float* out, int numSamples, const ParamSnapshot& s) {
    if (!isActive()) return;

    // Apply parameter snapshot to all sub-components.
    const float tune = s.oscCoarse + s.oscFine * 0.01f;
    const float hz = midiToHz(note_) * std::pow(2.0f, tune / 12.0f);
    osc_.setFrequency(hz);
    osc_.setWaveform(static_cast<Oscillator::Waveform>(s.oscWaveform));
    amp_.setParameters(s.ampAttackS, s.ampDecayS, s.ampSustain, s.ampReleaseS);
    for (auto& slot : slots_) slot->updateParameters(s);

    jassert(numSamples <= (int) scratch_.size());
    float* tmp = scratch_.data();
    osc_.processBlock(tmp, numSamples);
    for (size_t i = 0; i < slots_.size(); ++i)
        slots_[i]->process(*slotStates_[i], tmp, numSamples);

    for (int i = 0; i < numSamples; ++i) {
        out[i] += tmp[i] * amp_.nextSample() * velocity_;
    }
}
