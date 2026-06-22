#pragma once
#include <memory>

// Stable wrapper hiding the generated Cmajor AsymDrive class (memoryless tanh shaper).
class AsymDriveAdapter {
public:
    AsymDriveAdapter();
    ~AsymDriveAdapter();
    AsymDriveAdapter(AsymDriveAdapter&&) noexcept;
    AsymDriveAdapter& operator=(AsymDriveAdapter&&) noexcept;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    void setParams(float drive01, float biasFixed, float maxDriveDb) noexcept;
    void process(float* mono, int numSamples) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
