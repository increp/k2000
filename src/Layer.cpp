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
}

void Layer::updateParameters(const ParamSnapshot& s) {
    snapshot_ = s;
    activeAlgorithmId_ = (std::size_t) s.algorithmId;
    for (auto& b : palette_)
        if (b) b->updateParameters(s);
}
