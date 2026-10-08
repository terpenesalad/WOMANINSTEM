#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Daw/Plugins/BuiltinProcessor.h"
#include "UI/LookAndFeel.h"
#include "UI/RigPanel.h"

namespace wis::daw
{

/** Knobs, switches and choice boxes for a built-in plugin's parameters (all of them, or a chosen set),
    laid out in rows that wrap to the available width. */
class ParamPanel : public juce::Component
{
public:
    ParamPanel (BuiltinProcessor& p, const juce::StringArray& onlyIds = {}, juce::Colour accent = theme::accent,
                const juce::StringArray& excludeIds = {});
    /** Height needed at a given width. */
    int heightFor (int width) const;
    void resized() override;

    static constexpr int knobW = 86, knobH = 92, comboW = 170, toggleW = 150, switchH = 46;

private:
    juce::OwnedArray<wis::Knob> knobs;
    juce::OwnedArray<juce::ComboBox> combos;
    juce::OwnedArray<juce::Label> labels;
    juce::OwnedArray<juce::ToggleButton> toggles;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAttachments;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachments;
    juce::Array<juce::Component*> switches;   // combos (with labels) and toggles, in parameter order
    int layout (int width, bool apply);
};

/** Title + factory preset menu across the top of a built-in plugin's editor. */
class EditorHeader : public juce::Component
{
public:
    EditorHeader (BuiltinProcessor& p, const juce::String& titleText = {});
    void resized() override;
    void setTitle (const juce::String& t) { title.setText (t, juce::dontSendNotification); }
    void refreshPreset() { presets.setSelectedItemIndex (proc.getCurrentProgram(), juce::dontSendNotification); }
    std::function<void()> onPresetChanged;
    /** Extra buttons placed left of the preset box. */
    void addButton (juce::Component& c, int width) { extras.add ({ &c, width }); addAndMakeVisible (c); }

private:
    BuiltinProcessor& proc;
    juce::Label title;
    juce::ComboBox presets;
    juce::Array<std::pair<juce::Component*, int>> extras;
};

/** Paints the standard editor background with the coloured stripe on top. */
inline void paintEditorBackground (juce::Graphics& g, juce::Rectangle<int> r, juce::Colour stripe)
{
    g.fillAll (theme::panel);
    g.setColour (stripe);
    g.fillRect (r.removeFromTop (3));
}

/** Registers the custom editors for the samplers, HomeKeys, Loop Station and Vocal Tune. */
void installInstrumentEditors();
void installBeatLabEditor();
void installVoiceEditors();

} // namespace wis::daw
