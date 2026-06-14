#include "Layer.h"
#include "dsp/blocks/SVFFilter.h"
#include "dsp/blocks/Waveshaper.h"

namespace {
std::unique_ptr<DSPBlock> makeBlock(BlockTypeId t) {
    switch (t) {
        case BlockTypeId::SvfFilter:  return std::make_unique<SVFFilter>();
        case BlockTypeId::Waveshaper: return std::make_unique<Waveshaper>();
        case BlockTypeId::None:       return nullptr;
    }
    return nullptr;
}
}  // namespace

Layer::Layer() {
    for (std::size_t i = 0; i < algorithm_.slotCount; ++i)
        slots_[i] = makeBlock(algorithm_.blockTypePerSlot[i]);
}

void Layer::prepare(double sr, int maxBlock) {
    for (std::size_t i = 0; i < algorithm_.slotCount; ++i)
        slots_[i]->prepare(sr, maxBlock);
}

void Layer::updateParameters(const ParamSnapshot& s) {
    snapshot_ = s;
    for (std::size_t i = 0; i < algorithm_.slotCount; ++i)
        slots_[i]->updateParameters(s);
}
