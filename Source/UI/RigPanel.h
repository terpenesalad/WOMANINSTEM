#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Rig/RigProcessor.h"
#include "Engine/AudioEngine.h"
#include "LookAndFeel.h"
#include "TunerView.h"

namespace wis
{

/** A rotary knob with its name above and value below, bound to a rig parameter. */
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& label);
    void resized() override;
    juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };

private:
    juce::Label label;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
};

/** One effect "pedal" in the board: title bar with power switch, optional selector, knobs. */
class EffectModule : public juce::Component
{
public:
    EffectModule (juce::AudioProcessorValueTreeState& state, const juce::String& title, const char* powerParam, juce::Colour accent);

    Knob& addKnob (const char* paramId, const juce::String& label);
    juce::ComboBox& addChoice (const char* paramId);
    void addExtra (juce::Component& c, int width);    // shown in the selector row (after the selector)
    void addSide (juce::Component& c, int width);     // shown in the knob row after the knobs (width <= 0: fill)
    void setTooltipText (const juce::String& t) { tooltip = t; }

    void paint (juce::Graphics&) override;
    void resized() override;

    juce::Colour accent;

private:
    juce::AudioProcessorValueTreeState& state;
    juce::String title, tooltip;
    PowerButton power;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAttachment;
    juce::OwnedArray<Knob> knobs;
    std::unique_ptr<juce::ComboBox> choice;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> choiceAttachment;
    juce::Array<juce::Component*> extras, sides;
    juce::Array<int> extraWidths, sideWidths;
};

/** Stacks components vertically (used for small controls beside a module's knobs). */
class VStack : public juce::Component
{
public:
    void add (juce::Component& c, int height) { items.add (&c); heights.add (height); addAndMakeVisible (c); }
    void resized() override
    {
        auto r = getLocalBounds();
        for (int i = 0; i < items.size(); ++i)
        {
            items[i]->setBounds (r.removeFromTop (heights[i]));
            r.removeFromTop (4);
        }
    }
private:
    juce::Array<juce::Component*> items;
    juce::Array<int> heights;
};

/** The whole play-along rig: input & tuner, pedals, amp, cab, studio effects, presets. */
class RigPanel : public juce::Component, private juce::Timer
{
public:
    /** engine may be null when the rig is used as a Studio plugin (inputs then come from the track). */
    RigPanel (RigProcessor& rig, AudioEngine* engine);
    ~RigPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void refreshInputs();          // after the audio device changes
    void refreshPresetList();
    void showCurrentPresetName();
    void updateFileLabels();

    std::function<void (const juce::String&)> onStatus;   // short status messages for the footer

private:
    void timerCallback() override;
    void choosePreset (int id);
    void saveUserPreset();
    void chooseNamFile();
    void chooseIrFile();

    RigProcessor& rig;
    AudioEngine* engine;
    juce::AudioProcessorValueTreeState& state;

    // header
    juce::Label title;
    juce::ComboBox presetBox;
    juce::TextButton savePreset { "Save" };
    juce::Array<juce::File> userPresetFiles;

    // input
    juce::ComboBox inputBox;
    LevelMeter inputMeter { true };
    juce::TextButton monitor { "Monitor" };
    TunerView tunerView;
    juce::TextButton tunerMute { "Mute" };
    juce::Label clipLed;
    VStack inputSide;
    int clipHold = 0;

    std::unique_ptr<EffectModule> inputModule, tunerModule, gateM, compM, driveM, ampM, cabM, eqM, chorusM, delayM, reverbM, outputM;
    juce::TextButton loadNam { "Load NAM..." }, loadIr { "Load IR..." };
    juce::Label namName, irName;
    juce::ToggleButton pingPong { "Ping-pong" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> pingPongAttachment;
    LevelMeter outputMeter { true };

    std::unique_ptr<juce::FileChooser> chooser;
};

} // namespace wis
