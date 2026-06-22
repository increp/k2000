#pragma once
#include "../../src/dsp/spine/FilterModel.h"
#include "../../src/dsp/spine/SpineState.h"
#include <cmath>
#include <algorithm>
#include <new>

// Test-only second FilterModel. A trivial one-pole LP with a DC gain, plus a global
// live-state counter so crossfade tests can assert no leak / no double-free.
class CountingFilterModel : public FilterModel {
public:
    struct VoiceState : public FilterModel::State {
        float z[2] = {0.0f, 0.0f};
        VoiceState()  { ++count(); }
        ~VoiceState() override { --count(); }
        static int& count() { static int c = 0; return c; }
    };

    static int liveStates() { return VoiceState::count(); }

    void prepare(double sr) noexcept override { sampleRate_ = sr; }
    std::size_t stateSize()  const noexcept override { return sizeof(VoiceState); }
    std::size_t stateAlign() const noexcept override { return alignof(VoiceState); }
    FilterModel::State* constructState(void* mem) const override { return new (mem) VoiceState(); }
    void reset(State& s) const noexcept override {
        auto& v = static_cast<VoiceState&>(s); v.z[0] = v.z[1] = 0.0f;
    }
    void setCommon(float cutoffHz, float, float) noexcept override {
        // one-pole coefficient from cutoff
        const float x = std::exp(-2.0f * 3.14159265f * cutoffHz / (float) sampleRate_);
        a_ = std::clamp(x, 0.0f, 0.9999f);
    }
    void setGain(float g) noexcept { gain_ = g; }
    void processStereo(State& s, float* l, float* r, int n) const noexcept override {
        auto& v = static_cast<VoiceState&>(s);
        for (int i = 0; i < n; ++i) {
            v.z[0] = a_ * v.z[0] + (1.0f - a_) * l[i]; l[i] = gain_ * v.z[0];
            v.z[1] = a_ * v.z[1] + (1.0f - a_) * r[i]; r[i] = gain_ * v.z[1];
        }
    }
private:
    double sampleRate_ = 48000.0;
    float a_ = 0.5f, gain_ = 1.0f;
};
static_assert(sizeof(CountingFilterModel::VoiceState) <= kMaxSpineStateBytes,
              "CountingFilterModel state exceeds kMaxSpineStateBytes");
