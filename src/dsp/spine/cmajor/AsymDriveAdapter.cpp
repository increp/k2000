#include "AsymDriveAdapter.h"
#include "generated/AsymDrive.h"

#include <algorithm>
#include <cstdint>

using Generated = AsymDrive;

struct AsymDriveAdapter::Impl {
    Generated dsp;
    double sr = 48000.0;

    static constexpr int      kMaxBlock  = (int) Generated::maxFramesPerBlock;
    static constexpr uint32_t kOutHandle = static_cast<uint32_t>(Generated::EndpointHandles::out);

    void prepare(double sampleRate) { sr = sampleRate; dsp.initialise(0, sr); }
    void reset() { dsp.reset(); }
    void setParams(float drive01, float biasFixed, float maxDriveDb) {
        dsp.addEvent_drive01(drive01);
        dsp.addEvent_biasFixed(biasFixed);
        dsp.addEvent_maxDriveDb(maxDriveDb);
    }
    void process(float* mono, int numSamples) {
        int i = 0;
        while (i < numSamples) {
            const int n = std::min(numSamples - i, kMaxBlock);
            dsp.setInputFrames_in(&mono[i], (uint32_t) n, 0);
            dsp.advance(n);
            dsp.copyOutputFrames(kOutHandle, &mono[i], (uint32_t) n);
            i += n;
        }
    }
};

AsymDriveAdapter::AsymDriveAdapter() : impl_(std::make_unique<Impl>()) {}
AsymDriveAdapter::~AsymDriveAdapter() = default;
AsymDriveAdapter::AsymDriveAdapter(AsymDriveAdapter&&) noexcept = default;
AsymDriveAdapter& AsymDriveAdapter::operator=(AsymDriveAdapter&&) noexcept = default;

void AsymDriveAdapter::prepare(double sr) noexcept { impl_->prepare(sr); }
void AsymDriveAdapter::reset() noexcept { impl_->reset(); }
void AsymDriveAdapter::setParams(float d, float b, float m) noexcept { impl_->setParams(d, b, m); }
void AsymDriveAdapter::process(float* mono, int n) noexcept { impl_->process(mono, n); }
