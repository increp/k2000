#include "WtOscAdapter.h"
#include "generated/WtOsc.h"

#include <algorithm>
#include <cstdint>

using Generated = WtOsc;

struct WtOscAdapter::Impl {
    Generated dsp;
    double sr = 48000.0;
    float table_[WtOscAdapter::kTableSize] = {};

    static constexpr int      kMaxBlock    = (int) Generated::maxFramesPerBlock;
    static constexpr uint32_t kOutHandle   = static_cast<uint32_t>(Generated::EndpointHandles::out);
    static constexpr uint32_t kTableHandle = static_cast<uint32_t>(Generated::EndpointHandles::tableIn);

    // setValue copies table_ into the patch's value-endpoint state (Array<float,256>,
    // layout-compatible with a contiguous float[256]). reset() zeroes state, so re-push.
    void pushTable() { dsp.setValue(kTableHandle, table_, 0); }

    void prepare(double sampleRate) { sr = sampleRate; dsp.initialise(0, sr); pushTable(); }
    void reset() { dsp.reset(); pushTable(); }
    void setTable(const float* t, int n) {
        const int m = std::min(n, WtOscAdapter::kTableSize);
        std::copy(t, t + m, table_);
        for (int i = m; i < WtOscAdapter::kTableSize; ++i) table_[i] = 0.0f;
        pushTable();
    }
    void setFrequency(float hz) { dsp.addEvent_frequency(hz); }
    void process(float* mono, int numSamples) {
        int i = 0;
        while (i < numSamples) {
            const int n = std::min(numSamples - i, kMaxBlock);
            dsp.advance(n);
            dsp.copyOutputFrames(kOutHandle, &mono[i], (uint32_t) n);
            i += n;
        }
    }
};

WtOscAdapter::WtOscAdapter() : impl_(std::make_unique<Impl>()) {}
WtOscAdapter::~WtOscAdapter() = default;
WtOscAdapter::WtOscAdapter(WtOscAdapter&&) noexcept = default;
WtOscAdapter& WtOscAdapter::operator=(WtOscAdapter&&) noexcept = default;

void WtOscAdapter::prepare(double sr) noexcept { impl_->prepare(sr); }
void WtOscAdapter::reset() noexcept { impl_->reset(); }
void WtOscAdapter::setTable(const float* t, int n) noexcept { impl_->setTable(t, n); }
void WtOscAdapter::setFrequency(float hz) noexcept { impl_->setFrequency(hz); }
void WtOscAdapter::process(float* mono, int n) noexcept { impl_->process(mono, n); }
