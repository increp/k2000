#pragma once
#include <juce_core/juce_core.h>

// One-pole DC blocker, stereo. y[n] = x[n] - x[n-1] + R*y[n-1], corner ~8 Hz.
class DcBlocker {
public:
    void prepare(double sampleRate) noexcept {
        R_ = 1.0f - (float) (2.0 * 3.14159265358979 * 8.0 / sampleRate); // ~8 Hz // CALIB
    }
    void reset() noexcept { x1_[0]=x1_[1]=y1_[0]=y1_[1]=0.0f; }
    float process(float x, int ch) noexcept {
        jassert(ch >= 0 && ch < 2);
        const float y = x - x1_[ch] + R_ * y1_[ch];
        x1_[ch] = x; y1_[ch] = y;
        return y;
    }
private:
    float R_ = 0.999f;
    float x1_[2] = {0,0}, y1_[2] = {0,0};
};
