#include "Layer.h"
#include "dsp/blocks/SVFFilter.h"
#include "dsp/blocks/Waveshaper.h"

Layer::Layer() {
    palette_[(int) BlockTypeId::SvfFilter]  = std::make_unique<SVFFilter>();
    palette_[(int) BlockTypeId::Waveshaper] = std::make_unique<Waveshaper>();
}

void Layer::prepare(double sr, int maxBlock) {
    for (auto& b : palette_)
        if (b) b->prepare(sr, maxBlock);
    sampleRate_ = sr;
    models_.clear();
    for (std::size_t i = 0; i < FilterModelLibrary::count(); ++i) {
        auto m = FilterModelLibrary::create(i);
        m->prepare(sampleRate_);
        models_.push_back(std::move(m));
    }
    currentModelId_ = 0;
    huggett_ = dynamic_cast<HuggettFilter*>(models_[0].get());
    moog_ = nullptr;
    for (auto& m : models_) if (auto* mg = dynamic_cast<MoogLadder*>(m.get())) moog_ = mg;
    hpStage_.prepare(sr);
}

void Layer::updateParameters(const ParamSnapshot& s) {
    snapshot_ = s;
    activeAlgorithmId_ = (std::size_t) s.algorithmId;
    for (auto& b : palette_)
        if (b) b->updateParameters(s);

    currentModelId_ = (std::size_t) s.spineModel;
    if (currentModelId_ >= models_.size()) currentModelId_ = 0;

    // Common core -> ALL models, so an outgoing model keeps tracking cutoff/res/drive
    // through a live crossfade (Task 5).
    for (auto& m : models_)
        m->setCommon(s.svfCutoffHz, s.svfResonance, s.spineDrive);

    if (huggett_) {
        int routingIdx = s.huggettRouting;
        if (routingIdx == 0) {
            switch (s.svfType) { case 1: routingIdx = 2; break;
                                 case 2: routingIdx = 1; break;
                                 default: routingIdx = 0; break; }
        }
        huggett_->setRouting(static_cast<HuggettFilter::Routing>(routingIdx));
        huggett_->setSlope(s.spineSlope == 0 ? HuggettFilter::Slope::db12 : HuggettFilter::Slope::db24);
        huggett_->setSeparation(s.spineSeparationOct);
        huggett_->setPostDrive(s.huggettPostDrive);
    }
    if (moog_) {
        moog_->setMode (static_cast<MoogLadder::Mode>(s.moogMode));
        moog_->setSlope(s.spineSlope == 0 ? MoogLadder::Slope::db12 : MoogLadder::Slope::db24);
        moog_->setBass (s.moogBassAmount, s.moogBassWave, s.moogBassOctave);
        moog_->setSeparation(s.spineSeparationOct);   // no-op, for symmetry
    }
    hpStage_.setParams(s.hpCutoffHz, s.hpResonance,
                       s.hpSlope == 0 ? HuggettHpStage::Slope::db12 : HuggettHpStage::Slope::db24);
}
