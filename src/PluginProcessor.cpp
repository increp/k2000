#include "PluginProcessor.h"

#if !K2000_TESTING
  #include "PluginEditor.h"
#endif

K2000AudioProcessor::K2000AudioProcessor()
    : juce::AudioProcessor(BusesProperties()
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_(*this, nullptr, "PARAMS", params::createLayout()) {}

void K2000AudioProcessor::prepareToPlay(double sr, int samplesPerBlock) {
    program_.prepare(sr, samplesPerBlock, 1);
    voiceManager_.setProgram(&program_);  // bind before voices size state
    voiceManager_.prepare(sr, samplesPerBlock, 1);
    scratchL_.assign(samplesPerBlock, 0.0f);
    scratchR_.assign(samplesPerBlock, 0.0f);
    limiter_.prepare(sr);
    limiter_.reset();
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

    // Build a snapshot per layer and push parameters + routing to each slot.
    // The VoiceManager allocates voices per matched layer from the shared pool.
    float masterDb = 0.0f;  // master.gain is layer-independent; captured from layer 0
    for (std::size_t i = 0; i < program_.numLayers(); ++i) {
        auto snap = params::snapshot(apvts_, (int) i);
        if (i == 0) masterDb = snap.masterGainDb;
        auto& slot = program_.slot(i);
        slot.layer.updateParameters(snap);
        float levelGain = 1.0f;
        slot.routing = params::routing(apvts_, (int) i, levelGain);
        slot.layer.setLevel(levelGain);
    }

    // Render stereo into scratch. prepareToPlay sizes scratch_ to the host's
    // declared upper bound; a larger block here would mean a host bug or a
    // missed prepare call, not something to silently allocate around.
    jassert((int) scratchL_.size() >= n);
    std::fill(scratchL_.begin(), scratchL_.begin() + n, 0.0f);
    std::fill(scratchR_.begin(), scratchR_.begin() + n, 0.0f);
    voiceManager_.renderBlock(scratchL_.data(), scratchR_.data(), n, midi);

    const float gainLin = juce::Decibels::decibelsToGain(masterDb);
    for (int c = 0; c < outCh; ++c) {
        float* ch = buffer.getWritePointer(c);
        const float* src = (c == 1 && outCh > 1) ? scratchR_.data() : scratchL_.data();
        for (int i = 0; i < n; ++i) ch[i] = src[i] * gainLin;
    }
    if (limiterEnabled_.load(std::memory_order_relaxed) && outCh > 0) {
        float* L = buffer.getWritePointer(0);
        float* R = (outCh > 1) ? buffer.getWritePointer(1) : nullptr;
        limiter_.process(L, R, n);
        gainReductionDb_.store(limiter_.gainReductionDb(), std::memory_order_relaxed);
    } else {
        gainReductionDb_.store(0.0f, std::memory_order_relaxed);
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
//   <K2000Root limiterEnabled="1">
//     <Params> ...APVTS state... </Params>
//   </K2000Root>
//
// Preset backward-compatibility is explicitly not a project constraint, so there
// is no schema version or migration: old projects load on a best-effort basis
// (matching param ids restore; the rest take their layout defaults).
void K2000AudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto root = std::make_unique<juce::XmlElement>("K2000Root");
    root->setAttribute("limiterEnabled", limiterEnabled_.load(std::memory_order_relaxed) ? 1 : 0);

    if (auto state = apvts_.copyState(); state.isValid()) {
        if (auto paramsXml = state.createXml()) {
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
    limiterEnabled_.store(xml->getBoolAttribute("limiterEnabled", true), std::memory_order_relaxed);  // absent (old project) -> ON

    if (auto* params = xml->getChildByName("Params"))
        if (auto* paramsRoot = params->getFirstChildElement())
            apvts_.replaceState(juce::ValueTree::fromXml(*paramsRoot));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new K2000AudioProcessor();
}
