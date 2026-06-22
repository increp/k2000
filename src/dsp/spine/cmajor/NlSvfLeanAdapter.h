#pragma once
#include <memory>

// Zero-copy variant of NlSvfAdapter: the caller writes straight into the generated
// input buffer and reads straight from the generated output buffer (the generated
// cmajIO arrays are public), avoiding setInputFrames_in/copyOutputFrames memcpys.
// Still PIMPL/heap state (in-place state is a deferred adoption task).
class NlSvfLeanAdapter {
public:
    NlSvfLeanAdapter();
    ~NlSvfLeanAdapter();
    NlSvfLeanAdapter(NlSvfLeanAdapter&&) noexcept;
    NlSvfLeanAdapter& operator=(NlSvfLeanAdapter&&) noexcept;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    void setParams(float cutoffHz, float resonance, float resSat, int tap) noexcept;

    int  maxBlock() const noexcept;            // == Generated::maxFramesPerBlock (512)
    float* inBlock() noexcept;                 // write up to maxBlock() input samples here
    const float* outBlock() const noexcept;    // valid after advanceBlock()
    void advanceBlock(int n) noexcept;         // n <= maxBlock(): render in place, no copies
    void process(float* mono, int numSamples) noexcept;  // convenience (one copy pair)

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
