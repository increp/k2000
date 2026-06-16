#include "PluginEditor.h"
#include "params/Parameters.h"
#include "dsp/AlgorithmLibrary.h"

K2000AudioProcessorEditor::K2000AudioProcessorEditor(K2000AudioProcessor& p)
    : juce::AudioProcessorEditor(&p), processorRef(p) {
    setLookAndFeel(&lnf_);
    buildStaticControls();

    // master gain is not per-layer — bind once.
    binder_.bind(masterGain_.slider(), params::masterGain);

    bindLayer(0);
    setSize(920, 620);
}

K2000AudioProcessorEditor::~K2000AudioProcessorEditor() {
    binder_.clear();        // detach all attachments while the controls are still alive
    setLookAndFeel(nullptr);
}

// One-time setup: section children, combo item lists, captions, edit-layer combo.
void K2000AudioProcessorEditor::buildStaticControls() {
    title_.setText(juce::String("k2000  v") + JucePlugin_VersionString,
                   juce::dontSendNotification);
    title_.setFont(juce::Font(16.0f, juce::Font::bold));
    addAndMakeVisible(title_);

    editLayerLabel_.setText("Edit Layer", juce::dontSendNotification);
    editLayerLabel_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(editLayerLabel_);
    for (int i = 0; i < params::kNumLayers; ++i)
        editLayerCombo_.addItem("Layer " + juce::String(i), i + 1);
    editLayerCombo_.setSelectedId(1, juce::dontSendNotification);
    editLayerCombo_.onChange = [this] {
        editLayer_ = editLayerCombo_.getSelectedId() - 1;
        bindLayer(editLayer_);
    };
    addAndMakeVisible(editLayerCombo_);
    addAndMakeVisible(masterGain_);

    // Source / DSP section
    addAndMakeVisible(sourceSection_);
    auto addToSource = [this](juce::Component& c) { sourceSection_.addAndMakeVisible(c); };
    oscWaveLbl_.setText("Wave", juce::dontSendNotification);
    oscWaveLbl_.setJustificationType(juce::Justification::centred);
    oscWave_.addItemList(juce::StringArray{ "Saw", "Square", "Triangle", "Sine" }, 1);
    addToSource(oscWaveLbl_); addToSource(oscWave_);
    addToSource(oscCoarse_); addToSource(oscFine_);
    algoLbl_.setText("Algo", juce::dontSendNotification);
    algoLbl_.setJustificationType(juce::Justification::centred);
    juce::StringArray algoItems;
    for (std::size_t i = 0; i < AlgorithmLibrary::count(); ++i)
        algoItems.add(AlgorithmLibrary::byIndex(i).displayName);
    algo_.addItemList(algoItems, 1);
    addToSource(algoLbl_); addToSource(algo_);
    addToSource(shaperDrive_); addToSource(shaperMix_);

    // Filter section
    addAndMakeVisible(filterSection_);
    filterTypeLbl_.setText("Type", juce::dontSendNotification);
    filterTypeLbl_.setJustificationType(juce::Justification::centred);
    filterType_.addItemList(juce::StringArray{ "LP", "HP", "BP", "Notch" }, 1);
    filterSection_.addAndMakeVisible(filterTypeLbl_);
    filterSection_.addAndMakeVisible(filterType_);
    filterSection_.addAndMakeVisible(filterCutoff_);
    filterSection_.addAndMakeVisible(filterRes_);

    // Amp env section
    addAndMakeVisible(ampEnvSection_);
    for (auto* k : { &ampA_, &ampD_, &ampS_, &ampR_ })
        ampEnvSection_.addAndMakeVisible(*k);

    // Reserved sections — visible (framed/dimmed) but no children.
    for (auto* s : { &mixerSection_, &driveSection_, &ampSection_,
                     &modEnvSection_, &lfoSection_, &modMatrixSection_, &fxSection_ })
        addAndMakeVisible(*s);

    // Routing section
    addAndMakeVisible(routingSection_);
    enableLbl_.setText("Enable", juce::dontSendNotification);
    enableLbl_.setJustificationType(juce::Justification::centred);
    routingSection_.addAndMakeVisible(enableLbl_);
    routingSection_.addAndMakeVisible(enable_);
    for (auto* k : { &keyLo_, &keyHi_, &velLo_, &velHi_, &level_ })
        routingSection_.addAndMakeVisible(*k);
    channelLbl_.setText("Channel", juce::dontSendNotification);
    channelLbl_.setJustificationType(juce::Justification::centred);
    juce::StringArray chanItems{ "Omni" };
    for (int ch = 1; ch <= 16; ++ch) chanItems.add(juce::String(ch));
    channel_.addItemList(chanItems, 1);
    routingSection_.addAndMakeVisible(channelLbl_);
    routingSection_.addAndMakeVisible(channel_);
}

// (Re)bind every per-layer control to the chosen layer's params. binder_'s
// detach-before-rebind contract guarantees the layer we leave is untouched.
void K2000AudioProcessorEditor::bindLayer(int layer) {
    const auto& ids = params::layerIds(layer);

    binder_.bind(oscWave_,             ids.oscWaveform);
    binder_.bind(oscCoarse_.slider(),  ids.oscCoarse);
    binder_.bind(oscFine_.slider(),    ids.oscFine);
    binder_.bind(algo_,                ids.algorithm);
    binder_.bind(shaperDrive_.slider(),ids.shaperDrive);
    binder_.bind(shaperMix_.slider(),  ids.shaperMix);

    binder_.bind(filterType_,          ids.filterType);
    binder_.bind(filterCutoff_.slider(),ids.filterCutoff);
    binder_.bind(filterRes_.slider(),  ids.filterResonance);

    binder_.bind(ampA_.slider(), ids.ampAttack);
    binder_.bind(ampD_.slider(), ids.ampDecay);
    binder_.bind(ampS_.slider(), ids.ampSustain);
    binder_.bind(ampR_.slider(), ids.ampRelease);

    binder_.bind(enable_,        ids.enable);
    binder_.bind(keyLo_.slider(),ids.keyLo);
    binder_.bind(keyHi_.slider(),ids.keyHi);
    binder_.bind(velLo_.slider(),ids.velLo);
    binder_.bind(velHi_.slider(),ids.velHi);
    binder_.bind(level_.slider(),ids.level);
    binder_.bind(channel_,       ids.channel);
}

void K2000AudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(SummitLookAndFeel::panelBg);
}

void K2000AudioProcessorEditor::resized() {
    auto area = getLocalBounds().reduced(10);

    // Top bar (h 40): title | spacer | edit-layer | master gain
    {
        auto bar = area.removeFromTop(40);
        title_.setBounds(bar.removeFromLeft(180));
        masterGain_.setBounds(bar.removeFromRight(70));
        editLayerCombo_.setBounds(bar.removeFromRight(110).reduced(0, 8));
        editLayerLabel_.setBounds(bar.removeFromRight(90).reduced(0, 8));
    }
    area.removeFromTop(8);

    // Helper: lay a row of {label,control} cells across a rectangle.
    auto layoutCells = [](juce::Rectangle<int> r,
                          std::initializer_list<std::pair<juce::Component*, juce::Component*>> cells) {
        const int n = (int) cells.size();
        if (n == 0) return;
        const int w = r.getWidth() / n;
        int x = r.getX();
        for (auto& [lbl, ctl] : cells) {
            if (lbl) lbl->setBounds(x, r.getY(), w, 16);
            ctl->setBounds(x, r.getY() + (lbl ? 16 : 0), w, r.getHeight() - (lbl ? 16 : 0));
            x += w;
        }
    };

    // Signal row (h 250): source(48%) | mixer | filter | drive | amp
    {
        auto row = area.removeFromTop(250);
        auto source = row.removeFromLeft((int) (row.getWidth() * 0.48f));
        sourceSection_.setBounds(source.reduced(2));
        const int rest = row.getWidth();
        mixerSection_.setBounds(row.removeFromLeft((int) (rest * 0.10f)).reduced(2));
        filterSection_.setBounds(row.removeFromLeft((int) (rest * 0.46f)).reduced(2));
        driveSection_.setBounds(row.removeFromLeft((int) (rest * 0.10f)).reduced(2));
        ampSection_.setBounds(row.reduced(2));

        // Source children: two stacked cell-rows inside contentBounds.
        auto sc = sourceSection_.contentBounds();
        auto top = sc.removeFromTop(sc.getHeight() / 2);
        layoutCells(top, { { &oscWaveLbl_, &oscWave_ }, { nullptr, &oscCoarse_ }, { nullptr, &oscFine_ } });
        layoutCells(sc,  { { &algoLbl_, &algo_ }, { nullptr, &shaperDrive_ }, { nullptr, &shaperMix_ } });

        // Filter children.
        layoutCells(filterSection_.contentBounds(),
                    { { &filterTypeLbl_, &filterType_ }, { nullptr, &filterCutoff_ }, { nullptr, &filterRes_ } });
    }
    area.removeFromTop(8);

    // Modulation row (h 150): amp env | mod envs | lfo | mod matrix | fx
    {
        auto row = area.removeFromTop(150);
        const int w = row.getWidth() / 5;
        ampEnvSection_.setBounds(row.removeFromLeft(w).reduced(2));
        modEnvSection_.setBounds(row.removeFromLeft(w).reduced(2));
        lfoSection_.setBounds(row.removeFromLeft(w).reduced(2));
        modMatrixSection_.setBounds(row.removeFromLeft(w).reduced(2));
        fxSection_.setBounds(row.reduced(2));

        layoutCells(ampEnvSection_.contentBounds(),
                    { { nullptr, &ampA_ }, { nullptr, &ampD_ }, { nullptr, &ampS_ }, { nullptr, &ampR_ } });
    }
    area.removeFromTop(8);

    // Routing strip (remaining): enable | key/vel/level | channel
    {
        routingSection_.setBounds(area.reduced(2));
        auto rc = routingSection_.contentBounds();
        const int n = 7;
        const int w = rc.getWidth() / n;
        int x = rc.getX();
        enableLbl_.setBounds(x, rc.getY(), w, 16);
        enable_.setBounds(x + w / 2 - 14, rc.getY() + 20, 28, 28);
        x += w;
        for (auto* k : { &keyLo_, &keyHi_, &velLo_, &velHi_, &level_ }) {
            k->setBounds(x, rc.getY(), w, rc.getHeight());
            x += w;
        }
        channelLbl_.setBounds(x, rc.getY(), w, 16);
        channel_.setBounds(x, rc.getY() + 16, w, 26);
    }
}
