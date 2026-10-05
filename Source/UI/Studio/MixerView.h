#pragma once

#include "StudioContext.h"
#include "TrackHeader.h"

namespace wis::daw
{

class ChannelStrip;

/** Console view: one channel strip per track (instrument, inserts, pan, fader, meters) + master. */
class MixerView : public juce::Component, private juce::ValueTree::Listener, private juce::Timer
{
public:
    explicit MixerView (StudioContext& ctx);
    ~MixerView() override;
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void valueTreeChildAdded (juce::ValueTree& p, juce::ValueTree&) override        { if (affects (p)) needsRebuild = true; }
    void valueTreeChildRemoved (juce::ValueTree& p, juce::ValueTree&, int) override  { if (affects (p)) needsRebuild = true; }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override           { needsRebuild = true; }
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeRedirected (juce::ValueTree&) override                             { needsRebuild = true; }
    static bool affects (const juce::ValueTree& parent);
    void timerCallback() override;
    void rebuild();

    StudioContext& ctx;
    juce::Viewport viewport;
    juce::Component strips;
    juce::OwnedArray<ChannelStrip> channelStrips;
    std::unique_ptr<ChannelStrip> masterStrip;
    bool needsRebuild = true;
};

} // namespace wis::daw
