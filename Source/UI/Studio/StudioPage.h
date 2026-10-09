#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "StudioContext.h"
#include "ControlBar.h"
#include "ArrangementView.h"
#include "PianoRoll.h"
#include "MixerView.h"
#include "BrowserPanel.h"
#include "PluginEditors.h"
#include "Library/SongLibrary.h"

namespace wis::daw
{

class BeatLab;

/** The Studio: a full DAW page (control bar, library, arrangement, editor / mixer / keyboard). */
class StudioPage : public juce::Component,
                   public juce::DragAndDropContainer,
                   private juce::Timer
{
public:
    StudioPage (Project& project, DawEngine& engine, PluginHost& host, juce::PropertiesFile& settings);
    ~StudioPage() override;

    void setDeviceManager (juce::AudioDeviceManager* dm) { deviceManager = dm; }
    void setActive (bool active);
    void saveSettings();

    /** Builds a project from a separated song: one track per stem (+ an empty track for you). */
    void openSongInStudio (const wis::SongInfo& song, const std::array<bool, wis::numStemIds>& muted,
                           const juce::ValueTree& rigState, const juce::String& playingPart);

    bool confirmDiscardThen (std::function<void()> next);   // returns false if it had to ask
    void openProjectFile (const juce::File& f);
    void showShortcuts();
    /** The plugin list changed (e.g. the background scan of the plugins folder found something). */
    void pluginsChanged() { browser.refresh(); }
    juce::String getProjectName() const;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;

private:
    void timerCallback() override;

    // panels
    void setBottomPanel (int which);   // 0 none, 1 editor, 2 mixer, 3 keys, 4 plugins
    void toggleBrowser();
    void updatePanelButtons();

    // actions
    void addTrackMenu();
    void projectMenu();
    void trackMenu (int trackId, juce::Point<int> screenPos);
    void openEditor (int clipId);
    void openPluginWindow (const juce::ValueTree& pluginNode);
    void openFloatingPluginWindow (const juce::String& slotId);
    void dockPlugin (const juce::String& slotId);
    juce::String pluginTitle (const juce::String& slotId);
    bool canDock (const juce::String& slotId);
    void closePluginWindow (const juce::String& slotId);
    void applyBrowserItem (const juce::String& item, int trackId, double beat);
    void importFiles (const juce::StringArray& files, int trackId, double beat);
    Track trackForInstrument (int trackId, const juce::String& name);
    void bounceInPlace (int trackId);
    void patternToSong (BeatLab& b, const juce::MidiMessageSequence& seq, double lengthBeats);
    void audioFromPlugin (BuiltinProcessor& p, const juce::File& f);

    void newProject();
    void openProject();
    void saveProject (std::function<void()> then = {});
    void saveProjectAs (std::function<void()> then = {});
    void exportDialog (bool stems);
    void exportMidi();
    void showPluginManager();
    /** Pick a plugin file (.dll / .vst3 / .clap, including old 32-bit VSTs), test it, and put it on a track. */
    void addPluginFile();
    void addRecent (const juce::File& f);
    void setStatus (const juce::String&);
    void updateTitle();

    // editing
    void deleteSelectedClips();
    void duplicateSelectedClips();
    void splitAtPlayhead();
    void copySelected();
    void paste();

    // musical typing
    bool handleTypingKey (const juce::KeyPress&);
    void releaseTypingKeys (bool all);

    Project& project;
    DawEngine& engine;
    PluginHost& host;
    juce::PropertiesFile& settings;
    juce::AudioDeviceManager* deviceManager = nullptr;
    StudioContext ctx;

    ControlBar controlBar { ctx };
    BrowserPanel browser { ctx };
    ArrangementView arrangement { ctx };
    PianoRoll pianoRoll { ctx };
    AudioClipEditor audioEditor { ctx };
    MixerView mixer { ctx };
    PluginDock dock;
    juce::MidiKeyboardComponent keyboard { engine.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard };
    juce::Label keysHint, status;
    juce::StretchableLayoutResizerBar splitter { &layout, 1, false };
    juce::StretchableLayoutManager layout;

    bool showBrowser = true;
    int bottomPanel = 1;
    int panelBeforeDock = 2;        // where to go back to when the last docked plugin closes
    bool dockPlugins = true;        // open built-in plugins in the bottom panel (false: floating windows)
    bool typing = false;
    int typingOctave = 5;      // C5 = middle C (MIDI 60)
    int typingVelocity = 100;
    std::map<int, int> heldTypingKeys;   // keyCode -> note

    juce::Array<juce::ValueTree> clipboard;
    std::map<juce::String, std::unique_ptr<PluginWindow>> pluginWindows;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::DocumentWindow> pluginManagerWindow;
    juce::File lastFolder;
    int autosaveTicks = 0;
    double statusTime = 0;
};

} // namespace wis::daw
