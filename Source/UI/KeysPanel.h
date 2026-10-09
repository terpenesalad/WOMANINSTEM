#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "Engine/AudioEngine.h"
#include "LookAndFeel.h"

namespace wis::daw { class PluginHost; }

namespace wis
{

/** Play Along's KEYS: a piano (or Delay Lama, the Sound Library, a synth...) to play along with the song
    from a MIDI keyboard, the on-screen keyboard or the computer keys. */
class KeysPanel : public juce::Component, private juce::Timer
{
public:
    KeysPanel (AudioEngine& engine, juce::PropertiesFile& settings);
    ~KeysPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void visibilityChanged() override;

    /** Loads the last-used instrument if none is loaded yet (message thread). */
    void ensureInstrument();
    void saveSettings();
    void focusKeyboard() { keyboard.grabKeyboardFocus(); }

    std::function<void()> onBack;

    static juce::StringArray instrumentIds();
    /** The Studio's plugin list: scanned plugin instruments (VST3, VST, old 32-bit VSTs, CLAP) can be played too. */
    void setPluginHost (daw::PluginHost* h) { host = h; refreshInstrumentList(); }
    void refreshInstrumentList();

private:
    void timerCallback() override;
    void loadInstrument (const juce::String& id, const juce::String& state);
    int itemIdFor (const juce::String& id) const;
    void refreshPresets();
    void openEditor();
    void refreshMidiLabel();

    AudioEngine& engine;
    juce::PropertiesFile& settings;
    juce::String currentId;
    daw::PluginHost* host = nullptr;
    juce::StringArray externalIds;      // "ext:<plugin identifier>", item ids 100 + index

    juce::TextButton back { "< RIG" }, edit { "Edit the sound..." }, sustain { "Sustain" }, octDown { "Oct -" }, octUp { "Oct +" };
    juce::ComboBox instrumentBox, presetBox;
    juce::Slider volume { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Label title, midiLabel, hint;
    LevelMeter meter { false };
    juce::MidiKeyboardComponent keyboard { engine.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard };
    std::unique_ptr<juce::DocumentWindow> editorWindow;
    int baseOctave = 5;
};

} // namespace wis
