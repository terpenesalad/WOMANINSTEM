#include "RigPanel.h"
#include "Studio/PluginEditors.h"

namespace wis
{

// ---- Knob -------------------------------------------------------------------------------------------

Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& text)
    : attachment (state, paramId, slider)
{
    label.setText (text, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (uiFont (11.5f, true));
    label.setColour (juce::Label::textColourId, theme::textDim);
    label.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (label);

    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 16);
    slider.setMouseDragSensitivity (180);
    slider.setVelocityBasedMode (false);
    if (auto* p = state.getParameter (paramId))
    {
        slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
        slider.setTooltip (p->getName (64) + " (double-click to reset)");
    }
    addAndMakeVisible (slider);
}

void Knob::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (15));
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, r.getWidth(), 15);
    slider.setBounds (r);
}

// ---- EffectModule -------------------------------------------------------------------------------------

EffectModule::EffectModule (juce::AudioProcessorValueTreeState& s, const juce::String& t, const char* powerParam, juce::Colour c)
    : accent (c), state (s), title (t)
{
    if (powerParam != nullptr)
    {
        power.onColour = c;
        power.setTooltip ("Turn " + t.toLowerCase() + " on/off");
        powerAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, powerParam, power);
        addAndMakeVisible (power);
    }
}

Knob& EffectModule::addKnob (const char* paramId, const juce::String& label)
{
    auto* k = knobs.add (new Knob (state, paramId, label));
    k->slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    addAndMakeVisible (k);
    return *k;
}

juce::ComboBox& EffectModule::addChoice (const char* paramId)
{
    choice = std::make_unique<juce::ComboBox>();
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramId)))
        choice->addItemList (p->choices, 1);
    choiceAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, paramId, *choice);
    addAndMakeVisible (*choice);
    return *choice;
}

void EffectModule::addExtra (juce::Component& c, int width)
{
    extras.add (&c);
    extraWidths.add (width);
    addAndMakeVisible (c);
}

void EffectModule::addSide (juce::Component& c, int width)
{
    sides.add (&c);
    sideWidths.add (width);
    addAndMakeVisible (c);
}

void EffectModule::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (3.0f);
    const bool on = powerAttachment == nullptr || power.getToggleState();

    g.setColour (theme::panelRaised);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (on ? accent.withAlpha (0.55f) : theme::outline);
    g.drawRoundedRectangle (r, 8.0f, 1.0f);

    // top accent strip
    g.setColour (on ? accent : theme::ledOff);
    g.fillRoundedRectangle (r.withHeight (3.0f).reduced (10.0f, 0.0f), 1.5f);

    if (flash > 0.0f)
    {
        g.setColour (accent.withAlpha (flash));
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 2.5f);
    }

    g.setColour (on ? theme::text : theme::textDim);
    g.setFont (uiFont (12.5f, true));
    g.drawText (title.toUpperCase(), r.reduced (10.0f, 0.0f).withHeight (26.0f).withTrimmedLeft (powerAttachment != nullptr ? 22.0f : 0.0f),
                juce::Justification::centredLeft);
}

void EffectModule::resized()
{
    auto r = getLocalBounds().reduced (10, 6);
    auto header = r.removeFromTop (22);
    if (powerAttachment != nullptr)
        power.setBounds (header.removeFromLeft (18).withSizeKeepingCentre (18, 18));

    const bool hasRow = choice != nullptr || ! extras.isEmpty();
    if (hasRow)
    {
        auto row = r.removeFromTop (24);
        int fixed = 0;
        for (auto w : extraWidths) fixed += w + 4;
        if (choice != nullptr)
        {
            choice->setBounds (row.removeFromLeft (juce::jmax (60, row.getWidth() - fixed)));
            row.removeFromLeft (4);
        }
        for (int i = 0; i < extras.size(); ++i)
        {
            const int w = extraWidths[i] > 0 ? extraWidths[i] : row.getWidth();
            extras[i]->setBounds (row.removeFromLeft (w));
            row.removeFromLeft (4);
        }
        r.removeFromTop (4);
    }

    int sideFixed = 0;
    for (auto w : sideWidths) sideFixed += juce::jmax (0, w) + 6;
    bool sideFills = false;
    for (auto w : sideWidths) sideFills = sideFills || w <= 0;

    const int knobArea = sideFills ? juce::jmin (r.getWidth(), knobs.size() * 64) : r.getWidth() - sideFixed;
    auto knobRow = r.removeFromLeft (knobArea);
    if (! knobs.isEmpty())
    {
        const int kw = knobRow.getWidth() / knobs.size();
        for (auto* k : knobs)
            k->setBounds (knobRow.removeFromLeft (kw).reduced (1, 0));
    }

    for (int i = 0; i < sides.size(); ++i)
    {
        r.removeFromLeft (6);
        const int w = sideWidths[i] > 0 ? sideWidths[i] : r.getWidth();
        sides[i]->setBounds (r.removeFromLeft (w));
    }
}

// ---- RigPanel ------------------------------------------------------------------------------------------

RigPanel::RigPanel (RigProcessor& r, AudioEngine* e)
    : rig (r), engine (e), state (r.getState())
{
    title.setText ("YOUR RIG", juce::dontSendNotification);
    title.setFont (uiFont (15.0f, true));
    title.setColour (juce::Label::textColourId, theme::text);
    addAndMakeVisible (title);

    presetBox.setTextWhenNothingSelected ("Choose a preset...");
    presetBox.onChange = [this] { choosePreset (presetBox.getSelectedId()); };
    addAndMakeVisible (presetBox);
    savePreset.setTooltip ("Save the current rig as your own preset");
    savePreset.onClick = [this] { saveUserPreset(); };
    addAndMakeVisible (savePreset);

    // ---- input ----
    inputModule = std::make_unique<EffectModule> (state, "Input", nullptr, theme::accent);
    inputBox.setTooltip ("Which input on your audio interface your instrument or mic is plugged into");
    inputBox.onChange = [this]
    {
        const int id = inputBox.getSelectedId();
        if (engine != nullptr) engine->selectedInput = id == 900 ? AudioEngine::stereoSum : id == 901 ? AudioEngine::noInput : id - 1;
    };
    inputModule->addExtra (inputBox, 0);
    inputModule->addKnob (pid::inputGain, "Gain");
    monitor.setClickingTogglesState (true);
    monitor.setToggleState (true, juce::dontSendNotification);
    monitor.setTooltip ("Hear your instrument through the rig");
    monitor.onClick = [this] { if (engine != nullptr) engine->monitorOn = monitor.getToggleState(); };
    clipLed.setText ("CLIP", juce::dontSendNotification);
    clipLed.setFont (uiFont (10.0f, true));
    clipLed.setJustificationType (juce::Justification::centred);
    clipLed.setColour (juce::Label::textColourId, theme::textFaint);
    clipLed.setTooltip ("Lights red if your interface input is clipping - turn the gain down on the interface");
    inputSide.add (monitor, 26);
    inputSide.add (clipLed, 16);
    inputModule->addSide (inputMeter, 10);
    inputModule->addSide (inputSide, 76);

    // ---- tuner ----
    tunerModule = std::make_unique<EffectModule> (state, "Tuner", nullptr, theme::good);
    tunerMute.setClickingTogglesState (true);
    tunerMute.setTooltip ("Silence the rig while you tune");
    tunerMute.setColour (juce::TextButton::buttonOnColourId, theme::warn.darker (0.1f));
    tunerMute.onClick = [this] { rig.tunerMute = tunerMute.getToggleState(); };
    tunerModule->addExtra (tunerMute, 0);
    tunerModule->addSide (tunerView, 0);

    // ---- strings & pickups ----
    charM = std::make_unique<EffectModule> (state, "Strings & Pickups", nullptr, juce::Colour (0xffeab308));
    charM->addChoice (pid::charType).setTooltip ("Make your instrument sound like another: flatwounds, a 60s violin bass, a foam mute, "
                                                  "fresh roundwounds, a split-coil bass pickup, single coils or humbuckers");
    charM->addKnob (pid::charAmount, "Amount");

    // ---- pedals ----
    gateM = std::make_unique<EffectModule> (state, "Gate", pid::gateOn, juce::Colour (0xff64748b));
    gateM->addKnob (pid::gateThresh, "Thresh");
    gateM->addKnob (pid::gateRelease, "Release");

    compM = std::make_unique<EffectModule> (state, "Compressor", pid::compOn, juce::Colour (0xff38bdf8));
    compM->addKnob (pid::compThresh, "Thresh");
    compM->addKnob (pid::compRatio, "Ratio");
    compM->addKnob (pid::compAttack, "Attack");
    compM->addKnob (pid::compRelease, "Release");
    compM->addKnob (pid::compLevel, "Level");

    driveM = std::make_unique<EffectModule> (state, "Drive", pid::driveOn, juce::Colour (0xff22c55e));
    driveM->addChoice (pid::driveType);
    driveM->addKnob (pid::driveAmount, "Drive");
    driveM->addKnob (pid::driveTone, "Tone");
    driveM->addKnob (pid::driveLevel, "Level");

    ampM = std::make_unique<EffectModule> (state, "Amp", pid::ampOn, theme::accent);
    ampM->addChoice (pid::ampModel);
    loadNam.setTooltip ("Load a Neural Amp Modeler capture (.nam) - thousands are free at tone3000.com");
    loadNam.onClick = [this] { chooseNamFile(); };
    ampM->addExtra (loadNam, 92);
    namName.setFont (uiFont (11.0f));
    namName.setColour (juce::Label::textColourId, theme::textDim);
    ampM->addExtra (namName, 120);
    ampM->addKnob (pid::ampGain, "Gain");
    ampM->addKnob (pid::ampBass, "Bass");
    ampM->addKnob (pid::ampMid, "Mid");
    ampM->addKnob (pid::ampTreble, "Treble");
    ampM->addKnob (pid::ampPresence, "Presence");
    ampM->addKnob (pid::ampMaster, "Master");

    cabM = std::make_unique<EffectModule> (state, "Cabinet", pid::cabOn, juce::Colour (0xfff97316));
    cabM->addChoice (pid::cabType);
    loadIr.setTooltip ("Load a speaker cabinet impulse response (.wav)");
    loadIr.onClick = [this] { chooseIrFile(); };
    cabM->addExtra (loadIr, 76);
    if (auto* mp = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (pid::cabMic)))
        micBox.addItemList (mp->choices, 1);
    micAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, pid::cabMic, micBox);
    micBox.setTooltip ("Microphone on the speaker: dynamic (punchy), ribbon (warm, smooth), condenser (detailed) or a dynamic + ribbon blend");
    cabM->addExtra (micBox, 128);
    cabM->addKnob (pid::cabMicPos, "Mic Pos").slider.setTooltip ("Where the mic points: 0 = centre of the cone (bright), 10 = edge (dark, round)");
    cabM->addKnob (pid::cabRoom, "Room").slider.setTooltip ("Distance from the cab: close and punchy, or a bit of studio room");
    cabM->addKnob (pid::cabDiBlend, "DI Blend").slider.setTooltip ("Blend in a clean DI under the miked amp (time-aligned), the studio way to record bass");
    cabM->addKnob (pid::cabLowCut, "Low Cut");
    cabM->addKnob (pid::cabHighCut, "High Cut");
    irName.setFont (uiFont (11.0f));
    irName.setColour (juce::Label::textColourId, theme::textDim);
    irName.setJustificationType (juce::Justification::centredLeft);

    eqM = std::make_unique<EffectModule> (state, "Studio EQ", pid::eqOn, juce::Colour (0xffa3e635));
    eqM->addKnob (pid::eqLow, "Low");
    eqM->addKnob (pid::eqLowMid, "Lo Mid");
    eqM->addKnob (pid::eqHighMid, "Hi Mid");
    eqM->addKnob (pid::eqHigh, "High");

    tapeM = std::make_unique<EffectModule> (state, "Tape", pid::tapeOn, juce::Colour (0xffd97706));
    tapeM->addKnob (pid::tapeDrive, "Drive").slider.setTooltip ("A 60s studio console and tape machine: warmth, gentle compression, low-end bump, soft top");

    chorusM = std::make_unique<EffectModule> (state, "Chorus", pid::chorusOn, juce::Colour (0xff60a5fa));
    chorusM->addKnob (pid::chorusRate, "Rate");
    chorusM->addKnob (pid::chorusDepth, "Depth");
    chorusM->addKnob (pid::chorusMix, "Mix");

    delayM = std::make_unique<EffectModule> (state, "Delay", pid::delayOn, juce::Colour (0xffc084fc));
    pingPongAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, pid::delayPingPong, pingPong);
    delayM->addExtra (pingPong, 0);
    delayM->addKnob (pid::delayTime, "Time");
    delayM->addKnob (pid::delayFeedback, "Repeats");
    delayM->addKnob (pid::delayTone, "Tone");
    delayM->addKnob (pid::delayMix, "Mix");

    reverbM = std::make_unique<EffectModule> (state, "Reverb", pid::reverbOn, juce::Colour (0xff2dd4bf));
    reverbM->addKnob (pid::reverbSize, "Size");
    reverbM->addKnob (pid::reverbDamp, "Damp");
    reverbM->addKnob (pid::reverbPreDelay, "Pre-Dly");
    reverbM->addKnob (pid::reverbMix, "Mix");

    outputM = std::make_unique<EffectModule> (state, "Output", nullptr, theme::accent2);
    outputM->addKnob (pid::outLevel, "Level");
    outputM->addSide (outputMeter, 10);

    for (auto* m : { inputModule.get(), tunerModule.get(), charM.get(), gateM.get(), compM.get(), driveM.get(), ampM.get(), cabM.get(),
                     eqM.get(), tapeM.get(), chorusM.get(), delayM.get(), reverbM.get(), outputM.get() })
        addAndMakeVisible (m);

    refreshPresetList();
    refreshInputs();
    updateFileLabels();

    // ---- pedalboard ----
    board.onOpenPedal = [this] (const juce::String& uid) { openPedal (uid); };
    board.onShowBlock = [this] (const juce::String& key) { showBlock (key); };
    addAndMakeVisible (board);
    rig.addBoardListener (this);

    startTimerHz (30);
}

RigPanel::~RigPanel()
{
    stopTimer();
    rig.removeBoardListener (this);
    pedalWindows.clear();
}

void RigPanel::refreshInputs()
{
    if (engine == nullptr)
    {
        inputBox.clear (juce::dontSendNotification);
        inputBox.addItem ("From the track's input", 1);
        inputBox.setSelectedId (1, juce::dontSendNotification);
        inputBox.setEnabled (false);
        monitor.setVisible (false);
        return;
    }
    const int previous = engine->selectedInput.load();
    inputBox.clear (juce::dontSendNotification);

    auto names = engine->getActiveInputNames();
    auto chans = engine->getActiveInputChannels();
    for (int i = 0; i < chans.size(); ++i)
        inputBox.addItem (names[i], chans[i] + 1);
    if (chans.size() >= 2)
        inputBox.addItem ("Inputs 1 + 2 (summed)", 900);
    inputBox.addItem ("No input", 901);

    int sel = previous == AudioEngine::stereoSum ? 900 : previous == AudioEngine::noInput ? 901 : previous + 1;
    if (inputBox.indexOfItemId (sel) < 0)
        sel = chans.isEmpty() ? 901 : chans[0] + 1;
    inputBox.setSelectedId (sel, juce::sendNotificationSync);

    if (chans.isEmpty())
        inputBox.setTooltip ("No inputs are enabled - open Audio Settings and turn on your interface's inputs");
}

void RigPanel::refreshPresetList()
{
    presetBox.clear (juce::dontSendNotification);
    presetBox.addSectionHeading ("Factory");
    auto names = RigProcessor::factoryPresetNames();
    for (int i = 0; i < names.size(); ++i)
        presetBox.addItem (names[i], i + 1);

    userPresetFiles.clear();
    for (auto& entry : juce::RangedDirectoryIterator (RigProcessor::userPresetDirectory(), false, "*.wisrig"))
        userPresetFiles.add (entry.getFile());
    std::sort (userPresetFiles.begin(), userPresetFiles.end(),
               [] (const juce::File& a, const juce::File& b) { return a.getFileName().compareIgnoreCase (b.getFileName()) < 0; });

    if (! userPresetFiles.isEmpty())
    {
        presetBox.addSeparator();
        presetBox.addSectionHeading ("Your presets");
        for (int i = 0; i < userPresetFiles.size(); ++i)
            presetBox.addItem (userPresetFiles[i].getFileNameWithoutExtension(), 1001 + i);
    }
    presetBox.addSeparator();
    presetBox.addItem ("Open presets folder...", 9998);
    showCurrentPresetName();
}

void RigPanel::showCurrentPresetName()
{
    const auto name = rig.getPresetName();
    for (int i = 0; i < presetBox.getNumItems(); ++i)
        if (presetBox.getItemText (i) == name)
        {
            presetBox.setSelectedItemIndex (i, juce::dontSendNotification);
            return;
        }
    if (name.isNotEmpty())
        presetBox.setText (name, juce::dontSendNotification);
}

void RigPanel::choosePreset (int id)
{
    if (id <= 0) return;

    if (id == 9998)
    {
        RigProcessor::userPresetDirectory().startAsProcess();
        presetBox.setSelectedId (0, juce::dontSendNotification);
        return;
    }

    if (id <= 1000)
    {
        rig.loadFactoryPreset (id - 1);
        if (onStatus) onStatus ("Loaded preset: " + presetBox.getText());
    }
    else if (auto idx = id - 1001; juce::isPositiveAndBelow (idx, userPresetFiles.size()))
    {
        auto err = rig.loadUserPreset (userPresetFiles[idx]);
        if (onStatus) onStatus (err.isEmpty() ? "Loaded preset: " + presetBox.getText() : err);
    }
    updateFileLabels();
    repaint();
}

void RigPanel::saveUserPreset()
{
    auto* w = new juce::AlertWindow ("Save preset", "Name your rig preset:", juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", presetBox.getSelectedId() > 1000 ? presetBox.getText() : juce::String ("My Rig"));
    w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<RigPanel> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int result)
    {
        if (safe == nullptr || result != 1) return;
        const auto name = w->getTextEditorContents ("name");
        auto err = safe->rig.saveUserPreset (name);
        safe->refreshPresetList();
        for (int i = 0; i < safe->userPresetFiles.size(); ++i)
            if (safe->userPresetFiles[i].getFileNameWithoutExtension() == juce::File::createLegalFileName (name.trim()))
                safe->presetBox.setSelectedId (1001 + i, juce::dontSendNotification);
        if (safe->onStatus) safe->onStatus (err.isEmpty() ? "Saved preset \"" + name + "\"" : err);
    }), true);
}

void RigPanel::chooseNamFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Load a NAM amp capture", juce::File(), "*.nam");
    juce::Component::SafePointer<RigPanel> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe] (const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (safe == nullptr || ! file.existsAsFile()) return;

        if (safe->onStatus) safe->onStatus ("Loading " + file.getFileName() + "...");
        auto* rigPtr = &safe->rig;
        juce::Thread::launch ([safe, rigPtr, file]
        {
            auto err = rigPtr->getNam().load (file);   // heavy part off the message thread
            juce::MessageManager::callAsync ([safe, rigPtr, err, file]
            {
                if (safe == nullptr) return;
                if (err.isEmpty())
                    rigPtr->setParam (pid::ampModel, (float) (int) AmpType::namCapture);
                safe->updateFileLabels();
                if (safe->onStatus)
                    safe->onStatus (err.isEmpty() ? "Amp capture loaded: " + file.getFileNameWithoutExtension() : err);
            });
        });
    });
}

void RigPanel::chooseIrFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Load a cabinet impulse response", juce::File(), "*.wav;*.aif;*.aiff;*.flac");
    juce::Component::SafePointer<RigPanel> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe] (const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (safe == nullptr || ! file.existsAsFile()) return;
        const bool ok = safe->rig.loadImpulseResponse (file);
        safe->updateFileLabels();
        if (safe->onStatus) safe->onStatus (ok ? "Cabinet IR loaded: " + file.getFileNameWithoutExtension()
                                               : "Couldn't read " + file.getFileName());
    });
}

void RigPanel::updateFileLabels()
{
    auto& nam = rig.getNam();
    if (nam.isLoaded())
    {
        juce::String t = nam.getModelName();
        if (nam.sampleRateMismatch())
            t << "  (" << juce::String (nam.getModelSampleRate() / 1000.0, 1) << "k model)";
        namName.setText (t, juce::dontSendNotification);
        namName.setTooltip (nam.sampleRateMismatch()
            ? "This capture was made at " + juce::String ((int) nam.getModelSampleRate()) + " Hz. Set your interface to the same rate in Audio Settings for the exact tone."
            : nam.getFile().getFullPathName());
    }
    else
    {
        namName.setText ("no capture loaded", juce::dontSendNotification);
        namName.setTooltip ({});
    }

    auto ir = rig.getImpulseResponseFile();
    irName.setText (ir.existsAsFile() ? ir.getFileNameWithoutExtension() : juce::String(), juce::dontSendNotification);
    irName.setTooltip (ir.getFullPathName());
    loadIr.setTooltip (ir.existsAsFile() ? "Loaded impulse response: " + ir.getFileNameWithoutExtension() + " (click to load another)"
                                         : juce::String ("Load a speaker cabinet impulse response (.wav)"));
}

void RigPanel::timerCallback()
{
    inputMeter.setLevel (rig.getInputPeak());
    outputMeter.setLevel (rig.getOutputPeak());

    if (rig.isClipping()) clipHold = 45;
    clipLed.setColour (juce::Label::textColourId, clipHold > 0 ? theme::bad : theme::textFaint);
    if (clipHold > 0) --clipHold;

    static int tick = 0;
    if ((++tick & 1) == 0)
        tunerView.setReading (rig.getTuner().analyse());

    for (auto* m : { gateM.get(), compM.get(), driveM.get(), ampM.get(), cabM.get(), eqM.get(), tapeM.get(), chorusM.get(), delayM.get(), reverbM.get() })
        if (m->flash > 0.0f) { m->flash = juce::jmax (0.0f, m->flash - 0.035f); m->repaint(); }

    // keep module highlight in sync with power switches
    for (auto* m : { gateM.get(), compM.get(), driveM.get(), ampM.get(), cabM.get(), eqM.get(), tapeM.get(), chorusM.get(), delayM.get(), reverbM.get() })
        m->repaint (0, 0, m->getWidth(), 30);
}

void RigPanel::openPedal (const juce::String& uid)
{
    if (auto it = pedalWindows.find (uid); it != pedalWindows.end())
    {
        it->second->setVisible (true);
        it->second->toFront (true);
        return;
    }
    auto* proc = rig.getPedalProcessor (uid);
    if (proc == nullptr) return;
    juce::String name = proc->getName();
    for (auto& item : rig.getBoard())
        if (item.key == uid) name = item.name;
    juce::Component::SafePointer<RigPanel> safe (this);
    auto win = std::make_unique<daw::PluginWindow> ("Pedal: " + name, *proc, [safe, uid]
    {
        // closed with its X: delete it after this callback has returned
        juce::MessageManager::callAsync ([safe, uid] { if (safe != nullptr) safe->pedalWindows.erase (uid); });
        if (safe != nullptr)
            if (auto it = safe->pedalWindows.find (uid); it != safe->pedalWindows.end()) it->second->setVisible (false);
    });
    pedalWindows[uid] = std::move (win);
}

void RigPanel::pedalRemoved (const juce::String& uid)
{
    pedalWindows.erase (uid);   // the editor goes before its pedal does
}

void RigPanel::showBlock (const juce::String& key)
{
    EffectModule* m = nullptr;
    if (key == "gate") m = gateM.get();
    else if (key == "comp") m = compM.get();
    else if (key == "drive") m = driveM.get();
    else if (key == "ampcab") { m = ampM.get(); cabM->flash = 1.0f; cabM->repaint(); }
    else if (key == "eq") m = eqM.get();
    else if (key == "tape") m = tapeM.get();
    else if (key == "chorus") m = chorusM.get();
    else if (key == "delay") m = delayM.get();
    else if (key == "reverb") m = reverbM.get();
    if (m != nullptr) { m->flash = 1.0f; m->repaint(); }
}

void RigPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    g.setColour (theme::outline);
    g.drawHorizontalLine (0, 0.0f, (float) getWidth());
}

void RigPanel::resized()
{
    auto r = getLocalBounds().reduced (10, 6);

    auto header = r.removeFromTop (32);
    title.setBounds (header.removeFromLeft (110));
    savePreset.setBounds (header.removeFromRight (70).reduced (0, 3));
    header.removeFromRight (6);
    presetBox.setBounds (header.removeFromRight (280).reduced (0, 3));
    r.removeFromTop (2);
    board.setBounds (r.removeFromTop (36));
    r.removeFromTop (4);

    const int rowH = (r.getHeight() - 4) / 2;
    auto row1 = r.removeFromTop (rowH);
    r.removeFromTop (4);
    auto row2 = r;

    auto layoutRow = [] (juce::Rectangle<int> row, std::initializer_list<std::pair<EffectModule*, float>> mods)
    {
        float total = 0;
        for (auto& m : mods) total += m.second;
        const float unit = (float) row.getWidth() / total;
        for (auto& m : mods)
            m.first->setBounds (row.removeFromLeft ((int) std::round (m.second * unit)));
    };

    layoutRow (row1, { { inputModule.get(), 2.6f }, { charM.get(), 2.6f }, { gateM.get(), 2.1f }, { compM.get(), 4.6f }, { driveM.get(), 3.2f }, { ampM.get(), 6.2f } });
    layoutRow (row2, { { tunerModule.get(), 2.6f }, { cabM.get(), 5.6f }, { eqM.get(), 3.6f }, { tapeM.get(), 1.5f }, { chorusM.get(), 2.7f },
                       { delayM.get(), 3.6f }, { reverbM.get(), 3.6f }, { outputM.get(), 1.4f } });
}

} // namespace wis
