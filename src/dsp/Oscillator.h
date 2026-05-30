#pragma once

class Oscillator {
public:
    enum class Waveform { Saw, Square, Triangle, Sine };

    void prepare(double sampleRate);
    void reset();
    void setWaveform(Waveform w);
    void setFrequency(float hz);

    float processSample();
    void processBlock(float* buffer, int numSamples);

private:
    double sampleRate_ = 44100.0;
    double phase_ = 0.0;      // 0..1
    double phaseInc_ = 0.0;   // freq / sampleRate
    float frequency_ = 0.0f;
    Waveform waveform_ = Waveform::Saw;
    double leakyInt_ = 0.0;   // triangle integrator state
};
