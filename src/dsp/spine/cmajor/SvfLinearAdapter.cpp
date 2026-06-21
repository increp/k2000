#include "SvfLinearAdapter.h"
#include "generated/SvfLinear.h"   // the committed generated class

#include <algorithm>
#include <cstdint>

// The generated class type name as it appears in generated/SvfLinear.h.
using Generated = SvfLinear;

struct SvfLinearAdapter::Impl {
    Generated dsp;
    double sr = 48000.0;

    // The generated render model is block-oriented with a fixed maximum block
    // size (Generated::maxFramesPerBlock == 512; cmajIO.in/out are Array<float,512>).
    // Larger process() calls are chunked into <= kMaxBlock sub-blocks.
    static constexpr int      kMaxBlock = (int) Generated::maxFramesPerBlock;
    static constexpr uint32_t kOutHandle =
        static_cast<uint32_t>(Generated::EndpointHandles::out);

    void prepare(double sampleRate) {
        sr = sampleRate;
        dsp.initialise(/*sessionID*/ 0, sr);   // sets sample rate + clears state
    }
    void reset() {
        dsp.reset();   // memset state + re-run _initialise (cheapest correct reset)
    }
    void setParams(float cutoffHz, float resonance, int tap) {
        // Events are queued now and dispatched at the next advance() — exactly the
        // generated "set event/param BEFORE advance" model. setParams is block-rate.
        dsp.addEvent_cutoffHz(cutoffHz);
        dsp.addEvent_resonance(resonance);
        dsp.addEvent_tap((int32_t) tap);
    }
    void process(float* mono, int numSamples) {
        int i = 0;
        while (i < numSamples) {
            const int n = std::min(numSamples - i, kMaxBlock);
            dsp.setInputFrames_in(&mono[i], (uint32_t) n, /*trailingToClear*/ 0);
            dsp.advance(n);
            dsp.copyOutputFrames(kOutHandle, &mono[i], (uint32_t) n);
            i += n;
        }
    }
};

SvfLinearAdapter::SvfLinearAdapter() : impl_(std::make_unique<Impl>()) {}
SvfLinearAdapter::~SvfLinearAdapter() = default;
SvfLinearAdapter::SvfLinearAdapter(SvfLinearAdapter&&) noexcept = default;
SvfLinearAdapter& SvfLinearAdapter::operator=(SvfLinearAdapter&&) noexcept = default;

void SvfLinearAdapter::prepare(double sr) noexcept { impl_->prepare(sr); }
void SvfLinearAdapter::reset() noexcept { impl_->reset(); }
void SvfLinearAdapter::setParams(float c, float r, int t) noexcept { impl_->setParams(c, r, t); }
void SvfLinearAdapter::process(float* mono, int n) noexcept { impl_->process(mono, n); }
