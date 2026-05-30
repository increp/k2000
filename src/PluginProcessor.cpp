#include "PluginProcessor.h"
#include "dsp/DSPBlock.h"

#if !K2000_TESTING
  #include "PluginEditor.h"
#endif

K2000AudioProcessor::K2000AudioProcessor()
    : juce::AudioProcessor(BusesProperties()
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_(*this, nullptr, "PARAMS", params::createLayout()) {}

void K2000AudioProcessor::prepareToPlay(double sr, int samplesPerBlock) {
    voiceManager_.prepare(sr, samplesPerBlock);
    monoScratch_.assign(samplesPerBlock, 0.0f);
}

void K2000AudioProcessor::releaseResources() {}

bool K2000AudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo()
        || out == juce::AudioChannelSet::mono();
}

void K2000AudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                       juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int outCh = buffer.getNumChannels();

    buffer.clear();

    // Build the snapshot once for this block.
    auto snap = params::snapshot(apvts_);

    // Render mono into scratch.
    if ((int) monoScratch_.size() < n) monoScratch_.assign(n, 0.0f);
    std::fill(monoScratch_.begin(), monoScratch_.begin() + n, 0.0f);
    voiceManager_.renderBlock(monoScratch_.data(), n, midi, snap);

    // Apply master gain (dB -> linear)
    const float gainLin = juce::Decibels::decibelsToGain(snap.masterGainDb);

    // Copy mono scratch to all output channels with master gain.
    for (int c = 0; c < outCh; ++c) {
        float* ch = buffer.getWritePointer(c);
        for (int i = 0; i < n; ++i)
            ch[i] = monoScratch_[i] * gainLin;
    }
}

juce::AudioProcessorEditor* K2000AudioProcessor::createEditor() {
#if K2000_TESTING
    return nullptr;
#else
    return new K2000AudioProcessorEditor(*this);
#endif
}

// State format:
//   <K2000Root>
//     <Slots>
//       <Slot index="0" type="svf_filter"/>
//       <Slot index="1" type="waveshaper"/>
//     </Slots>
//     <Params> ...APVTS state... </Params>
//   </K2000Root>
//
// v1 ignores the Slots block on load (slot types are fixed) but writes it
// from day one so v1 presets stay loadable in v4 when the user can change
// slot types.
void K2000AudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto root = std::make_unique<juce::XmlElement>("K2000Root");

    auto* slots = root->createNewChildElement("Slots");
    auto* s0 = slots->createNewChildElement("Slot");
    s0->setAttribute("index", 0);
    s0->setAttribute("type", "svf_filter");
    auto* s1 = slots->createNewChildElement("Slot");
    s1->setAttribute("index", 1);
    s1->setAttribute("type", "waveshaper");

    if (auto state = apvts_.copyState(); state.isValid()) {
        auto paramsXml = state.createXml();
        if (paramsXml) {
            auto* wrapper = root->createNewChildElement("Params");
            wrapper->addChildElement(paramsXml.release());
        }
    }

    copyXmlToBinary(*root, destData);
}

void K2000AudioProcessor::setStateInformation(const void* data, int size) {
    auto xml = getXmlFromBinary(data, size);
    if (xml == nullptr) return;
    if (xml->getTagName() != "K2000Root") return;

    if (auto* params = xml->getChildByName("Params")) {
        if (auto* paramsRoot = params->getFirstChildElement()) {
            apvts_.replaceState(juce::ValueTree::fromXml(*paramsRoot));
        }
    }
    // Slot type metadata: ignored in v1 (types are fixed); v4 will read it here.
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new K2000AudioProcessor();
}
