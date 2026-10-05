#pragma once

#include "StudioContext.h"

namespace wis::daw
{

/** Small round toggle used for M / S / R / I buttons. */
class HeaderToggle : public juce::Button
{
public:
    HeaderToggle (const juce::String& label, juce::Colour on) : juce::Button (label), onColour (on) { setClickingTogglesState (true); }
    void paintButton (juce::Graphics& g, bool over, bool down) override;
    juce::Colour onColour;
};

/** The left-hand strip of one track in the arrangement. */
class TrackHeader : public juce::Component, private juce::ValueTree::Listener
{
public:
    TrackHeader (StudioContext& ctx, juce::ValueTree track);
    ~TrackHeader() override;

    int trackId() const { return (int) track[ids::id]; }
    juce::ValueTree getTree() const { return track; }
    void refreshMeter();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    std::function<void (int from, int to)> onReorder;

private:
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void sync();
    void rename();

    StudioContext& ctx;
    juce::ValueTree track;
    HeaderToggle mute { "M", theme::warn }, solo { "S", theme::good }, arm { "R", theme::bad }, monitor { "I", juce::Colour (0xff60a5fa) };
    juce::Slider volume { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Slider pan { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    LevelMeter meterL { false }, meterR { false };
    juce::TextButton instrumentButton;
    bool dragging = false;
    int dragStartY = 0;
};

} // namespace wis::daw
