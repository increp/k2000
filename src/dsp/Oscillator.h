#pragma once

class Oscillator {
public:
    void prepare(double sampleRate);
    void reset();
    // Sets the four blend weights (each 0..1, typically). The four shapes are
    // evaluated at one shared phase and combined proportionally -- see
    // processSample() for the exact math. A weight of exactly 0 skips that
    // shape's computation entirely (cheap: no trig/polyBLEP call).
    void setBlend(float sine, float tri, float saw, float pulse);
    // Duty cycle for the Pulse component only, in (0, 1) -- e.g. 0.5 is a
    // standard square. Does NOT affect Triangle, which derives from its own
    // internal fixed-50%-duty square regardless of this value.
    void setPulseDuty(float duty);
    void setFrequency(float hz);

    float processSample();
    void processBlock(float* buffer, int numSamples);

private:
    double sampleRate_ = 44100.0;
    double phase_ = 0.0;      // 0..1
    double phaseInc_ = 0.0;   // freq / sampleRate
    float frequency_ = 0.0f;
    float blendSine_ = 0.0f, blendTri_ = 0.0f, blendSaw_ = 1.0f, blendPulse_ = 0.0f;
    float pulseDuty_ = 0.5f;
    double leakyInt_ = 0.0;   // triangle integrator state
};
