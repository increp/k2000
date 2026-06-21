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
    spineModelId_ = 0;
    spineModel_ = FilterModelLibrary::create(0);
    spineModel_->prepare(sampleRate_);
    huggett_ = dynamic_cast<HuggettFilter*>(spineModel_.get());
    hpStage_.prepare(sr);
}

void Layer::updateParameters(const ParamSnapshot& s) {
    snapshot_ = s;
    activeAlgorithmId_ = (std::size_t) s.algorithmId;
    for (auto& b : palette_)
        if (b) b->updateParameters(s);

    // Plan 3 / hot-swap note: Voice fetches layer_->spineModel() FRESH each render
    // (SpineFilterSlot no longer caches the model), so rebuilding spineModel_ to the
    // SAME type is safe — active voices pick up the new object next block. The
    // remaining hazard is rebuilding to a DIFFERENT model TYPE: each voice's
    // pre-allocated State was made for the prior type and would be mismatched. DEAD
    // today (FilterModelLibrary has one entry). The live hot-swap plan must
    // re-make/crossfade per-voice state on a type change. See register Q17/Q18.
    if (snapshot_.spineModel != (int) spineModelId_) {
        spineModelId_ = (std::size_t) snapshot_.spineModel;
        spineModel_ = FilterModelLibrary::create(spineModelId_);
        spineModel_->prepare(sampleRate_);
        huggett_ = dynamic_cast<HuggettFilter*>(spineModel_.get());
    }
    if (huggett_) {
        huggett_->setCommon(snapshot_.svfCutoffHz, snapshot_.svfResonance, snapshot_.spineDrive);
        // OQ3: spine.huggett.routing is the source of truth for spine mode. When it is
        // still at its default (0) AND a legacy preset selected a non-LP filter.type,
        // seed the routing once from filter.type (filter.type order LP,HP,BP,Notch ->
        // routing order LP,BP,HP; Notch->LP). Otherwise use the stored routing.
        int routingIdx = snapshot_.huggettRouting;
        if (routingIdx == 0) {
            switch (snapshot_.svfType) { case 1: routingIdx = 2; break;   // HP
                                         case 2: routingIdx = 1; break;   // BP
                                         default: routingIdx = 0; break; } // LP / Notch
        }
        huggett_->setRouting(static_cast<HuggettFilter::Routing>(routingIdx));
        huggett_->setSlope(snapshot_.spineSlope == 0 ? HuggettFilter::Slope::db12 : HuggettFilter::Slope::db24);
        huggett_->setSeparation(snapshot_.spineSeparationOct);
    }
    hpStage_.setParams(snapshot_.hpCutoffHz, snapshot_.hpResonance,
                       snapshot_.hpSlope == 0 ? HuggettHpStage::Slope::db12
                                              : HuggettHpStage::Slope::db24);
    if (huggett_) huggett_->setPostDrive(snapshot_.huggettPostDrive);
}
