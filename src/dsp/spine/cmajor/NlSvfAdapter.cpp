#include "NlSvfAdapter.h"
#include "generated/NlSvf.h"

#include <algorithm>
#include <cstdint>

using Generated = NlSvf;

struct NlSvfAdapter::Impl {
    Generated dsp;
    double sr = 48000.0;

    static constexpr int      kMaxBlock  = (int) Generated::maxFramesPerBlock;
    static constexpr uint32_t kOutHandle = static_cast<uint32_t>(Generated::EndpointHandles::out);

    void prepare(double sampleRate) { sr = sampleRate; dsp.initialise(0, sr); }
    void reset() { dsp.reset(); }
    void setParams(float cutoffHz, float resonance, float resSat, int tap) {
        dsp.addEvent_cutoffHz(cutoffHz);
        dsp.addEvent_resonance(resonance);
        dsp.addEvent_resSat(resSat);
        dsp.addEvent_tap((int32_t) tap);
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

NlSvfAdapter::NlSvfAdapter() : impl_(std::make_unique<Impl>()) {}
NlSvfAdapter::~NlSvfAdapter() = default;
NlSvfAdapter::NlSvfAdapter(NlSvfAdapter&&) noexcept = default;
NlSvfAdapter& NlSvfAdapter::operator=(NlSvfAdapter&&) noexcept = default;

void NlSvfAdapter::prepare(double sr) noexcept { impl_->prepare(sr); }
void NlSvfAdapter::reset() noexcept { impl_->reset(); }
void NlSvfAdapter::setParams(float c, float r, float rs, int t) noexcept { impl_->setParams(c, r, rs, t); }
void NlSvfAdapter::process(float* mono, int n) noexcept { impl_->process(mono, n); }
