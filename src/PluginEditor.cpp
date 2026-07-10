#include "PluginEditor.h"
#include "params/Parameters.h"
#include "util/Utf8.h"

namespace {
constexpr int kRailW   = 14;   // wood side rails
constexpr int kHeaderH = 92;   // cream header plate
constexpr int kFooterH = 78;   // cream footer plate (layer routing strip)
}

K2000AudioProcessorEditor::K2000AudioProcessorEditor(K2000AudioProcessor& p)
    : juce::AudioProcessorEditor(&p), processorRef(p) {
    setLookAndFeel(&lnf_);
    buildStaticControls();

    // master gain is not per-layer — bind once.
    binder_.bind(masterGain_, params::masterGain);

    bindLayer(0);
    startTimerHz(24);
    setSize(1400, 1050);
}

K2000AudioProcessorEditor::~K2000AudioProcessorEditor() {
    stopTimer();
    binder_.clear();        // detach all attachments while the controls are still alive
    setLookAndFeel(nullptr);
}

// One-time setup: section children, combo item lists, captions, edit-layer combo.
void K2000AudioProcessorEditor::buildStaticControls() {
    title_.setText(juce::String("Bernie  v") + JucePlugin_VersionString,
                   juce::dontSendNotification);
    title_.setFont(VintageLookAndFeel::condensedFont(18.0f));
    title_.setColour(juce::Label::textColourId, VintageLookAndFeel::creamText);
    addAndMakeVisible(title_);

    editLayerLabel_.setText("Edit Layer", juce::dontSendNotification);
    editLayerLabel_.setJustificationType(juce::Justification::centredRight);
    editLayerLabel_.setColour(juce::Label::textColourId, VintageLookAndFeel::creamText);
    addAndMakeVisible(editLayerLabel_);
    for (int i = 0; i < params::kNumLayers; ++i)
        editLayerCombo_.addItem("Layer " + juce::String(i), i + 1);
    editLayerCombo_.setSelectedId(1, juce::dontSendNotification);
    editLayerCombo_.onChange = [this] {
        editLayer_ = editLayerCombo_.getSelectedId() - 1;
        bindLayer(editLayer_);
    };
    addAndMakeVisible(editLayerCombo_);
    masterGainLbl_.setText("OUTPUT", juce::dontSendNotification);
    masterGainLbl_.setJustificationType(juce::Justification::centred);
    masterGainLbl_.setColour(juce::Label::textColourId, VintageLookAndFeel::creamText);
    addAndMakeVisible(masterGainLbl_);
    masterGain_.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 15);
    addAndMakeVisible(masterGain_);

    menuButton_.onClick = [this] { showOversamplingMenu(); };
    addAndMakeVisible(menuButton_);

    // VAST DSP panel (bottom row) — the algorithm selector's new home.
    addAndMakeVisible(vastDspSection_);
    algoLbl_.setText("Algo", juce::dontSendNotification);
    algoLbl_.setJustificationType(juce::Justification::centred);
    // Same UTF-8-correct names that build the algorithm choice param.
    algo_.addItemList(params::algoNames(), 1);
    vastDspSection_.addAndMakeVisible(algoLbl_);
    vastDspSection_.addAndMakeVisible(algo_);

    // Filter section
    addAndMakeVisible(filterSection_);
    filterSection_.addAndMakeVisible(filterCutoff_);
    filterSection_.addAndMakeVisible(filterRes_);
    spineModelLbl_.setText("Filter", juce::dontSendNotification);
    spineModelLbl_.setJustificationType(juce::Justification::centred);
    spineModel_.addItemList(params::algoNamesSpine(), 1);
    filterSection_.addAndMakeVisible(spineModelLbl_);
    filterSection_.addAndMakeVisible(spineModel_);
    spineSlopeLbl_.setText("Slope", juce::dontSendNotification);
    spineSlopeLbl_.setJustificationType(juce::Justification::centred);
    spineSlope_.addItemList(juce::StringArray{ "12 dB", "24 dB" }, 1);
    filterSection_.addAndMakeVisible(spineSlopeLbl_);
    filterSection_.addAndMakeVisible(spineSlope_);
    filterSection_.addAndMakeVisible(spineSeparation_);
    filterSection_.addAndMakeVisible(spinePostDrive_);
    spineRoutingLbl_.setText("Filter Routing", juce::dontSendNotification);
    spineRoutingLbl_.setJustificationType(juce::Justification::centred);
    spineRouting_.addItemList(juce::StringArray{ "LP", "BP", "HP", "Notch",
        util::u8("LP\xE2\x86\x92" "HP"), util::u8("LP\xE2\x86\x92" "BP"), util::u8("HP\xE2\x86\x92" "BP"),
        "LP+HP", "LP+BP", "HP+BP", "LP+LP", "BP+BP", "HP+HP" }, 1);
    filterSection_.addAndMakeVisible(spineRoutingLbl_);
    filterSection_.addAndMakeVisible(spineRouting_);

    // Moog-only controls (hidden until the Moog model is selected)
    moogModeLbl_.setText("Mode", juce::dontSendNotification);
    moogModeLbl_.setJustificationType(juce::Justification::centred);
    moogMode_.addItemList(juce::StringArray{ "LP", "BP", "HP" }, 1);
    filterSection_.addAndMakeVisible(moogModeLbl_);
    filterSection_.addAndMakeVisible(moogMode_);
    moogWaveLbl_.setText("Wave", juce::dontSendNotification);
    moogWaveLbl_.setJustificationType(juce::Justification::centred);
    moogWave_.addItemList(juce::StringArray{ "Sine", "Triangle", "Saw" }, 1);
    moogOctaveLbl_.setText("Octave", juce::dontSendNotification);
    moogOctaveLbl_.setJustificationType(juce::Justification::centred);
    moogOctave_.addItemList(juce::StringArray{ "0", "-1 oct", "-2 oct" }, 1);

    // Wire model-selection visibility switching
    spineModel_.onChange = [this] { updateModelVisibility(); resized(); };
    updateModelVisibility();

    // HP pre-filter band controls
    hpSectionLbl_.setText("HP PRE", juce::dontSendNotification);
    hpSectionLbl_.setJustificationType(juce::Justification::centredLeft);
    filterSection_.addAndMakeVisible(hpSectionLbl_);
    hpSlopeLbl_.setText("Slope", juce::dontSendNotification);
    hpSlopeLbl_.setJustificationType(juce::Justification::centred);
    hpSlope_.addItemList(juce::StringArray{ "12 dB", "24 dB" }, 1);
    filterSection_.addAndMakeVisible(hpSlopeLbl_);
    filterSection_.addAndMakeVisible(hpSlope_);
    filterSection_.addAndMakeVisible(hpCutoff_);
    filterSection_.addAndMakeVisible(hpReso_);

    // Reserved Filter-Env sub-frame inside the VCF panel (params don't exist yet).
    filterSection_.addAndMakeVisible(filterEnvSection_);

    // Amp env section
    addAndMakeVisible(ampEnvSection_);
    for (auto* k : { &ampA_, &ampD_, &ampS_, &ampR_ })
        ampEnvSection_.addAndMakeVisible(*k);

    // Reserved sections — visible (framed/dimmed) but no children yet.
    for (auto* s : { &vco1Section_, &vco2Section_, &vco3Section_,
                     &mixerSection_, &outputSection_,
                     &modEnvSection_, &lfoSection_, &modMatrixSection_, &fxSection_ })
        addAndMakeVisible(*s);
    addAndMakeVisible(ampSection_);

    // Amp section: hearing-safety output limiter (protected control — not an APVTS param).
    safetyLbl_.setText("Safety", juce::dontSendNotification);
    safetyLbl_.setJustificationType(juce::Justification::centred);
    ampSection_.addAndMakeVisible(safetyLbl_);

    safetyLimiter_.setButtonText("Limiter");
    safetyLimiter_.setToggleState(processorRef.isLimiterEnabled(), juce::dontSendNotification);
    safetyLimiter_.onClick = [this] {
        if (safetyLimiter_.getToggleState()) {           // turning ON — always allowed, no warning
            processorRef.setLimiterEnabled(true);
            return;
        }
        // JUCE 2-button result codes (juce_AlertWindow.h): button[0] -> 1, button[1] -> 0,
        // and a dismiss/escape also -> 0. So "Disable" is button[0] (the only code meaning
        // disable) and "Cancel" is button[1]; every non-Disable outcome (Cancel, escape,
        // close) returns 0 and keeps the limiter ON. Fail-safe.
        auto opts = juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::WarningIcon)
            .withTitle("Disable safety limiter?")
            .withMessage("Disabling the safety limiter allows dangerously loud output that "
                         "can damage hearing or equipment. Continue?")
            .withButton("Disable")   // button[0] -> result 1
            .withButton("Cancel");   // button[1] -> result 0 (also the dismiss code)
        juce::AlertWindow::showAsync(opts, [this](int result) {
            if (result == 1) processorRef.setLimiterEnabled(false);                 // explicit Disable
            else safetyLimiter_.setToggleState(true, juce::dontSendNotification);   // Cancel/escape/close -> stay ON
        });
    };
    ampSection_.addAndMakeVisible(safetyLimiter_);
    ampSection_.setReserved(false);   // Amp is now a live section (safety limiter), not a placeholder

    limitIndicator_.setText("LIMIT", juce::dontSendNotification);
    limitIndicator_.setJustificationType(juce::Justification::centred);
    limitIndicator_.setColour(juce::Label::textColourId, juce::Colours::darkgrey);
    ampSection_.addAndMakeVisible(limitIndicator_);

    // Footer routing strip — controls sit directly on the cream chassis plate.
    enableLbl_.setText("Enable", juce::dontSendNotification);
    enableLbl_.setJustificationType(juce::Justification::centred);
    enableLbl_.setColour(juce::Label::textColourId, VintageLookAndFeel::creamText);
    addAndMakeVisible(enableLbl_);
    addAndMakeVisible(enable_);
    for (auto* k : { &keyLo_, &keyHi_, &velLo_, &velHi_, &level_ }) {
        k->setCaptionColour(VintageLookAndFeel::creamText);
        addAndMakeVisible(*k);
    }
    channelLbl_.setText("Channel", juce::dontSendNotification);
    channelLbl_.setJustificationType(juce::Justification::centred);
    channelLbl_.setColour(juce::Label::textColourId, VintageLookAndFeel::creamText);
    juce::StringArray chanItems{ "Omni" };
    for (int ch = 1; ch <= 16; ++ch) chanItems.add(juce::String(ch));
    channel_.addItemList(chanItems, 1);
    addAndMakeVisible(channelLbl_);
    addAndMakeVisible(channel_);
}

// (Re)bind every per-layer control to the chosen layer's params. binder_'s
// detach-before-rebind contract guarantees the layer we leave is untouched.
void K2000AudioProcessorEditor::bindLayer(int layer) {
    const auto& ids = params::layerIds(layer);

    binder_.bind(algo_,                ids.algorithm);
    binder_.bind(shaperDrive_.slider(),ids.shaperDrive);
    binder_.bind(shaperMix_.slider(),  ids.shaperMix);

    binder_.bind(filterCutoff_.slider(),ids.filterCutoff);
    binder_.bind(filterRes_.slider(),  ids.filterResonance);

    binder_.bind(spineModel_,               ids.spineModel);
    binder_.bind(spineSlope_,               ids.spineSlope);
    binder_.bind(spineRouting_,             ids.spineHuggettRouting);
    binder_.bind(spineSeparation_.slider(), ids.spineSeparation);
    binder_.bind(moogMode_,                 ids.spineMoogMode);
    binder_.bind(moogWave_,                 ids.spineMoogBassWave);
    binder_.bind(moogOctave_,               ids.spineMoogBassOctave);
    binder_.bind(moogBass_.slider(),        ids.spineMoogBassAmount);

    binder_.bind(hpCutoff_.slider(),      ids.spineHpCutoff);
    binder_.bind(hpReso_.slider(),        ids.spineHpResonance);
    binder_.bind(hpSlope_,               ids.spineHpSlope);
    binder_.bind(spinePostDrive_.slider(), ids.spinePostDrive);

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
    g.fillAll(VintageLookAndFeel::windowBg);

    // Wood side rails, full height.
    VintageLookAndFeel::fillWood(g, { 0, 0, kRailW, getHeight() });
    VintageLookAndFeel::fillWood(g, { getWidth() - kRailW, 0, kRailW, getHeight() });

    // Cream header + footer plates between the rails.
    const juce::Rectangle<int> header(kRailW, 0, getWidth() - 2 * kRailW, kHeaderH);
    const juce::Rectangle<int> footer(kRailW, getHeight() - kFooterH,
                                      getWidth() - 2 * kRailW, kFooterH);
    VintageLookAndFeel::fillCream(g, header);
    VintageLookAndFeel::fillCream(g, footer);
    g.setColour(VintageLookAndFeel::panelEdge.withAlpha(0.6f));
    g.drawHorizontalLine(header.getBottom() - 1, (float) header.getX(), (float) header.getRight());
    g.drawHorizontalLine(footer.getY(), (float) footer.getX(), (float) footer.getRight());

    // Chassis screws at the plate corners.
    VintageLookAndFeel::drawScrew(g, (float) header.getX() + 18.0f,     (float) header.getY() + 18.0f, 5.0f);
    VintageLookAndFeel::drawScrew(g, (float) header.getRight() - 18.0f, (float) header.getY() + 18.0f, 5.0f);
    VintageLookAndFeel::drawScrew(g, (float) footer.getX() + 18.0f,     (float) footer.getBottom() - 18.0f, 5.0f);
    VintageLookAndFeel::drawScrew(g, (float) footer.getRight() - 18.0f, (float) footer.getBottom() - 18.0f, 5.0f);

    // Blank recessed VU plates (Stage 3 puts real meters here).
    VintageLookAndFeel::drawRecessedWell(g, vuWellRect(0));
    VintageLookAndFeel::drawRecessedWell(g, vuWellRect(1));

    // Footer strip label.
    g.setColour(VintageLookAndFeel::creamText);
    g.setFont(VintageLookAndFeel::condensedFont(15.0f));
    g.drawText("LAYER ROUTING", footer.reduced(20, 0), juce::Justification::centredLeft);
}

juce::Rectangle<float> K2000AudioProcessorEditor::vuWellRect(int index) const {
    const float w = 168.0f, h = 56.0f, gap = 18.0f;
    const float right = (float) getWidth() - kRailW - 340.0f;  // left of the layer/OS/output cluster
    const float x = right - (float) (2 - index) * (w + gap);
    return { x, ((float) kHeaderH - h) * 0.5f, w, h };
}

void K2000AudioProcessorEditor::timerCallback() {
    const bool active = processorRef.gainReductionDb() > 0.1f;
    limitIndicator_.setColour(juce::Label::textColourId,
                              active ? juce::Colours::red : juce::Colours::darkgrey);
    // keep the toggle in sync if state changed via load
    if (safetyLimiter_.getToggleState() != processorRef.isLimiterEnabled())
        safetyLimiter_.setToggleState(processorRef.isLimiterEnabled(), juce::dontSendNotification);
    limitIndicator_.repaint();
}

void K2000AudioProcessorEditor::showOversamplingMenu() {
    const int curRT  = processorRef.realtimeOS();
    const int curOFF = processorRef.offlineOS();

    juce::PopupMenu osFlat;
    osFlat.addSectionHeader("Realtime oversampling");
    osFlat.addItem(101, "Off", true, curRT == 1);
    osFlat.addItem(102, "2x",  true, curRT == 2);
    osFlat.addItem(103, "4x",  true, curRT == 4);
    osFlat.addItem(104, "8x",  true, curRT == 8);
    osFlat.addSectionHeader("Offline oversampling");
    osFlat.addItem(201, "Same as Realtime", true, curOFF == 0);
    osFlat.addItem(202, "2x", true, curOFF == 2);
    osFlat.addItem(203, "4x", true, curOFF == 4);
    osFlat.addItem(204, "8x", true, curOFF == 8);

    juce::PopupMenu root;
    root.addSubMenu("Oversampling: " + juce::String(curRT == 1 ? "Off" : juce::String(curRT) + "x"),
                    osFlat);

    root.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(menuButton_),
        [this](int r) {
            switch (r) {
                case 101: processorRef.setRealtimeOS(1); break;
                case 102: processorRef.setRealtimeOS(2); break;
                case 103: processorRef.setRealtimeOS(4); break;
                case 104: processorRef.setRealtimeOS(8); break;
                case 201: processorRef.setOfflineOS(0);  break;
                case 202: processorRef.setOfflineOS(2);  break;
                case 203: processorRef.setOfflineOS(4);  break;
                case 204: processorRef.setOfflineOS(8);  break;
                default: break;
            }
        });
}

void K2000AudioProcessorEditor::updateModelVisibility() {
    const bool moog = (spineModel_.getSelectedItemIndex() == 1);
    // Moog-only controls
    juce::Component* moogControls[] = { &moogModeLbl_, &moogMode_ };
    for (auto* c : moogControls)
        c->setVisible(moog);
    // Huggett-only controls
    juce::Component* huggettControls[] = { &spineRoutingLbl_, &spineRouting_,
                                           &spineSeparation_,  &spinePostDrive_ };
    for (auto* c : huggettControls)
        c->setVisible(!moog);
}

void K2000AudioProcessorEditor::resized() {
    // --- Header (cream plate): version | ... VU wells (paint-only) ... | layer | OS | OUTPUT ---
    {
        auto bar = juce::Rectangle<int>(kRailW, 0, getWidth() - 2 * kRailW, kHeaderH).reduced(14, 10);
        title_.setBounds(bar.removeFromLeft(170));
        auto outZone = bar.removeFromRight(84);
        masterGainLbl_.setBounds(outZone.removeFromTop(14));
        masterGain_.setBounds(outZone);
        bar.removeFromRight(10);
        menuButton_.setBounds(bar.removeFromRight(34).reduced(0, 18));
        bar.removeFromRight(8);
        editLayerCombo_.setBounds(bar.removeFromRight(100).withSizeKeepingCentre(100, 26));
        editLayerLabel_.setBounds(bar.removeFromRight(70).withSizeKeepingCentre(70, 26));
    }

    // Helper: lay a row of {label,control} cells across a rectangle.
    auto layoutCells = [](juce::Rectangle<int> r,
                          std::initializer_list<std::pair<juce::Component*, juce::Component*>> cells) {
        constexpr int kComboH = 26;   // combos render at a fixed height (see below)
        const int n = (int) cells.size();
        if (n == 0) return;
        const int w = r.getWidth() / n;
        int x = r.getX();
        for (auto& [lbl, ctl] : cells) {
            if (lbl) lbl->setBounds(x, r.getY(), w, 16);
            const int topY  = r.getY() + (lbl ? 16 : 0);
            const int cellH = r.getHeight() - (lbl ? 16 : 0);
            if (dynamic_cast<juce::ComboBox*>(ctl) != nullptr)
                // A combo must NOT fill the cell: JUCE sets the popup's item
                // height from the box height, so a tall box yields a
                // screen-filling dropdown. Pin it to a normal height, top-aligned.
                ctl->setBounds(x + 4, topY, w - 8, juce::jmin(cellH, kComboH));
            else
                ctl->setBounds(x, topY, w, cellH);
            x += w;
        }
    };

    // --- Main content between header and footer, inside the rails ---
    auto content = getLocalBounds()
                       .withTrimmedLeft(kRailW).withTrimmedRight(kRailW)
                       .withTrimmedTop(kHeaderH).withTrimmedBottom(kFooterH)
                       .reduced(10);

    // Rows claimed from the bottom: mod row, gap, bottom control row, gap.
    auto modRow = content.removeFromBottom(100);
    content.removeFromBottom(8);
    auto botRow = content.removeFromBottom(150);
    content.removeFromBottom(8);

    // Main columns: VCO stack (left) | VCF | slim OUTPUT column (right).
    auto vcoCol = content.removeFromLeft((int) ((float) content.getWidth() * 0.52f));
    content.removeFromLeft(8);
    auto outCol = content.removeFromRight(130);
    content.removeFromRight(8);
    filterSection_.setBounds(content);
    outputSection_.setBounds(outCol);

    // Three equal VCO panels, empty until Stage 2.
    const int vcoH = (vcoCol.getHeight() - 16) / 3;
    vco1Section_.setBounds(vcoCol.removeFromTop(vcoH));
    vcoCol.removeFromTop(8);
    vco2Section_.setBounds(vcoCol.removeFromTop(vcoH));
    vcoCol.removeFromTop(8);
    vco3Section_.setBounds(vcoCol);

    // --- VCF panel internals: reserved Filter-Env frame at the bottom, then the
    //     existing three-row filter layout (HP band + shared row + model row). ---
    {
        auto fc = filterSection_.contentBounds();
        auto envArea = fc.removeFromBottom((int) ((float) fc.getHeight() * 0.26f));
        filterEnvSection_.setBounds(envArea.reduced(2));
        fc.removeFromBottom(6);

        const int rowH   = fc.getHeight() / 3;
        const int divGap = 4;

        // HP pre-filter row. No enable toggle — the HP is OFF when its cutoff
        // knob sits at 0; turning it up engages it.
        auto hpRow = fc.removeFromTop(rowH);
        fc.removeFromTop(divGap);
        const int lblW = 58;
        hpSectionLbl_.setBounds(hpRow.getX(), hpRow.getY(), lblW, 16);
        hpRow.removeFromLeft(lblW);
        layoutCells(hpRow, { { nullptr,      &hpCutoff_ },
                              { nullptr,      &hpReso_   },
                              { &hpSlopeLbl_, &hpSlope_  } });

        // Shared main row: model, cutoff, reso, slope (slope applies to both models).
        const int mainH = fc.getHeight() / 2;
        auto mainTop = fc.removeFromTop(mainH);
        layoutCells(mainTop, { { &spineModelLbl_, &spineModel_ },
                                { nullptr,         &filterCutoff_ },
                                { nullptr,         &filterRes_ },
                                { &spineSlopeLbl_, &spineSlope_ } });
        // Model-specific rows share the same rectangle; updateModelVisibility()
        // shows only the active group.
        layoutCells(fc,  { { &spineRoutingLbl_, &spineRouting_ },
                            { nullptr,          &spineSeparation_ },
                            { nullptr,          &spinePostDrive_ } });
        layoutCells(fc,  { { &moogModeLbl_, &moogMode_ } });
        updateModelVisibility();
    }

    // --- Bottom control row: OSC BLEND | VAST DSP | AMP ENV | AMP ---
    {
        auto b = botRow;
        const int bw = b.getWidth();
        mixerSection_.setBounds(b.removeFromLeft((int) ((float) bw * 0.30f)).reduced(2));
        vastDspSection_.setBounds(b.removeFromLeft((int) ((float) bw * 0.20f)).reduced(2));
        ampEnvSection_.setBounds(b.removeFromLeft((int) ((float) bw * 0.30f)).reduced(2));
        ampSection_.setBounds(b.reduced(2));

        layoutCells(vastDspSection_.contentBounds(), { { &algoLbl_, &algo_ } });
        layoutCells(ampEnvSection_.contentBounds(),
                    { { nullptr, &ampA_ }, { nullptr, &ampD_ }, { nullptr, &ampS_ }, { nullptr, &ampR_ } });
        auto ac = ampSection_.contentBounds();
        safetyLbl_.setBounds(ac.removeFromTop(16));
        safetyLimiter_.setBounds(ac.removeFromTop(28).reduced(4, 2));
        limitIndicator_.setBounds(ac.removeFromTop(20));
    }

    // --- Mod row: MOD ENVS | LFO 1-4 | MOD MATRIX | FX CHAINS (all reserved) ---
    {
        const int w = modRow.getWidth() / 4;
        modEnvSection_.setBounds(modRow.removeFromLeft(w).reduced(2));
        lfoSection_.setBounds(modRow.removeFromLeft(w).reduced(2));
        modMatrixSection_.setBounds(modRow.removeFromLeft(w).reduced(2));
        fxSection_.setBounds(modRow.reduced(2));
    }

    // --- Footer (cream plate): LAYER ROUTING label (painted) | controls ---
    {
        auto f = juce::Rectangle<int>(kRailW, getHeight() - kFooterH,
                                      getWidth() - 2 * kRailW, kFooterH).reduced(14, 8);
        f.removeFromLeft(150);   // painted "LAYER ROUTING" text zone
        auto en = f.removeFromLeft(60);
        enableLbl_.setBounds(en.removeFromTop(14));
        enable_.setBounds(en.getCentreX() - 12, en.getY() + 2, 24, 24);
        f.removeFromLeft(8);
        for (auto* k : { &keyLo_, &keyHi_, &velLo_, &velHi_, &level_ }) {
            k->setBounds(f.removeFromLeft(88));
            f.removeFromLeft(6);
        }
        auto ch = f.removeFromRight(110);
        channelLbl_.setBounds(ch.removeFromTop(14));
        channel_.setBounds(ch.removeFromTop(26));
    }
}
