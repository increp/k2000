#pragma once
#include <memory>

// Stable wrapper hiding the generated Cmajor SVF class. Tasks 3-5 depend ONLY on
// this interface; the generated-class specifics live in SvfLinearAdapter.cpp.
class SvfLinearAdapter {
public:
    enum Tap { LP = 0, HP = 1, BP = 2 };

    SvfLinearAdapter();
    ~SvfLinearAdapter();
    SvfLinearAdapter(SvfLinearAdapter&&) noexcept;
    SvfLinearAdapter& operator=(SvfLinearAdapter&&) noexcept;

    void prepare(double sampleRate) noexcept;   // (re)create + initialise the generated instance
    void reset() noexcept;                        // clear filter state
    void setParams(float cutoffHz, float resonance, int tap) noexcept;  // block-rate
    void process(float* mono, int numSamples) noexcept;  // in-place mono render

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
