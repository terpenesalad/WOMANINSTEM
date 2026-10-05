#pragma once

#include "MainComponent.h"
#include "Studio/StudioPage.h"

namespace wis
{

/** The top-level window content: a slim mode bar ([PLAY ALONG | STUDIO], audio settings, help) above
    either the Play-Along page or the Studio. Both pages share one audio device; only the visible one is connected. */
class AppShell : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    explicit AppShell (juce::PropertiesFile& settings);
    ~AppShell() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    void setMode (int mode);   // 0 play along, 1 studio
    int getMode() const { return mode; }

    /** Opens a file from the command line / Explorer: projects go to the Studio, songs to Play Along. */
    void openFile (const juce::File& f);

    /** Asks to save the Studio song if needed, then calls quit. */
    void requestQuit (std::function<void()> quit);
    void saveState();

private:
    class ModeTab;

    juce::PropertiesFile& settings;
    MainComponent playAlong { settings };
    daw::Project project;
    daw::PluginHost host;
    daw::DawEngine dawEngine { project, host };
    std::unique_ptr<daw::StudioPage> studio;

    std::unique_ptr<ModeTab> playTab, studioTab;
    juce::TextButton audioButton { "Audio & MIDI" }, helpButton { "?" };
    int mode = 0;
};

} // namespace wis
