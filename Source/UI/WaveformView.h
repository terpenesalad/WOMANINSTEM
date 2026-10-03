#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Engine/StemPlayer.h"

namespace wis
{

/** Multitrack-style lanes: a time ruler, the full mix overview, and one waveform lane per stem.
    Click to seek, drag to set a loop, double-click to clear the loop. */
class WaveformView : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    explicit WaveformView (StemPlayer& player);

    void setSong (PlayableSong::Ptr song);

    /** Lane layout, shared with the mixer strips so rows line up. */
    struct Lane { int stem; int y; int height; };
    void setLanes (std::vector<Lane> lanes, int rulerHeight, int mixHeight);

    void refresh();     // call from a UI timer: moves the playhead / meters

    std::function<void (double)> onSeek;
    std::function<void (bool, double, double)> onLoopChanged;
    std::function<void (const juce::File&)> onFileDropped;
    std::function<void()> onOpenClicked;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragHover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragHover = false; repaint(); }

    std::array<bool, numStemIds> stemAudible {};   // set by MainComponent for dimming muted lanes

private:
    void rebuildCache();
    double xToSeconds (float x) const;
    float secondsToX (double s) const;
    juce::Rectangle<int> waveArea() const { return getLocalBounds().reduced (0, 0); }

    StemPlayer& player;
    PlayableSong::Ptr song;
    std::vector<Lane> lanes;
    int rulerH = 22, mixH = 44;

    juce::Image cache;
    bool cacheDirty = true;
    std::array<bool, numStemIds> cachedAudible {};

    double lastPos = -1.0;
    bool dragging = false, draggingLoop = false;
    float dragStartX = 0.0f;
    float hoverX = -1.0f;
    bool dragHover = false;
};

} // namespace wis
