#pragma once
#include <memory>

// Zero-copy adapter for the fused NlSvfDrive processor (SVF + resonance saturator + drive
// in one advance). Used to measure whether fusing a multi-stage chain into a single Cmajor
// processor amortizes the per-advance overhead (the realistic v6 shape).
class NlSvfDriveLeanAdapter {
public:
    NlSvfDriveLeanAdapter();
    ~NlSvfDriveLeanAdapter();
    NlSvfDriveLeanAdapter(NlSvfDriveLeanAdapter&&) noexcept;
    NlSvfDriveLeanAdapter& operator=(NlSvfDriveLeanAdapter&&) noexcept;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    void setParams(float cutoffHz, float resonance, float resSat, int tap,
                   float drive01, float biasFixed, float maxDriveDb) noexcept;

    int  maxBlock() const noexcept;
    float* inBlock() noexcept;
    const float* outBlock() const noexcept;
    void advanceBlock(int n) noexcept;
    void process(float* mono, int numSamples) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
