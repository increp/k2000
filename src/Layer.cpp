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
}

void Layer::updateParameters(const ParamSnapshot& s) {
    snapshot_ = s;
    activeAlgorithmId_ = (std::size_t) s.algorithmId;
    for (auto& b : palette_)
        if (b) b->updateParameters(s);

    if (snapshot_.spineModel != (int) spineModelId_) {
        spineModelId_ = (std::size_t) snapshot_.spineModel;
        spineModel_ = FilterModelLibrary::create(spineModelId_);
        spineModel_->prepare(sampleRate_);
        huggett_ = dynamic_cast<HuggettFilter*>(spineModel_.get());
    }
    if (huggett_) {
        huggett_->setCommon(snapshot_.svfCutoffHz, snapshot_.svfResonance, snapshot_.spineDrive);
        huggett_->setMode(static_cast<HuggettFilter::Mode>(juce::jlimit(0, 2, snapshot_.svfType)));
        huggett_->setSlope(snapshot_.spineSlope == 0 ? HuggettFilter::Slope::db12 : HuggettFilter::Slope::db24);
        huggett_->setSeparation(snapshot_.spineSeparationOct);
    }
}
