#include "Oscillator.h"
#include <cmath>

namespace {
    constexpr double kTwoPi = 6.283185307179586;

    // Standard polyBLEP correction. t is the phase in [0,1), dt is phaseInc.
    // Returns the value to subtract at upward discontinuity, or to add at
    // downward discontinuity (caller flips sign as needed).
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

void Oscillator::setWaveform(Waveform w) { waveform_ = w; }

void Oscillator::setFrequency(float hz) {
    frequency_ = hz;
    phaseInc_ = double(hz) / sampleRate_;
}

float Oscillator::processSample() {
    // Guard against degenerate / negative freq
    if (phaseInc_ <= 0.0) return 0.0f;

    double v = 0.0;
    const double dt = phaseInc_;
    const double t  = phase_;

    switch (waveform_) {
        case Waveform::Sine:
            v = std::sin(kTwoPi * t);
            break;

        case Waveform::Saw:
            v = 2.0 * t - 1.0;
            v -= polyBLEP(t, dt);
            break;

        case Waveform::Square:
            v = (t < 0.5) ? 1.0 : -1.0;
            v += polyBLEP(t, dt);
            v -= polyBLEP(std::fmod(t + 0.5, 1.0), dt);
            break;

        case Waveform::Triangle: {
            // Integrate a polyBLEP-corrected square. Leaky integrator keeps
            // DC out. Scale by 4*dt to get ~unit-amplitude triangle.
            double sq = (t < 0.5) ? 1.0 : -1.0;
            sq += polyBLEP(t, dt);
            sq -= polyBLEP(std::fmod(t + 0.5, 1.0), dt);
            leakyInt_ = leakyInt_ * 0.999 + sq * 4.0 * dt;
            v = leakyInt_;
            break;
        }
    }

    phase_ += dt;
    if (phase_ >= 1.0) phase_ -= 1.0;
    return float(v);
}

void Oscillator::processBlock(float* buf, int n) {
    for (int i = 0; i < n; ++i) buf[i] = processSample();
}
