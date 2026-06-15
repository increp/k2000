#include "PluginProcessor.h"
#include "dsp/DSPBlock.h"

#if !K2000_TESTING
  #include "PluginEditor.h"
#endif

namespace {
// v1 flat param IDs → v2 layer.* IDs. One row per renamed param; master.gain
// stays top-level and is intentionally absent. Add rows here as future renames
// land. See ADR 0007.
constexpr struct { const char* from; const char* to; } kV1ToV2Renames[] = {
    {"osc.waveform",    "layer.osc.waveform"},
    {"osc.coarse",      "layer.osc.coarse"},
    {"osc.fine",        "layer.osc.fine"},
    {"slot0.type",      "layer.slot0.type"},
    {"slot0.cutoff",    "layer.slot0.cutoff"},
    {"slot0.resonance", "layer.slot0.resonance"},
    {"slot1.drive",     "layer.slot1.drive"},
    {"slot1.mix",       "layer.slot1.mix"},
    {"amp.attack",      "layer.amp.attack"},
    {"amp.decay",       "layer.amp.decay"},
    {"amp.sustain",     "layer.amp.sustain"},
    {"amp.release",     "layer.amp.release"},
};

// v2 layer.slot* IDs → v3 block-type IDs. master.gain, layer.osc.*, layer.amp.*,
// layer.algorithm are unchanged across v2→v3.
constexpr struct { const char* from; const char* to; } kV2ToV3Renames[] = {
    {"layer.slot0.type",      "layer.filter.type"},
    {"layer.slot0.cutoff",    "layer.filter.cutoff"},
    {"layer.slot0.resonance", "layer.filter.resonance"},
    {"layer.slot1.drive",     "layer.shaper.drive"},
    {"layer.slot1.mix",       "layer.shaper.mix"},
};

// Applies a rename table to the APVTS state element (tag "PARAMS") in place.
template <typename Table>
void applyRenames(juce::XmlElement& paramsRoot, const Table& table) {
    for (auto* p : paramsRoot.getChildWithTagNameIterator("PARAM")) {
        const juce::String pid = p->getStringAttribute("id");
        for (const auto& r : table)
            if (pid == r.from) { p->setAttribute("id", r.to); break; }
    }
}

// Rewrites old flat PARAM ids to their layer.* names in place. `paramsRoot` is
// the APVTS state element (tag "PARAMS") holding <PARAM id=.. value=../> kids.
void migrateV1ToV2(juce::XmlElement& paramsRoot) { applyRenames(paramsRoot, kV1ToV2Renames); }

// Rewrites v2 layer.slot* IDs to their block-type names in place.
void migrateV2ToV3(juce::XmlElement& paramsRoot) { applyRenames(paramsRoot, kV2ToV3Renames); }

// v3->v4: every Layer-scoped id moves from the single "layer." prefix to
// "layer0." (the second layer is new and defaults disabled). This is a prefix
// rewrite, not a 1:1 table. master.gain is untouched (no "layer." prefix).
void migrateV3ToV4(juce::XmlElement& paramsRoot) {
    for (auto* p : paramsRoot.getChildWithTagNameIterator("PARAM")) {
        const juce::String pid = p->getStringAttribute("id");
        if (pid.startsWith("layer."))
            p->setAttribute("id", "layer0." + pid.substring(6));
    }
}
}  // namespace

K2000AudioProcessor::K2000AudioProcessor()
    : juce::AudioProcessor(BusesProperties()
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_(*this, nullptr, "PARAMS", params::createLayout()) {}

void K2000AudioProcessor::prepareToPlay(double sr, int samplesPerBlock) {
    program_.prepare(sr, samplesPerBlock);
    voiceManager_.setLayer(&program_.layer());  // bind before voices size state
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

    // Build a snapshot per layer and push parameters + routing to each slot.
    // Rendering is still single-layer (VoiceManager unchanged) — only layer0 plays.
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

    // Render mono into scratch. prepareToPlay sizes scratch_ to the host's
    // declared upper bound; a larger block here would mean a host bug or a
    // missed prepare call, not something to silently allocate around.
    jassert((int) monoScratch_.size() >= n);
    std::fill(monoScratch_.begin(), monoScratch_.begin() + n, 0.0f);
    voiceManager_.renderBlock(monoScratch_.data(), n, midi);

    // Apply master gain (dB -> linear)
    const float gainLin = juce::Decibels::decibelsToGain(masterDb);

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
    root->setAttribute("v", 4);  // schema version; gates the cumulative load shim

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
            // Cumulative migration: apply each shim in order based on stored version.
            const int v = xml->getIntAttribute("v", 1);
            if (v < 2) migrateV1ToV2(*paramsRoot);
            if (v < 3) migrateV2ToV3(*paramsRoot);
            if (v < 4) migrateV3ToV4(*paramsRoot);
            apvts_.replaceState(juce::ValueTree::fromXml(*paramsRoot));
        }
    }
    // Slot type metadata: ignored in v1 (types are fixed); v4 will read it here.
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new K2000AudioProcessor();
}
