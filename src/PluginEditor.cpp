#include "PluginEditor.h"
#include "params/Parameters.h"
#include "util/Utf8.h"

K2000AudioProcessorEditor::K2000AudioProcessorEditor(K2000AudioProcessor& p)
    : juce::AudioProcessorEditor(&p), processorRef(p) {
    setLookAndFeel(&lnf_);
    buildStaticControls();

    // master gain is not per-layer — bind once.
    binder_.bind(masterGain_, params::masterGain);

    bindLayer(0);
    startTimerHz(24);
    setSize(1040, 740);
}

K2000AudioProcessorEditor::~K2000AudioProcessorEditor() {
    stopTimer();
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
    masterGainLbl_.setText("Gain", juce::dontSendNotification);
    masterGainLbl_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(masterGainLbl_);
    masterGain_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 52, 22);
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
    // Same UTF-8-correct names that build the algorithm choice param.
    algo_.addItemList(params::algoNames(), 1);
    addToSource(algoLbl_); addToSource(algo_);
    addToSource(shaperDrive_); addToSource(shaperMix_);

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
    filterSection_.addAndMakeVisible(moogWaveLbl_);
    filterSection_.addAndMakeVisible(moogWave_);
    moogOctaveLbl_.setText("Octave", juce::dontSendNotification);
    moogOctaveLbl_.setJustificationType(juce::Justification::centred);
    moogOctave_.addItemList(juce::StringArray{ "0", "-1 oct", "-2 oct" }, 1);
    filterSection_.addAndMakeVisible(moogOctaveLbl_);
    filterSection_.addAndMakeVisible(moogOctave_);
    filterSection_.addAndMakeVisible(moogBass_);

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

    // Amp env section
    addAndMakeVisible(ampEnvSection_);
    for (auto* k : { &ampA_, &ampD_, &ampS_, &ampR_ })
        ampEnvSection_.addAndMakeVisible(*k);

    // Reserved sections — visible (framed/dimmed) but no children.
    for (auto* s : { &mixerSection_, &driveSection_, &ampSection_,
                     &modEnvSection_, &lfoSection_, &modMatrixSection_, &fxSection_ })
        addAndMakeVisible(*s);

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
    g.fillAll(SummitLookAndFeel::panelBg);
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

void K2000AudioProcessorEditor::updateModelVisibility() {
    const bool moog = (spineModel_.getSelectedItemIndex() == 1);
    // Moog-only controls
    juce::Component* moogControls[] = { &moogModeLbl_,  &moogMode_,
                                        &moogWaveLbl_,  &moogWave_,
                                        &moogOctaveLbl_, &moogOctave_,
                                        &moogBass_ };
    for (auto* c : moogControls)
        c->setVisible(moog);
    // Huggett-only controls
    juce::Component* huggettControls[] = { &spineRoutingLbl_, &spineRouting_,
                                           &spineSeparation_,  &spinePostDrive_ };
    for (auto* c : huggettControls)
        c->setVisible(!moog);
}

void K2000AudioProcessorEditor::resized() {
    auto area = getLocalBounds().reduced(10);

    // Top bar (h 40): title | spacer | edit-layer | master gain
    {
        auto bar = area.removeFromTop(40);
        title_.setBounds(bar.removeFromLeft(180));
        masterGain_.setBounds(bar.removeFromRight(150).reduced(4, 8));
        masterGainLbl_.setBounds(bar.removeFromRight(40).reduced(0, 8));
        editLayerCombo_.setBounds(bar.removeFromRight(110).reduced(0, 8));
        editLayerLabel_.setBounds(bar.removeFromRight(90).reduced(0, 8));
    }
    area.removeFromTop(8);

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

    // Signal row (h 330): source(48%) | mixer | filter | drive | amp
    {
        auto row = area.removeFromTop(330);
        auto source = row.removeFromLeft((int) (row.getWidth() * 0.40f));
        sourceSection_.setBounds(source.reduced(2));
        // FILTER is now a primary 6-control section, so it gets the lion's share
        // of the remaining width; the still-empty reserved placeholders are slim.
        const int rest = row.getWidth();
        mixerSection_.setBounds(row.removeFromLeft((int) (rest * 0.09f)).reduced(2));
        filterSection_.setBounds(row.removeFromLeft((int) (rest * 0.55f)).reduced(2));
        driveSection_.setBounds(row.removeFromLeft((int) (rest * 0.09f)).reduced(2));
        ampSection_.setBounds(row.reduced(2));
        {
            auto ac = ampSection_.contentBounds();
            safetyLbl_.setBounds(ac.removeFromTop(16));
            safetyLimiter_.setBounds(ac.removeFromTop(28).reduced(4, 2));
            limitIndicator_.setBounds(ac.removeFromTop(20));
        }

        // Source children: two stacked cell-rows inside contentBounds.
        auto sc = sourceSection_.contentBounds();
        auto top = sc.removeFromTop(sc.getHeight() / 2);
        layoutCells(top, { { &oscWaveLbl_, &oscWave_ }, { nullptr, &oscCoarse_ }, { nullptr, &oscFine_ } });
        layoutCells(sc,  { { &algoLbl_, &algo_ }, { nullptr, &shaperDrive_ }, { nullptr, &shaperMix_ } });

        // Filter children: Layout B — HP pre-band + divider + two main rows.
        // Row 1 (HP band): HP PRE label+enable, HP cutoff, HP reso, HP slope, HP drive.
        // [4 px divider gap]
        // Row 2 (main top): filter type, cutoff, reso.
        // Row 3 (main bot): spine model, slope, separation, post-drive.
        {
            auto fc = filterSection_.contentBounds();
            const int rowH   = fc.getHeight() / 3;
            const int divGap = 4;

            // HP pre-filter row
            auto hpRow = fc.removeFromTop(rowH);
            fc.removeFromTop(divGap);  // visual divider gap
            // HP section label (left column). No enable toggle — the HP is OFF when its
            // cutoff knob sits at 0; turning it up engages it.
            const int lblW = 58;
            hpSectionLbl_.setBounds(hpRow.getX(), hpRow.getY(), lblW, 16);
            hpRow.removeFromLeft(lblW);
            // Remaining cells: HP cut, HP reso, HP slope (HP is clean — no drive)
            layoutCells(hpRow, { { nullptr,      &hpCutoff_ },
                                  { nullptr,      &hpReso_   },
                                  { &hpSlopeLbl_, &hpSlope_  } });

            // Main filter rows — split remaining height equally.
            // The model selector ("Filter") lives in the main top row so the dropdown is
            // wide enough for both model names (Huggett/Moog); it is always visible.
            const int mainH = (fc.getHeight()) / 2;
            auto mainTop = fc.removeFromTop(mainH);
            layoutCells(mainTop, { { &spineModelLbl_, &spineModel_ },
                                    { nullptr,         &filterCutoff_ },
                                    { nullptr,         &filterRes_ } });
            // Both model-specific rows share the same cell rectangle;
            // updateModelVisibility() ensures only the active group is shown.
            layoutCells(fc,  { { &spineRoutingLbl_, &spineRouting_ },
                                { &spineSlopeLbl_,  &spineSlope_ },
                                { nullptr,          &spineSeparation_ },
                                { nullptr,          &spinePostDrive_ } });
            layoutCells(fc,  { { &moogModeLbl_,    &moogMode_ },
                                { &moogWaveLbl_,    &moogWave_ },
                                { &moogOctaveLbl_,  &moogOctave_ },
                                { nullptr,          &moogBass_ } });
            updateModelVisibility();
        }
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
