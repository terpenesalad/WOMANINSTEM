#pragma once

#include "StudioContext.h"
#include "TrackHeader.h"

namespace wis::daw
{

/** The main multitrack timeline: track headers on the left, ruler + clips on the right. */
class ArrangementView : public juce::Component,
                        public juce::DragAndDropTarget,
                        public juce::FileDragAndDropTarget,
                        private juce::ValueTree::Listener,
                        private juce::ChangeListener,
                        private juce::Timer,
                        private juce::ScrollBar::Listener
{
public:
    explicit ArrangementView (StudioContext& ctx);
    ~ArrangementView() override;

    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scale) override;

    // DnD from the browser / OS
    bool isInterestedInDragSource (const SourceDetails&) override { return true; }
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override { dropHighlight = {}; repaint(); }
    void itemDropped (const SourceDetails&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void fileDragMove (const juce::StringArray&, int x, int y) override;
    void fileDragExit (const juce::StringArray&) override { dropHighlight = {}; repaint(); }
    void filesDropped (const juce::StringArray&, int x, int y) override;

    std::function<void (const juce::String& item, int trackId, double beat)> onBrowserDrop;
    std::function<void (const juce::StringArray& files, int trackId, double beat)> onFilesDrop;
    std::function<void()> onAddTrack;

    /** Zoom so the whole song fits. */
    void zoomToFit();
    void setZoom (double pixelsPerBeat, double anchorBeat, int anchorX);
    void scrollToShowBeat (double beat);

    struct Row { juce::ValueTree track; int y = 0, h = 0, laneH = 0; };
    std::vector<Row> rows() const;
    int headerWidth() const { return 236; }

    // Coordinates in the canvas
    double xToBeat (float x) const;
    float beatToX (double beat) const;

    class Canvas;
    class Ruler;

private:
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override       { structureChanged(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { structureChanged(); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override        { structureChanged(); }
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeRedirected (juce::ValueTree&) override                          { structureChanged(); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void scrollBarMoved (juce::ScrollBar*, double) override;

    void structureChanged() { needsRebuild = true; repaintAll(); }
    void rebuildHeaders();
    void layoutHeaders();
    void updateScrollBars();
    void repaintAll();
    int totalHeight() const;
    juce::Point<int> canvasPoint (int x, int y) const;
    void updateDropHighlight (int x, int y);

    StudioContext& ctx;
    std::unique_ptr<Canvas> canvas;
    std::unique_ptr<Ruler> ruler;
    juce::Component headerHolder;
    juce::OwnedArray<TrackHeader> headers;
    juce::ScrollBar vScroll { true }, hScroll { false };
    juce::TextButton addTrack { "+  Track" };
    juce::ComboBox snapBox;
    juce::Slider zoomSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    int scrollY = 0;
    bool needsRebuild = true;
    juce::Rectangle<int> dropHighlight;
    double lastPlayhead = -1.0;
    friend class Canvas;
    friend class Ruler;
};

} // namespace wis::daw
