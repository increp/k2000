#pragma once

class Envelope {
public:
    void prepare(double sampleRate);
    void reset();
    void setParameters(float attackS, float decayS, float sustain, float releaseS);
    void noteOn();
    void noteOff();
    bool isActive() const;
    float nextSample();

private:
    enum class Stage { Idle, Attack, Decay, Sustain, Release };
    Stage stage_ = Stage::Idle;
    double sampleRate_ = 44100.0;
    float attackInc_  = 0.0f;
    float decayCoef_  = 0.0f;
    float releaseCoef_ = 0.0f;
    float sustain_    = 0.5f;
    float value_      = 0.0f;
};
