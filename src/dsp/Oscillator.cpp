#include "Oscillator.h"
#include <cmath>

namespace {
    constexpr double kTwoPi = 6.283185307179586;

    // Standard polyBLEP correction for a discontinuity AT PHASE 0. t is the
    // phase in [0,1), dt is phaseInc. To correct a discontinuity at some
    // other phase X, call polyBLEP(fmod(t - X + 1.0, 1.0), dt) instead --
    // this shifts phase so X maps to 0 from this function's point of view.
    inline double polyBLEP(double t, double dt) {
        if (t < dt) {
            t /= dt;
            return t + t - t * t - 1.0;
        } else if (t > 1.0 - dt) {
            t = (t - 1.0) / dt;
            return t * t + t + t + 1.0;
        }
        return 0.0;
    }
}

void Oscillator::prepare(double sr) {
    sampleRate_ = sr;
    phase_ = 0.0;
    phaseInc_ = double(frequency_) / sampleRate_;
    leakyInt_ = 0.0;
}

void Oscillator::reset() {
    phase_ = 0.0;
    leakyInt_ = 0.0;
}

void Oscillator::setBlend(float sine, float tri, float saw, float pulse) {
    blendSine_ = sine; blendTri_ = tri; blendSaw_ = saw; blendPulse_ = pulse;
}

void Oscillator::setPulseDuty(float duty) { pulseDuty_ = duty; }

void Oscillator::setFrequency(float hz) {
    frequency_ = hz;
    phaseInc_ = double(hz) / sampleRate_;
}

float Oscillator::processSample() {
    // Guard against degenerate / negative freq
    if (phaseInc_ <= 0.0) return 0.0f;

    const double dt = phaseInc_;
    const double t  = phase_;

    const double total = double(blendSine_) + double(blendTri_) + double(blendSaw_) + double(blendPulse_);
    double v = 0.0;

    if (total > 0.0) {
        if (blendSine_ != 0.0f) {
            v += blendSine_ * std::sin(kTwoPi * t);
        }
        if (blendTri_ != 0.0f) {
            // Integrate a polyBLEP-corrected FIXED 50%-duty square. This is
            // independent of pulseDuty_ -- Triangle always derives from its
            // own internal 50% square, never the Pulse component's duty.
            double sq = (t < 0.5) ? 1.0 : -1.0;
            sq += polyBLEP(t, dt);
            sq -= polyBLEP(std::fmod(t + 0.5, 1.0), dt);
            leakyInt_ = leakyInt_ * 0.999 + sq * 4.0 * dt;
            v += blendTri_ * leakyInt_;
        }
        if (blendSaw_ != 0.0f) {
            double saw = 2.0 * t - 1.0;
            saw -= polyBLEP(t, dt);
            v += blendSaw_ * saw;
        }
        if (blendPulse_ != 0.0f) {
            const double duty = double(pulseDuty_);
            double pulse = (t < duty) ? 1.0 : -1.0;
            pulse += polyBLEP(t, dt);
            pulse -= polyBLEP(std::fmod(t - duty + 1.0, 1.0), dt);
            v += blendPulse_ * pulse;
        }
        v /= total;
    }

    phase_ += dt;
    if (phase_ >= 1.0) phase_ -= 1.0;
    return float(v);
}

void Oscillator::processBlock(float* buf, int n) {
    for (int i = 0; i < n; ++i) buf[i] = processSample();
}
