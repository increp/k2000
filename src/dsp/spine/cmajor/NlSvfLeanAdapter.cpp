#include "NlSvfLeanAdapter.h"
#include "generated/NlSvf.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

using Generated = NlSvf;

struct NlSvfLeanAdapter::Impl {
    Generated dsp;
    double sr = 48000.0;
    static constexpr int      kMaxBlock  = (int) Generated::maxFramesPerBlock;
    static constexpr uint32_t kOutHandle = static_cast<uint32_t>(Generated::EndpointHandles::out);

    void prepare(double sampleRate) { sr = sampleRate; dsp.initialise(0, sr); }
    void reset() { dsp.reset(); }
    void setParams(float c, float r, float rs, int t) {
        dsp.addEvent_cutoffHz(c); dsp.addEvent_resonance(r); dsp.addEvent_resSat(rs); dsp.addEvent_tap((int32_t) t);
    }
    // Zero-copy: caller writes straight into the generated input buffer, advance renders in
    // place, caller reads straight from the generated output buffer. (cmajIO is public.)
    float*       inBlock()  { return dsp.cmajIO.in.elements; }
    const float* outBlock() const { return reinterpret_cast<const float*>(&dsp.cmajIO.out); }
    void advanceBlock(int n) { dsp.advance(n); }

    // Convenience path mirrors NlSvfAdapter (one copy in, one out) for apples-to-apples use.
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

NlSvfLeanAdapter::NlSvfLeanAdapter() : impl_(std::make_unique<Impl>()) {}
NlSvfLeanAdapter::~NlSvfLeanAdapter() = default;
NlSvfLeanAdapter::NlSvfLeanAdapter(NlSvfLeanAdapter&&) noexcept = default;
NlSvfLeanAdapter& NlSvfLeanAdapter::operator=(NlSvfLeanAdapter&&) noexcept = default;

void NlSvfLeanAdapter::prepare(double sr) noexcept { impl_->prepare(sr); }
void NlSvfLeanAdapter::reset() noexcept { impl_->reset(); }
void NlSvfLeanAdapter::setParams(float c, float r, float rs, int t) noexcept { impl_->setParams(c, r, rs, t); }
int  NlSvfLeanAdapter::maxBlock() const noexcept { return Impl::kMaxBlock; }
float* NlSvfLeanAdapter::inBlock() noexcept { return impl_->inBlock(); }
const float* NlSvfLeanAdapter::outBlock() const noexcept { return impl_->outBlock(); }
void NlSvfLeanAdapter::advanceBlock(int n) noexcept { impl_->advanceBlock(n); }
void NlSvfLeanAdapter::process(float* mono, int n) noexcept { impl_->process(mono, n); }
