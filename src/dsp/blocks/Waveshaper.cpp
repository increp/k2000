#include "Waveshaper.h"
#include <cmath>
#include <algorithm>

void Waveshaper::prepare(double, int) {}

std::unique_ptr<DSPBlock::VoiceState> Waveshaper::makeVoiceState() const {
    return std::make_unique<Waveshaper::VoiceState>();
}

void Waveshaper::resetVoice(DSPBlock::VoiceState&) {
    // Stateless — nothing to reset.
}

void Waveshaper::updateParameters(const ParamSnapshot& s) {
    drive_ = std::clamp(s.wsDrive, 0.0f, 1.0f);
    mix_   = std::clamp(s.wsMix,   0.0f, 1.0f);
}

void Waveshaper::process(DSPBlock::VoiceState&, float* buf, int n) {
    // Drive: 0..1 maps to gain 1..10 before tanh; output normalised by tanh's saturation.
    const float gain = 1.0f + drive_ * 9.0f;
    const float invGain = 1.0f / std::tanh(gain);  // normalise full-scale input to ±1
    for (int i = 0; i < n; ++i) {
        const float dry = buf[i];
        const float wet = std::tanh(dry * gain) * invGain;
        buf[i] = dry + (wet - dry) * mix_;
    }
}
