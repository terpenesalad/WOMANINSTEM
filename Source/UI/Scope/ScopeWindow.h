#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "Engine/ScopeFeed.h"
#include "ScopeRenderer.h"

namespace wis
{

/** The oscilloscope: a glowing beam drawn by the sound of your instrument, the song or the whole mix
    (the trippy, Tame Impala style projections). Controls sit in a strip at the bottom that hides itself
    in full screen, so the window can go straight onto a projector or second screen. */
class ScopeView : public juce::Component, private juce::Timer
{
public:
    ScopeView (ScopeFeed& feed, juce::PropertiesFile& settings);
    ~ScopeView() override;

    /** 0 = Play Along, 1 = Studio: changes the list of sources. */
    void setAppMode (int mode);
    /** Starts / stops drawing and tapping the audio. */
    void setRunning (bool shouldRun);

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    std::function<void()> onToggleFullScreen, onClose;
    void setFullScreenState (bool fs);

    /** Renders `seconds` of a test signal (no audio device): for the UI self-test and snapshots. */
    juce::Image renderTestFrame (const ScopeSettings& s, int width, int height, double seconds);
    /** Puts a test-signal picture on screen (self-test snapshots). */
    void showTestPicture() { frame = renderTestFrame (cfg, renderer.getWidth(), renderer.getHeight(), 2.0); repaint(); }

    ScopeSettings& getSettings() { return cfg; }

private:
    class Controls;
    void timerCallback() override;
    void applySource();
    void setSourceIndex (int index);
    void saveSettings();
    void updateRendererSize();
    juce::String sourceName() const;

    ScopeFeed& feed;
    juce::PropertiesFile& settings;
    ScopeSettings cfg;
    ScopeRenderer renderer;
    juce::Image frame;
    std::uint64_t cursor = 0;
    std::vector<float> bufA, bufB;
    double lastFrameMs = 0.0, lastMoveMs = 0.0, quietSinceMs = 0.0;
    int appMode = 0;
    int sourcePlay = 0, sourceStudio = 0;   // index into each mode's source list
    bool running = false, fullScreen = false, controlsShown = true;

    std::unique_ptr<Controls> controls;
    friend class Controls;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScopeView)
};

/** A separate, resizable window for the scope (drag it to a projector, then press F for full screen). */
class ScopeWindow : public juce::DocumentWindow
{
public:
    ScopeWindow (ScopeFeed& feed, juce::PropertiesFile& settings);
    ~ScopeWindow() override;

    void closeButtonPressed() override;
    void setAppMode (int mode) { view->setAppMode (mode); }
    void show();
    void toggleFullScreen();
    bool isFullScreenNow() const;
    void saveBounds();

private:
    juce::PropertiesFile& settings;
    ScopeView* view = nullptr;
};

} // namespace wis
