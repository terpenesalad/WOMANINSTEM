#include "KeysPanel.h"
#include "Daw/Plugins/BuiltinProcessor.h"
#include "Studio/PluginEditors.h"

namespace wis
{

juce::StringArray KeysPanel::instrumentIds() { return { "piano", "yeti", "soundfont", "synth", "homekeys" }; }

KeysPanel::KeysPanel (AudioEngine& e, juce::PropertiesFile& s) : engine (e), settings (s)
{
    title.setText ("KEYS", juce::dontSendNotification);
    title.setFont (uiFont (15.0f, true));
    title.setColour (juce::Label::textColourId, theme::text);
    addAndMakeVisible (title);

    back.setTooltip ("Back to your amp rig (the keys keep playing)");
    back.onClick = [this] { if (onBack) onBack(); };
    addAndMakeVisible (back);

    const auto ids = instrumentIds();
    for (int i = 0; i < ids.size(); ++i)
        if (auto* info = daw::findBuiltin (ids[i]))
            instrumentBox.addItem (info->name, i + 1);
    instrumentBox.setTooltip ("What you play: Piano Room (real grand, vintage grand and upright pianos in rooms), the singing Yodel Yeti, "
                              "the General MIDI Sound Library, the Studio Synth or the HomeKeys 20");
    instrumentBox.onChange = [this]
    {
        const auto id = instrumentIds()[instrumentBox.getSelectedId() - 1];
        if (id.isNotEmpty() && id != currentId) loadInstrument (id, {});
    };
    addAndMakeVisible (instrumentBox);

    presetBox.setTextWhenNothingSelected ("Sound...");
    presetBox.onChange = [this]
    {
        if (auto* p = engine.getKeysInstrument(); p != nullptr && presetBox.getSelectedId() > 0)
            p->setCurrentProgram (presetBox.getSelectedId() - 1);
    };
    addAndMakeVisible (presetBox);

    edit.setTooltip ("Every knob of the sound: hammers, felt, tuning, tone, the room...");
    edit.onClick = [this] { openEditor(); };
    addAndMakeVisible (edit);

    sustain.setClickingTogglesState (true);
    sustain.setTooltip ("Sustain pedal, latched on (a MIDI sustain pedal works too)");
    sustain.onClick = [this]
    {
        if (auto* bp = dynamic_cast<daw::BuiltinProcessor*> (engine.getKeysInstrument()))
            if (bp->state.getParameter ("pedal") != nullptr) bp->setParam ("pedal", sustain.getToggleState() ? 1.0f : 0.0f);
    };
    addAndMakeVisible (sustain);

    volume.setRange (-36.0, 12.0, 0.1);
    volume.setDoubleClickReturnValue (true, 0.0);
    volume.setTextValueSuffix (" dB");
    volume.setValue (settings.getDoubleValue ("keys.volume", 0.0), juce::dontSendNotification);
    engine.keysVolumeDb = (float) volume.getValue();
    volume.onValueChange = [this] { engine.keysVolumeDb = (float) volume.getValue(); };
    volume.setTooltip ("Keys volume");
    addAndMakeVisible (volume);
    addAndMakeVisible (meter);

    midiLabel.setFont (uiFont (12.0f));
    midiLabel.setColour (juce::Label::textColourId, theme::textDim);
    addAndMakeVisible (midiLabel);
    hint.setFont (uiFont (12.0f));
    hint.setColour (juce::Label::textColourId, theme::textFaint);
    hint.setText ("Click the keyboard, then play with your computer keys: A W S E D F T G Y H U J K O L P  -  Z / X = octave down / up  -  "
                  "Space still plays / pauses the song", juce::dontSendNotification);
    addAndMakeVisible (hint);

    octDown.onClick = [this] { baseOctave = juce::jmax (0, baseOctave - 1); keyboard.setKeyPressBaseOctave (baseOctave); keyboard.setLowestVisibleKey (juce::jmax (0, baseOctave * 12 - 12)); };
    octUp.onClick = [this] { baseOctave = juce::jmin (8, baseOctave + 1); keyboard.setKeyPressBaseOctave (baseOctave); keyboard.setLowestVisibleKey (juce::jmax (0, baseOctave * 12 - 12)); };
    addAndMakeVisible (octDown);
    addAndMakeVisible (octUp);

    keyboard.setOctaveForMiddleC (4);
    keyboard.setKeyPressBaseOctave (baseOctave);   // the A key plays middle C
    keyboard.setAvailableRange (21, 108);
    keyboard.setVelocity (0.75f, true);
    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, theme::accent.withAlpha (0.6f));
    keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, theme::accent2.withAlpha (0.3f));
    addAndMakeVisible (keyboard);

    refreshMidiLabel();
    startTimerHz (20);
}

KeysPanel::~KeysPanel()
{
    editorWindow.reset();
}

void KeysPanel::visibilityChanged()
{
    if (isVisible())
    {
        ensureInstrument();
        refreshMidiLabel();
    }
}

void KeysPanel::ensureInstrument()
{
    if (engine.getKeysInstrument() != nullptr) return;
    auto id = settings.getValue ("keys.instrument", "piano");
    if (! instrumentIds().contains (id)) id = "piano";
    loadInstrument (id, settings.getValue ("keys.state"));
}

void KeysPanel::loadInstrument (const juce::String& id, const juce::String& state)
{
    editorWindow.reset();   // the editor goes before its instrument
    auto proc = daw::createBuiltin (id);
    if (proc == nullptr) return;
    if (state.isNotEmpty()) daw::decodeState (*proc, state);
    currentId = id;
    auto old = engine.setKeysInstrument (std::move (proc));
    old.reset();
    instrumentBox.setSelectedId (instrumentIds().indexOf (id) + 1, juce::dontSendNotification);
    refreshPresets();
    settings.setValue ("keys.instrument", id);
}

void KeysPanel::refreshPresets()
{
    presetBox.clear (juce::dontSendNotification);
    auto* p = engine.getKeysInstrument();
    if (p == nullptr) return;
    for (int i = 0; i < p->getNumPrograms(); ++i)
        if (p->getProgramName (i).isNotEmpty()) presetBox.addItem (p->getProgramName (i), i + 1);
    presetBox.setSelectedId (p->getCurrentProgram() + 1, juce::dontSendNotification);
    auto* bp = dynamic_cast<daw::BuiltinProcessor*> (p);
    const bool hasPedal = bp != nullptr && bp->state.getParameter ("pedal") != nullptr;
    sustain.setVisible (hasPedal);
    if (hasPedal) sustain.setToggleState (bp->param ("pedal") > 0.5f, juce::dontSendNotification);
}

void KeysPanel::openEditor()
{
    ensureInstrument();
    auto* p = engine.getKeysInstrument();
    if (p == nullptr) return;
    if (editorWindow != nullptr) { editorWindow->setVisible (true); editorWindow->toFront (true); return; }
    juce::Component::SafePointer<KeysPanel> safe (this);
    editorWindow = std::make_unique<daw::PluginWindow> ("Keys: " + p->getName(), *p, [safe]
    {
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) { safe->editorWindow.reset(); safe->refreshPresets(); } });
        if (safe != nullptr && safe->editorWindow != nullptr) safe->editorWindow->setVisible (false);
    });
}

void KeysPanel::saveSettings()
{
    settings.setValue ("keys.volume", volume.getValue());
    if (auto* p = engine.getKeysInstrument())
    {
        settings.setValue ("keys.instrument", currentId);
        settings.setValue ("keys.state", daw::encodeState (*p));
    }
}

void KeysPanel::refreshMidiLabel()
{
    juce::StringArray names;
    for (auto& d : juce::MidiInput::getAvailableDevices()) names.add (d.name);
    midiLabel.setText (names.isEmpty() ? juce::String ("No MIDI keyboard found: plug one in (USB) or use the computer keys")
                                       : "MIDI: " + names.joinIntoString (", "), juce::dontSendNotification);
}

void KeysPanel::timerCallback()
{
    meter.setLevel (engine.readKeysPeak());
    static int tick = 0;
    if ((++tick % 40) == 0 && isShowing()) refreshMidiLabel();
    if (auto* p = engine.getKeysInstrument(); p != nullptr && presetBox.getSelectedId() != p->getCurrentProgram() + 1 && presetBox.getNumItems() > 0)
        presetBox.setSelectedId (p->getCurrentProgram() + 1, juce::dontSendNotification);
}

bool KeysPanel::keyPressed (const juce::KeyPress& k)
{
    const auto c = juce::CharacterFunctions::toLowerCase (k.getTextCharacter());
    if (! k.getModifiers().isAnyModifierKeyDown())
    {
        if (c == 'z') { octDown.triggerClick(); return true; }
        if (c == 'x') { octUp.triggerClick(); return true; }
    }
    return false;
}

void KeysPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    g.setColour (theme::outline);
    g.drawHorizontalLine (0, 0.0f, (float) getWidth());
}

void KeysPanel::resized()
{
    auto r = getLocalBounds().reduced (10, 6);
    auto header = r.removeFromTop (32);
    back.setBounds (header.removeFromLeft (70).reduced (0, 3));
    header.removeFromLeft (10);
    title.setBounds (header.removeFromLeft (60));
    instrumentBox.setBounds (header.removeFromLeft (180).reduced (0, 3));
    header.removeFromLeft (8);
    presetBox.setBounds (header.removeFromLeft (240).reduced (0, 3));
    header.removeFromLeft (8);
    edit.setBounds (header.removeFromLeft (140).reduced (0, 3));
    header.removeFromLeft (8);
    sustain.setBounds (header.removeFromLeft (80).reduced (0, 3));
    header.removeFromLeft (16);
    meter.setBounds (header.removeFromRight (90).reduced (0, 11));
    header.removeFromRight (8);
    volume.setBounds (header.removeFromRight (200).reduced (0, 4));
    midiLabel.setBounds (header);

    r.removeFromTop (4);
    auto hintRow = r.removeFromTop (22);
    octUp.setBounds (hintRow.removeFromRight (60));
    hintRow.removeFromRight (6);
    octDown.setBounds (hintRow.removeFromRight (60));
    hint.setBounds (hintRow);
    r.removeFromTop (6);
    keyboard.setBounds (r);
    keyboard.setKeyWidth (juce::jmax (14.0f, (float) r.getWidth() / 36.0f));
    keyboard.setLowestVisibleKey (juce::jmax (21, baseOctave * 12 - 12));
}

} // namespace wis
