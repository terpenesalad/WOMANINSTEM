#include "Daw/Model/MidiLoops.h"
#include "Daw/Instruments/VintageRhythms.h"
#include "Daw/Instruments/HomeKeys.h"
#include "Daw/Instruments/BeatLab.h"
#include "StudioPage.h"
#include "PluginMenus.h"
#include "Daw/Model/DrumPatterns.h"
#include "Daw/Model/JunkLoops.h"
#include "Daw/Model/VocalChains.h"
#include "Daw/Instruments/Junkyard.h"
#include "Daw/Model/TempoDetect.h"
#include "Daw/Engine/AudioCache.h"
#include "Daw/Instruments/SoundFontInstrument.h"
#include "Separation/AudioFileLoader.h"

namespace wis::daw
{

namespace
{
    class ExportJob : public juce::ThreadWithProgressWindow
    {
    public:
        ExportJob (DawEngine& e, std::vector<DawEngine::ExportOptions> j)
            : ThreadWithProgressWindow ("Exporting...", true, true), engine (e), jobs (std::move (j)) {}

        void run() override
        {
            for (size_t i = 0; i < jobs.size() && ! threadShouldExit(); ++i)
            {
                setStatusMessage ("Exporting " + jobs[i].file.getFileName() + "  (" + juce::String ((int) i + 1) + "/" + juce::String ((int) jobs.size()) + ")");
                error = engine.renderExport (jobs[i], [this, i] (float p)
                {
                    setProgress ((i + p) / (double) jobs.size());
                    return ! threadShouldExit();
                });
                if (error.isNotEmpty()) break;
            }
        }

        DawEngine& engine;
        std::vector<DawEngine::ExportOptions> jobs;
        juce::String error;
    };
}


static constexpr int controlBarHeight = 54;
static constexpr int statusHeight = 22;
static constexpr int browserWidth = 260;

static const char* typingKeys = "awsedftgyhujkolp;'";

// =====================================================================================================
StudioPage::StudioPage (Project& p, DawEngine& e, PluginHost& h, juce::PropertiesFile& s)
    : project (p), engine (e), host (h), settings (s), ctx (p, e, h)
{
    setWantsKeyboardFocus (true);
    installBuiltinEditors();

    ctx.openEditor = [this] (int id) { openEditor (id); };
    ctx.openPluginWindow = [this] (const juce::ValueTree& n) { openPluginWindow (n); };
    ctx.status = [this] (const juce::String& t) { setStatus (t); };
    ctx.showTrackMenu = [this] (int id, juce::Point<int> pos) { trackMenu (id, pos); };

    engine.onSlotRemoved = [this] (const juce::String& id) { closePluginWindow (id); };

    // plugins open in the bottom panel ("Plugin"), like GarageBand's Smart Controls; Pop out floats one
    dock.getProcessor = [this] (const juce::String& id) { return engine.getProcessor (id); };
    dock.onPopOut = [this] (const juce::String& id)
    {
        dockPlugins = false;
        openFloatingPluginWindow (id);
        if (dock.isEmpty()) setBottomPanel (panelBeforeDock == 4 ? 2 : panelBeforeDock);
    };
    dock.onEmpty = [this] { if (bottomPanel == 4) setBottomPanel (panelBeforeDock == 4 ? 2 : panelBeforeDock); };
    dock.onWantsHeight = [this] (int h)
    {
        // grow the bottom panel so the whole editor shows (up to most of the window); never shrink it
        if (bottomPanel != 4 || dock.getHeight() >= h) return;
        const int total = dock.getBottom() - arrangement.getY();
        if (total <= 0) return;
        layout.setItemPosition (1, juce::jmax (140, total - h - 6));
        resized();
    };

    // samples used by the samplers are copied into the song folder, so songs stay self-contained
    BuiltinProcessor::fileToRef = [this] (const juce::File& f) { return project.makeRef (project.importIntoProject (f)); };
    BuiltinProcessor::refToFile = [this] (const juce::String& r) { return project.resolve (r); };
    BuiltinProcessor::onAudioToTrack = [this] (BuiltinProcessor& p, const juce::File& f) { audioFromPlugin (p, f); };
    BeatLab::onPatternToSong = [this] (BeatLab& b, const juce::MidiMessageSequence& seq, double lengthBeats) { patternToSong (b, seq, lengthBeats); };
    engine.onError = [this] (const juce::String& m) { setStatus (m); };
    engine.onRecordingFinished = [this] { setStatus ("Recorded. Press R to record another take, or Cmd/Ctrl+Z to undo it."); };

    controlBar.onProjectMenu = [this] { projectMenu(); };
    controlBar.onToggleBrowser = [this] { toggleBrowser(); };
    controlBar.onToggleEditor = [this] { setBottomPanel (bottomPanel == 1 ? 0 : 1); };
    controlBar.onToggleMixer = [this] { setBottomPanel (bottomPanel == 2 ? 0 : 2); };
    controlBar.onToggleKeys = [this] { setBottomPanel (bottomPanel == 3 ? 0 : 3); };
    controlBar.onTogglePlugins = [this] { setBottomPanel (bottomPanel == 4 ? 0 : 4); };
    controlBar.onToggleTyping = [this]
    {
        typing = ! typing;
        releaseTypingKeys (true);
        setStatus (typing ? "Musical Typing ON: A-' play notes, W E T Y U O P sharps, Z/X octave, C/V velocity. Ctrl+K to turn off."
                          : "Musical Typing off.");
        updatePanelButtons();
        grabKeyboardFocus();
    };
    controlBar.cpuUsage = [this] { return deviceManager != nullptr ? deviceManager->getCpuUsage() : 0.0; };

    arrangement.onAddTrack = [this] { addTrackMenu(); };
    arrangement.onBrowserDrop = [this] (const juce::String& item, int trackId, double beat) { applyBrowserItem (item, trackId, beat); };
    arrangement.onFilesDrop = [this] (const juce::StringArray& files, int trackId, double beat) { importFiles (files, trackId, beat); };
    browser.onActivate = [this] (const juce::String& item) { applyBrowserItem (item, ctx.selectedTrack, engine.getPositionBeats()); };

    keyboard.setAvailableRange (21, 108);
    keyboard.setOctaveForMiddleC (4);   // middle C = C4, same as the piano roll
    keyboard.setLowestVisibleKey (36);
    keyboard.setKeyWidth (22.0f);
    keyboard.setWantsKeyboardFocus (false);
    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, theme::accent);
    keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, theme::accent.withAlpha (0.3f));
    keysHint.setText ("Click the keys, play a USB MIDI keyboard, or turn on Musical Typing (Ctrl+K) and use A S D F G H J K L for white keys, "
                      "W E T Y U O P for black keys, Z / X octave, C / V velocity. Notes go to the selected instrument track.", juce::dontSendNotification);
    keysHint.setFont (uiFont (12.0f));
    keysHint.setColour (juce::Label::textColourId, theme::textDim);

    status.setFont (uiFont (12.0f));
    status.setColour (juce::Label::textColourId, theme::textDim);

    for (juce::Component* c : std::initializer_list<juce::Component*> { &controlBar, &browser, &arrangement, &pianoRoll, &audioEditor, &mixer, &dock, &keyboard, &keysHint, &status, &splitter })
        addChildComponent (c);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &controlBar, &browser, &arrangement, &status, &splitter })
        c->setVisible (true);

    layout.setItemLayout (0, 140, -1.0, -0.64);
    layout.setItemLayout (1, 6, 6, 6);
    layout.setItemLayout (2, 110, -0.8, -0.36);

    showBrowser = settings.getBoolValue ("studioBrowser", true);
    bottomPanel = settings.getIntValue ("studioBottom", 1);
    if (bottomPanel == 4) bottomPanel = 2;   // nothing is docked yet
    dockPlugins = settings.getBoolValue ("studioDockPlugins", true);
    lastFolder = juce::File (settings.getValue ("studioLastFolder", juce::File::getSpecialLocation (juce::File::userMusicDirectory).getFullPathName()));

    project.onLoaded = [this]
    {
        ctx.selectedClips.clear();
        ctx.selectedTrack = project.numTracks() > 0 ? project.track (0).id() : 0;
        engine.selectedTrackId = ctx.selectedTrack;
        pianoRoll.setClip (0);
        audioEditor.setClip (0);
        ctx.pixelsPerBeat = (double) project.tree().getProperty (ids::zoom, 24.0);
        ctx.scrollBeats = 0;
        engine.setPositionBeats ((double) project.tree().getProperty (ids::playhead, 0.0));
        for (auto& [id, w] : pluginWindows) juce::ignoreUnused (id, w);
        pluginWindows.clear();
        dock.clear();
        if (bottomPanel == 4) bottomPanel = panelBeforeDock == 4 ? 2 : panelBeforeDock;
        updatePanelButtons();
        updateTitle();
        resized();
    };

    // open the last project, or start a friendly new one
    const juce::File last (settings.getValue ("lastProject"));
    if (last.existsAsFile() && project.load (last).isEmpty())
        setStatus ("Opened " + last.getFileNameWithoutExtension());
    else
    {
        project.createNew ("My Song");
        auto t = project.addTrack (kindInstrument, "Grand Piano");
        project.setInstrument (t, soundFontRef (0, 0));
        project.undo().clearUndoHistory();
        ctx.selectTrack (t.id());
        project.markClean();
        setStatus ("Welcome to the Studio! Play the piano with your MIDI keyboard or Musical Typing (Ctrl+K), then press R to record.");
    }
    if (ctx.selectedTrack == 0 && project.numTracks() > 0) ctx.selectTrack (project.track (0).id());

    updatePanelButtons();
    startTimerHz (4);
}

StudioPage::~StudioPage()
{
    stopTimer();
    releaseTypingKeys (true);
    pluginWindows.clear();
    dock.clear();
    pluginManagerWindow.reset();
    engine.onSlotRemoved = nullptr;
    BuiltinProcessor::onAudioToTrack = nullptr;
    BeatLab::onPatternToSong = nullptr;
    engine.onError = nullptr;
    engine.onRecordingFinished = nullptr;
    project.onLoaded = nullptr;
}

void StudioPage::setActive (bool active)
{
    if (! active)
    {
        engine.stop();
        releaseTypingKeys (true);
        for (auto& [id, w] : pluginWindows) w->setVisible (false);
    }
    else
    {
        for (auto& [id, w] : pluginWindows) w->setVisible (true);
        browser.refresh();
        grabKeyboardFocus();
    }
}

void StudioPage::saveSettings()
{
    settings.setValue ("studioBrowser", showBrowser);
    settings.setValue ("studioBottom", bottomPanel);
    settings.setValue ("studioDockPlugins", dockPlugins);
    settings.setValue ("studioLastFolder", lastFolder.getFullPathName());
    if (project.hasBeenSaved()) settings.setValue ("lastProject", project.getFile().getFullPathName());
    project.tree().setProperty (ids::zoom, ctx.pixelsPerBeat, nullptr);
    project.tree().setProperty (ids::playhead, engine.getPositionBeats(), nullptr);
    settings.saveIfNeeded();
}

void StudioPage::setStatus (const juce::String& t)
{
    status.setText (t, juce::dontSendNotification);
    statusTime = juce::Time::getMillisecondCounterHiRes();
}

void StudioPage::updateTitle()
{
    if (auto* top = getTopLevelComponent())
        top->setName ("WOMANINSTEM  -  " + project.tree()[ids::name].toString() + (project.isDirty() ? " *" : ""));
}

void StudioPage::timerCallback()
{
    // Scale Lock / Vocal Tune follow the song's key
    if (BuiltinProcessor::songKey.load() != project.key() || BuiltinProcessor::songScale.load() != project.scale())
    {
        BuiltinProcessor::songKey = project.key();
        BuiltinProcessor::songScale = project.scale();
        browser.refresh();
    }
    // autosave a backup every ~2 minutes while there are unsaved changes
    if (project.undo().canUndo()) project.markDirty();
    if (project.isDirty() && ++autosaveTicks > 4 * 120 && ! engine.isRecording())
    {
        autosaveTicks = 0;
        engine.flushPluginStates();
        if (auto xml = project.tree().createXml())
            xml->writeTo (project.getFolder().getChildFile ("Autosave" + Project::fileExtension()));
    }
    updateTitle();
}

// ---- panels ---------------------------------------------------------------------------------------------

void StudioPage::setBottomPanel (int which)
{
    if (which == 4 && bottomPanel != 4) panelBeforeDock = bottomPanel == 0 ? 2 : bottomPanel;
    bottomPanel = which;
    updatePanelButtons();
    resized();
}

void StudioPage::toggleBrowser()
{
    showBrowser = ! showBrowser;
    updatePanelButtons();
    resized();
}

void StudioPage::updatePanelButtons()
{
    controlBar.setPanelStates (showBrowser, bottomPanel == 1, bottomPanel == 2, bottomPanel == 3, bottomPanel == 4, typing);
}

void StudioPage::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg);
}

void StudioPage::resized()
{
    auto r = getLocalBounds();
    controlBar.setBounds (r.removeFromTop (controlBarHeight));
    status.setBounds (r.removeFromBottom (statusHeight).reduced (12, 0));

    browser.setVisible (showBrowser);
    if (showBrowser) browser.setBounds (r.removeFromLeft (browserWidth));

    const bool editorClipIsMidi = project.clipById (pianoRoll.getClipId()).isValid();
    pianoRoll.setVisible (bottomPanel == 1 && (editorClipIsMidi || ! audioEditor.isVisible()));
    audioEditor.setVisible (bottomPanel == 1 && ! pianoRoll.isVisible());
    mixer.setVisible (bottomPanel == 2);
    dock.setVisible (bottomPanel == 4);
    keyboard.setVisible (bottomPanel == 3);
    keysHint.setVisible (bottomPanel == 3);
    splitter.setVisible (bottomPanel != 0);

    if (bottomPanel == 0)
    {
        arrangement.setBounds (r);
        return;
    }

    juce::Component* bottom = bottomPanel == 1 ? (pianoRoll.isVisible() ? (juce::Component*) &pianoRoll : (juce::Component*) &audioEditor)
                            : bottomPanel == 2 ? (juce::Component*) &mixer
                            : bottomPanel == 4 ? (juce::Component*) &dock : nullptr;
    juce::Component dummy;
    juce::Component* comps[] = { &arrangement, &splitter, bottom != nullptr ? bottom : &dummy };
    layout.layOutComponents (comps, 3, r.getX(), r.getY(), r.getWidth(), r.getHeight(), true, true);

    if (bottomPanel == 3)
    {
        auto kr = dummy.getBounds().reduced (10, 8);
        keysHint.setBounds (kr.removeFromTop (34));
        keyboard.setBounds (kr);
        keyboard.setKeyWidth (juce::jlimit (14.0f, 40.0f, kr.getWidth() / 52.0f));
    }
}

// ---- editors / plugin windows ----------------------------------------------------------------------------

void StudioPage::openEditor (int clipId)
{
    auto c = project.clipById (clipId);
    if (! c.isValid()) return;
    if (c.isMidi())
    {
        pianoRoll.setClip (clipId);
        audioEditor.setVisible (false);
        pianoRoll.setVisible (true);
    }
    else
    {
        audioEditor.setClip (clipId);
        pianoRoll.setClip (0);
        pianoRoll.setVisible (false);
        audioEditor.setVisible (true);
    }
    ctx.editorClip = clipId;
    setBottomPanel (1);
}

void StudioPage::openPluginWindow (const juce::ValueTree& node)
{
    const auto slotId = node[ids::id].toString();
    // keep the slot's display name in sync with the instrument's preset
    if (auto* bp = dynamic_cast<BuiltinProcessor*> (engine.getProcessor (slotId)))
    {
        juce::ValueTree n (node);
        bp->onDisplayNameChanged = [n] (const juce::String& name) mutable { n.setProperty (ids::name, name, nullptr); };
    }
    if (pluginWindows.count (slotId) == 0 && dockPlugins && canDock (slotId))
        dockPlugin (slotId);
    else
        openFloatingPluginWindow (slotId);
}

bool StudioPage::canDock (const juce::String& slotId)
{
    // built-in plugins dock; other makers' plugins draw into their own native windows, so they float
    return dynamic_cast<BuiltinProcessor*> (engine.getProcessor (slotId)) != nullptr;
}

juce::String StudioPage::pluginTitle (const juce::String& slotId)
{
    std::function<juce::ValueTree (juce::ValueTree)> find = [&] (juce::ValueTree t) -> juce::ValueTree
    {
        if (t.hasType (ids::PLUGIN) && t[ids::id].toString() == slotId) return t;
        for (auto c : t) if (auto f = find (c); f.isValid()) return f;
        return {};
    };
    auto node = find (project.tree());
    if (! node.isValid()) return "Plugin";
    auto track = Track (node.getParent().getParent());
    return (track.isValid() && track.v.hasType (ids::TRACK) ? track.name() + "  -  " : juce::String ("Master  -  ")) + node[ids::name].toString();
}

void StudioPage::dockPlugin (const juce::String& slotId)
{
    if (engine.getProcessor (slotId) == nullptr) { openFloatingPluginWindow (slotId); return; }   // shows why
    closePluginWindow (slotId);   // an editor can only be in one place
    dockPlugins = true;
    setBottomPanel (4);
    dock.show (slotId, pluginTitle (slotId));
}

void StudioPage::openFloatingPluginWindow (const juce::String& slotId)
{
    if (auto it = pluginWindows.find (slotId); it != pluginWindows.end())
    {
        it->second->setVisible (true);
        it->second->toFront (true);
        return;
    }

    auto* proc = engine.getProcessor (slotId);
    if (proc == nullptr)
    {
        auto err = engine.getSlotError (slotId);
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Plugin", err.isNotEmpty() ? err : juce::String ("The plugin is still loading."));
        return;
    }
    dock.remove (slotId);
    std::function<void()> onDock;
    if (canDock (slotId)) onDock = [this, slotId] { dockPlugin (slotId); };
    pluginWindows[slotId] = std::make_unique<PluginWindow> (pluginTitle (slotId), *proc, [this, slotId] { closePluginWindow (slotId); }, onDock);
}

void StudioPage::closePluginWindow (const juce::String& slotId)
{
    dock.remove (slotId);
    if (auto it = pluginWindows.find (slotId); it != pluginWindows.end())
    {
        auto w = std::move (it->second);
        pluginWindows.erase (it);
        w.reset();
    }
}

// ---- tracks ---------------------------------------------------------------------------------------------

void StudioPage::addTrackMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("New track");
    m.addItem (16, "Piano Room  (real grand, vintage grand and upright pianos, in a room you choose)");
    m.addItem (1, "Software Instrument  (Sound Library: keys, strings, synths...)");
    m.addItem (2, "Drummer  (drum kit + 8 bars of groove)");
    m.addItem (3, "Audio: Guitar or Bass  (with Amp & Pedals)");
    {
        juce::PopupMenu vocals;
        juce::String lastGroup;
        const auto& chains = vocalChains();
        for (int i = 0; i < (int) chains.size(); ++i)
        {
            if (chains[(size_t) i].group != lastGroup) { vocals.addSectionHeader (chains[(size_t) i].group); lastGroup = chains[(size_t) i].group; }
            vocals.addItem (100 + i, chains[(size_t) i].name);
        }
        m.addSubMenu ("Audio: Microphone / Vocals  (pick a vocal sound: studio, folk, psych, weird...)", vocals);
    }
    m.addItem (5, "Audio: Line in / Other");
    m.addItem (6, "Studio Synth");
    m.addItem (7, "HomeKeys 20  (80s keyboard with rhythm box + auto accompaniment)");
    m.addItem (8, "Vintage Rhythm Box  (drum machine + 8 bars of Slow Rock)");
    m.addItem (9, "Sampler  (play / slice any sound)");
    m.addItem (10, "Drum Pads  (16 pads for your own samples)");
    m.addItem (11, "Audio: Loop Station  (looper pedal on your input)");
    m.addItem (15, "Beat Lab  (groovebox: beats, loops, samples, glitch)");
    m.addItem (19, "Junkyard Percussion  (junk, finger snaps, bowed strings, sound spaces)");
    m.addItem (20, "Whistler  (whistling that glides between notes)");
    m.addItem (18, "Plugin from a File...  (a .dll / .vst3 / .clap you downloaded, old 32-bit VSTs too)");
    m.addSeparator();
    m.addItem (12, "Aux Bus: Reverb  (shared reverb you send tracks to)");
    m.addItem (13, "Aux Bus: Delay");
    m.addItem (14, "Aux Bus: Empty  (group / submix)");
    m.showMenuAsync (juce::PopupMenu::Options(), [this] (int r)
    {
        if (r == 0) return;
        if (r == 18) { addPluginFile(); return; }
        const int insertAt = [&] { auto t = project.trackById (ctx.selectedTrack); return t.isValid() ? project.tracks().indexOf (t.v) + 1 : -1; }();
        ctx.beginEdit ("New track");
        Track t;
        if (r >= 100 && r < 100 + (int) vocalChains().size())
        {
            t = project.addTrack (kindAudio, "Vocals", insertAt);
            applyVocalChain (project, t, r - 100);
            t.v.setProperty (ids::arm, true, nullptr);
            ctx.selectTrack (t.id());
            setStatus ("Vocals (" + vocalChains()[(size_t) (r - 100)].name + "): choose your mic input in the track menu (right-click the track), "
                       "then press R to record. Change the sound any time: right-click > Vocal Sound.");
            return;
        }
        switch (r)
        {
            case 1: t = project.addTrack (kindInstrument, "Grand Piano", insertAt); project.setInstrument (t, soundFontRef (0, 0)); break;
            case 2:
            {
                t = project.addTrack (kindInstrument, "Drums", insertAt);
                auto kit = soundFontRef (128, 0); kit.name = "Standard Kit";
                project.setInstrument (t, kit);
                const double bpb = project.beatsPerBar();
                insertDrumPattern (project, t, 0, std::floor (engine.getPositionBeats() / bpb) * bpb, 8, true);
                break;
            }
            case 3:
                t = project.addTrack (kindAudio, "Guitar", insertAt);
                project.setPlugin (t.inserts(), -1, builtinRef ("amprig"));
                t.v.setProperty (ids::monitor, true, nullptr);
                t.v.setProperty (ids::arm, true, nullptr);
                break;
            case 4:
                t = project.addTrack (kindAudio, "Vocals", insertAt);
                applyVocalChain (project, t, defaultVocalChain);
                t.v.setProperty (ids::arm, true, nullptr);
                break;
            case 19:
            {
                t = project.addTrack (kindInstrument, "Junkyard", insertAt);
                project.setInstrument (t, builtinPresetRef ("junkyard", 0));
                break;
            }
            case 20: t = project.addTrack (kindInstrument, "Whistler", insertAt); project.setInstrument (t, builtinPresetRef ("whistle", 0)); break;
            case 5: t = project.addTrack (kindAudio, "Audio", insertAt); t.v.setProperty (ids::arm, true, nullptr); break;
            case 6: t = project.addTrack (kindInstrument, "Synth", insertAt); project.setInstrument (t, synthRef (1)); break;
            case 7: t = project.addTrack (kindInstrument, "HomeKeys 20", insertAt); project.setInstrument (t, builtinPresetRef ("homekeys", 0)); break;
            case 8:
            {
                t = project.addTrack (kindInstrument, "Rhythm Box", insertAt);
                project.setInstrument (t, builtinPresetRef ("rhythmbox", 0));
                const double bpb = project.beatsPerBar();
                insertVintageRhythm (project, t, 0, std::floor (engine.getPositionBeats() / bpb) * bpb, 8);
                break;
            }
            case 9: t = project.addTrack (kindInstrument, "Sampler", insertAt); project.setInstrument (t, builtinRef ("sampler")); break;
            case 10: t = project.addTrack (kindInstrument, "Drum Pads", insertAt); project.setInstrument (t, builtinPresetRef ("drumpads", 0)); break;
            case 15: t = project.addTrack (kindInstrument, "Beat Lab", insertAt); project.setInstrument (t, builtinPresetRef ("beatlab", 0)); break;
            case 16: t = project.addTrack (kindInstrument, "Piano Room", insertAt); project.setInstrument (t, builtinPresetRef ("piano", 0)); break;
            case 11:
                t = project.addTrack (kindAudio, "Loop Station", insertAt);
                project.setPlugin (t.inserts(), -1, builtinRef ("looper"));
                t.v.setProperty (ids::monitor, true, nullptr);
                break;
            case 12: case 13: case 14:
            {
                t = project.addBus (r == 12 ? "Reverb Bus" : r == 13 ? "Delay Bus" : "Aux Bus", insertAt);
                if (r != 14)
                {
                    const char* fxId = r == 12 ? "reverb" : "delay";
                    auto ref = builtinRef (fxId);
                    if (auto proto = createBuiltin (fxId)) { proto->setParam ("mix", 1.0f); ref.state = encodeState (*proto); }
                    project.setPlugin (t.inserts(), -1, ref);
                }
                break;
            }
            default: return;
        }
        ctx.selectTrack (t.id());
        if (r == 16)
        {
            engine.rebuildNow();
            openPluginWindow (t.instrument());
            setStatus ("Piano Room ready: play your MIDI keyboard (or Ctrl+K for Musical Typing); pick a piano, a preset and a room in its window.");
        }
        else if (r == 15)
        {
            engine.rebuildNow();
            openPluginWindow (t.instrument());
            setStatus ("Beat Lab: press Play in it (or play the song), click steps to make a beat, drop loops and samples onto lanes.");
        }
        else if (r == 19)
        {
            engine.rebuildNow();
            openPluginWindow (t.instrument());
            setStatus ("Junkyard: every key is a different thing (snaps and slaps low, wood, metal, then sound-space textures high up). "
                       "Library > Drums has finger-snap loops, junk grooves and sound spaces for it.");
        }
        else if (r == 20)
            setStatus ("Whistler: play legato (hold one key while pressing the next) and it glides between the notes.");
        else if (r >= 12)
            setStatus ("Bus added. Send tracks to it from the mixer (+ Send) or route a track's output into it.");
        else if (r == 11)
        {
            engine.rebuildNow();
            openPluginWindow (t.inserts().getChild (0));
            setStatus ("Loop Station: choose the input in the track menu, then hit the big button to record a loop.");
        }
        else if (r == 7 || r == 8)
            setStatus ("Press play: the rhythm runs with the song. Pick rhythms and tones on the HomeKeys panel (double-click the instrument) or Library > Drums.");
        else if (r >= 3 && r <= 5)
            setStatus ("Choose the input for \"" + t.name() + "\" in its track menu (right-click the track), then press R to record.");
        else if (r == 2)
            setStatus ("Drummer added. Swap the groove from Library > Drums, or double-click the clip to edit the beat.");
        else
            setStatus ("Play it with your MIDI keyboard or Musical Typing (Ctrl+K). Pick another sound in Library > Sounds.");
    });
}

void StudioPage::trackMenu (int trackId, juce::Point<int> screenPos)
{
    auto t = project.trackById (trackId);
    if (! t.isValid()) return;
    ctx.selectTrack (trackId);

    juce::PopupMenu m;
    m.addSectionHeader (t.name());
    m.addItem (1, "Rename...");
    juce::PopupMenu colours;
    for (int i = 0; i < 12; ++i) colours.addColouredItem (100 + i, "Colour " + juce::String (i + 1), trackColourForIndex (i));
    m.addSubMenu ("Colour", colours);
    m.addItem (2, "Duplicate Track");
    m.addItem (3, (bool) t.v[ids::showAutomation] ? "Hide Automation" : "Show Automation (volume, pan, sends, plugin knobs)");
    juce::PopupMenu heights;
    heights.addItem (10, "Small", true, (int) t.v[ids::height] <= 48);
    heights.addItem (11, "Medium", true, (int) t.v[ids::height] > 48 && (int) t.v[ids::height] <= 80);
    heights.addItem (12, "Large", true, (int) t.v[ids::height] > 80);
    m.addSubMenu ("Track Height", heights);

    PluginMenu fx = PluginMenu::effects (host);
    m.addSubMenu ("Add Effect", fx.menu);
    PluginMenu inst = PluginMenu::instruments (host);
    PluginMenu mfx = PluginMenu::midiEffects (host);
    if (t.isInstrument())
    {
        m.addSubMenu ("Add MIDI Effect (arpeggiator...)", mfx.menu);
        m.addSubMenu ("Instrument", inst.menu);
        m.addItem (4, "Open Instrument");
    }
    if (! t.isBus())
        m.addItem (6, "Bounce in Place (render to a new audio track)");
    if (t.isAudio())
    {
        juce::PopupMenu inputs;
        juce::StringArray names;
        if (deviceManager != nullptr)
            if (auto* dev = deviceManager->getCurrentAudioDevice())
            {
                auto all = dev->getInputChannelNames();
                auto active = dev->getActiveInputChannels();
                for (int ch = 0; ch < all.size(); ++ch) if (active[ch]) names.add (all[ch]);
            }
        if (names.isEmpty()) inputs.addItem (-1, "No inputs - enable them in Audio Settings", false);
        for (int i = 0; i < names.size(); ++i)
            inputs.addItem (200 + i, names[i] + " (mono)", true, (int) t.v[ids::input] == i && ! (bool) t.v[ids::inputStereo]);
        for (int i = 0; i + 1 < names.size(); i += 2)
            inputs.addItem (300 + i, names[i] + " + " + names[i + 1] + " (stereo)", true, (int) t.v[ids::input] == i && (bool) t.v[ids::inputStereo]);
        m.addSubMenu ("Input", inputs);
        m.addItem (5, "Input Monitoring", true, (bool) t.v[ids::monitor]);
        juce::PopupMenu vocals;
        juce::String lastGroup;
        const auto& chains = vocalChains();
        for (int i = 0; i < (int) chains.size(); ++i)
        {
            if (chains[(size_t) i].group != lastGroup) { vocals.addSectionHeader (chains[(size_t) i].group); lastGroup = chains[(size_t) i].group; }
            vocals.addItem (500 + i, chains[(size_t) i].name);
        }
        m.addSubMenu ("Vocal Sound (replaces this track's effects)", vocals);
    }
    m.addSeparator();
    m.addItem (9, "Delete Track");

    auto fxRefs = fx.refs;
    auto instRefs = inst.refs;
    auto mfxRefs = mfx.refs;
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ screenPos.x, screenPos.y, 1, 1 }),
                     [this, trackId, fxRefs, instRefs, mfxRefs] (int r)
    {
        auto tr = project.trackById (trackId);
        if (! tr.isValid() || r == 0) return;
        if (r == 1)
        {
            auto* w = new juce::AlertWindow ("Rename track", {}, juce::MessageBoxIconType::NoIcon, this);
            w->addTextEditor ("name", tr.name());
            w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            auto v = tr.v;
            w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, v] (int res) mutable
            {
                if (res == 1 && w->getTextEditorContents ("name").trim().isNotEmpty())
                { ctx.beginEdit ("Rename"); v.setProperty (ids::name, w->getTextEditorContents ("name").trim(), project.um()); }
            }), true);
        }
        else if (r == 2) { ctx.beginEdit ("Duplicate track"); engine.flushPluginStates(); ctx.selectTrack (project.duplicateTrack (tr).id()); }
        else if (r == 3) tr.v.setProperty (ids::showAutomation, ! (bool) tr.v[ids::showAutomation], nullptr);
        else if (r == 4) { if (tr.instrument().isValid()) openPluginWindow (tr.instrument()); }
        else if (r == 5) tr.v.setProperty (ids::monitor, ! (bool) tr.v[ids::monitor], nullptr);
        else if (r == 6) bounceInPlace (trackId);
        else if (r == 9) { engine.flushPluginStates(); ctx.beginEdit ("Delete track"); project.removeTrack (tr); setStatus ("Track deleted (Ctrl+Z to undo)."); }
        else if (r >= 10 && r <= 12) { ctx.beginEdit ("Track height"); tr.v.setProperty (ids::height, r == 10 ? 44 : r == 11 ? 72 : 120, project.um()); }
        else if (r >= 100 && r < 112) { ctx.beginEdit ("Colour"); tr.v.setProperty (ids::colour, (juce::int64) trackColourForIndex (r - 100).getARGB(), project.um()); }
        else if (r >= 200 && r < 300) { tr.v.setProperty (ids::input, r - 200, nullptr); tr.v.setProperty (ids::inputStereo, false, nullptr); }
        else if (r >= 300 && r < 400) { tr.v.setProperty (ids::input, r - 300, nullptr); tr.v.setProperty (ids::inputStereo, true, nullptr); }
        else if (r >= 500 && r < 500 + (int) vocalChains().size())
        {
            engine.flushPluginStates();
            ctx.beginEdit ("Vocal sound");
            applyVocalChain (project, tr, r - 500);
            engine.rebuildNow();
            setStatus (tr.name() + ": " + vocalChains()[(size_t) (r - 500)].name + " - " + vocalChains()[(size_t) (r - 500)].description + ".");
        }
        else if (auto it = fxRefs.find (r); it != fxRefs.end())
        {
            ctx.beginEdit ("Add effect");
            project.setPlugin (tr.inserts(), -1, it->second);
            engine.rebuildNow();
            openPluginWindow (tr.inserts().getChild (tr.inserts().getNumChildren() - 1));
        }
        else if (auto it2 = instRefs.find (r); it2 != instRefs.end())
        {
            ctx.beginEdit ("Change instrument");
            project.setInstrument (tr, PluginMenu::resolve (it2->second));
        }
        else if (auto it3 = mfxRefs.find (r); it3 != mfxRefs.end() && tr.midiFx().isValid())
        {
            ctx.beginEdit ("Add MIDI effect");
            project.setPlugin (tr.midiFx(), -1, PluginMenu::resolve (it3->second));
            engine.rebuildNow();
            openPluginWindow (tr.midiFx().getChild (tr.midiFx().getNumChildren() - 1));
        }
    });
}

void StudioPage::bounceInPlace (int trackId)
{
    auto t = project.trackById (trackId);
    if (! t.isValid() || t.isBus()) return;
    double end = 0.0;
    for (auto c : t.clips()) end = juce::jmax (end, Clip (c).endBeats (project.tempo()));
    if (end <= 0.0) { setStatus ("Nothing to bounce on \"" + t.name() + "\"."); return; }
    const double bpb = project.beatsPerBar();
    end = std::ceil (end / bpb) * bpb;

    engine.flushPluginStates();
    DawEngine::ExportOptions o;
    o.file = project.audioFolder().getNonexistentChildFile (juce::File::createLegalFileName (t.name() + " (bounce)"), ".wav");
    o.format = 0;
    o.bitDepth = 24;
    o.startBeat = 0.0;
    o.endBeat = end;
    o.tailSeconds = 2.0;
    o.onlyTracks.add (trackId);
    o.bounce = true;

    engine.beginExport();
    ExportJob job (engine, { o });
    const bool finished = job.runThread();
    engine.endExport();
    if (! finished || job.error.isNotEmpty()) { setStatus (job.error.isNotEmpty() ? job.error : juce::String ("Bounce cancelled.")); return; }

    ctx.beginEdit ("Bounce in place");
    auto b = project.addTrack (kindAudio, t.name() + " (bounced)", project.tracks().indexOf (t.v) + 1);
    b.v.setProperty (ids::colour, t.v[ids::colour], project.um());
    b.v.setProperty (ids::volume, t.v[ids::volume], project.um());
    b.v.setProperty (ids::pan, t.v[ids::pan], project.um());
    b.v.setProperty (ids::output, t.output(), project.um());
    for (auto s : t.sends()) project.setSend (b, (int) s[ids::bus], (float) (double) s[ids::level], (bool) s[ids::pre]);
    project.addAudioClip (b, o.file, 0.0, 0.0, AudioCache::fileLengthSeconds (o.file), t.name());
    t.v.setProperty (ids::mute, true, project.um());
    ctx.selectTrack (b.id());
    setStatus ("Bounced \"" + t.name() + "\" to audio (the original is muted, not deleted - Ctrl+Z undoes it).");
}

void StudioPage::audioFromPlugin (BuiltinProcessor& p, const juce::File& f)
{
    Track owner;
    for (auto tv : project.tracks())
    {
        Track t (tv);
        for (auto n : t.inserts()) if (engine.getProcessor (n[ids::id].toString()) == &p) owner = t;
    }
    ctx.beginEdit ("Add loop");
    auto copy = project.importIntoProject (f);
    if (copy != f) f.deleteFile();
    auto target = owner.isValid() && owner.isAudio() ? owner : project.addTrack (kindAudio, "Loop", owner.isValid() ? project.tracks().indexOf (owner.v) + 1 : -1);
    const double bpb = project.beatsPerBar();
    const double at = std::floor (engine.getPositionBeats() / bpb) * bpb;
    auto c = project.addAudioClip (target, copy, at, 0.0, AudioCache::fileLengthSeconds (copy), "Loop");
    ctx.selectTrack (target.id());
    ctx.selectClip (c.id(), false);
    setStatus ("The loop is on \"" + target.name() + "\" at bar " + juce::String ((int) (at / bpb) + 1) + ". Clear the Loop Station if you don't want to hear it twice.");
}

void StudioPage::patternToSong (BeatLab& b, const juce::MidiMessageSequence& seq, double lengthBeats)
{
    Track owner;
    for (auto tv : project.tracks())
    {
        Track t (tv);
        if (t.instrument().isValid() && engine.getProcessor (t.instrument()[ids::id].toString()) == &b) owner = t;
    }
    if (! owner.isValid()) { setStatus ("Couldn't find the Beat Lab's track."); return; }
    ctx.beginEdit ("Beat Lab pattern to song");
    const double bpb = project.beatsPerBar();
    const double at = std::floor (engine.getPositionBeats() / bpb) * bpb;
    const double len = std::ceil (lengthBeats / bpb - 1.0e-9) * bpb;
    auto c = project.addMidiClip (owner, at, len, "Beat Lab " + juce::String::charToString ((juce::juce_wchar) ('A' + b.getPatternEditing())));
    for (int i = 0; i < seq.getNumEvents(); ++i)
    {
        auto* ev = seq.getEventPointer (i);
        if (! ev->message.isNoteOn()) continue;
        const double start = ev->message.getTimeStamp();
        const double end = ev->noteOffObject != nullptr ? ev->noteOffObject->message.getTimeStamp() : start + 0.1;
        project.addNote (c, ev->message.getNoteNumber(), start, juce::jmax (0.01, end - start), juce::jlimit (1, 127, (int) ev->message.getVelocity()));
    }
    // the clip plays it now; pause the Beat Lab's own sequencer so it doesn't play twice
    b.setParam ("sync", 0.0f);
    if (b.isRunning()) b.startStop();
    ctx.selectTrack (owner.id());
    ctx.selectClip (c.id(), false);
    setStatus ("Pattern added at bar " + juce::String ((int) (at / bpb) + 1) + " on \"" + owner.name() + "\". Loop or copy the clip to make a song.");
}

Track StudioPage::trackForInstrument (int trackId, const juce::String& name)
{
    auto t = project.trackById (trackId);
    if (t.isValid() && t.isInstrument()) return t;
    const int insertAt = t.isValid() ? project.tracks().indexOf (t.v) + 1 : -1;
    return project.addTrack (kindInstrument, name, insertAt);
}

void StudioPage::applyBrowserItem (const juce::String& item, int trackId, double beat)
{
    const auto kind = item.upToFirstOccurrenceOf (":", false, false);
    const auto rest = item.fromFirstOccurrenceOf (":", false, false);

    if (kind == "sound" || kind == "synth" || kind == "vsti")
    {
        PluginRef ref;
        if (kind == "sound")
        {
            const int bank = rest.upToFirstOccurrenceOf (":", false, false).getIntValue();
            const int prog = rest.fromFirstOccurrenceOf (":", false, false).getIntValue();
            ref = soundFontRef (bank, prog);
            if (auto sf = SoundFontCache::defaultSoundFont(); sf.existsAsFile())
                for (auto& p : SoundFontCache::get().presetsFor (sf))
                    if (p.bank == bank && p.program == prog) ref.name = p.name;
        }
        else if (kind == "synth") ref = synthRef (rest.getIntValue());
        else
        {
            auto d = host.known.getTypeForIdentifierString (rest);
            if (d == nullptr) return;
            ref = PluginHost::refFor (*d);
        }
        ctx.beginEdit ("Choose instrument");
        auto t = trackForInstrument (trackId, ref.name);
        project.setInstrument (t, ref);
        if (t.clips().getNumChildren() == 0) t.v.setProperty (ids::name, ref.name, project.um());
        ctx.selectTrack (t.id());
        setStatus (ref.name + " is ready - play it with your keyboard or Musical Typing.");
    }
    else if (kind == "inst")
    {
        const auto id = rest.upToFirstOccurrenceOf (":", false, false);
        const int preset = rest.fromFirstOccurrenceOf (":", false, false).getIntValue();
        auto ref = preset >= 0 ? builtinPresetRef (id, preset) : builtinRef (id);
        ctx.beginEdit ("Choose instrument");
        const auto trackName = id == "homekeys" ? juce::String ("HomeKeys 20") : ref.name;
        auto t = trackForInstrument (trackId, trackName);
        project.setInstrument (t, ref);
        if (t.clips().getNumChildren() == 0) t.v.setProperty (ids::name, trackName, project.um());
        ctx.selectTrack (t.id());
        engine.rebuildNow();
        if (id == "sampler" || id == "drumpads" || id == "homekeys" || id == "beatlab" || id == "piano" || id == "junkyard") openPluginWindow (t.instrument());
        setStatus (id == "sampler" ? juce::String ("Sampler ready: drop an audio file onto it, then play it from your keyboard.")
                 : id == "homekeys" ? juce::String ("HomeKeys 20 ready: play along with its rhythm box (press play), or turn on Auto Accompaniment.")
                 : id == "beatlab" ? juce::String ("Beat Lab ready: press Play in it (or play the song), click steps, drop loops onto lanes.")
                 : ref.name + " is ready.");
    }
    else if (kind == "rhythm")
    {
        const int index = rest.getIntValue();
        const auto& rh = vintageRhythms()[(size_t) juce::jlimit (0, (int) vintageRhythms().size() - 1, index)];
        auto sel = project.trackById (trackId);
        // on a HomeKeys track, just switch its built-in rhythm
        if (sel.isValid() && sel.instrument()[ids::uid].toString() == "homekeys")
        {
            if (auto* hk = dynamic_cast<BuiltinProcessor*> (engine.getProcessor (sel.instrument()[ids::id].toString())))
            {
                hk->setParam ("rhythm", (float) index);
                setStatus ("HomeKeys rhythm: " + rh.name + " (it plays while the song plays).");
                return;
            }
        }
        ctx.beginEdit ("Add rhythm");
        auto t = sel;
        if (! t.isValid() || t.instrument()[ids::uid].toString() != "rhythmbox")
        {
            t = Track();
            for (auto tv : project.tracks()) if (Track (tv).instrument()[ids::uid].toString() == "rhythmbox") { t = Track (tv); break; }
        }
        if (! t.isValid())
        {
            t = project.addTrack (kindInstrument, "Rhythm Box");
            project.setInstrument (t, builtinPresetRef ("rhythmbox", 0));
        }
        const double bpb = project.beatsPerBar();
        auto c = insertVintageRhythm (project, t, index, std::floor (beat / bpb) * bpb, 8);
        ctx.selectTrack (t.id());
        ctx.selectClip (c.id(), false);
        setStatus ("Added 8 bars of " + rh.name + ". It sounds right at about " + juce::String ((int) rh.tempo) + " BPM"
                   + (rh.beats == 3 ? " in 3/4." : "."));
    }
    else if (kind == "loop")
    {
        const int prog = rest.upToFirstOccurrenceOf (":", false, false).getIntValue();
        const int style = rest.fromFirstOccurrenceOf (":", false, false).getIntValue();
        const auto& st = loopStyles()[(size_t) juce::jlimit (0, (int) loopStyles().size() - 1, style)];
        ctx.beginEdit ("Add MIDI loop");
        auto t = project.trackById (trackId);
        if (! t.isValid() || ! t.isInstrument())
        {
            const int insertAt = t.isValid() ? project.tracks().indexOf (t.v) + 1 : -1;
            auto ref = st.instrument();
            t = project.addTrack (kindInstrument, st.category == "Bass" ? juce::String ("Bass") : ref.name, insertAt);
            project.setInstrument (t, ref);
        }
        const double bpb = project.beatsPerBar();
        auto c = insertMidiLoop (project, t, prog, style, std::floor (beat / bpb) * bpb);
        ctx.selectTrack (t.id());
        ctx.selectClip (c.id(), false);
        setStatus ("Added " + describeProgression (project, prog) + " (" + st.name + "). Double-click the clip to edit the notes.");
    }
    else if (kind == "junk")
    {
        const int index = rest.getIntValue();
        const auto& loops = junkLoops();
        if (! juce::isPositiveAndBelow (index, (int) loops.size())) return;
        const auto& loop = loops[(size_t) index];
        // a Junkyard track playing its kit (not one of its single-instrument sources)
        auto isKit = [this] (const Track& tr)
        {
            if (! tr.isValid() || ! tr.isInstrument() || tr.instrument()[ids::uid].toString() != "junkyard") return false;
            if (auto* p = dynamic_cast<BuiltinProcessor*> (engine.getProcessor (tr.instrument()[ids::id].toString())))
                return (int) p->param ("source") == 0;
            return true;
        };
        ctx.beginEdit ("Add " + loop.group);
        auto t = project.trackById (trackId);
        const bool space = ! loop.events.empty();
        if (! isKit (t) || (space && t.clips().getNumChildren() > 0 && ! t.name().containsIgnoreCase ("space")))
        {
            // sound spaces get a track (and a room) of their own; loops reuse a free kit track
            t = Track();
            if (! space)
                for (auto tv : project.tracks()) if (isKit (Track (tv)) && ! Track (tv).name().containsIgnoreCase ("space")) { t = Track (tv); break; }
        }
        if (! t.isValid())
        {
            const auto sel = project.trackById (trackId);
            const int insertAt = sel.isValid() ? project.tracks().indexOf (sel.v) + 1 : -1;
            const auto name = space ? juce::String ("Sound Space") : loop.group == "Finger Snaps" ? juce::String ("Finger Snaps") : juce::String ("Junkyard");
            t = project.addTrack (kindInstrument, name, insertAt);
            project.setInstrument (t, builtinPresetRef ("junkyard", loop.preset));
        }
        const double bpb = project.beatsPerBar();
        auto c = insertJunkLoop (project, t, index, std::floor (beat / bpb) * bpb, 8);
        ctx.selectTrack (t.id());
        ctx.selectClip (c.id(), false);
        setStatus (space ? "Added the \"" + loop.name + "\" sound space (8 bars). Stretch or loop the clip; edit the notes to move things around."
                         : "Added 8 bars of " + loop.name + ". It's MIDI, so it follows the song's tempo (it feels good around "
                           + juce::String ((int) loop.suggestedTempo) + " BPM).");
    }
    else if (kind == "vchain")
    {
        const int index = rest.getIntValue();
        if (! juce::isPositiveAndBelow (index, (int) vocalChains().size())) return;
        auto t = project.trackById (trackId != 0 ? trackId : ctx.selectedTrack);
        engine.flushPluginStates();
        ctx.beginEdit ("Vocal sound");
        if (! t.isValid() || ! t.isAudio())
        {
            t = project.addTrack (kindAudio, "Vocals");
            t.v.setProperty (ids::arm, true, nullptr);
        }
        applyVocalChain (project, t, index);
        engine.rebuildNow();
        ctx.selectTrack (t.id());
        setStatus (t.name() + ": " + vocalChains()[(size_t) index].name + " - " + vocalChains()[(size_t) index].description + ".");
    }
    else if (kind == "pattern")
    {
        ctx.beginEdit ("Add drum groove");
        auto isDrums = [] (const Track& tr)
        {
            if (! tr.isValid() || ! tr.isInstrument()) return false;
            const auto inst = tr.instrument()[ids::name].toString() + " " + tr.name();
            return inst.containsIgnoreCase ("drum") || inst.containsIgnoreCase ("kit") || inst.containsIgnoreCase ("perc");
        };
        auto t = project.trackById (trackId);
        if (! isDrums (t))   // never put a groove on the piano: use a drum track, or make one
        {
            t = Track();
            for (auto tv : project.tracks()) if (isDrums (Track (tv))) { t = Track (tv); break; }
        }
        if (! t.isValid())
        {
            t = project.addTrack (kindInstrument, "Drums");
            auto kit = soundFontRef (128, 0); kit.name = "Standard Kit";
            project.setInstrument (t, kit);
        }
        const double bpb = project.beatsPerBar();
        const double start = std::floor (beat / bpb) * bpb;
        auto c = insertDrumPattern (project, t, rest.getIntValue(), start, 8, true);
        ctx.selectTrack (t.id());
        ctx.selectClip (c.id(), false);
        setStatus ("Added 8 bars of " + c.name() + ". Tip: the groove's feel suits about "
                   + juce::String ((int) drumPatterns()[rest.getIntValue()].suggestedTempo) + " BPM.");
    }
    else if (kind == "fx" || kind == "vstfx")
    {
        auto t = project.trackById (trackId != 0 ? trackId : ctx.selectedTrack);
        PluginRef ref;
        if (kind == "fx") ref = builtinRef (rest);
        else if (auto d = host.known.getTypeForIdentifierString (rest)) ref = PluginHost::refFor (*d);
        else return;
        if (auto* info = findBuiltin (rest); kind == "fx" && info != nullptr && info->midiFx)
        {
            if (! t.isValid() || ! t.isInstrument() || ! t.midiFx().isValid())
            {
                setStatus (ref.name + " is a MIDI effect: put it on an instrument track.");
                return;
            }
            ctx.beginEdit ("Add MIDI effect");
            project.setPlugin (t.midiFx(), -1, ref);
            engine.rebuildNow();
            openPluginWindow (t.midiFx().getChild (t.midiFx().getNumChildren() - 1));
            setStatus (ref.name + " added before " + t.name() + "'s instrument.");
            return;
        }
        ctx.beginEdit ("Add effect");
        auto parent = t.isValid() ? t.inserts() : project.masterInserts();
        project.setPlugin (parent, -1, ref);
        engine.rebuildNow();
        openPluginWindow (parent.getChild (parent.getNumChildren() - 1));
        setStatus (ref.name + " added to " + (t.isValid() ? t.name() : juce::String ("the master bus")) + ".");
    }
    else if (kind == "song" || kind == "stem")
    {
        const auto songId = rest.upToFirstOccurrenceOf (":", false, false);
        auto song = SongLibrary::findSong (songId);
        if (! song) return;
        ctx.beginEdit ("Add stems");
        const auto stemKey = rest.fromFirstOccurrenceOf (":", false, false);
        int added = 0;
        for (auto& st : wis::allStems())
        {
            if (! song->present[(size_t) st.id]) continue;
            if (kind == "stem" && stemKey != st.key) continue;
            auto f = project.importIntoProject (song->stemFile (st.id));
            auto t = project.addTrack (kindAudio, st.displayName);
            t.v.setProperty (ids::colour, (juce::int64) wis::stemColour (st.id).getARGB(), nullptr);
            project.addAudioClip (t, f, beat, 0.0, AudioCache::fileLengthSeconds (f), song->title + " - " + st.displayName);
            ++added;
            ctx.selectTrack (t.id());
        }
        setStatus ("Added " + juce::String (added) + " stem" + (added == 1 ? "" : "s") + " from " + song->displayName() + ".");
    }
}

void StudioPage::importFiles (const juce::StringArray& files, int trackId, double beat)
{
    ctx.beginEdit ("Import");
    int imported = 0;
    for (auto& path : files)
    {
        juce::File f (path);
        if (f.hasFileExtension ("wisproj")) { openProjectFile (f); return; }
        if (f.hasFileExtension ("mid;midi"))
        {
            auto err = project.importMidiFile (f, beat, project.numTracks() == 0);
            if (err.isNotEmpty()) setStatus (err); else ++imported;
            continue;
        }
        const double len = AudioCache::fileLengthSeconds (f);
        if (len <= 0) { setStatus ("Can't read " + f.getFileName()); continue; }
        auto copy = project.importIntoProject (f);
        auto t = project.trackById (trackId);
        if (! t.isValid() || ! t.isAudio() || imported > 0)
            t = project.addTrack (kindAudio, f.getFileNameWithoutExtension());
        project.addAudioClip (t, copy, beat, 0.0, len);
        ++imported;
    }
    if (imported > 0) setStatus ("Imported " + juce::String (imported) + " file" + (imported == 1 ? "" : "s") + ".");
}

// ---- editing ---------------------------------------------------------------------------------------------

void StudioPage::deleteSelectedClips()
{
    if (ctx.selectedClips.isEmpty()) return;
    ctx.beginEdit ("Delete");
    for (int id : ctx.selectedClips) if (auto c = project.clipById (id); c.isValid()) project.deleteClip (c);
    ctx.clearClipSelection();
}

void StudioPage::duplicateSelectedClips()
{
    if (ctx.selectedClips.isEmpty()) return;
    ctx.beginEdit ("Duplicate");
    const double tempo = project.tempo();
    double lo = 1e9, hi = 0;
    for (int id : ctx.selectedClips)
        if (auto c = project.clipById (id); c.isValid()) { lo = juce::jmin (lo, c.start()); hi = juce::jmax (hi, c.endBeats (tempo)); }
    juce::Array<int> fresh;
    for (int id : ctx.selectedClips)
        if (auto c = project.clipById (id); c.isValid()) fresh.add (project.duplicateClip (c, c.start() + (hi - lo)).id());
    ctx.selectedClips = fresh;
    ctx.sendChangeMessage();
}

void StudioPage::splitAtPlayhead()
{
    const double at = engine.getPositionBeats();
    ctx.beginEdit ("Split");
    int n = 0;
    auto targets = ctx.selectedClips;
    if (targets.isEmpty())   // nothing selected: split whatever is under the playhead on the selected track
        if (auto t = project.trackById (ctx.selectedTrack); t.isValid())
            for (auto cv : t.clips()) targets.add ((int) cv[ids::id]);
    for (int id : targets)
        if (auto c = project.clipById (id); c.isValid() && project.splitClip (c, at).isValid()) ++n;
    setStatus (n > 0 ? "Split " + juce::String (n) + " clip" + (n == 1 ? "" : "s") + " at the playhead." : juce::String ("Nothing under the playhead to split."));
}

void StudioPage::copySelected()
{
    clipboard.clear();
    for (int id : ctx.selectedClips)
        if (auto c = project.clipById (id); c.isValid())
        {
            auto copy = c.v.createCopy();
            copy.setProperty ("srcTrack", project.tracks().indexOf (project.trackForClip (c.v).v), nullptr);
            clipboard.add (copy);
        }
    if (! clipboard.isEmpty()) setStatus ("Copied " + juce::String (clipboard.size()) + " clip(s).");
}

void StudioPage::paste()
{
    if (clipboard.isEmpty()) return;
    double lo = 1e9;
    int topTrack = 1 << 30;
    for (auto& c : clipboard) { lo = juce::jmin (lo, (double) c[ids::start]); topTrack = juce::jmin (topTrack, (int) c["srcTrack"]); }
    auto sel = project.trackById (ctx.selectedTrack);
    const int base = sel.isValid() ? project.tracks().indexOf (sel.v) : topTrack;
    const double at = engine.getPositionBeats();
    ctx.beginEdit ("Paste");
    juce::Array<int> fresh;
    for (auto& c : clipboard)
    {
        const int ti = base + ((int) c["srcTrack"] - topTrack);
        auto t = juce::isPositiveAndBelow (ti, project.numTracks()) ? project.track (ti) : Track();
        if (! t.isValid() || t.isBus() || t.isInstrument() != Clip (c).isMidi()) t = project.track (juce::jlimit (0, project.numTracks() - 1, (int) c["srcTrack"]));
        if (! t.isValid() || t.isBus() || t.isInstrument() != Clip (c).isMidi()) continue;
        auto copy = c.createCopy();
        copy.removeProperty ("srcTrack", nullptr);
        copy.setProperty (ids::id, project.allocateId(), nullptr);
        copy.setProperty (ids::start, at + ((double) c[ids::start] - lo), nullptr);
        t.clips().appendChild (copy, project.um());
        fresh.add ((int) copy[ids::id]);
    }
    ctx.selectedClips = fresh;
    ctx.sendChangeMessage();
}

// ---- keyboard ---------------------------------------------------------------------------------------------

bool StudioPage::handleTypingKey (const juce::KeyPress& k)
{
    if (! typing || k.getModifiers().isCommandDown() || k.getModifiers().isCtrlDown() || k.getModifiers().isAltDown()) return false;
    const auto ch = juce::CharacterFunctions::toLowerCase (k.getTextCharacter());
    if (ch == 'z') { typingOctave = juce::jmax (1, typingOctave - 1); setStatus ("Musical Typing: octave C" + juce::String (typingOctave - 1)); return true; }
    if (ch == 'x') { typingOctave = juce::jmin (8, typingOctave + 1); setStatus ("Musical Typing: octave C" + juce::String (typingOctave - 1)); return true; }
    if (ch == 'c') { typingVelocity = juce::jmax (10, typingVelocity - 15); setStatus ("Velocity " + juce::String (typingVelocity)); return true; }
    if (ch == 'v') { typingVelocity = juce::jmin (127, typingVelocity + 15); setStatus ("Velocity " + juce::String (typingVelocity)); return true; }

    const char* pos = std::strchr (typingKeys, (int) ch);
    if (pos == nullptr || ch == 0) return false;
    const int note = juce::jlimit (0, 127, typingOctave * 12 + (int) (pos - typingKeys));
    const int code = k.getKeyCode();
    if (heldTypingKeys.count (code) == 0)
    {
        heldTypingKeys[code] = note;
        engine.keyboardState.noteOn (1, note, typingVelocity / 127.0f);
    }
    return true;
}

void StudioPage::releaseTypingKeys (bool all)
{
    for (auto it = heldTypingKeys.begin(); it != heldTypingKeys.end();)
    {
        if (all || ! juce::KeyPress::isKeyCurrentlyDown (it->first))
        {
            engine.keyboardState.noteOff (1, it->second, 0.0f);
            it = heldTypingKeys.erase (it);
        }
        else ++it;
    }
}

bool StudioPage::keyStateChanged (bool)
{
    if (! heldTypingKeys.empty()) releaseTypingKeys (false);
    return false;
}

bool StudioPage::keyPressed (const juce::KeyPress& k)
{
    const auto cmd = juce::ModifierKeys::commandModifier;
    const auto shift = juce::ModifierKeys::shiftModifier;

    if (k == juce::KeyPress ('k', cmd, 0)) { controlBar.onToggleTyping(); return true; }
    if (handleTypingKey (k)) return true;

    if (k == juce::KeyPress::spaceKey) { engine.togglePlay(); return true; }
    if (k == juce::KeyPress ('z', cmd, 0)) { engine.flushPluginStates(); project.undo().undo(); setStatus ("Undo: " + project.undo().getRedoDescription()); return true; }
    if (k == juce::KeyPress ('z', cmd | shift, 0) || k == juce::KeyPress ('y', cmd, 0)) { project.undo().redo(); setStatus ("Redo"); return true; }
    if (k == juce::KeyPress ('s', cmd, 0)) { saveProject(); return true; }
    if (k == juce::KeyPress ('s', cmd | shift, 0)) { saveProjectAs(); return true; }
    if (k == juce::KeyPress ('o', cmd, 0)) { openProject(); return true; }
    if (k == juce::KeyPress ('n', cmd, 0)) { newProject(); return true; }
    if (k == juce::KeyPress ('e', cmd, 0)) { exportDialog (false); return true; }
    if (k == juce::KeyPress ('i', cmd, 0)) { projectMenu(); return true; }
    if (k == juce::KeyPress ('d', cmd, 0)) { duplicateSelectedClips(); return true; }
    if (k == juce::KeyPress ('t', cmd, 0)) { splitAtPlayhead(); return true; }
    if (k == juce::KeyPress ('c', cmd, 0)) { copySelected(); return true; }
    if (k == juce::KeyPress ('v', cmd, 0)) { paste(); return true; }
    if (k == juce::KeyPress ('a', cmd, 0))
    {
        ctx.selectedClips.clear();
        for (auto t : project.tracks()) for (auto c : Track (t).clips()) ctx.selectedClips.add ((int) c[ids::id]);
        ctx.sendChangeMessage();
        return true;
    }
    if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) { deleteSelectedClips(); return true; }
    if (k == juce::KeyPress::returnKey || k == juce::KeyPress::homeKey) { engine.setPositionBeats (0.0); return true; }

    if (k.getModifiers().isAnyModifierKeyDown() && ! k.getModifiers().isShiftDown()) return false;
    const auto ch = juce::CharacterFunctions::toLowerCase (k.getTextCharacter());
    const double bpb = project.beatsPerBar();
    switch (ch)
    {
        case 'r': engine.record(); return true;
        case 'c': project.tree().setProperty (ids::cycleOn, ! (bool) project.tree()[ids::cycleOn], nullptr); return true;
        case 'k': project.tree().setProperty (ids::metronome, ! (bool) project.tree()[ids::metronome], nullptr); return true;
        case ',': engine.setPositionBeats (juce::jmax (0.0, std::ceil (engine.getPositionBeats() / bpb - 1.0001) * bpb)); return true;
        case '.': engine.setPositionBeats (std::floor (engine.getPositionBeats() / bpb + 1.0001) * bpb); return true;
        case 'x': setBottomPanel (bottomPanel == 2 ? 0 : 2); return true;
        case 'e': setBottomPanel (bottomPanel == 1 ? 0 : 1); return true;
        case 'p': setBottomPanel (bottomPanel == 4 ? 0 : 4); return true;
        case 'b': toggleBrowser(); return true;
        case 'm': if (auto t = project.trackById (ctx.selectedTrack); t.isValid()) { ctx.beginEdit ("Mute"); t.v.setProperty (ids::mute, ! (bool) t.v[ids::mute], project.um()); } return true;
        case 's': if (auto t = project.trackById (ctx.selectedTrack); t.isValid()) { ctx.beginEdit ("Solo"); t.v.setProperty (ids::solo, ! (bool) t.v[ids::solo], project.um()); } return true;
        case 'z': arrangement.zoomToFit(); return true;
        default: break;
    }
    if (k == juce::KeyPress::upKey || k == juce::KeyPress::downKey)
    {
        auto t = project.trackById (ctx.selectedTrack);
        const int idx = t.isValid() ? project.tracks().indexOf (t.v) : -1;
        const int next = juce::jlimit (0, project.numTracks() - 1, idx + (k == juce::KeyPress::upKey ? -1 : 1));
        if (project.numTracks() > 0) ctx.selectTrack (project.track (next).id());
        return true;
    }
    return false;
}

// ---- project ---------------------------------------------------------------------------------------------

bool StudioPage::confirmDiscardThen (std::function<void()> next)
{
    if (! project.isDirty())
    {
        next();
        return true;
    }
    juce::AlertWindow::showYesNoCancelBox (juce::MessageBoxIconType::QuestionIcon, "Save changes?",
        "Do you want to save the changes to \"" + project.tree()[ids::name].toString() + "\"?", "Save", "Don't Save", "Cancel", this,
        juce::ModalCallbackFunction::create ([this, next] (int r)
        {
            if (r == 1) saveProject (next);
            else if (r == 2) next();
        }));
    return false;
}

void StudioPage::newProject()
{
    confirmDiscardThen ([this]
    {
        auto* w = new juce::AlertWindow ("New song", "Name your song:", juce::MessageBoxIconType::NoIcon, this);
        w->addTextEditor ("name", "New Song");
        w->addComboBox ("template", { "Empty", "Songwriter (piano, bass, drums, vocals)", "Band (drums, bass, guitar, keys)", "Beat maker (drums, synth bass, pad)" }, "Start with");
        w->addButton ("Create", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int r)
        {
            if (r != 1) return;
            const int tmpl = w->getComboBoxComponent ("template")->getSelectedItemIndex();
            engine.stop();
            project.createNew (w->getTextEditorContents ("name").trim());
            auto kit = soundFontRef (128, 0); kit.name = "Standard Kit";
            auto addInst = [this] (const juce::String& name, PluginRef ref) { auto t = project.addTrack (kindInstrument, name); project.setInstrument (t, ref); return t; };
            auto addAudio = [this] (const juce::String& name, std::initializer_list<const char*> fx) { auto t = project.addTrack (kindAudio, name); for (auto* f : fx) project.setPlugin (t.inserts(), -1, builtinRef (f)); return t; };
            if (tmpl == 1)
            {
                addInst ("Grand Piano", soundFontRef (0, 0));
                addInst ("Bass", soundFontRef (0, 33));
                auto d = addInst ("Drums", kit);
                insertDrumPattern (project, d, 4, 0.0, 8, true);
                { auto v = project.addTrack (kindAudio, "Vocals"); applyVocalChain (project, v, defaultVocalChain); }
            }
            else if (tmpl == 2)
            {
                auto d = addInst ("Drums", kit);
                insertDrumPattern (project, d, 0, 0.0, 8, true);
                addAudio ("Bass", { "amprig" });
                addAudio ("Guitar", { "amprig" });
                addInst ("Keys", soundFontRef (0, 4));
            }
            else if (tmpl == 3)
            {
                project.setTempo (90.0);
                auto d = addInst ("Drums", kit);
                insertDrumPattern (project, d, 9, 0.0, 8, true);
                addInst ("Synth Bass", synthRef (2));
                addInst ("Pad", synthRef (1));
            }
            else
            {
                addInst ("Grand Piano", soundFontRef (0, 0));
            }
            project.undo().clearUndoHistory();
            if (project.numTracks() > 0) ctx.selectTrack (project.track (0).id());
            project.save();
            addRecent (project.getFile());
            setStatus ("Created \"" + project.tree()[ids::name].toString() + "\" in " + project.getFolder().getFullPathName());
        }), true);
    });
}

void StudioPage::openProject()
{
    confirmDiscardThen ([this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Open a song", Project::defaultProjectsFolder(), "*" + Project::fileExtension());
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
        {
            if (fc.getResult().existsAsFile()) openProjectFile (fc.getResult());
        });
    });
}

void StudioPage::openProjectFile (const juce::File& f)
{
    engine.stop();
    auto err = project.load (f);
    if (err.isNotEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Open", err);
        return;
    }
    addRecent (f);
    // offer the autosave if it's newer
    auto autosave = f.getSiblingFile ("Autosave" + Project::fileExtension());
    if (autosave.existsAsFile() && autosave.getLastModificationTime() > f.getLastModificationTime() + juce::RelativeTime::seconds (5))
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Recover unsaved changes?",
            "There's an autosave of this song that's newer than the last save. Open it instead?", "Open autosave", "Keep saved version", this,
            juce::ModalCallbackFunction::create ([this, autosave, f] (int r)
            {
                if (r == 1 && project.load (autosave).isEmpty())
                {
                    project.tree().setProperty (ids::name, f.getFileNameWithoutExtension(), nullptr);
                    // keep saving to the real file
                    project.saveAs (f);
                    project.markDirty();
                }
            }));
    setStatus ("Opened " + f.getFileNameWithoutExtension());
}

void StudioPage::saveProject (std::function<void()> then)
{
    engine.flushPluginStates();
    project.tree().setProperty (ids::zoom, ctx.pixelsPerBeat, nullptr);
    project.tree().setProperty (ids::playhead, engine.getPositionBeats(), nullptr);
    auto err = project.save();
    if (err.isNotEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Save", err);
        return;
    }
    project.getFolder().getChildFile ("Autosave" + Project::fileExtension()).deleteFile();
    addRecent (project.getFile());
    setStatus ("Saved " + project.getFile().getFullPathName());
    if (then) then();
}

void StudioPage::saveProjectAs (std::function<void()> then)
{
    chooser = std::make_unique<juce::FileChooser> ("Save song as", Project::defaultProjectsFolder().getChildFile (project.tree()[ids::name].toString()), "*" + Project::fileExtension());
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, then] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (f == juce::File()) return;
        engine.flushPluginStates();
        auto err = project.saveAs (f);
        if (err.isNotEmpty()) { juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Save As", err); return; }
        addRecent (project.getFile());
        setStatus ("Saved as " + project.getFile().getFullPathName());
        if (then) then();
    });
}

void StudioPage::addRecent (const juce::File& f)
{
    juce::StringArray recent;
    recent.addTokens (settings.getValue ("recentProjects"), "|", {});
    recent.removeString (f.getFullPathName());
    recent.insert (0, f.getFullPathName());
    while (recent.size() > 10) recent.remove (recent.size() - 1);
    settings.setValue ("recentProjects", recent.joinIntoString ("|"));
    settings.setValue ("lastProject", f.getFullPathName());
    settings.saveIfNeeded();
}

void StudioPage::projectMenu()
{
    juce::PopupMenu m;
    addMenuItem (m, 1, "New Song...\tCtrl+N");
    addMenuItem (m, 2, "Open...\tCtrl+O");
    juce::PopupMenu recentMenu;
    juce::StringArray recent;
    recent.addTokens (settings.getValue ("recentProjects"), "|", {});
    for (int i = 0; i < recent.size(); ++i)
        if (juce::File (recent[i]).existsAsFile()) recentMenu.addItem (500 + i, juce::File (recent[i]).getFileNameWithoutExtension());
    m.addSubMenu ("Open Recent", recentMenu, recentMenu.getNumItems() > 0);
    addMenuItem (m, 3, "Save\tCtrl+S");
    addMenuItem (m, 4, "Save As...\tCtrl+Shift+S");
    m.addItem (5, "Show Song Folder");
    m.addSeparator();
    m.addItem (6, "Import Audio or MIDI File...");
    addMenuItem (m, 7, "Export Mix (WAV / FLAC / OGG)...\tCtrl+E");
    m.addItem (8, "Export Stems (one file per track)...");
    m.addItem (9, "Export MIDI File...");
    m.addSeparator();
    addMenuItem (m, 10, "Undo " + project.undo().getUndoDescription() + "\tCtrl+Z", project.undo().canUndo());
    addMenuItem (m, 11, "Redo " + project.undo().getRedoDescription() + "\tCtrl+Y", project.undo().canRedo());
    m.addSeparator();
    m.addItem (14, "Add a Plugin File (.dll / .vst3 / .clap, old 32-bit VSTs too)...");
    m.addItem (15, "Open My Plugins Folder  (anything in it loads automatically)");
    m.addItem (12, "Plugin Manager (scan VST3 / VST / CLAP / LV2)...");
    m.addItem (13, "Studio Shortcuts...");

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&controlBar.projectButton), [this, recent] (int r)
    {
        switch (r)
        {
            case 1: newProject(); break;
            case 2: openProject(); break;
            case 3: saveProject(); break;
            case 4: saveProjectAs(); break;
            case 5: project.getFolder().startAsProcess(); break;
            case 6:
                chooser = std::make_unique<juce::FileChooser> ("Import audio or MIDI", lastFolder, wis::supportedAudioWildcard() + ";*.mid;*.midi");
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
                                      [this] (const juce::FileChooser& fc)
                {
                    juce::StringArray files;
                    for (auto& f : fc.getResults()) files.add (f.getFullPathName());
                    if (! files.isEmpty()) { lastFolder = fc.getResult().getParentDirectory(); importFiles (files, ctx.selectedTrack, engine.getPositionBeats()); }
                });
                break;
            case 7: exportDialog (false); break;
            case 8: exportDialog (true); break;
            case 9: exportMidi(); break;
            case 10: engine.flushPluginStates(); project.undo().undo(); break;
            case 11: project.undo().redo(); break;
            case 12: showPluginManager(); break;
            case 13: showShortcuts(); break;
            case 14: addPluginFile(); break;
            case 15: PluginHost::userPluginFolder().startAsProcess(); break;
            default:
                if (r >= 500) confirmDiscardThen ([this, f = juce::File (recent[r - 500])] { openProjectFile (f); });
                break;
        }
    });
}

void StudioPage::showShortcuts()
{
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Studio shortcuts",
                    "Space  play / stop          R  record          Enter  go to start\n"
                    "C  cycle on/off          K  metronome          ,  .  back / forward a bar\n"
                    "Ctrl+K  Musical Typing (A S D F... play notes, Z/X octave)\n"
                    "Ctrl+Z / Ctrl+Y  undo / redo          Ctrl+S  save          Ctrl+E  export\n"
                    "Ctrl+T  split at playhead          Ctrl+D  duplicate          Ctrl+C / V  copy / paste\n"
                    "Del  delete          M / S  mute / solo track          Up / Down  select track\n"
                    "E  editor          X  mixer          P  plugin panel     B  library          Z  zoom to fit\n\n"
                    "Arrangement: drag clips to move (Alt = copy, Shift = no snap), drag edges to trim, top corners of audio clips for fades. "
                    "Double-click a clip to edit it, or double-click empty space on an instrument track for a new MIDI clip. "
                    "Drag in the ruler's top strip to set the cycle. Ctrl + mouse wheel zooms.\n"
                    "Ctrl+drag an audio clip's right edge to time-stretch it. Right-click a clip for Time & Pitch, Reverse, Normalize, "
                    "Convert to Sampler. Right-click the ruler for markers (Verse, Chorus...). Right-click an automation lane to automate "
                    "sends or any plugin knob.\n\n"
                    "Mixer: + MIDI FX (arpeggiator...) before the instrument, + Send to a bus, Out: to route into a bus, "
                    "right-click a compressor / gate / vocoder for its side-chain input.\n\n"
                    "Piano roll: double-click (or Draw mode) to add notes, drag to move, drag the right edge to resize, "
                    "arrows to transpose / nudge, Q to quantize, the lane at the bottom sets velocity.");
}

juce::String StudioPage::getProjectName() const { return project.tree()[ids::name].toString(); }

// ---- export ------------------------------------------------------------------------------------------------

void StudioPage::exportDialog (bool stems)
{
    auto* w = new juce::AlertWindow (stems ? "Export stems" : "Export mix", stems ? "One audio file per track, ready for any DAW." : "Bounce the whole song to an audio file.",
                                     juce::MessageBoxIconType::NoIcon, this);
    w->addComboBox ("format", { "WAV 24-bit", "WAV 16-bit", "WAV 32-bit float", "FLAC (lossless)", "OGG Vorbis (small)" }, "Format");
    w->addComboBox ("rate", { "Project rate (" + juce::String (engine.getSampleRate() / 1000.0, 1) + " kHz)", "44.1 kHz (CD)", "48 kHz", "96 kHz" }, "Sample rate");
    const bool hasCycle = (bool) project.tree()[ids::cycleOn];
    w->addComboBox ("range", { "Whole song", "Cycle region" }, "Range");
    if (hasCycle) w->getComboBoxComponent ("range")->setSelectedItemIndex (1);
    w->addComboBox ("normalise", { "No normalising", "Normalise to -0.3 dB" }, "Level");
    w->addButton ("Export", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, stems] (int r)
    {
        if (r != 1) return;
        const int fmt = w->getComboBoxComponent ("format")->getSelectedItemIndex();
        const int rate = w->getComboBoxComponent ("rate")->getSelectedItemIndex();
        const int range = w->getComboBoxComponent ("range")->getSelectedItemIndex();
        const bool norm = w->getComboBoxComponent ("normalise")->getSelectedItemIndex() == 1;

        DawEngine::ExportOptions base;
        base.format = fmt <= 2 ? 0 : fmt == 3 ? 1 : 2;
        base.bitDepth = fmt == 1 ? 16 : fmt == 2 ? 32 : 24;
        base.sampleRate = rate == 1 ? 44100.0 : rate == 2 ? 48000.0 : rate == 3 ? 96000.0 : 0.0;
        base.normalise = norm;
        const double bpb = project.beatsPerBar();
        if (range == 1) { base.startBeat = project.tree()[ids::cycleStart]; base.endBeat = project.tree()[ids::cycleEnd]; }
        else { base.startBeat = 0; base.endBeat = juce::jmax (bpb, std::ceil (project.contentEndBeats() / bpb) * bpb); }
        const auto ext = base.format == 0 ? ".wav" : base.format == 1 ? ".flac" : ".ogg";
        const auto songName = project.tree()[ids::name].toString();

        auto run = [this, stems, base, ext, songName] (const juce::File& target)
        {
            std::vector<DawEngine::ExportOptions> jobs;
            if (stems)
            {
                target.createDirectory();
                for (auto tv : project.tracks())
                {
                    Track t (tv);
                    if ((bool) tv[ids::mute]) continue;
                    auto o = base;
                    o.file = target.getChildFile (juce::File::createLegalFileName (songName + " - " + t.name()) + ext);
                    o.onlyTracks.add (t.id());
                    jobs.push_back (o);
                }
            }
            else
            {
                auto o = base;
                o.file = target.withFileExtension (ext);
                jobs.push_back (o);
            }
            if (jobs.empty()) return;

            engine.beginExport();
            ExportJob job (engine, jobs);
            const bool finished = job.runThread();
            engine.endExport();

            if (! finished || job.error == "Cancelled") setStatus ("Export cancelled.");
            else if (job.error.isNotEmpty()) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Export", job.error);
            else
            {
                setStatus ("Exported to " + (stems ? target : jobs.front().file).getFullPathName());
                (stems ? target : jobs.front().file).revealToUser();
            }
        };

        const auto defaultDir = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
        if (stems)
        {
            chooser = std::make_unique<juce::FileChooser> ("Choose a folder for the stems", defaultDir);
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [run, songName] (const juce::FileChooser& fc)
            {
                if (fc.getResult().isDirectory()) run (fc.getResult().getChildFile (juce::File::createLegalFileName (songName) + " Stems"));
            });
        }
        else
        {
            chooser = std::make_unique<juce::FileChooser> ("Export mix", defaultDir.getChildFile (juce::File::createLegalFileName (songName) + ext), juce::String ("*") + ext);
            chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                                  [run] (const juce::FileChooser& fc) { if (fc.getResult() != juce::File()) run (fc.getResult()); });
        }
    }), true);
}

void StudioPage::exportMidi()
{
    chooser = std::make_unique<juce::FileChooser> ("Export MIDI file",
        juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile (juce::File::createLegalFileName (project.tree()[ids::name].toString()) + ".mid"), "*.mid");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
    {
        if (fc.getResult() == juce::File()) return;
        auto err = project.exportMidiFile (fc.getResult().withFileExtension (".mid"));
        setStatus (err.isEmpty() ? "Exported " + fc.getResult().getFileName() : err);
    });
}

// ---- plugins ------------------------------------------------------------------------------------------------

void StudioPage::showPluginManager()
{
    if (pluginManagerWindow != nullptr) { pluginManagerWindow->toFront (true); return; }

    host.useOutOfProcessScanning();
    auto deadMansPedal = ModelManager::appDataDirectory().getChildFile ("plugin-scan-crashes.txt");
    auto* list = new juce::PluginListComponent (host.formats, host.known, deadMansPedal, &settings, false);
    list->setSize (760, 520);

    struct Win : public juce::DocumentWindow
    {
        Win (StudioPage& o) : DocumentWindow ("Plugin Manager  -  click Options... > Scan for new or updated plug-ins (VST3, VST, CLAP, LV2)", theme::panel, closeButton), owner (o) {}
        void closeButtonPressed() override { owner.browser.refresh(); juce::MessageManager::callAsync ([&o = owner] { o.pluginManagerWindow.reset(); }); }
        StudioPage& owner;
    };
    auto* w = new Win (*this);
    w->setUsingNativeTitleBar (true);
    w->setContentOwned (list, true);
    w->setResizable (true, false);
    w->centreWithSize (760, 520);
    w->setVisible (true);
    pluginManagerWindow.reset (w);
    setStatus ("Scan for plugins: Options > Scan for VST3, VST, CLAP or LV2 plugins. Each plugin is tested in a separate process, so a crashing plugin can't take the app down.");
}

void StudioPage::addPluginFile()
{
    auto start = juce::File (settings.getValue ("plugins.lastFolder"));
    if (! start.isDirectory()) start = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Downloads");
    chooser = std::make_unique<juce::FileChooser> ("Choose a plugin file", start, "*.dll;*.vst3;*.clap;*.so");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file == juce::File()) return;
        settings.setValue ("plugins.lastFolder", file.getParentDirectory().getFullPathName());

        auto* format = host.formatForFile (file);
        if (format == nullptr) { setStatus ("This build can't load " + file.getFileExtension() + " plugins."); return; }

        setStatus ("Testing " + file.getFileName() + " in a separate process...");
        juce::MouseCursor::showWaitCursor();
        host.useOutOfProcessScanning();
        juce::OwnedArray<juce::PluginDescription> found;
        // WOMANINSTEM keeps its own copy, so the plugin still works if the download is tidied away
        const auto kept = PluginHost::keepCopy (file);
        host.known.scanAndAddFile (kept.getFullPathName(), false, found, *format);
        juce::MouseCursor::hideWaitCursor();
        browser.refresh();

        if (found.isEmpty())
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Couldn't load " + file.getFileName(),
                file.getFileName() + " didn't load as a plugin. It may not be a VST / VST3 / CLAP plugin (some downloads are installers: run those first), "
                "it may need files that came with it in the same folder, or it may have crashed while starting (it was tested in a separate process, "
                "so nothing else was affected).", "OK", this);
            setStatus ("Couldn't load " + file.getFileName());
            return;
        }

        const auto* d = found.getFirst();
        const bool bridged = d->descriptiveName.contains ("32-bit");
        if (d->isInstrument)
        {
            applyBrowserItem ("vsti:" + d->createIdentifierString(), -1, engine.getPositionBeats());
            auto t = project.trackById (ctx.selectedTrack);
            engine.rebuildNow();
            if (t.isValid()) openPluginWindow (t.instrument());
            setStatus (d->name + " is on a new track" + (bridged ? juce::String (" (an old 32-bit plugin: it runs in its own window)") : juce::String())
                       + ". Play it with your MIDI keyboard or Ctrl+K. Next time it's under Library > Sounds > Plugin Instruments.");
        }
        else
        {
            const int trackId = ctx.selectedTrack;
            applyBrowserItem ("vstfx:" + d->createIdentifierString(), trackId, engine.getPositionBeats());
            setStatus (d->name + " was added to the selected track's effects" + (bridged ? juce::String (" (32-bit, bridged)") : juce::String())
                       + ". Next time it's under Library > FX > Plugin Effects.");
        }
    });
}

// ---- Play Along -> Studio -----------------------------------------------------------------------------------

void StudioPage::openSongInStudio (const wis::SongInfo& song, const std::array<bool, wis::numStemIds>& muted,
                                   const juce::ValueTree& rigState, const juce::String& playingPart)
{
    confirmDiscardThen ([this, song, muted, rigState, playingPart]
    {
        engine.stop();
        project.createNew (song.title);

        // tempo from the drums (or the bass) so the stems sit on the bar grid
        TempoEstimate est;
        for (auto stem : { wis::StemId::drums, wis::StemId::bass, wis::StemId::other })
        {
            if (! song.present[(size_t) stem]) continue;
            auto loaded = wis::loadAudioFile (song.stemFile (stem), 0.0);
            if (! loaded.ok()) continue;
            est = estimateTempo (loaded.audio, loaded.sampleRate);
            if (est.confidence > 0.05f) break;
        }
        project.setTempo (est.bpm);
        const double spb = 60.0 / est.bpm;
        const double bpb = project.beatsPerBar();
        const double firstBeat = est.firstBeatSeconds / spb;
        const double k = juce::jmax (1.0, std::ceil (firstBeat / bpb));
        const double startBeat = juce::jmax (0.0, k * bpb - firstBeat);   // the detected beat lands on a downbeat

        for (auto& st : wis::allStems())
        {
            if (! song.present[(size_t) st.id]) continue;
            auto f = project.importIntoProject (song.stemFile (st.id));
            auto t = project.addTrack (kindAudio, st.displayName);
            t.v.setProperty (ids::colour, (juce::int64) wis::stemColour (st.id).getARGB(), nullptr);
            t.v.setProperty (ids::mute, muted[(size_t) st.id], nullptr);
            auto c = project.addAudioClip (t, f, startBeat, 0.0, AudioCache::fileLengthSeconds (f), st.displayName);
            // stems follow the song tempo: slow the song down to practise, and they stretch along
            if (est.confidence > 0.05f) project.setClipFollowTempo (c, est.bpm, true);
        }

        // a track for you
        if (playingPart.containsIgnoreCase ("drum"))
        {
            auto t = project.addTrack (kindInstrument, "My Drums");
            auto kit = soundFontRef (128, 0); kit.name = "Standard Kit";
            project.setInstrument (t, kit);
            t.v.setProperty (ids::arm, true, nullptr);
            ctx.selectTrack (t.id());
        }
        else if (playingPart.containsIgnoreCase ("keys") || playingPart.containsIgnoreCase ("piano"))
        {
            auto t = project.addTrack (kindInstrument, "My Keys");
            project.setInstrument (t, soundFontRef (0, 0));
            t.v.setProperty (ids::arm, true, nullptr);
            ctx.selectTrack (t.id());
        }
        else
        {
            const bool vocals = playingPart.containsIgnoreCase ("vocal");
            auto t = project.addTrack (kindAudio, "My " + (playingPart.isNotEmpty() && playingPart != "Just listening" ? playingPart : juce::String ("Part")));
            if (vocals)
            {
                project.setPlugin (t.inserts(), -1, builtinRef ("eq"));
                project.setPlugin (t.inserts(), -1, builtinRef ("compressor"));
                project.setPlugin (t.inserts(), -1, builtinRef ("reverb"));
            }
            else
            {
                auto ref = builtinRef ("amprig");
                if (rigState.isValid())
                    if (auto xml = rigState.createXml())
                    {
                        juce::MemoryBlock mb;
                        juce::AudioProcessor::copyXmlToBinary (*xml, mb);
                        ref.state = mb.toBase64Encoding();
                    }
                project.setPlugin (t.inserts(), -1, ref);
            }
            t.v.setProperty (ids::arm, true, nullptr);
            t.v.setProperty (ids::monitor, true, nullptr);
            ctx.selectTrack (t.id());
        }

        project.undo().clearUndoHistory();
        project.save();
        addRecent (project.getFile());
        engine.setPositionBeats (0.0);
        juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<StudioPage> (this)] { if (sp) sp->arrangement.zoomToFit(); });
        setStatus ("\"" + song.title + "\" is in the Studio at " + juce::String (est.bpm, 1) + " BPM (detected). Your track is armed - press R to record.");
    });
}

} // namespace wis::daw
