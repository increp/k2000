#pragma once
#include <memory>

// Stable wrapper hiding the generated Cmajor NlSvf class (linear SVF + resonance saturator).
class NlSvfAdapter {
public:
    enum Tap { LP = 0, HP = 1, BP = 2 };

    NlSvfAdapter();
    ~NlSvfAdapter();
    NlSvfAdapter(NlSvfAdapter&&) noexcept;
    NlSvfAdapter& operator=(NlSvfAdapter&&) noexcept;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    void setParams(float cutoffHz, float resonance, float resSat, int tap) noexcept;
    void process(float* mono, int numSamples) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
