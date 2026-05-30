#include "Envelope.h"
#include <cmath>
#include <algorithm>

void Envelope::prepare(double sr) {
    sampleRate_ = sr;
    reset();
}

void Envelope::reset() {
    stage_ = Stage::Idle;
    value_ = 0.0f;
}

// Exponential coefficient that decays from current value to target over
// `timeSeconds` reaching ~99% of the way (3 time constants).
static float expCoef(float timeSeconds, double sampleRate) {
    if (timeSeconds <= 0.0f) return 0.0f;
    return float(std::exp(-1.0 / (timeSeconds * sampleRate / 3.0)));
}

void Envelope::setParameters(float attackS, float decayS, float sustain, float releaseS) {
    attackInc_  = (attackS > 0.0f) ? float(1.0 / (attackS * sampleRate_)) : 1.0f;
    decayCoef_  = expCoef(decayS, sampleRate_);
    releaseCoef_ = expCoef(releaseS, sampleRate_);
    sustain_    = std::clamp(sustain, 0.0f, 1.0f);
}

void Envelope::noteOn() {
    stage_ = Stage::Attack;
}

void Envelope::noteOff() {
    if (stage_ != Stage::Idle) stage_ = Stage::Release;
}

bool Envelope::isActive() const {
    return stage_ != Stage::Idle;
}

float Envelope::nextSample() {
    switch (stage_) {
        case Stage::Idle:
            value_ = 0.0f;
            break;

        case Stage::Attack:
            value_ += attackInc_;
            if (value_ >= 1.0f) {
                value_ = 1.0f;
                stage_ = Stage::Decay;
            }
            break;

        case Stage::Decay:
            value_ = sustain_ + (value_ - sustain_) * decayCoef_;
            if (std::abs(value_ - sustain_) < 1e-4f) {
                value_ = sustain_;
                stage_ = Stage::Sustain;
            }
            break;

        case Stage::Sustain:
            value_ = sustain_;
            break;

        case Stage::Release:
            value_ *= releaseCoef_;
            if (value_ < 1e-4f) {
                value_ = 0.0f;
                stage_ = Stage::Idle;
            }
            break;
    }
    return value_;
}
