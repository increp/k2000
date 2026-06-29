#pragma once
#include <array>
#include <vector>
#include "Halfband2x.h"

// Per-voice cascade of Halfband2x stages giving 1x/2x/4x/8x. Stage j (0-based)
// bridges 2^j <-> 2^(j+1). Mono upsample (osc->graph), stereo downsample (spine->out).
// Buffers pre-sized for the MAX factor at prepare(); setFactor() only changes the
// active depth + clears state, never allocates.
//
// Round-trip latency in base samples; values verified by VoiceOversamplerTests impulse test.
class VoiceOversampler {
public:
    static constexpr int kMaxFactor = 8;
    static constexpr int kMaxStages = 3;   // 2^3 = 8

    void prepare(int maxBaseBlock) noexcept {
        maxBase_ = maxBaseBlock;
        for (auto& b : upScratch_)  b.assign((size_t) maxBaseBlock * kMaxFactor, 0.0f);
        for (auto& b : dnScratchL_) b.assign((size_t) maxBaseBlock * kMaxFactor, 0.0f);
        for (auto& b : dnScratchR_) b.assign((size_t) maxBaseBlock * kMaxFactor, 0.0f);
        setFactor(factor_);
    }

    void setFactor(int factor) noexcept {
        factor_ = (factor==2||factor==4||factor==8) ? factor : 1;
        stages_ = (factor_==8)?3 : (factor_==4)?2 : (factor_==2)?1 : 0;
        for (auto& s : upHb_) s.reset();
        for (auto& s : dnHbL_) s.reset();
        for (auto& s : dnHbR_) s.reset();
    }
    int factor() const noexcept { return factor_; }
    int osBlock(int nBase) const noexcept { return nBase * factor_; }

    // Round-trip latency in base-rate samples. Values verified by the impulse test
    // in VoiceOversamplerTests — if these change, update to match the measured peak.
    static int latencyBaseSamples(int factor) noexcept {
        switch (factor) { case 2: return 36; case 4: return 54; case 8: return 63; default: return 0; }
    }

    // nBase base samples -> nBase*factor mono samples.
    void processMonoUp(const float* baseIn, int nBase, float* osOut) noexcept {
        if (stages_ == 0) { for (int i = 0; i < nBase; ++i) osOut[i] = baseIn[i]; return; }
        const float* src = baseIn; int n = nBase;
        for (int s = 0; s < stages_; ++s) {
            float* dst = (s == stages_ - 1) ? osOut : upScratch_[(size_t) s].data();
            upHb_[(size_t) s].upsample(src, n, dst);
            src = dst; n *= 2;
        }
    }

    // nBase*factor stereo samples -> nBase base samples (stereo).
    void processStereoDown(const float* osL, const float* osR, int nBase,
                           float* baseL, float* baseR) noexcept {
        if (stages_ == 0) { for (int i = 0; i < nBase; ++i) { baseL[i]=osL[i]; baseR[i]=osR[i]; } return; }
        const float* sL = osL; const float* sR = osR; int n = nBase * factor_;
        for (int s = 0; s < stages_; ++s) {
            const int outN = n / 2;
            float* dL = (s == stages_ - 1) ? baseL : dnScratchL_[(size_t) s].data();
            float* dR = (s == stages_ - 1) ? baseR : dnScratchR_[(size_t) s].data();
            dnHbL_[(size_t) s].downsample(sL, outN, dL);
            dnHbR_[(size_t) s].downsample(sR, outN, dR);
            sL = dL; sR = dR; n = outN;
        }
    }

private:
    int maxBase_ = 0, factor_ = 1, stages_ = 0;
    std::array<Halfband2x, kMaxStages> upHb_, dnHbL_, dnHbR_;
    std::array<std::vector<float>, kMaxStages> upScratch_, dnScratchL_, dnScratchR_;
};
