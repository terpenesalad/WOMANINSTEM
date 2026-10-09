#include "MainComponent.h"
#include "Separation/AudioFileLoader.h"
#include "Separation/StemSeparator.h"
#include "Common/Resample.h"
#include <thread>

namespace wis
{

// =====================================================================================================
//  SongJob: everything slow happens here, off the message thread.
//    open file  -> (library hit? load) : (download models, decode, separate, save) -> resample -> overviews
// =====================================================================================================
class SongJob : public juce::Thread
{
public:
    enum class Kind { openFile, openLibrary, rebuild };

    SongJob (Kind k, juce::File f, SongInfo info, SeparationSettings s, double deviceRate)
        : juce::Thread ("WOMANINSTEM job"), kind (k), file (std::move (f)), songInfo (std::move (info)), settings (s), rate (deviceRate)
    {}

    ~SongJob() override { stopThread (30000); }

    void run() override
    {
        try
        {
            runJob();
        }
        catch (const std::bad_alloc&)
        {
            setError ("Ran out of memory. Close other programs and try again (or use Standard quality).");
        }
        catch (const std::exception& e)
        {
            setError (juce::String ("Unexpected error: ") + e.what());
        }
        finished = true;
        if (onDone) juce::MessageManager::callAsync (onDone);
    }

    float getProgress() const { return progress.load(); }
    juce::String getStage() const { const juce::ScopedLock sl (lock); return stage; }
    juce::String getError() const { const juce::ScopedLock sl (lock); return error; }
    bool isFinished() const { return finished.load(); }
    bool wasCancelled() const { return cancelled.load(); }
    void cancel() { cancelled = true; signalThreadShouldExit(); }

    PlayableSong::Ptr result;
    SongInfo songInfo;
    bool wasCached = false;
    std::function<void()> onDone;

private:
    void setStage (float p, const juce::String& s)
    {
        progress = p;
        const juce::ScopedLock sl (lock);
        stage = s;
    }

    void setError (const juce::String& e)
    {
        const juce::ScopedLock sl (lock);
        error = e;
    }

    bool shouldStop() const { return cancelled.load() || threadShouldExit(); }

    void runJob()
    {
        StemBuffers stems;
        bool haveStems = false;
        wasCached = kind != Kind::openFile;

        if (kind == Kind::openFile)
        {
            setStage (0.0f, "Checking your library");
            const auto id = SongLibrary::computeId (file, settings.quality);
            if (auto existing = SongLibrary::findSong (id))
            {
                songInfo = *existing;
                wasCached = true;
            }
            else
            {
                if (! separate (id, stems))
                    return;
                haveStems = true;
            }
        }

        if (! haveStems)
        {
            setStage (0.0f, "Loading stems");
            auto err = SongLibrary::loadStems (songInfo, stems, [this] { return shouldStop(); });
            if (err.isNotEmpty()) { setError (err); return; }
        }

        if (shouldStop()) { setError ("Cancelled"); return; }

        // ---- convert to the device sample rate (each stem on its own thread) ----
        setStage (0.97f, "Preparing playback at " + juce::String (rate / 1000.0, 1) + " kHz");
        PlayableSong::Ptr song = new PlayableSong();
        song->sampleRate = rate;
        song->songId = songInfo.id;

        std::vector<std::thread> workers;
        for (int i = 0; i < numStemIds; ++i)
        {
            song->present[(size_t) i] = songInfo.present[(size_t) i] && stems[(size_t) i].getNumSamples() > 0;
            if (! song->present[(size_t) i]) continue;
            workers.emplace_back ([&, i] { song->stems[(size_t) i] = resampleBuffer (stems[(size_t) i], stemSampleRate, rate); stems[(size_t) i] = {}; });
        }
        for (auto& w : workers) w.join();

        int len = 0;
        for (int i = 0; i < numStemIds; ++i)
            if (song->present[(size_t) i])
                len = juce::jmax (len, song->stems[(size_t) i].getNumSamples());
        song->length = len;

        if (len == 0) { setError ("This song has no audible stems."); return; }

        setStage (0.99f, "Drawing waveforms");
        song->buildOverviews();
        result = song;
        setStage (1.0f, "Ready");
    }

    bool separate (const juce::String& id, StemBuffers& stems)
    {
        // 1) models (one-time download)
        auto missing = StemSeparator::missingModels (settings.quality);
        for (int m = 0; m < missing.size(); ++m)
        {
            auto spec = modelSpec (missing[m]);
            const auto label = "Downloading the AI model (" + juce::String (spec.approxBytes / 1'000'000) + " MB, one time only)"
                             + (missing.size() > 1 ? "  " + juce::String (m + 1) + "/" + juce::String (missing.size()) : juce::String());
            setStage (0.0f, label);
            auto err = ModelManager::download (missing[m], [this] (juce::int64 done, juce::int64 total)
            {
                progress = (float) ((double) done / (double) juce::jmax ((juce::int64) 1, total));
            }, [this] { return ! shouldStop(); });

            if (err.isNotEmpty()) { setError (err); return false; }
        }

        // 2) decode
        setStage (0.0f, "Decoding " + file.getFileName());
        auto loaded = loadAudioFile (file, stemSampleRate, [this] { return shouldStop(); });
        if (! loaded.ok()) { setError (loaded.error); return false; }

        songInfo = {};
        songInfo.id = id;
        songInfo.title = loaded.title;
        songInfo.artist = loaded.artist;
        songInfo.sourcePath = file.getFullPathName();
        songInfo.durationSeconds = loaded.audio.getNumSamples() / stemSampleRate;
        songInfo.added = juce::Time::getCurrentTime();
        songInfo.quality = settings.quality;

        // 3) separate
        StemSeparator separator;
        SeparatedStems separated;
        auto err = separator.separate (loaded.audio, settings,
                                       [this] (float p, const juce::String& s) { setStage (0.02f + p * 0.9f, s); },
                                       [this] { return shouldStop(); }, separated);
        if (err.isNotEmpty()) { setError (err); return false; }

        loaded.audio = {};   // free memory

        // 4) save to library
        setStage (0.93f, "Saving stems to your library");
        err = SongLibrary::saveSong (songInfo, separated, [this] (float p) { progress = 0.93f + p * 0.04f; });
        if (err.isNotEmpty()) { setError (err); return false; }

        for (int i = 0; i < numStemIds; ++i)
            if (separated.present[(size_t) i])
                stems[(size_t) i] = std::move (separated.audio[(size_t) i]);
        return true;
    }

    Kind kind;
    juce::File file;
    SeparationSettings settings;
    double rate;

    std::atomic<float> progress { 0.0f };
    std::atomic<bool> finished { false }, cancelled { false };
    juce::CriticalSection lock;
    juce::String stage, error;
};

// =====================================================================================================

static juce::String fmtTime (double s)
{
    if (s < 0) s = 0;
    const int m = (int) (s / 60.0);
    const int sec = (int) s % 60;
    const int tenth = (int) ((s - std::floor (s)) * 10.0);
    return juce::String (m) + ":" + juce::String (sec).paddedLeft ('0', 2) + "." + juce::String (tenth);
}

MainComponent::MainComponent (juce::PropertiesFile& s) : settings (s)
{
    setOpaque (true);
    setWantsKeyboardFocus (true);

    // ---- header ----
    openButton.setColour (juce::TextButton::buttonColourId, theme::accent);
    openButton.setTooltip ("Open an MP3/FLAC/WAV and split it into stems (Ctrl+O)");
    openButton.onClick = [this] { browseForSong(); };
    libraryButton.setTooltip ("Songs you've already split - open instantly or export stems");
    libraryButton.onClick = [this] { showLibrary(); };
    audioButton.setTooltip ("Choose your audio interface, inputs, sample rate and buffer size");
    audioButton.onClick = [this] { showAudioSettings(); };
    helpButton.setTooltip ("Quick start guide");
    helpButton.onClick = [this] { showHelp(); };

    qualityBox.addItem ("Standard quality", 1);
    qualityBox.addItem ("Max quality (4x slower)", 2);
    qualityBox.setTooltip ("Max quality adds fine-tuned models for vocals, drums and bass (downloads ~250 MB once)");
    qualityBox.setSelectedId (settings.getIntValue ("quality", 1), juce::dontSendNotification);
    qualityBox.onChange = [this] { settings.setValue ("quality", qualityBox.getSelectedId()); };

    songTitle.setFont (uiFont (17.0f, true));
    songTitle.setColour (juce::Label::textColourId, theme::text);
    songTitle.setText ("No song loaded", juce::dontSendNotification);
    songSub.setFont (uiFont (12.5f));
    songSub.setColour (juce::Label::textColourId, theme::textDim);
    deviceInfo.setFont (uiFont (11.5f));
    deviceInfo.setColour (juce::Label::textColourId, theme::textDim);
    deviceInfo.setJustificationType (juce::Justification::centredRight);

    masterVolume.setRange (-40.0, 6.0, 0.1);
    masterVolume.setSkewFactorFromMidPoint (-10.0);
    masterVolume.setValue (settings.getDoubleValue ("masterVolume", 0.0), juce::dontSendNotification);
    masterVolume.setDoubleClickReturnValue (true, 0.0);
    masterVolume.setPopupDisplayEnabled (true, true, nullptr);
    masterVolume.setTextValueSuffix (" dB");
    masterVolume.setTooltip ("Master volume (everything you hear)");
    masterVolume.onValueChange = [this] { engine.masterVolumeDb = (float) masterVolume.getValue(); };
    engine.masterVolumeDb = (float) masterVolume.getValue();

    limiterLed.setText ("LIMIT", juce::dontSendNotification);
    limiterLed.setFont (uiFont (10.0f, true));
    limiterLed.setJustificationType (juce::Justification::centred);
    limiterLed.setColour (juce::Label::textColourId, theme::textFaint);
    limiterLed.setTooltip ("The safety limiter is protecting your ears/speakers - turn something down");

    for (juce::Component* c : std::initializer_list<juce::Component*> { &openButton, &libraryButton, &audioButton, &helpButton, &qualityBox, &songTitle, &songSub,
                                &deviceInfo, &masterVolume, &masterMeterL, &masterMeterR, &limiterLed })
        addAndMakeVisible (c);

    // ---- transport ----
    playButton.setColour (juce::TextButton::buttonColourId, theme::panelRaised.brighter (0.1f));
    playButton.setTooltip ("Play / pause (Space)");
    playButton.onClick = [this] { togglePlay(); };
    startButton.setTooltip ("Back to the start (Home)");
    startButton.onClick = [this] { player.seekSeconds (player.isLooping() ? player.getLoopStartSeconds() : 0.0); };
    loopButton.setClickingTogglesState (true);
    loopButton.setColour (juce::TextButton::buttonOnColourId, theme::accent2);
    loopButton.setTooltip ("Loop a section (L). Drag across the waveform to choose it.");
    loopButton.onClick = [this]
    {
        if (loopButton.getToggleState() && player.getLoopEndSeconds() <= player.getLoopStartSeconds())
        {
            // no region yet: loop 8 seconds from the playhead
            const double p = player.getPositionSeconds();
            player.setLoop (true, p, juce::jmin (player.getLengthSeconds(), p + 8.0));
        }
        else
            player.setLoop (loopButton.getToggleState(), player.getLoopStartSeconds(), player.getLoopEndSeconds());
        updateLoopButton();
    };
    recordButton.setClickingTogglesState (false);
    recordButton.setColour (juce::TextButton::buttonOnColourId, theme::bad);
    recordButton.setTooltip ("Record yourself (R): saves the mix, your rig sound and your dry DI as separate WAV files");
    recordButton.onClick = [this] { toggleRecord(); };
    resetMix.setTooltip ("Unmute everything and reset all stem volumes");
    resetMix.onClick = [this] { mixer.resetAll(); partBox.setSelectedId (1, juce::dontSendNotification); };
    studioButton.setColour (juce::TextButton::buttonColourId, theme::accent2.withAlpha (0.85f));
    studioButton.setTooltip ("Take this song into the Studio: every stem on its own track, tempo detected, and a track ready for you to record on");
    studioButton.onClick = [this] { openInStudio(); };

    timeLabel.setFont (uiFont (15.0f, true));
    timeLabel.setColour (juce::Label::textColourId, theme::text);
    timeLabel.setJustificationType (juce::Justification::centred);

    speedLabel.setFont (uiFont (12.0f));
    speedLabel.setColour (juce::Label::textColourId, theme::textDim);
    speed.setRange (50.0, 125.0, 1.0);
    speed.setValue (100.0, juce::dontSendNotification);
    speed.setDoubleClickReturnValue (true, 100.0);
    speed.setColour (juce::Slider::trackColourId, theme::accent2);
    speed.setTooltip ("Practice speed - slows the song down without changing the pitch (double-click = 100%)");
    speed.onValueChange = [this] { player.setSpeed ((float) speed.getValue() / 100.0f); updateTransportLabels(); };

    transposeLabel.setFont (uiFont (12.0f));
    transposeLabel.setColour (juce::Label::textColourId, theme::textDim);
    transposeLabel.setText ("Key", juce::dontSendNotification);
    transpose.setRange (-12.0, 12.0, 1.0);
    transpose.setValue (0.0, juce::dontSendNotification);
    transpose.setTextValueSuffix (" st");
    transpose.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 52, 24);
    transpose.setIncDecButtonsMode (juce::Slider::incDecButtonsNotDraggable);
    transpose.setTooltip ("Transpose the song in semitones (e.g. -1 for songs tuned to Eb)");
    transpose.onValueChange = [this] { player.setTransposeSemitones ((float) transpose.getValue()); };

    partLabel.setText ("I'm playing", juce::dontSendNotification);
    partLabel.setFont (uiFont (12.0f));
    partLabel.setColour (juce::Label::textColourId, theme::textDim);
    partBox.addItem ("Just listening", 1);
    partBox.addItem ("Bass", 2);
    partBox.addItem ("Guitar", 3);
    partBox.addItem ("Keys / Piano", 4);
    partBox.addItem ("Lead Vocals", 5);
    partBox.addItem ("Backing Vocals", 6);
    partBox.addItem ("Drums", 7);
    partBox.setSelectedId (1, juce::dontSendNotification);
    partBox.setTooltip ("Mutes the part you're going to play, so you replace it");
    partBox.onChange = [this] { applyPartChoice(); };

    for (juce::Component* c : std::initializer_list<juce::Component*> { &playButton, &startButton, &loopButton, &recordButton, &resetMix, &studioButton, &timeLabel,
                                &speedLabel, &speed, &transposeLabel, &transpose, &partLabel, &partBox })
        addAndMakeVisible (c);

    // ---- areas ----
    addAndMakeVisible (mixer);
    addAndMakeVisible (waveform);
    addAndMakeVisible (rigPanel);
    addChildComponent (keysPanel);
    keysPanel.onBack = [this] { showKeys (false); };
    keysButton.setTooltip ("Play keys along with the song: a real piano in a room, Delay Lama, synths... from a MIDI keyboard or your computer keys (Ctrl+K)");
    keysButton.setColour (juce::TextButton::buttonColourId, theme::accent2.withAlpha (0.35f));
    keysButton.onClick = [this] { showKeys (true); };
    addAndMakeVisible (keysButton);

    status.setFont (uiFont (12.0f));
    status.setColour (juce::Label::textColourId, theme::textDim);
    addAndMakeVisible (status);

    addChildComponent (overlay);
    overlay.onCancel = [this] { cancelJob(); };

    mixer.onMixChanged = [this] { waveform.stemAudible = mixer.audibleStems(); };
    waveform.onSeek = [this] (double t) { player.seekSeconds (t); };
    waveform.onLoopChanged = [this] (bool on, double a, double b)
    {
        player.setLoop (on, a, b);
        if (! on) player.setLoop (false, 0.0, 0.0);
        updateLoopButton();
    };
    waveform.onFileDropped = [this] (const juce::File& f) { openFile (f); };
    waveform.onOpenClicked = [this] { browseForSong(); };

    rigPanel.onStatus = [this] (const juce::String& t) { setStatus (t); };

    lastFolder = juce::File (settings.getValue ("lastFolder", juce::File::getSpecialLocation (juce::File::userMusicDirectory).getFullPathName()));

    // ---- restore rig ----
    if (auto rigXml = settings.getXmlValue ("rigState"))
        rig.restorePresetState (juce::ValueTree::fromXml (*rigXml));
    else
        rig.loadFactoryPreset (2);   // classic rock crunch as a friendly default
    rigPanel.updateFileLabels();
    rigPanel.showCurrentPresetName();

    engine.selectedInput = settings.getIntValue ("inputChannel", 0);
    engine.monitorOn = true;

    setupAudio();
    enableMidi (true);

    setSize (1360, 860);
    startTimerHz (30);
    setStatus ("Welcome! Open a song, plug in your instrument, and pick a preset.");

    // reopen the last song (instant - it's in the library)
    const auto lastSong = settings.getValue ("lastSongId");
    if (lastSong.isNotEmpty())
        if (auto info = SongLibrary::findSong (lastSong))
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this), i = *info]
            {
                if (safe != nullptr) safe->openFromLibrary (i);
            });
}

MainComponent::~MainComponent()
{
    stopTimer();
    if (job != nullptr) { job->onDone = nullptr; job->cancel(); job.reset(); }
    saveState();
    recorder.stop();
    deviceManager.removeAudioCallback (&engine);
    enableMidi (false);
    deviceManager.removeChangeListener (this);
    deviceManager.closeAudioDevice();
    if (libraryWindow != nullptr) delete libraryWindow.getComponent();
}

// ---- audio --------------------------------------------------------------------------------------------

void MainComponent::setupAudio()
{
    engine.onSampleRateChanged = [this] (double rate)
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this), rate]
        {
            if (safe == nullptr) return;
            safe->pendingRate = rate;
            safe->rebuildForSampleRate();
        });
    };

    auto saved = settings.getXmlValue ("audioState");
    juce::String err;

    if (saved == nullptr)
    {
        // First run: prefer ASIO (lowest latency) when an ASIO driver is installed.
        deviceManager.initialise (2, 2, nullptr, true);
        for (auto* type : deviceManager.getAvailableDeviceTypes())
        {
            if (type->getTypeName() == "ASIO")
            {
                type->scanForDevices();
                if (! type->getDeviceNames().isEmpty())
                {
                    deviceManager.setCurrentAudioDeviceType ("ASIO", true);
                    break;
                }
            }
        }
        auto setup = deviceManager.getAudioDeviceSetup();
        setup.bufferSize = 128;
        setup.useDefaultInputChannels = true;
        setup.useDefaultOutputChannels = true;
        deviceManager.setAudioDeviceSetup (setup, true);
    }
    else
    {
        err = deviceManager.initialise (2, 2, saved.get(), true);
    }

    deviceManager.addAudioCallback (&engine);
    deviceManager.addChangeListener (this);
    changeListenerCallback (&deviceManager);

    if (err.isNotEmpty())
        setStatus ("Audio device problem: " + err + " - open Audio & MIDI (top right).", true);
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    rigPanel.refreshInputs();

    if (auto* dev = deviceManager.getCurrentAudioDevice())
    {
        const auto type = deviceManager.getCurrentAudioDeviceType();
        juce::String t;
        t << dev->getName() << "  |  " << type << "  |  " << juce::String (dev->getCurrentSampleRate() / 1000.0, 1) << " kHz  |  "
          << dev->getCurrentBufferSizeSamples() << " samples  |  ~" << juce::String (engine.getRoundTripLatencyMs(), 1) << " ms";
        deviceInfo.setText (t, juce::dontSendNotification);

        if (engine.getRoundTripLatencyMs() > 25.0 && type != "ASIO")
            deviceInfo.setTooltip ("Latency is high. In Audio & MIDI choose your interface's ASIO driver (or 'Windows Audio (Exclusive Mode)') and a smaller buffer size (64-128).");
        else
            deviceInfo.setTooltip ("Round-trip latency estimate: input + output + one buffer");
    }
    else
    {
        deviceInfo.setText ("No audio device - open Audio & MIDI (top right)", juce::dontSendNotification);
    }
}

void MainComponent::showAudioSettings()
{
    auto* selector = new juce::AudioDeviceSelectorComponent (deviceManager, 0, 8, 2, 8, true, false, true, false);
    selector->setSize (580, 560);

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (selector);
    o.dialogTitle = "Audio & MIDI Settings";
    o.dialogBackgroundColour = theme::panel;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.componentToCentreAround = this;
    auto* w = o.launchAsync();
    juce::ignoreUnused (w);
    setStatus ("Tip: pick your interface's ASIO driver and 64-128 samples for the lowest latency.");
}

// ---- song opening -----------------------------------------------------------------------------------

void MainComponent::browseForSong()
{
    chooser = std::make_unique<juce::FileChooser> ("Open a song to split into stems", lastFolder, supportedAudioWildcard());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe = juce::Component::SafePointer<MainComponent> (this)] (const juce::FileChooser& fc)
    {
        if (safe == nullptr) return;
        auto f = fc.getResult();
        if (f.existsAsFile())
            safe->openFile (f);
    });
}

void MainComponent::openFile (const juce::File& file)
{
    if (! file.existsAsFile()) return;
    if (job != nullptr) { setStatus ("Already working on a song - cancel it first.", true); return; }

    lastFolder = file.getParentDirectory();
    settings.setValue ("lastFolder", lastFolder.getFullPathName());

    SeparationSettings s;
    s.quality = qualityBox.getSelectedId() == 2 ? SeparationQuality::maximum : SeparationQuality::standard;
    s.splitBackingVocals = true;

    const double rate = engine.getSampleRate() > 0 ? engine.getSampleRate() : 48000.0;
    startJob (std::make_unique<SongJob> (SongJob::Kind::openFile, file, SongInfo {}, s, rate), file.getFileNameWithoutExtension(), "SPLITTING INTO STEMS");
}

void MainComponent::openFromLibrary (const SongInfo& info)
{
    if (job != nullptr) return;
    if (libraryWindow != nullptr) libraryWindow->exitModalState (0);
    const double rate = engine.getSampleRate() > 0 ? engine.getSampleRate() : 48000.0;
    startJob (std::make_unique<SongJob> (SongJob::Kind::openLibrary, juce::File(), info, SeparationSettings {}, rate), info.displayName(), "OPENING FROM YOUR LIBRARY");
}

void MainComponent::rebuildForSampleRate()
{
    auto song = player.getSong();
    if (! hasSong || song == nullptr || pendingRate <= 0.0 || std::abs (song->sampleRate - pendingRate) < 1.0 || job != nullptr)
        return;

    startJob (std::make_unique<SongJob> (SongJob::Kind::rebuild, juce::File(), currentInfo, SeparationSettings {}, pendingRate),
              currentInfo.displayName(), "PREPARING FOR " + juce::String (pendingRate / 1000.0, 1) + " KHZ");
}

void MainComponent::startJob (std::unique_ptr<SongJob> j, const juce::String& title, const juce::String& heading)
{
    job = std::move (j);
    job->onDone = [safe = juce::Component::SafePointer<MainComponent> (this)] { if (safe != nullptr) safe->jobFinished(); };
    overlay.start (title, heading);
    if (! heading.startsWith ("SPLITTING"))
        overlay.setDetail ("Loading your stems - this only takes a moment.");
    overlay.setVisible (true);
    overlay.toFront (false);
    player.pause();
    job->startThread (juce::Thread::Priority::normal);
}

void MainComponent::cancelJob()
{
    if (job != nullptr)
    {
        job->cancel();
        overlay.update (job->getProgress(), "Cancelling...");
    }
}

void MainComponent::jobFinished()
{
    if (job == nullptr || ! job->isFinished()) return;

    std::unique_ptr<SongJob> done (std::move (job));
    done->stopThread (5000);
    overlay.setVisible (false);

    const auto err = done->getError();
    if (err == "Cancelled" || done->wasCancelled())
    {
        setStatus ("Cancelled.");
        return;
    }
    if (err.isNotEmpty() || done->result == nullptr)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Couldn't open the song",
                                                err.isNotEmpty() ? err : juce::String ("Unknown error"), "OK", this);
        return;
    }

    setSong (done->result, done->songInfo);
    if (done->wasCached)
        setStatus ("Opened \"" + done->songInfo.displayName() + "\" from your library.");
    else
        setStatus ("Done! Mute the part you want to play (\"I'm playing\"), plug in, and press Space.");

    if (pendingRate > 0.0 && std::abs (pendingRate - done->result->sampleRate) > 1.0)
        rebuildForSampleRate();   // device rate changed while we were working
}

void MainComponent::setSong (PlayableSong::Ptr song, const SongInfo& info)
{
    const bool sameSong = hasSong && info.id == currentInfo.id;
    const double keepPos = sameSong ? player.getPositionSeconds() : 0.0;
    const bool keepLoop = sameSong && player.isLooping();
    const double ls = player.getLoopStartSeconds(), le = player.getLoopEndSeconds();

    currentInfo = info;
    hasSong = true;
    player.setSong (song);
    if (sameSong)
    {
        player.seekSeconds (keepPos);
        if (keepLoop) player.setLoop (true, ls, le);
    }

    waveform.setSong (song);
    mixer.setSong (song);
    waveform.stemAudible = mixer.audibleStems();

    songTitle.setText (info.title, juce::dontSendNotification);
    juce::String sub;
    if (info.artist.isNotEmpty()) sub << info.artist << "   ";
    int detected = 0;
    for (auto p : info.present) detected += p ? 1 : 0;
    sub << detected << " stems   " << (int) info.durationSeconds / 60 << ":" << juce::String ((int) info.durationSeconds % 60).paddedLeft ('0', 2);
    if (info.quality == SeparationQuality::maximum) sub << "   Max quality";
    songSub.setText (sub, juce::dontSendNotification);

    settings.setValue ("lastSongId", info.id);
    applyPartChoice();
    updateLoopButton();
    grabKeyboardFocus();
}

// ---- library / export -----------------------------------------------------------------------------------

void MainComponent::showLibrary()
{
    if (libraryWindow != nullptr) { libraryWindow->toFront (true); return; }

    auto* view = new LibraryView();
    view->onOpen = [this] (const SongInfo& info) { openFromLibrary (info); };
    view->onExport = [this] (const SongInfo& info) { exportStems (info); };

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (view);
    o.dialogTitle = "Your Library";
    o.dialogBackgroundColour = theme::panel;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.componentToCentreAround = this;
    libraryWindow = o.launchAsync();
}

void MainComponent::exportStems (const SongInfo& info)
{
    chooser = std::make_unique<juce::FileChooser> ("Choose where to save the stems", lastFolder);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [safe = juce::Component::SafePointer<MainComponent> (this), info] (const juce::FileChooser& fc)
    {
        if (safe == nullptr) return;
        auto dir = fc.getResult();
        if (! dir.isDirectory()) return;
        safe->setStatus ("Exporting stems...");
        juce::Thread::launch ([safe, info, dir]
        {
            auto err = SongLibrary::exportStems (info, dir);
            juce::MessageManager::callAsync ([safe, err, dir]
            {
                if (safe != nullptr)
                    safe->setStatus (err.isEmpty() ? "Stems exported to " + dir.getFullPathName() : err, err.isNotEmpty());
            });
        });
    });
}

// ---- transport ----------------------------------------------------------------------------------------

void MainComponent::togglePlay()
{
    if (! hasSong) { browseForSong(); return; }
    player.togglePlay();
}

void MainComponent::toggleRecord()
{
    if (recorder.isRecording())
    {
        recorder.stop();
        setStatus ("Recording saved to " + recorder.getLastFolder().getFullPathName() + " (Mix, Rig and DI files).");
        return;
    }

    const double sr = engine.getSampleRate();
    if (sr <= 0) { setStatus ("No audio device running.", true); return; }

    auto err = recorder.start (Recorder::defaultFolder(), hasSong ? currentInfo.title : juce::String ("Jam"), sr);
    if (err.isNotEmpty()) { setStatus (err, true); return; }
    if (hasSong && ! player.isPlaying()) player.play();
    setStatus ("Recording... press R or the Rec button again to stop.");
}

void MainComponent::applyPartChoice()
{
    static const StemId parts[] = { StemId::count, StemId::bass, StemId::guitar, StemId::piano, StemId::leadVocals, StemId::backingVocals, StemId::drums };
    const int idx = partBox.getSelectedId() - 1;

    for (int i = 1; i < 7; ++i)
        player.control (parts[i]).mute = false;
    if (idx > 0 && idx < 7)
        player.control (parts[idx]).mute = true;

    mixer.sync();
    waveform.stemAudible = mixer.audibleStems();
}

void MainComponent::updateLoopButton()
{
    loopButton.setToggleState (player.isLooping(), juce::dontSendNotification);
    waveform.repaint();
}

void MainComponent::updateTransportLabels()
{
    const double pos = player.getPositionSeconds();
    const double len = player.getLengthSeconds();
    timeLabel.setText (fmtTime (pos) + "  /  " + fmtTime (len).upToLastOccurrenceOf (".", false, false), juce::dontSendNotification);
    playButton.setButtonText (player.isPlaying() ? "Pause" : "Play");
    speedLabel.setText ("Speed " + juce::String ((int) speed.getValue()) + "%", juce::dontSendNotification);

    const bool rec = recorder.isRecording();
    recordButton.setToggleState (rec, juce::dontSendNotification);
    recordButton.setButtonText (rec ? "Stop " + fmtTime (recorder.getSecondsRecorded()).upToLastOccurrenceOf (".", false, false) : juce::String ("Rec"));
}

void MainComponent::setStatus (const juce::String& text, bool important)
{
    status.setText (text, juce::dontSendNotification);
    status.setColour (juce::Label::textColourId, important ? theme::warn : theme::textDim);
    statusTime = juce::Time::getMillisecondCounterHiRes();
}

// ---- timer --------------------------------------------------------------------------------------------

void MainComponent::timerCallback()
{
    if (job != nullptr)
        overlay.update (job->getProgress(), job->getStage());

    updateTransportLabels();

    // a MIDI keyboard was played before any keys sound was chosen: load the piano
    if (engine.midiNoteSeen.exchange (false) && engine.getKeysInstrument() == nullptr)
    {
        keysPanel.ensureInstrument();
        setStatus ("MIDI keyboard detected: playing Piano Room. Click KEYS to change the sound.");
    }
    waveform.refresh();
    mixer.refresh();

    masterMeterL.setLevel (engine.readOutputPeakL());
    masterMeterR.setLevel (engine.readOutputPeakR());
    if (engine.readLimiterActive()) limiterHold = 30;
    limiterLed.setColour (juce::Label::textColourId, limiterHold > 0 ? theme::warn : theme::textFaint);
    if (limiterHold > 0) --limiterHold;

    static int slow = 0;
    if (++slow % 30 == 0)
    {
        player.releaseOldSongs();
        if (auto* dev = deviceManager.getCurrentAudioDevice())
            juce::ignoreUnused (dev);
    }
}

// ---- keys / drag & drop ------------------------------------------------------------------------------

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    if (overlay.isVisible()) return false;

    if (key == juce::KeyPress::spaceKey) { togglePlay(); return true; }
    if (key == juce::KeyPress::homeKey)  { startButton.triggerClick(); return true; }
    if (key == juce::KeyPress::leftKey)  { player.seekSeconds (juce::jmax (0.0, player.getPositionSeconds() - 5.0)); return true; }
    if (key == juce::KeyPress::rightKey) { player.seekSeconds (juce::jmin (player.getLengthSeconds(), player.getPositionSeconds() + 5.0)); return true; }
    if (key.getTextCharacter() == 'l' || key.getTextCharacter() == 'L') { loopButton.triggerClick(); return true; }
    if (key.getTextCharacter() == 'r' || key.getTextCharacter() == 'R') { toggleRecord(); return true; }
    if (key == juce::KeyPress ('o', juce::ModifierKeys::commandModifier, 0)) { browseForSong(); return true; }
    if (key == juce::KeyPress ('k', juce::ModifierKeys::commandModifier, 0)) { showKeys (! keysPanel.isVisible()); return true; }
    return false;
}

bool MainComponent::isInterestedInFileDrag (const juce::StringArray& files)
{
    return waveform.isInterestedInFileDrag (files);
}

void MainComponent::filesDropped (const juce::StringArray& files, int, int)
{
    if (! files.isEmpty()) openFile (juce::File (files[0]));
}

// ---- help -----------------------------------------------------------------------------------------------

void MainComponent::showHelp()
{
    juce::String t;
    t << "1. Plug your bass, guitar or mic into your USB audio interface.\n"
      << "2. Audio & MIDI (top right): choose the interface's ASIO driver, 48 kHz, 64-128 samples.\n"
      << "3. Pick your input in the rig's INPUT box and a preset (e.g. 'Bass - Vintage Tube').\n"
      << "4. Open Song (or drop an MP3/FLAC on the window). The first time, the AI model (55 MB) downloads.\n"
      << "5. Choose 'I'm playing: Bass' to mute the original bass, press Space and play along.\n\n"
      << "Playing keys? Click KEYS (Ctrl+K): a real piano in a room (or Delay Lama, synths...) from a MIDI keyboard or your computer keys.\n\n"
      << "Shortcuts: Space play/pause, Home restart, Left/Right skip 5 s, L loop, R record, Ctrl+O open, Ctrl+K keys, Ctrl+Shift+O scope.\n"
      << "Drag across the waveform to loop a section; double-click to clear it. Slow tricky parts down with Speed.\n\n"
      << "Amp captures: load any .nam file (free at tone3000.com) in the AMP section. "
      << "Cabinet IRs: load any .wav IR in the CABINET section.\n\n"
      << "Recordings go to Music\\WOMANINSTEM Recordings. Your separated songs live in the Library.";
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "WOMANINSTEM - quick start", t, "Let's play", this);
}

// ---- app shell ------------------------------------------------------------------------------------------

void MainComponent::setActive (bool shouldBeActive)
{
    if (active == shouldBeActive) return;
    active = shouldBeActive;
    if (active)
    {
        deviceManager.addAudioCallback (&engine);
        enableMidi (true);
        rigPanel.refreshInputs();
        startTimerHz (30);
    }
    else
    {
        if (player.isPlaying()) player.pause();
        if (recorder.isRecording()) toggleRecord();
        deviceManager.removeAudioCallback (&engine);
        enableMidi (false);
        engine.keyboardState.allNotesOff (0);
        stopTimer();
    }
}

void MainComponent::enableMidi (bool on)
{
    // MIDI keyboards play the keys (the Studio attaches its own MIDI input while it owns the device)
    deviceManager.removeMidiInputDeviceCallback ({}, &engine.midiCollector);
    if (! on) return;
    for (auto& m : juce::MidiInput::getAvailableDevices())
        if (! deviceManager.isMidiInputDeviceEnabled (m.identifier))
            deviceManager.setMidiInputDeviceEnabled (m.identifier, true);
    deviceManager.addMidiInputDeviceCallback ({}, &engine.midiCollector);
}

void MainComponent::showKeys (bool show)
{
    keysPanel.setVisible (show);
    rigPanel.setVisible (! show);
    keysButton.setVisible (! show);
    if (show) keysPanel.focusKeyboard();
    else grabKeyboardFocus();
}

void MainComponent::openInStudio()
{
    if (! hasSong)
    {
        setStatus ("Open a song first - then this button builds a multitrack Studio project from its stems.", true);
        return;
    }
    if (onOpenInStudio == nullptr) return;
    std::array<bool, numStemIds> muted {};
    const auto audible = mixer.audibleStems();   // takes solo into account
    for (size_t i = 0; i < muted.size(); ++i)
        muted[i] = ! audible[i];
    const auto part = partBox.getSelectedId() > 1 ? partBox.getText() : juce::String();
    onOpenInStudio (currentInfo, muted, rig.createPresetState(), part);
}

// ---- state -----------------------------------------------------------------------------------------------

void MainComponent::saveState()
{
    if (auto xml = deviceManager.createStateXml())
        settings.setValue ("audioState", xml.get());
    if (auto rigXml = rig.createPresetState().createXml())
        settings.setValue ("rigState", rigXml.get());
    settings.setValue ("inputChannel", engine.selectedInput.load());
    settings.setValue ("masterVolume", masterVolume.getValue());
    keysPanel.saveSettings();
    settings.saveIfNeeded();
}

// ---- layout -----------------------------------------------------------------------------------------------

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg);

    // header background
    auto header = getLocalBounds().removeFromTop (64);
    g.setColour (theme::panel);
    g.fillRect (header);
    g.setColour (theme::outline);
    g.drawHorizontalLine (header.getBottom() - 1, 0.0f, (float) getWidth());

    if (embedded)
    {
        g.setColour (theme::panel);
        g.fillRect (0, 64, getWidth(), 48);
        g.setColour (theme::outline);
        g.drawHorizontalLine (111, 0.0f, (float) getWidth());
        return;
    }

    // logo
    auto logo = header.reduced (16, 0).removeFromLeft (170).toFloat();
    g.setFont (uiFont (21.0f, true));
    juce::ColourGradient grad (theme::accent, logo.getX(), 0, theme::accent2, logo.getRight(), 0, false);
    g.setGradientFill (grad);
    g.drawText ("WOMANINSTEM", logo.withY (11.0f).withHeight (26.0f), juce::Justification::centredLeft);
    g.setColour (theme::textFaint);
    g.setFont (uiFont (11.0f));
    g.drawText ("split it. mute it. play it.", logo.withY (36.0f).withHeight (16.0f), juce::Justification::centredLeft);

    // transport strip background
    g.setColour (theme::panel);
    g.fillRect (0, 64, getWidth(), 48);
    g.setColour (theme::outline);
    g.drawHorizontalLine (111, 0.0f, (float) getWidth());
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    overlay.setBounds (r);

    // ---- header (64) ----
    auto header = r.removeFromTop (64).reduced (16, 14);
    if (! embedded) header.removeFromLeft (180);
    openButton.setBounds (header.removeFromLeft (118));
    header.removeFromLeft (8);
    libraryButton.setBounds (header.removeFromLeft (84));
    header.removeFromLeft (8);
    qualityBox.setBounds (header.removeFromLeft (196));
    header.removeFromLeft (20);

    if (! embedded)
    {
        helpButton.setBounds (header.removeFromRight (36));
        header.removeFromRight (8);
        audioButton.setBounds (header.removeFromRight (128));
        header.removeFromRight (12);
    }
    auto meters = header.removeFromRight (130);
    limiterLed.setBounds (meters.removeFromRight (36));
    masterVolume.setBounds (meters.removeFromTop (meters.getHeight() / 2 + 2));
    masterMeterL.setBounds (meters.removeFromTop (5).reduced (6, 0));
    meters.removeFromTop (2);
    masterMeterR.setBounds (meters.removeFromTop (5).reduced (6, 0));
    header.removeFromRight (12);

    auto titleArea = header;
    songTitle.setBounds (titleArea.removeFromTop (titleArea.getHeight() / 2 + 2));
    songSub.setBounds (titleArea);

    // ---- transport (48) ----
    auto tr = r.removeFromTop (48).reduced (16, 8);
    startButton.setBounds (tr.removeFromLeft (40));
    tr.removeFromLeft (6);
    playButton.setBounds (tr.removeFromLeft (86));
    tr.removeFromLeft (6);
    loopButton.setBounds (tr.removeFromLeft (64));
    tr.removeFromLeft (6);
    recordButton.setBounds (tr.removeFromLeft (92));
    tr.removeFromLeft (12);
    timeLabel.setBounds (tr.removeFromLeft (150));
    tr.removeFromLeft (12);

    studioButton.setBounds (tr.removeFromRight (124));
    tr.removeFromRight (8);
    resetMix.setBounds (tr.removeFromRight (90));
    tr.removeFromRight (10);
    partBox.setBounds (tr.removeFromRight (150));
    partLabel.setBounds (tr.removeFromRight (74));
    tr.removeFromRight (14);
    transpose.setBounds (tr.removeFromRight (126));
    transposeLabel.setBounds (tr.removeFromRight (30));
    tr.removeFromRight (14);
    speedLabel.setBounds (tr.removeFromLeft (84));
    speed.setBounds (tr.reduced (0, 2));

    // ---- footer status (24) ----
    auto footer = r.removeFromBottom (24).reduced (16, 0);
    deviceInfo.setBounds (footer.removeFromRight (juce::jmin (620, footer.getWidth() / 2)));
    status.setBounds (footer);

    // ---- rig (bottom) ----
    rigPanel.setBounds (r.removeFromBottom (juce::jmin (rigHeight, r.getHeight() / 2 + 40)));
    keysPanel.setBounds (rigPanel.getBounds());
    keysButton.setBounds (rigPanel.getX() + 118, rigPanel.getY() + 9, 76, 26);

    // ---- lanes ----
    auto lanes = r;
    const int mixerW = juce::jmin (360, lanes.getWidth() / 3);
    mixer.setBounds (lanes.removeFromLeft (mixerW));
    waveform.setBounds (lanes);

    const int rulerH = 22, mixH = 40;
    const int rowH = juce::jmax (30, (r.getHeight() - rulerH - mixH) / numStemIds);
    std::vector<WaveformView::Lane> laneLayout;
    int y = rulerH + mixH;
    for (auto& s : allStems())
    {
        laneLayout.push_back ({ (int) s.id, y, rowH });
        y += rowH;
    }
    waveform.setLanes (laneLayout, rulerH, mixH);
    mixer.layoutRows (rulerH, mixH, rulerH + mixH, rowH);
}

} // namespace wis
