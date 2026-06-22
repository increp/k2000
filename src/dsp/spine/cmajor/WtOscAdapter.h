#pragma once
#include <memory>

// Stable wrapper hiding the generated Cmajor WtOsc class. setTable() pushes the
// wavetable at runtime via the generated value-array endpoint's setValue (the
// external float[] path is codegen-baked and cannot carry user-loaded data).
class WtOscAdapter {
public:
    static constexpr int kTableSize = 256;   // fixed by the WtOsc value endpoint

    WtOscAdapter();
    ~WtOscAdapter();
    WtOscAdapter(WtOscAdapter&&) noexcept;
    WtOscAdapter& operator=(WtOscAdapter&&) noexcept;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    void setTable(const float* table, int n) noexcept;   // n <= kTableSize; zero-padded
    void setFrequency(float hz) noexcept;
    void process(float* mono, int numSamples) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
