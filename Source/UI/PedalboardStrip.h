#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Rig/RigProcessor.h"

namespace wis
{

/** The rig's signal chain as a row of pedals: drag to re-order, click the LED to switch one on or off,
    "+ Add pedal" for wahs, octavers, phasers, fuzzes and the rest, right-click to remove. Pedals to the left of
    the amp go into its input; the ones to the right sit in its effects loop. */
class PedalboardStrip : public juce::Component, public juce::TooltipClient, private juce::ChangeListener, private juce::Timer
{
public:
    explicit PedalboardStrip (RigProcessor& rig);
    ~PedalboardStrip() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    juce::String getTooltip() override;

    /** A pedal's knobs should open (key = its uid). */
    std::function<void (const juce::String& uid)> onOpenPedal;
    /** One of the rig's own blocks was clicked (key = its RigBlock key): show it. */
    std::function<void (const juce::String& key)> onShowBlock;

    void showAddMenu (int insertAt = -1);

    static juce::Colour colourFor (const RigProcessor::BoardItem&);

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
    void timerCallback() override;
    void refresh();
    void layoutChips();
    int chipAt (juce::Point<int> p) const;
    int insertIndexFor (int x) const;
    juce::Rectangle<float> ledArea (int index) const;
    void showItemMenu (int index);

    RigProcessor& rig;
    std::vector<RigProcessor::BoardItem> items;
    std::vector<juce::Rectangle<int>> chips;
    juce::Rectangle<int> chainArea, inChip, outChip;
    juce::TextButton addButton { "+ Add pedal" };

    int hover = -1, pressed = -1, dragInsert = -1;
    bool dragging = false;
    juce::Point<int> dragPos;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PedalboardStrip)
};

} // namespace wis
