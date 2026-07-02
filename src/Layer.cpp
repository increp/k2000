#include "Layer.h"
#include "dsp/blocks/Waveshaper.h"

Layer::Layer() {
    palette_[(int) BlockTypeId::Waveshaper] = std::make_unique<Waveshaper>();
}

void Layer::prepare(double sr, int maxBlock) {
    for (auto& b : palette_)
        if (b) b->prepare(sr, maxBlock);
    sampleRate_ = sr;
    // Models are created ONCE and stay stable for the Layer's lifetime: voice
    // slots cache non-owning FilterModel* (to destroy the per-voice state those
    // models constructed), so recreating models_ on re-prepare dangles every
    // cached pointer — a use-after-free on the next slot prepare (OS-factor
    // change / Live<->Offline re-prepare). Re-prepare reconfigures in place.
    if (models_.empty()) {
        for (std::size_t i = 0; i < FilterModelLibrary::count(); ++i)
            models_.push_back(FilterModelLibrary::create(i));
        currentModelId_ = 0;
        huggett_ = nullptr;
        moog_    = nullptr;
        for (auto& m : models_) {
            if (auto* h  = dynamic_cast<HuggettFilter*>(m.get())) huggett_ = h;
            if (auto* mg = dynamic_cast<MoogLadder*>(m.get()))    moog_    = mg;
        }
    }
    for (auto& m : models_) m->prepare(sampleRate_);
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
        huggett_->setRouting(static_cast<HuggettFilter::Routing>(s.huggettRouting));
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
