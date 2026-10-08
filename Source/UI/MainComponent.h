#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "Engine/AudioEngine.h"
#include "Library/SongLibrary.h"
#include "LookAndFeel.h"
#include "WaveformView.h"
#include "StemMixer.h"
#include "RigPanel.h"
#include "SeparationOverlay.h"
#include "LibraryView.h"

namespace wis
{

class SongJob;

class MainComponent : public juce::Component,
                      public juce::FileDragAndDropTarget,
                      private juce::Timer,
                      private juce::ChangeListener
{
public:
    explicit MainComponent (juce::PropertiesFile& settings);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

    void saveState();

    /** Open a song: separates it (or loads it from the library if it was split before). */
    void openFile (const juce::File& file);
    void openFromLibrary (const SongInfo& info);

    // ---- hosting inside the app shell (Play Along | Studio) ----
    juce::AudioDeviceManager& getDeviceManager() { return deviceManager; }
    AudioEngine& getEngine() { return engine; }
    void setEmbedded (bool e) { embedded = e; audioButton.setVisible (! e); helpButton.setVisible (! e); resized(); repaint(); }
    /** Connects / disconnects the play-along engine from the audio device (only one page owns the audio at a time). */
    void setActive (bool active);
    bool isActive() const { return active; }
    void showAudioSettings();
    void showHelp();

    /** "Open in Studio": the current song, which stems are muted, the rig and the part being played. */
    std::function<void (const SongInfo&, const std::array<bool, numStemIds>& muted, const juce::ValueTree& rigState, const juce::String& part)> onOpenInStudio;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void setupAudio();
    void showLibrary();
    void openInStudio();
    void browseForSong();
    void exportStems (const SongInfo& info);

    void startJob (std::unique_ptr<SongJob> job, const juce::String& title, const juce::String& heading);
    void jobFinished();
    void cancelJob();
    void rebuildForSampleRate();

    void setSong (PlayableSong::Ptr song, const SongInfo& info);
    void togglePlay();
    void toggleRecord();
    void updateTransportLabels();
    void updateLoopButton();
    void setStatus (const juce::String& text, bool important = false);
    void applyPartChoice();

    juce::PropertiesFile& settings;
    juce::TooltipWindow tooltips { this, 600 };

    // audio
    juce::AudioDeviceManager deviceManager;
    RigProcessor rig;
    StemPlayer player;
    Recorder recorder;
    AudioEngine engine { rig, player, recorder };

    // song state
    SongInfo currentInfo;
    bool hasSong = false;
    std::unique_ptr<SongJob> job;

    // ---- header ----
    juce::TextButton openButton { "Open Song" }, libraryButton { "Library" }, audioButton { "Audio Settings" }, helpButton { "?" };
    juce::ComboBox qualityBox;
    juce::Label songTitle, songSub, deviceInfo;
    juce::Slider masterVolume { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    LevelMeter masterMeterL { false }, masterMeterR { false };
    juce::Label limiterLed;
    int limiterHold = 0;

    // ---- transport ----
    juce::TextButton playButton { "Play" }, startButton { "|<" }, loopButton { "Loop" }, recordButton { "Rec" }, resetMix { "Reset mix" }, studioButton { "Open in Studio" };
    juce::Label timeLabel, speedLabel, transposeLabel, partLabel;
    juce::Slider speed { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Slider transpose { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
    juce::ComboBox partBox;

    // ---- main areas ----
    StemMixer mixer { player };
    WaveformView waveform { player };
    RigPanel rigPanel { rig, &engine };
    SeparationOverlay overlay;
    juce::Label status;
    juce::String statusText;
    double statusTime = 0.0;

    std::unique_ptr<juce::FileChooser> chooser;
    juce::Component::SafePointer<juce::DialogWindow> libraryWindow;
    juce::File lastFolder;
    int rigHeight = 378;
    double pendingRate = 0.0;
    bool embedded = false, active = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace wis
