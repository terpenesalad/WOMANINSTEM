#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "Engine/AudioEngine.h"
#include "LookAndFeel.h"

namespace wis
{

/** Play Along's KEYS: a piano (or the singing yeti, the Sound Library, a synth...) to play along with the song
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

private:
    void timerCallback() override;
    void loadInstrument (const juce::String& id, const juce::String& state);
    void refreshPresets();
    void openEditor();
    void refreshMidiLabel();

    AudioEngine& engine;
    juce::PropertiesFile& settings;
    juce::String currentId;

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
