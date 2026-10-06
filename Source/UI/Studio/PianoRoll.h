#pragma once

#include "StudioContext.h"

namespace wis::daw
{

/** MIDI note editor for one clip: keyboard, note grid, velocity lane, tools. */
class PianoRoll : public juce::Component, private juce::ValueTree::Listener, private juce::Timer, private juce::ScrollBar::Listener
{
public:
    explicit PianoRoll (StudioContext& ctx);
    ~PianoRoll() override;

    void setClip (int clipId);
    int getClipId() const { return clip.isValid() ? (int) clip[ids::id] : 0; }

    void resized() override;
    void paint (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;

    class Grid;
    class Keys;
    class Velocity;

    // shared view state
    double ppb = 80.0, scrollBeats = 0.0;
    float rowH = 12.0f;
    float scrollY = 0.0f;
    double grid = 0.25;
    bool drawMode = false;
    bool drumMode = false;
    double lastLength = 0.25;
    int lastVelocity = 100;
    juce::Array<juce::ValueTree> selected;

    juce::ValueTree clip;
    StudioContext& ctx;

    float pitchToY (int pitch) const { return (127 - pitch) * rowH - scrollY; }
    int yToPitch (float y) const     { return juce::jlimit (0, 127, 127 - (int) std::floor ((y + scrollY) / rowH)); }
    double xToBeat (float x) const   { return scrollBeats + x / ppb; }
    float beatToX (double b) const   { return (float) ((b - scrollBeats) * ppb); }
    double snapBeat (double b, bool free) const { return free || grid <= 0 ? b : std::round (b / grid) * grid; }

    void previewNote (int pitch, int velocity);
    void deleteSelected();
    void transposeSelected (int semis);
    void nudgeSelected (double beats);
    void quantizeSelected();
    void selectAll();
    void repaintAll();
    void centreOnNotes();

private:
    void scrollBarMoved (juce::ScrollBar* bar, double start) override
    {
        if (bar == &hScroll) scrollBeats = start; else scrollY = (float) start;
        repaintAll();
    }
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { repaintAll(); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { repaintAll(); }
    void valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int) override;
    void timerCallback() override;

    std::unique_ptr<Grid> gridView;
    std::unique_ptr<Keys> keys;
    std::unique_ptr<Velocity> velocity;
    juce::ScrollBar hScroll { false }, vScroll { true };
    juce::Label title;
    juce::TextButton drawButton { "Draw" }, quantizeButton { "Quantize" }, octDown { "Oct -" }, octUp { "Oct +" }, duplicateButton { "Duplicate" };
    juce::ComboBox gridBox;
    juce::Slider velSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Label velLabel;
    int previewPitch = -1;
    int previewCountdown = 0;
};

/** Simple editor for an audio clip: gain, fades, takes. */
class AudioClipEditor : public juce::Component, private juce::ValueTree::Listener, private juce::ChangeListener
{
public:
    explicit AudioClipEditor (StudioContext& ctx);
    ~AudioClipEditor() override;
    void setClip (int clipId);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { sync(); }
    void sync();
    StudioContext& ctx;
    juce::ValueTree clip;
    juce::Label title, info;
    juce::Slider gain, fadeIn, fadeOut, speed, pitch;
    juce::Label gainL, fadeInL, fadeOutL, speedL, pitchL;
    juce::ComboBox takeBox;
    juce::ToggleButton follow { "Follow song tempo" }, reverse { "Reverse" };
    juce::TextButton normalize { "Normalize" };
};

} // namespace wis::daw
