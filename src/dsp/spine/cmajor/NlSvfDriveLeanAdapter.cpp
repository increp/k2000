#include "NlSvfDriveLeanAdapter.h"
#include "generated/NlSvfDrive.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

using Generated = NlSvfDrive;

struct NlSvfDriveLeanAdapter::Impl {
    Generated dsp;
    double sr = 48000.0;
    static constexpr int      kMaxBlock  = (int) Generated::maxFramesPerBlock;
    static constexpr uint32_t kOutHandle = static_cast<uint32_t>(Generated::EndpointHandles::out);

    void prepare(double sampleRate) { sr = sampleRate; dsp.initialise(0, sr); }
    void reset() { dsp.reset(); }
    void setParams(float c, float r, float rs, int t, float d, float b, float m) {
        dsp.addEvent_cutoffHz(c); dsp.addEvent_resonance(r); dsp.addEvent_resSat(rs); dsp.addEvent_tap((int32_t) t);
        dsp.addEvent_drive01(d); dsp.addEvent_biasFixed(b); dsp.addEvent_maxDriveDb(m);
    }
    float*       inBlock()  { return dsp.cmajIO.in.elements; }
    const float* outBlock() const { return reinterpret_cast<const float*>(&dsp.cmajIO.out); }
    void advanceBlock(int n) { dsp.advance(n); }

    void process(float* mono, int numSamples) {
        int i = 0;
        while (i < numSamples) {
            const int n = std::min(numSamples - i, kMaxBlock);
            std::memcpy(dsp.cmajIO.in.elements, &mono[i], (size_t) n * sizeof(float));
            dsp.advance(n);
            std::memcpy(&mono[i], &dsp.cmajIO.out, (size_t) n * sizeof(float));
            i += n;
        }
    }
};

NlSvfDriveLeanAdapter::NlSvfDriveLeanAdapter() : impl_(std::make_unique<Impl>()) {}
NlSvfDriveLeanAdapter::~NlSvfDriveLeanAdapter() = default;
NlSvfDriveLeanAdapter::NlSvfDriveLeanAdapter(NlSvfDriveLeanAdapter&&) noexcept = default;
NlSvfDriveLeanAdapter& NlSvfDriveLeanAdapter::operator=(NlSvfDriveLeanAdapter&&) noexcept = default;

void NlSvfDriveLeanAdapter::prepare(double sr) noexcept { impl_->prepare(sr); }
void NlSvfDriveLeanAdapter::reset() noexcept { impl_->reset(); }
void NlSvfDriveLeanAdapter::setParams(float c, float r, float rs, int t, float d, float b, float m) noexcept {
    impl_->setParams(c, r, rs, t, d, b, m);
}
int  NlSvfDriveLeanAdapter::maxBlock() const noexcept { return Impl::kMaxBlock; }
float* NlSvfDriveLeanAdapter::inBlock() noexcept { return impl_->inBlock(); }
const float* NlSvfDriveLeanAdapter::outBlock() const noexcept { return impl_->outBlock(); }
void NlSvfDriveLeanAdapter::advanceBlock(int n) noexcept { impl_->advanceBlock(n); }
void NlSvfDriveLeanAdapter::process(float* mono, int n) noexcept { impl_->process(mono, n); }
