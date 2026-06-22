#pragma once
#include "../FilterModel.h"
#include "SvfLinearAdapter.h"

// Test-only FilterModel backed by the generated Cmajor SVF. NOT registered in
// FilterModelLibrary; compiled into the test target only. Stereo = two adapters.
class CmajorSvfFilter : public FilterModel {
public:
    struct VoiceState : public FilterModel::State {
        SvfLinearAdapter l, r;
    };
    void prepare(double sampleRate) noexcept override { sampleRate_ = sampleRate; }
    std::size_t stateSize()  const noexcept override { return sizeof(VoiceState); }
    std::size_t stateAlign() const noexcept override { return alignof(VoiceState); }
    FilterModel::State* constructState(void* mem) const override;
    void reset(State& s) const noexcept override;
    void setCommon(float cutoffHz, float resonance, float drive) noexcept override;
    void setTap(int tap) noexcept { tap_ = tap; }   // 0=LP 1=HP 2=BP (Huggett-bank-style setter)
    void processStereo(State& s, float* left, float* right, int numSamples) const noexcept override;
private:
    double sampleRate_ = 48000.0;
    float  cutoffHz_ = 1000.0f, resonance_ = 0.0f;
    int    tap_ = 0;
};
