#include "FilterUnderTest.h"
#include "../../src/dsp/spine/MoogLadder.h"
#include "../../src/dsp/spine/HuggettFilter.h"

namespace chz {

FilterUnderTest::FilterUnderTest(juce::String name, std::unique_ptr<FilterModel> model,
                                 Configurator cfg)
    : name_(std::move(name)), model_(std::move(model)), cfg_(std::move(cfg)) {}

bool FilterUnderTest::supports(Mode m) {
    // Probe the configurator without disturbing measurement state.
    return cfg_(*model_, m);
}

void FilterUnderTest::setOperatingPoint(const OperatingPoint& op) {
    op_ = op;
    const double effSr = op.hostSampleRate * (double) op.osFactor;
    model_->prepare(effSr);
    cfg_(*model_, op.mode);
    model_->setCommon((float) op.cutoffHz, (float) op.resonance, (float) op.drive);
    state_.reset(model_->makeState());
    model_->reset(*state_);

    os_.prepare(kBlock);
    os_.setFactor(op.osFactor);
    const size_t cap = (size_t) kBlock * (size_t) VoiceOversampler::kMaxFactor;
    upL_.assign(cap, 0.0f); upR_.assign(cap, 0.0f);
    dnL_.assign((size_t) kBlock, 0.0f); dnR_.assign((size_t) kBlock, 0.0f);
}

void FilterUnderTest::reset() {
    if (state_) model_->reset(*state_);
    os_.setFactor(op_.osFactor);   // also clears the halfband state
}

void FilterUnderTest::process(float* mono, int n) {
    int done = 0;
    while (done < n) {
        const int blk = std::min(kBlock, n - done);
        if (op_.osFactor == 1) {
            // Base rate: duplicate L into R-scratch, discard R (mono measurement).
            std::copy(mono + done, mono + done + blk, dnR_.begin());
            model_->processStereo(*state_, mono + done, dnR_.data(), blk);
        } else {
            os_.processMonoUp(mono + done, blk, upL_.data());
            std::copy(upL_.begin(), upL_.begin() + os_.osBlock(blk), upR_.begin());
            model_->processStereo(*state_, upL_.data(), upR_.data(), os_.osBlock(blk));
            os_.processStereoDown(upL_.data(), upR_.data(), blk, dnL_.data(), dnR_.data());
            std::copy(dnL_.begin(), dnL_.begin() + blk, mono + done);
        }
        done += blk;
    }
}

std::unique_ptr<FilterUnderTest> makeMoogFut() {
    auto cfg = [](FilterModel& fm, Mode m) -> bool {
        auto& moog = static_cast<MoogLadder&>(fm);
        switch (m) {
            case Mode::LP12: moog.setMode(MoogLadder::Mode::LP); moog.setSlope(MoogLadder::Slope::db12); return true;
            case Mode::LP24: moog.setMode(MoogLadder::Mode::LP); moog.setSlope(MoogLadder::Slope::db24); return true;
            case Mode::BP:   moog.setMode(MoogLadder::Mode::BP); moog.setSlope(MoogLadder::Slope::db24); return true;
            case Mode::HP:   moog.setMode(MoogLadder::Mode::HP); moog.setSlope(MoogLadder::Slope::db24); return true;
            case Mode::Notch: return false;   // Moog ladder has no notch
        }
        return false;
    };
    return std::make_unique<FilterUnderTest>("moog", std::make_unique<MoogLadder>(), cfg);
}

std::unique_ptr<FilterUnderTest> makeHuggettFut() {
    auto cfg = [](FilterModel& fm, Mode m) -> bool {
        auto& hug = static_cast<HuggettFilter&>(fm);
        switch (m) {
            case Mode::LP12: hug.setRouting(HuggettFilter::Routing::LP); hug.setSlope(HuggettFilter::Slope::db12); return true;
            case Mode::LP24: hug.setRouting(HuggettFilter::Routing::LP); hug.setSlope(HuggettFilter::Slope::db24); return true;
            case Mode::BP:   hug.setRouting(HuggettFilter::Routing::BP); hug.setSlope(HuggettFilter::Slope::db24); return true;
            case Mode::HP:   hug.setRouting(HuggettFilter::Routing::HP); hug.setSlope(HuggettFilter::Slope::db24); return true;
            case Mode::Notch: hug.setRouting(HuggettFilter::Routing::Notch); hug.setSlope(HuggettFilter::Slope::db24); return true;
        }
        return false;
    };
    return std::make_unique<FilterUnderTest>("huggett", std::make_unique<HuggettFilter>(), cfg);
}

} // namespace chz
