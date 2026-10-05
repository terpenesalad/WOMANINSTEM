#include "DawEngine.h"
#include "Common/Resample.h"

namespace wis::daw
{

// =====================================================================================================
//  Slot
// =====================================================================================================
void Slot::prepare (double sr, int block)
{
    if (proc == nullptr) return;
    proc->setRateAndBufferSizeDetails (sr, block);
    proc->prepareToPlay (sr, block);
    channels = juce::jmax (2, proc->getTotalNumInputChannels(), proc->getTotalNumOutputChannels());
    scratch.setSize (channels, block, false, true, false);
}

void Slot::process (juce::AudioBuffer<float>& stereo, int n, juce::MidiBuffer& midi)
{
    if (proc == nullptr || n > scratch.getNumSamples())
        return;
    if (bypass.load())
    {
        if (instrument) stereo.clear (0, n);
        return;
    }

    const juce::ScopedLock sl (proc->getCallbackLock());
    if (proc->isSuspended())
        return;

    const int ins = proc->getTotalNumInputChannels();
    const int outs = proc->getTotalNumOutputChannels();
    juce::AudioBuffer<float> view (scratch.getArrayOfWritePointers(), channels, n);

    for (int c = 0; c < channels; ++c)
    {
        if (c < 2 && c < ins) view.copyFrom (c, 0, stereo, c, 0, n);
        else view.clear (c, 0, n);
    }
    if (ins == 1)
    {
        view.copyFrom (0, 0, stereo.getReadPointer (0), n, 0.5f);
        view.addFrom (0, 0, stereo.getReadPointer (1), n, 0.5f);
    }

    proc->processBlock (view, midi);

    if (outs >= 2)
    {
        stereo.copyFrom (0, 0, view, 0, 0, n);
        stereo.copyFrom (1, 0, view, 1, 0, n);
    }
    else if (outs == 1)
    {
        stereo.copyFrom (0, 0, view, 0, 0, n);
        stereo.copyFrom (1, 0, view, 0, 0, n);
    }
}

float Snapshot::Lane::valueAt (juce::int64 t) const
{
    if (points.empty()) return 0.0f;
    if (t <= points.front().first) return points.front().second;
    if (t >= points.back().first) return points.back().second;
    auto it = std::upper_bound (points.begin(), points.end(), t, [] (juce::int64 v, const auto& p) { return v < p.first; });
    const auto& b = *it;
    const auto& a = *(it - 1);
    const double f = b.first > a.first ? (double) (t - a.first) / (double) (b.first - a.first) : 0.0;
    return (float) (a.second + (b.second - a.second) * f);
}

// =====================================================================================================
//  Engine
// =====================================================================================================
static juce::TimeSliceThread& writerThread()
{
    static juce::TimeSliceThread t ("Recording writer");
    if (! t.isThreadRunning()) t.startThread (juce::Thread::Priority::high);
    return t;
}

// function-local so they're built on first use: the ids:: identifiers are inline globals, and copying them during
// static initialisation of this file could happen before they exist (it did, in rigtest)
static const std::vector<juce::Identifier>& mixProps()
{
    static const std::vector<juce::Identifier> v { ids::volume, ids::pan, ids::mute, ids::solo, ids::arm, ids::monitor, ids::input,
                                                   ids::inputStereo, ids::masterVolume, ids::metronome };
    return v;
}
static const std::vector<juce::Identifier>& uiOnlyProps()
{
    static const std::vector<juce::Identifier> v { ids::name, ids::colour, ids::height, ids::zoom, ids::playhead, ids::showAutomation,
                                                   ids::countIn, ids::state, ids::nextId, ids::version };
    return v;
}

DawEngine::DawEngine (Project& p, PluginHost& h) : project (p), host (h)
{
    project.tree().addListener (this);
    cache.addChangeListener (this);
    master.setSize (2, maxBlock);
    clickBuf.setSize (1, maxBlock);
    liveMidi.ensureSize (4096);
    startTimerHz (30);
}

DawEngine::~DawEngine()
{
    stopTimer();
    detach();
    if (recSessionActive.load() || recording.load())
        finishRecording();
    project.tree().removeListener (this);
    cache.removeChangeListener (this);
    {
        const juce::SpinLock::ScopedLockType sl (snapLock);
        pending = nullptr;
    }
    audioSnap = nullptr;
    graveyard.clear();
    slots.clear();
    slotGraveyard.clear();
    runtimes.clear();
}

// ---- device -------------------------------------------------------------------------------------------

void DawEngine::attach (juce::AudioDeviceManager& dm)
{
    if (deviceManager == &dm) return;
    detach();
    deviceManager = &dm;

    for (auto& m : juce::MidiInput::getAvailableDevices())
        if (! dm.isMidiInputDeviceEnabled (m.identifier))
            dm.setMidiInputDeviceEnabled (m.identifier, true);
    dm.addMidiInputDeviceCallback ({}, this);
    dm.addAudioCallback (this);
}

void DawEngine::detach()
{
    if (deviceManager == nullptr) return;
    stop();
    deviceManager->removeAudioCallback (this);
    deviceManager->removeMidiInputDeviceCallback ({}, this);
    deviceManager = nullptr;
}

void DawEngine::prepareOffline (double sr, int block)
{
    sampleRate = sr;
    maxBlock = juce::jmax (64, block);
    prepareAll();
    rebuildNow();
}

void DawEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    const double newRate = device->getCurrentSampleRate();
    const bool rateChanged = std::abs (newRate - sampleRate) > 0.5;
    sampleRate = newRate;
    maxBlock = juce::jmax (512, device->getCurrentBufferSizeSamples()) * 2;
    inputLatency = device->getInputLatencyInSamples();
    outputLatency = device->getOutputLatencyInSamples();
    midiCollector.reset (sampleRate);
    prepareAll();
    if (rateChanged) markDirty();
}

void DawEngine::audioDeviceStopped()
{
}

void DawEngine::prepareAll()
{
    const juce::ScopedLock sl (processLock);
    master.setSize (2, maxBlock, false, true, false);
    clickBuf.setSize (1, maxBlock, false, true, false);
    for (auto& [id, slot] : slots)
        slot->prepare (sampleRate, maxBlock);
    for (auto& [id, rt] : runtimes)
    {
        rt->buffer.setSize (2, maxBlock, false, true, false);
        rt->midi.ensureSize (8192);
    }
}

void DawEngine::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m)
{
    if (! m.isActiveSense() && ! m.isMidiClock())
        midiCollector.addMessageToQueue (m);
}

// ---- snapshot building ------------------------------------------------------------------------------

void DawEngine::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& prop)
{
    for (auto& p : mixProps())
        if (prop == p) { syncMixValues(); return; }
    for (auto& p : uiOnlyProps())
        if (prop == p) return;

    if (prop == ids::bypass && tree.hasType (ids::PLUGIN))
    {
        if (auto it = slots.find (tree[ids::id].toString()); it != slots.end())
            it->second->bypass = (bool) tree[ids::bypass];
        return;
    }
    markDirty();
}

TrackRT::Ptr DawEngine::runtimeFor (int id)
{
    if (auto it = runtimes.find (id); it != runtimes.end())
        return it->second;
    TrackRT::Ptr rt = new TrackRT();
    rt->id = id;
    rt->buffer.setSize (2, maxBlock);
    rt->midi.ensureSize (8192);
    runtimes[id] = rt;
    return rt;
}

void DawEngine::syncMixValues()
{
    for (auto tv : project.tracks())
    {
        Track t (tv);
        auto it = runtimes.find (t.id());
        if (it == runtimes.end()) continue;
        auto& rt = *it->second;
        rt.volumeDb = (float) (double) tv[ids::volume];
        rt.pan = (float) (double) tv[ids::pan];
        rt.mute = (bool) tv[ids::mute];
        rt.solo = (bool) tv[ids::solo];
        rt.arm = (bool) tv[ids::arm];
        rt.monitor = (bool) tv[ids::monitor];
        rt.input = (int) tv[ids::input];
        rt.inputStereo = (bool) tv[ids::inputStereo];
        rt.isInstrument = t.isInstrument();
    }
    masterVolume = juce::Decibels::decibelsToGain ((float) (double) project.tree()[ids::masterVolume]);
    metronomeOn = (bool) project.tree()[ids::metronome];

    // solo state lives in the snapshot; rebuild cheaply when it changes
    bool anySolo = false;
    for (auto tv : project.tracks()) anySolo = anySolo || (bool) tv[ids::solo];
    {
        const juce::SpinLock::ScopedLockType sl (snapLock);
        if (pending != nullptr && pending->anySolo != anySolo) markDirty();
    }
}

Slot::Ptr DawEngine::slotFor (const juce::ValueTree& node, bool instrument)
{
    const auto id = node[ids::id].toString();
    const auto uid = node[ids::uid].toString();
    if (auto it = slots.find (id); it != slots.end() && it->second->uid == uid)
        return it->second;

    Slot::Ptr s = new Slot();
    s->slotId = id;
    s->uid = uid;
    s->instrument = instrument;
    s->bypass = (bool) node[ids::bypass];
    s->proc = host.create (node, sampleRate, maxBlock, instrument, s->error);
    if (s->proc != nullptr)
    {
        s->proc->setPlayHead (this);
        s->prepare (sampleRate, maxBlock);
    }
    else if (onError)
    {
        onError (s->error);
    }

    if (auto it = slots.find (id); it != slots.end())
    {
        slotGraveyard.add (it->second);
        if (onSlotRemoved) onSlotRemoved (id);
    }
    slots[id] = s;
    return s;
}

void DawEngine::rebuildNow()
{
    dirty = false;
    rebuild();
}

void DawEngine::rebuild()
{
    Snapshot::Ptr s = new Snapshot();
    s->sampleRate = sampleRate;
    s->tempo = project.tempo();
    s->tsNum = project.tsNum();
    s->tsDen = project.tsDen();
    s->samplesPerBeat = sampleRate * 60.0 / s->tempo;
    const double spb = s->samplesPerBeat;
    auto toSamples = [spb] (double beats) { return (juce::int64) std::llround (beats * spb); };

    std::set<juce::String> usedSlots;
    std::set<int> usedTracks;

    for (auto tv : project.tracks())
    {
        Track t (tv);
        auto rt = runtimeFor (t.id());
        usedTracks.insert (t.id());

        Snapshot::TrackR tr;
        tr.rt = rt;
        tr.instrument = t.isInstrument();

        if (tr.instrument)
        {
            auto node = t.instrument();
            if (node.isValid())
            {
                tr.inst = slotFor (node, true);
                usedSlots.insert (tr.inst->slotId);
            }
        }
        for (auto pnode : t.inserts())
        {
            auto slot = slotFor (pnode, false);
            usedSlots.insert (slot->slotId);
            tr.inserts.add (slot);
        }

        for (auto cv : t.clips())
        {
            Clip c (cv);
            if (cv[ids::mute]) continue;

            if (c.isAudio())
            {
                auto data = cache.get (project.resolve (c.file()), sampleRate);
                if (data == nullptr) { ++s->missingAudio; continue; }
                Snapshot::AudioClipR a;
                a.data = data;
                a.start = toSamples (c.start());
                a.length = (juce::int64) std::llround (c.lengthSeconds() * sampleRate);
                a.offset = (juce::int64) std::llround (c.offsetSeconds() * sampleRate);
                a.gain = juce::Decibels::decibelsToGain (c.gainDb());
                a.fadeIn = (int) (c.fadeIn() * sampleRate);
                a.fadeOut = (int) (c.fadeOut() * sampleRate);
                if (a.length > 0) tr.audio.push_back (std::move (a));
            }
            else
            {
                Snapshot::MidiClipR m;
                const double len = c.midiLength();
                m.start = toSamples (c.start());
                m.end = toSamples (c.start() + len);
                for (auto e : cv)
                {
                    if (e.hasType (ids::NOTE))
                    {
                        const double ns = e[ids::s], nl = e[ids::l];
                        if (ns < 0.0 || ns >= len) continue;      // trimmed away (non-destructive)
                        const int pitch = juce::jlimit (0, 127, (int) e[ids::p]);
                        const int vel = juce::jlimit (1, 127, (int) e[ids::v]);
                        const auto on = toSamples (c.start() + ns);
                        const auto off = juce::jmax (on + 1, toSamples (c.start() + juce::jmin (len, ns + nl)));
                        m.events.push_back ({ on,  { (juce::uint8) 0x90, (juce::uint8) pitch, (juce::uint8) vel }, 3 });
                        m.events.push_back ({ off, { (juce::uint8) 0x80, (juce::uint8) pitch, (juce::uint8) 0 }, 3 });
                    }
                    else if (e.hasType (ids::CC))
                    {
                        const double b = e[ids::b];
                        if (b < 0.0 || b >= len) continue;
                        const int num = e[ids::n], val = e[ids::v];
                        Snapshot::MidiEv ev { toSamples (c.start() + b), {}, 3 };
                        if (num == ccPitchBend)       { ev.bytes[0] = 0xE0; ev.bytes[1] = (juce::uint8) (val & 127); ev.bytes[2] = (juce::uint8) ((val >> 7) & 127); }
                        else if (num == ccAftertouch) { ev.bytes[0] = 0xD0; ev.bytes[1] = (juce::uint8) (val & 127); ev.size = 2; }
                        else                          { ev.bytes[0] = 0xB0; ev.bytes[1] = (juce::uint8) (num & 127); ev.bytes[2] = (juce::uint8) (val & 127); }
                        m.events.push_back (ev);
                    }
                }
                // time order; at the same instant note-offs go first so repeated notes retrigger
                std::stable_sort (m.events.begin(), m.events.end(), [] (const auto& a, const auto& b)
                {
                    if (a.time != b.time) return a.time < b.time;
                    return (a.bytes[0] & 0xf0) == 0x80 && (b.bytes[0] & 0xf0) != 0x80;
                });
                tr.midi.push_back (std::move (m));
            }
        }

        for (auto lane : t.automation())
        {
            Snapshot::Lane l;
            for (auto p : lane)
                l.points.push_back ({ toSamples ((double) p[ids::b]), (float) (double) p[ids::v] });
            std::sort (l.points.begin(), l.points.end(), [] (auto& a, auto& b) { return a.first < b.first; });
            const auto param = lane[ids::param].toString();
            if (param == "volume") tr.volume = std::move (l);
            else if (param == "pan") tr.pan = std::move (l);
        }

        s->anySolo = s->anySolo || (bool) tv[ids::solo];
        s->tracks.push_back (std::move (tr));
    }

    for (auto pnode : project.masterInserts())
    {
        auto slot = slotFor (pnode, false);
        usedSlots.insert (slot->slotId);
        s->masterInserts.add (slot);
    }

    s->cycleOn = (bool) project.tree()[ids::cycleOn];
    s->cycleStart = toSamples ((double) project.tree()[ids::cycleStart]);
    s->cycleEnd = toSamples ((double) project.tree()[ids::cycleEnd]);
    if (s->cycleEnd - s->cycleStart < (juce::int64) (sampleRate * 0.05)) s->cycleOn = false;

    // drop runtimes / plugins that no longer exist
    for (auto it = runtimes.begin(); it != runtimes.end();)
        it = usedTracks.count (it->first) == 0 ? runtimes.erase (it) : std::next (it);
    for (auto it = slots.begin(); it != slots.end();)
    {
        if (usedSlots.count (it->first) == 0)
        {
            slotGraveyard.add (it->second);
            if (onSlotRemoved) onSlotRemoved (it->first);
            it = slots.erase (it);
        }
        else ++it;
    }

    syncMixValues();
    {
        const juce::SpinLock::ScopedLockType sl (snapLock);
        pending = s;
    }
    graveyard.add (s);
}

void DawEngine::timerCallback()
{
    if (offline.load()) return;
    if (dirty.exchange (false))
        rebuild();

    for (int i = graveyard.size(); --i >= 0;)
        if (graveyard.getObjectPointerUnchecked (i)->getReferenceCount() == 1)
            graveyard.remove (i);
    for (int i = slotGraveyard.size(); --i >= 0;)
        if (slotGraveyard.getObjectPointerUnchecked (i)->getReferenceCount() == 1)
            slotGraveyard.remove (i);

    if (recSessionActive.load())
        drainMidiFifo();

    // keep the saved playhead roughly current (for reopening the project)
    static int tick = 0;
    if (++tick % 60 == 0)
        cache.purgeUnused();
}

// ---- transport ---------------------------------------------------------------------------------------------

double DawEngine::getPositionBeats() const
{
    return (double) position.load() / (sampleRate * 60.0 / project.tempo());
}

double DawEngine::getPositionSeconds() const
{
    return (double) position.load() / sampleRate;
}

void DawEngine::setPositionBeats (double beats)
{
    const auto s = (juce::int64) std::llround (juce::jmax (0.0, beats) * sampleRate * 60.0 / project.tempo());
    seekRequest = s;
    position = s;
}

void DawEngine::play()
{
    if (playing.load() || countInRemaining.load() > 0) return;
    stopRequest = true;   // clear any hanging notes before starting
    playing = true;
}

void DawEngine::stop()
{
    const bool wasRecording = recSessionActive.load();
    playing = false;
    countInRemaining = 0;
    stopRequest = true;
    if (wasRecording)
        finishRecording();
}

void DawEngine::record()
{
    if (recSessionActive.load()) { stop(); return; }
    if (playing.load()) stop();

    // Nothing armed? Record onto the selected track (GarageBand style).
    bool anyArmed = false;
    for (auto tv : project.tracks()) anyArmed = anyArmed || (bool) tv[ids::arm];
    if (! anyArmed)
    {
        auto t = project.trackById (selectedTrackId.load());
        if (! t.isValid() && project.numTracks() > 0) t = project.track (0);
        if (! t.isValid())
        {
            if (onError) onError ("Add a track first, then press Record.");
            return;
        }
        t.v.setProperty (ids::arm, true, nullptr);
        syncMixValues();
    }

    startRecordingSession();

    const double spb = sampleRate * 60.0 / project.tempo();
    if ((bool) project.tree()[ids::countIn])
    {
        countInTotal = (juce::int64) std::llround (spb * project.beatsPerBar());
        countInRemaining = countInTotal;
    }
    else
    {
        stopRequest = true;
        recording = true;   // start immediately
        playing = true;
    }
}

void DawEngine::startRecordingSession()
{
    const juce::ScopedLock sl (processLock);
    recTracks.clear();
    {
        const juce::ScopedLock ll (liveLock);
        liveNotes.clear();
    }
    midiFifo.reset();
    recSamples = 0;
    recStartSample = position.load();

    for (auto tv : project.tracks())
    {
        Track t (tv);
        if (! (bool) tv[ids::arm]) continue;

        auto r = std::make_unique<RecordingTrack>();
        r->trackId = t.id();
        r->audio = ! t.isInstrument();
        r->input = (int) tv[ids::input];
        r->stereo = (bool) tv[ids::inputStereo];
        if (r->audio)
        {
            r->file = project.newRecordingFile (t.name());
            r->file.deleteFile();
            std::unique_ptr<juce::OutputStream> stream (r->file.createOutputStream().release());
            auto opts = juce::AudioFormatWriterOptions{}.withSampleRate (sampleRate).withNumChannels (r->stereo ? 2 : 1).withBitsPerSample (24);
            auto writer = juce::WavAudioFormat().createWriterFor (stream, opts);
            if (writer == nullptr)
            {
                if (onError) onError ("Couldn't create a recording file in " + project.audioFolder().getFullPathName());
                continue;
            }
            r->writer = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (writer.release(), writerThread(), (int) sampleRate * 4);
            r->peaks.assign ((size_t) (sampleRate * 3600.0 / 512.0), 0.0f);
        }
        recTracks.push_back (std::move (r));
    }

    {
        const juce::SpinLock::ScopedLockType sl2 (snapLock);
        recCycle = pending != nullptr && pending->cycleOn;
        recCycleStart = pending != nullptr ? pending->cycleStart : 0;
        recCycleEnd = pending != nullptr ? pending->cycleEnd : 0;
    }
    recSessionActive = true;
}

void DawEngine::drainMidiFifo()
{
    int s1, n1, s2, n2;
    midiFifo.prepareToRead (midiFifo.getNumReady(), s1, n1, s2, n2);
    const juce::ScopedLock ll (liveLock);
    auto take = [this] (int start, int num)
    {
        for (int i = 0; i < num; ++i)
        {
            auto& e = midiFifoData[(size_t) (start + i)];
            auto& notes = liveNotes[e.trackId];
            const int type = e.b0 & 0xf0;
            if (type == 0x90 && e.b2 > 0)
                notes.push_back ({ e.b1, e.b2, e.time, -1 });
            else if (type == 0x80 || (type == 0x90 && e.b2 == 0))
            {
                for (auto it = notes.rbegin(); it != notes.rend(); ++it)
                    if (it->pitch == e.b1 && it->off < 0) { it->off = juce::jmax (it->on + 1, e.time); break; }
            }
            else if (type == 0xB0 || type == 0xE0)
            {
                // controllers are stored as zero-length "notes" with negative pitch: -(1+cc) / -200 = pitch bend
                const int code = type == 0xE0 ? -200 : -(1 + e.b1);
                const int value = type == 0xE0 ? (e.b1 | (e.b2 << 7)) : e.b2;
                notes.push_back ({ code, value, e.time, e.time });
            }
        }
    };
    take (s1, n1);
    take (s2, n2);
    midiFifo.finishedRead (n1 + n2);
}

void DawEngine::finishRecording()
{
    if (! recSessionActive.load()) return;
    recording = false;
    recSessionActive = false;
    { const juce::ScopedLock sl (processLock); }   // wait for the audio thread to leave the callback
    drainMidiFifo();

    const juce::int64 total = recSamples.load();
    const juce::int64 start = recStartSample;
    const double spb = sampleRate * 60.0 / project.tempo();
    const juce::int64 latency = inputLatency + outputLatency;
    const double bpb = project.beatsPerBar();

    project.undo().beginNewTransaction ("Record");
    int created = 0;

    for (auto& r : recTracks)
    {
        auto t = project.trackById (r->trackId);
        if (! t.isValid()) continue;

        if (r->audio)
        {
            r->writer.reset();   // flush
            if (total < (juce::int64) (sampleRate * 0.1)) { r->file.deleteFile(); continue; }

            const juce::int64 cycleLen = recCycleEnd - recCycleStart;
            const juce::int64 firstPass = recCycle && start < recCycleEnd ? recCycleEnd - start : total;
            const bool cycled = recCycle && cycleLen > 0 && total > firstPass + (juce::int64) (sampleRate * 0.25);

            if (! cycled)
            {
                const double offset = (double) latency / sampleRate;
                const double length = (double) (total - latency) / sampleRate;
                project.addAudioClip (t, r->file, start / spb, offset, juce::jmax (0.05, length), t.name());
            }
            else
            {
                // one take per pass through the cycle, all in the same file
                juce::Array<double> takeOffsets;
                juce::Array<juce::int64> takeLengths;
                takeOffsets.add ((double) (recCycleStart - start + latency) / sampleRate);
                takeLengths.add (firstPass);
                for (juce::int64 fileStart = firstPass; fileStart < total; fileStart += cycleLen)
                {
                    takeOffsets.add ((double) (fileStart + latency) / sampleRate);
                    takeLengths.add (juce::jmin (cycleLen, total - fileStart));
                }
                int active = takeOffsets.size() - 1;
                if (takeLengths[active] < cycleLen / 2 && active > 0) --active;

                auto clip = project.addAudioClip (t, r->file, recCycleStart / spb, takeOffsets[active], (double) cycleLen / sampleRate, t.name());
                for (int i = 0; i < takeOffsets.size(); ++i)
                {
                    juce::ValueTree take (ids::TAKE);
                    take.setProperty (ids::offset, takeOffsets[i], nullptr);
                    clip.v.appendChild (take, nullptr);
                }
                clip.v.setProperty (ids::take, active, nullptr);
                clip.v.setProperty (ids::name, t.name() + " (" + juce::String (takeOffsets.size()) + " takes)", nullptr);
            }
            ++created;
        }
        else
        {
            std::vector<LiveNote> notes;
            {
                const juce::ScopedLock ll (liveLock);
                notes = liveNotes[r->trackId];
            }
            bool anyNote = false;
            for (auto& n : notes) anyNote = anyNote || n.pitch >= 0;
            if (! anyNote) continue;

            const juce::int64 endPos = recCycle ? recCycleEnd : start + total;
            for (auto& n : notes) if (n.off < 0) n.off = juce::jmax (n.on + 1, endPos);

            double clipStart, clipEnd;
            if (recCycle && total > recCycleEnd - start)
            {
                clipStart = recCycleStart / spb;
                clipEnd = recCycleEnd / spb;
            }
            else
            {
                double last = 0;
                for (auto& n : notes) last = juce::jmax (last, n.off / spb);
                clipStart = std::floor (start / spb / bpb) * bpb;
                clipEnd = juce::jmax (clipStart + bpb, std::ceil (last / bpb) * bpb);
            }

            auto clip = project.addMidiClip (t, clipStart, clipEnd - clipStart, t.name());
            for (auto& n : notes)
            {
                const double b = n.on / spb - clipStart;
                if (n.pitch >= 0)
                {
                    juce::ValueTree note (ids::NOTE);
                    note.setProperty (ids::p, n.pitch, nullptr);
                    note.setProperty (ids::s, b, nullptr);
                    note.setProperty (ids::l, juce::jmax (1.0 / 128.0, (n.off - n.on) / spb), nullptr);
                    note.setProperty (ids::v, juce::jlimit (1, 127, n.velocity), nullptr);
                    clip.v.appendChild (note, nullptr);
                }
                else
                {
                    juce::ValueTree cc (ids::CC);
                    cc.setProperty (ids::n, n.pitch == -200 ? ccPitchBend : -n.pitch - 1, nullptr);
                    cc.setProperty (ids::b, b, nullptr);
                    cc.setProperty (ids::v, n.velocity, nullptr);
                    clip.v.appendChild (cc, nullptr);
                }
            }
            ++created;
        }
    }

    recTracks.clear();
    {
        const juce::ScopedLock ll (liveLock);
        liveNotes.clear();
    }
    if (created > 0) project.markDirty();
    if (onRecordingFinished) onRecordingFinished();
}

std::vector<DawEngine::LiveRecording> DawEngine::getLiveRecordings()
{
    std::vector<LiveRecording> result;
    if (! recSessionActive.load()) return result;
    drainMidiFifo();

    const double spb = sampleRate * 60.0 / project.tempo();
    const juce::int64 total = recSamples.load();
    const juce::int64 start = recStartSample;
    juce::int64 end = start + total;
    if (recCycle && end > recCycleEnd && start < recCycleEnd) end = recCycleEnd;

    for (auto& r : recTracks)
    {
        LiveRecording lr;
        lr.trackId = r->trackId;
        lr.audio = r->audio;
        lr.startBeat = start / spb;
        lr.endBeat = juce::jmax (lr.startBeat, end / spb);
        if (r->audio)
        {
            const int n = r->numPeaks.load();
            lr.peaks.assign (r->peaks.begin(), r->peaks.begin() + n);
            lr.peakSeconds = 512.0 / sampleRate;
        }
        else
        {
            const juce::ScopedLock ll (liveLock);
            lr.notes = liveNotes[r->trackId];
        }
        result.push_back (std::move (lr));
    }
    return result;
}

// ---- meters / plugins ----------------------------------------------------------------------------------

std::pair<float, float> DawEngine::readTrackMeter (int id)
{
    if (auto it = runtimes.find (id); it != runtimes.end())
        return { it->second->meterL.exchange (0.0f), it->second->meterR.exchange (0.0f) };
    return { 0.0f, 0.0f };
}

float DawEngine::readTrackInputMeter (int id)
{
    if (auto it = runtimes.find (id); it != runtimes.end())
        return it->second->inputMeter.exchange (0.0f);
    return 0.0f;
}

juce::AudioProcessor* DawEngine::getProcessor (const juce::String& slotId)
{
    if (dirty.load()) rebuildNow();
    if (auto it = slots.find (slotId); it != slots.end())
        return it->second->proc.get();
    return nullptr;
}

juce::String DawEngine::getSlotError (const juce::String& slotId)
{
    if (auto it = slots.find (slotId); it != slots.end())
        return it->second->error;
    return {};
}

void DawEngine::flushPluginState (const juce::ValueTree& node)
{
    if (auto it = slots.find (node[ids::id].toString()); it != slots.end() && it->second->proc != nullptr)
    {
        juce::ValueTree n (node);
        n.setProperty (ids::state, encodeState (*it->second->proc), nullptr);
    }
}

void DawEngine::flushPluginStates()
{
    std::function<void (juce::ValueTree)> visit = [&] (juce::ValueTree v)
    {
        if (v.hasType (ids::PLUGIN)) flushPluginState (v);
        for (auto c : v)
            if (! c.hasType (ids::CLIPS)) visit (c);
    };
    visit (project.tree());
}

// ---- play head ---------------------------------------------------------------------------------------------

juce::Optional<juce::AudioPlayHead::PositionInfo> DawEngine::getPosition() const
{
    // Called by plugins from the audio thread: read only the audio thread's snapshot.
    PositionInfo info;
    auto* snap = audioSnap.get();
    if (snap == nullptr) return info;
    const double spb = snap->samplesPerBeat;
    const double ppq = (double) playheadForPlugins / spb;
    const double bpb = snap->tsNum * 4.0 / snap->tsDen;
    info.setBpm (snap->tempo);
    info.setTimeSignature (juce::AudioPlayHead::TimeSignature { snap->tsNum, snap->tsDen });
    info.setTimeInSamples (playheadForPlugins);
    info.setTimeInSeconds ((double) playheadForPlugins / snap->sampleRate);
    info.setPpqPosition (ppq);
    info.setPpqPositionOfLastBarStart (std::floor (ppq / bpb) * bpb);
    info.setBarCount ((juce::int64) std::floor (ppq / bpb));
    info.setIsPlaying (playheadRolling);
    info.setIsRecording (recording.load());
    info.setIsLooping (snap->cycleOn);
    info.setLoopPoints (juce::AudioPlayHead::LoopPoints { snap->cycleStart / spb, snap->cycleEnd / spb });
    return info;
}

// ---- audio ---------------------------------------------------------------------------------------------------

static inline float safetyLimit (float x, bool& hit) noexcept
{
    const float a = std::abs (x);
    constexpr float knee = 0.891f;
    if (a <= knee) return std::isfinite (x) ? x : 0.0f;
    hit = true;
    const float y = knee + (1.0f - knee) * std::tanh ((a - knee) / (1.0f - knee));
    return x < 0.0f ? -y : y;
}

void DawEngine::audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut, int n,
                                                  const juce::AudioIODeviceCallbackContext&)
{
    juce::ScopedNoDenormals nd;
    const juce::ScopedTryLock sl (processLock);
    if (! sl.isLocked())
    {
        for (int c = 0; c < numOut; ++c) if (out[c] != nullptr) juce::FloatVectorOperations::clear (out[c], n);
        return;
    }

    // drivers occasionally deliver larger blocks than announced: process in chunks
    const int chunk = juce::jmax (1, maxBlock);
    const float* inPtrs[64];
    float* outPtrs[64];
    for (int done = 0; done < n; done += chunk)
    {
        const int len = juce::jmin (chunk, n - done);
        for (int c = 0; c < juce::jmin (numIn, 64); ++c)  inPtrs[c] = in[c] != nullptr ? in[c] + done : nullptr;
        for (int c = 0; c < juce::jmin (numOut, 64); ++c) outPtrs[c] = out[c] != nullptr ? out[c] + done : nullptr;
        renderBlock (inPtrs, juce::jmin (numIn, 64), outPtrs, juce::jmin (numOut, 64), len);
    }
}

void DawEngine::sendNoteOffsToAll (Snapshot& s)
{
    for (auto& tr : s.tracks)
        tr.rt->sendNotesOff = true;
}

void DawEngine::renderBlock (const float* const* in, int numIn, float* const* out, int numOut, int n)
{
    if (! offline.load())
    {
        const juce::SpinLock::ScopedTryLockType sl (snapLock);
        if (sl.isLocked() && pending != audioSnap)
            audioSnap = pending;
    }

    for (int c = 0; c < numOut; ++c) if (out[c] != nullptr) juce::FloatVectorOperations::clear (out[c], n);
    if (audioSnap == nullptr || n > master.getNumSamples()) return;
    auto& s = *audioSnap;

    master.clear (0, n);
    clickBuf.clear (0, n);

    liveMidi.clear();
    if (! offline.load())
    {
        midiCollector.removeNextBlockOfMessages (liveMidi, n);
        keyboardState.processNextMidiBuffer (liveMidi, 0, n, true);
    }

    juce::int64 pos = position.load();
    if (auto seek = seekRequest.exchange (-1); seek >= 0)
    {
        pos = seek;
        sendNoteOffsToAll (s);
    }
    if (stopRequest.exchange (false))
        sendNoteOffsToAll (s);

    int done = 0;

    // count-in: clicks only, the song waits
    if (const auto remaining = countInRemaining.load(); remaining > 0)
    {
        const int c = (int) juce::jmin ((juce::int64) n, remaining);
        renderClick (s, 0, c, countInTotal - remaining);
        renderSegment (s, pos, c, 0, false, in, numIn);
        countInRemaining = remaining - c;
        done = c;
        if (remaining - c == 0)
        {
            recStartSample = pos;
            recSamples = 0;
            sendNoteOffsToAll (s);
            playing = true;
            if (recSessionActive.load()) recording = true;
        }
    }

    while (done < n)
    {
        int len = n - done;
        const bool rolling = playing.load() && countInRemaining.load() == 0;
        if (rolling && s.cycleOn && pos < s.cycleEnd && pos + len > s.cycleEnd)
            len = (int) (s.cycleEnd - pos);

        renderSegment (s, pos, len, done, rolling, in, numIn);
        if (rolling && metronomeOn.load())
            renderClick (s, done, len, pos);

        if (rolling)
        {
            pos += len;
            if (s.cycleOn && pos == s.cycleEnd)
            {
                pos = s.cycleStart;
                sendNoteOffsToAll (s);
            }
        }
        done += len;
    }
    position = pos;

    // master volume, metronome, safety limiter, meters
    const float mv = masterVolume.load();
    const float clickGain = juce::Decibels::decibelsToGain (metronomeVolumeDb.load());
    bool hit = false;
    float pl = 0, pr = 0;
    auto* L = master.getWritePointer (0);
    auto* R = master.getWritePointer (1);
    auto* C = clickBuf.getReadPointer (0);
    for (int i = 0; i < n; ++i)
    {
        const float l = safetyLimit (L[i] * mv + C[i] * clickGain, hit);
        const float r = safetyLimit (R[i] * mv + C[i] * clickGain, hit);
        L[i] = l; R[i] = r;
        pl = juce::jmax (pl, std::abs (l));
        pr = juce::jmax (pr, std::abs (r));
    }
    if (pl > masterL.load()) masterL = pl;
    if (pr > masterR.load()) masterR = pr;
    if (hit) clipped = true;

    if (numOut >= 2)
    {
        if (out[0] != nullptr) juce::FloatVectorOperations::copy (out[0], L, n);
        if (out[1] != nullptr) juce::FloatVectorOperations::copy (out[1], R, n);
    }
    else if (numOut == 1 && out[0] != nullptr)
    {
        for (int i = 0; i < n; ++i) out[0][i] = 0.5f * (L[i] + R[i]);
    }
}

void DawEngine::renderSegment (Snapshot& s, juce::int64 t0, int len, int off, bool rolling, const float* const* in, int numIn)
{
    playheadForPlugins = t0;
    playheadRolling = rolling;
    const juce::int64 t1 = t0 + len;
    const bool rec = recording.load() && rolling;
    const int selected = selectedTrackId.load();
    static juce::MidiBuffer emptyMidi;

    // tracks are summed straight into this segment of the master buffer, then the master inserts run on it
    juce::AudioBuffer<float> seg (master.getArrayOfWritePointers(), 2, off, len);

    for (auto& tr : s.tracks)
    {
        auto& rt = *tr.rt;
        if (rt.buffer.getNumSamples() < len) continue;
        juce::AudioBuffer<float> buf (rt.buffer.getArrayOfWritePointers(), 2, len);
        buf.clear();
        rt.midi.clear();

        if (rt.sendNotesOff)
        {
            for (int p = 0; p < 128; ++p)
                if (rt.playingNotes[(size_t) p]) rt.midi.addEvent (juce::MidiMessage::noteOff (1, p), 0);
            rt.midi.addEvent (juce::MidiMessage::controllerEvent (1, 64, 0), 0);
            rt.midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
            rt.playingNotes.reset();
            rt.sendNotesOff = false;
        }

        if (rolling)
        {
            for (auto& mc : tr.midi)
            {
                if (mc.end <= t0 || mc.start >= t1 || mc.events.empty()) continue;
                auto it = std::lower_bound (mc.events.begin(), mc.events.end(), t0, [] (const auto& e, juce::int64 t) { return e.time < t; });
                for (; it != mc.events.end() && it->time < t1; ++it)
                {
                    rt.midi.addEvent (it->bytes, it->size, (int) (it->time - t0));
                    const int type = it->bytes[0] & 0xf0;
                    if (type == 0x90) rt.playingNotes.set (it->bytes[1]);
                    else if (type == 0x80) rt.playingNotes.reset (it->bytes[1]);
                }
            }

            for (auto& ac : tr.audio)
            {
                const juce::int64 a = juce::jmax (t0, ac.start), b = juce::jmin (t1, ac.start + ac.length);
                if (a >= b) continue;
                auto& src = ac.data->buffer;
                const int srcLen = src.getNumSamples();
                const int srcCh = src.getNumChannels();
                for (juce::int64 t = a; t < b; ++t)
                {
                    const juce::int64 inClip = t - ac.start;
                    const juce::int64 si = ac.offset + inClip;
                    if (si < 0 || si >= srcLen) continue;
                    float g = ac.gain;
                    if (ac.fadeIn > 0 && inClip < ac.fadeIn) g *= (float) inClip / (float) ac.fadeIn;
                    const juce::int64 toEnd = ac.length - inClip;
                    if (ac.fadeOut > 0 && toEnd < ac.fadeOut) g *= (float) toEnd / (float) ac.fadeOut;
                    const int o = (int) (t - t0);
                    const float l = src.getSample (0, (int) si) * g;
                    const float r = srcCh > 1 ? src.getSample (1, (int) si) * g : l;
                    buf.addSample (0, o, l);
                    buf.addSample (1, o, r);
                }
            }
        }

        // live MIDI from keyboards / musical typing
        if (tr.instrument && (rt.id == selected || rt.arm.load()))
        {
            for (const auto meta : liveMidi)
            {
                if (meta.samplePosition < off || meta.samplePosition >= off + len) continue;
                const auto m = meta.getMessage();
                rt.midi.addEvent (m, meta.samplePosition - off);
                if (rec && rt.arm.load() && (m.isNoteOnOrOff() || m.isController() || m.isPitchWheel()))
                {
                    int s1, n1, s2, n2;
                    midiFifo.prepareToWrite (1, s1, n1, s2, n2);
                    if (n1 > 0)
                    {
                        auto* raw = m.getRawData();
                        const auto when = juce::jmax ((juce::int64) 0, t0 + (meta.samplePosition - off) - (juce::int64) outputLatency);
                        midiFifoData[(size_t) s1] = { when, rt.id, raw[0], (juce::uint8) (m.getRawDataSize() > 1 ? raw[1] : 0),
                                                      (juce::uint8) (m.getRawDataSize() > 2 ? raw[2] : 0) };
                        midiFifo.finishedWrite (1);
                    }
                }
            }
        }

        // audio input (monitoring + level meter for armed tracks)
        if (! tr.instrument && in != nullptr)
        {
            const int ch = rt.input.load();
            const bool stereoIn = rt.inputStereo.load();
            const float* inL = ch >= 0 && ch < numIn ? in[ch] : nullptr;
            const float* inR = stereoIn && ch + 1 < numIn ? in[ch + 1] : inL;
            if (inL != nullptr)
            {
                float pk = 0;
                for (int i = 0; i < len; ++i) pk = juce::jmax (pk, std::abs (inL[off + i]));
                if (pk > rt.inputMeter.load()) rt.inputMeter = pk;
                if (rt.monitor.load())
                {
                    buf.addFrom (0, 0, inL + off, len);
                    buf.addFrom (1, 0, (inR != nullptr ? inR : inL) + off, len);
                }
            }
        }

        if (tr.inst != nullptr)
            tr.inst->process (buf, len, rt.midi);

        for (auto* slot : tr.inserts)
        {
            emptyMidi.clear();
            slot->process (buf, len, emptyMidi);
        }

        // volume / pan (automation overrides the fader)
        const float volDb = ! tr.volume.points.empty() && rolling ? tr.volume.valueAt (t0) : rt.volumeDb.load();
        const float pan = ! tr.pan.points.empty() && rolling ? tr.pan.valueAt (t0) : rt.pan.load();
        const float g = juce::Decibels::decibelsToGain (volDb, -96.0f);
        const float angle = (juce::jlimit (-1.0f, 1.0f, pan) + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        const float gl = g * std::cos (angle) * juce::MathConstants<float>::sqrt2;
        const float gr = g * std::sin (angle) * juce::MathConstants<float>::sqrt2;
        buf.applyGainRamp (0, 0, len, rt.lastGainL, gl);
        buf.applyGainRamp (1, 0, len, rt.lastGainR, gr);
        rt.lastGainL = gl;
        rt.lastGainR = gr;

        const float ml = buf.getMagnitude (0, 0, len), mr = buf.getMagnitude (1, 0, len);
        if (ml > rt.meterL.load()) rt.meterL = ml;
        if (mr > rt.meterR.load()) rt.meterR = mr;

        const bool audible = ! rt.mute.load() && (! s.anySolo || rt.solo.load());
        if (audible)
        {
            seg.addFrom (0, 0, buf, 0, 0, len);
            seg.addFrom (1, 0, buf, 1, 0, len);
        }
    }

    for (auto* slot : s.masterInserts)
    {
        emptyMidi.clear();
        slot->process (seg, len, emptyMidi);
    }

    // recording: capture the raw input
    if (rec && in != nullptr)
    {
        for (auto& r : recTracks)
        {
            if (! r->audio || r->writer == nullptr) continue;
            const float* l = r->input >= 0 && r->input < numIn ? in[r->input] : nullptr;
            const float* rr = r->stereo && r->input + 1 < numIn ? in[r->input + 1] : l;
            if (l == nullptr) continue;
            const float* chans[2] = { l + off, (rr != nullptr ? rr : l) + off };
            r->writer->write (chans, len);

            for (int i = 0; i < len; ++i)
            {
                r->peakAcc = juce::jmax (r->peakAcc, std::abs (l[off + i]));
                if (++r->peakCount >= 512)
                {
                    const int np = r->numPeaks.load();
                    if (np < (int) r->peaks.size()) { r->peaks[(size_t) np] = r->peakAcc; r->numPeaks = np + 1; }
                    r->peakAcc = 0.0f;
                    r->peakCount = 0;
                }
            }
        }
    }
    if (rec) recSamples += len;
}

void DawEngine::renderClick (const Snapshot& s, int off, int len, juce::int64 t0)
{
    const int num = juce::jmax (1, s.tsNum);
    const double beatUnit = s.samplesPerBeat * 4.0 / juce::jmax (1, s.tsDen);

    auto* c = clickBuf.getWritePointer (0) + off;
    // find beat starts inside [t0, t0 + len)
    juce::int64 k = (juce::int64) std::ceil ((double) t0 / beatUnit - 1.0e-9);
    juce::int64 nextBeat = (juce::int64) std::llround (k * beatUnit);
    for (int i = 0; i < len; ++i)
    {
        if (t0 + i == nextBeat)
        {
            const bool accent = (k % num) == 0;
            clickFreq = accent ? 1760.0f : 1175.0f;
            clickAmp = accent ? 0.55f : 0.35f;
            clickRemaining = (int) (sampleRate * 0.035);
            clickPhase = 0.0f;
            ++k;
            nextBeat = (juce::int64) std::llround (k * beatUnit);
        }
        if (clickRemaining > 0)
        {
            const float env = (float) clickRemaining / (float) (sampleRate * 0.035);
            c[i] += std::sin (clickPhase) * clickAmp * env * env;
            clickPhase += juce::MathConstants<float>::twoPi * clickFreq / (float) sampleRate;
            --clickRemaining;
        }
    }
}

// ---- export ----------------------------------------------------------------------------------------------------

void DawEngine::beginExport()
{
    exportDevice = deviceManager;
    if (exportDevice != nullptr) exportDevice->removeAudioCallback (this);   // the audio thread must not run while we render
    stop();

    // make sure every file is decoded, and the snapshot is current
    for (auto tv : project.tracks())
        for (auto cv : Track (tv).clips())
            if (Clip (cv).isAudio())
                cache.getBlocking (project.resolve (Clip (cv).file()), sampleRate);
    rebuildNow();
    offline = true;
}

void DawEngine::endExport()
{
    offline = false;
    if (exportDevice != nullptr) exportDevice->addAudioCallback (this);
    exportDevice = nullptr;
}

juce::String DawEngine::exportAudio (const ExportOptions& o, const std::function<bool (float)>& progress)
{
    beginExport();
    auto r = renderExport (o, progress);
    endExport();
    return r;
}

juce::String DawEngine::renderExport (const ExportOptions& o, const std::function<bool (float)>& progress)
{
    const auto savedPos = position.load();
    const bool savedMetro = metronomeOn.load();
    metronomeOn = false;
    playing = false;
    countInRemaining = 0;

    Snapshot::Ptr snap;
    {
        const juce::SpinLock::ScopedLockType sl (snapLock);
        snap = pending;
    }
    // solo the requested tracks for stem export (temporarily, without touching the project)
    std::map<int, std::pair<bool, bool>> savedMuteSolo;
    if (! o.onlyTracks.isEmpty())
        for (auto& tr : snap->tracks)
        {
            savedMuteSolo[tr.rt->id] = { tr.rt->mute.load(), tr.rt->solo.load() };
            tr.rt->mute = ! o.onlyTracks.contains (tr.rt->id);
            tr.rt->solo = false;
        }
    const bool savedAnySolo = snap->anySolo;
    const bool savedCycle = snap->cycleOn;
    if (! o.onlyTracks.isEmpty()) snap->anySolo = false;
    snap->cycleOn = false;

    for (auto& [id, slot] : slots) if (slot->proc) slot->proc->setNonRealtime (true);

    const double spb = sampleRate * 60.0 / project.tempo();
    const juce::int64 start = (juce::int64) (o.startBeat * spb);
    const juce::int64 end = (juce::int64) (o.endBeat * spb + o.tailSeconds * sampleRate);
    const int total = (int) juce::jmax ((juce::int64) 1, end - start);

    juce::AudioBuffer<float> result (2, total);
    result.clear();

    {
        const juce::ScopedLock sl (processLock);
        audioSnap = snap;
        for (auto& tr : snap->tracks) { tr.rt->sendNotesOff = true; tr.rt->lastGainL = tr.rt->lastGainR = -1.0f; }
        position = start;
        seekRequest = -1;
        playing = true;

        juce::String cancelled;
        const int block = juce::jmin (512, maxBlock);
        float* outs[2];
        for (int doneSamples = 0; doneSamples < total; doneSamples += block)
        {
            const int len = juce::jmin (block, total - doneSamples);
            outs[0] = result.getWritePointer (0, doneSamples);
            outs[1] = result.getWritePointer (1, doneSamples);
            // first block: snap the gain ramps instead of fading in from the live values
            for (auto& tr : snap->tracks)
                if (tr.rt->lastGainL < 0) { tr.rt->lastGainL = tr.rt->lastGainR = juce::Decibels::decibelsToGain (tr.rt->volumeDb.load()); }
            renderBlock (nullptr, 0, outs, 2, len);
            if (progress && (doneSamples / block) % 32 == 0 && ! progress ((float) doneSamples / (float) total))
            {
                cancelled = "Cancelled";
                break;
            }
        }

        playing = false;
        for (auto& tr : snap->tracks) tr.rt->sendNotesOff = true;
        position = savedPos;
        snap->anySolo = savedAnySolo;
        snap->cycleOn = savedCycle;
        for (auto& tr : snap->tracks)
            if (auto it = savedMuteSolo.find (tr.rt->id); it != savedMuteSolo.end())
            { tr.rt->mute = it->second.first; tr.rt->solo = it->second.second; }
        metronomeOn = savedMetro;
        for (auto& [id, slot] : slots) if (slot->proc) slot->proc->setNonRealtime (false);

        if (cancelled.isNotEmpty())
            return cancelled;
    }

    // post: resample, normalise, write
    juce::AudioBuffer<float> outBuf = o.sampleRate > 0 && std::abs (o.sampleRate - sampleRate) > 0.5
                                    ? wis::resampleBuffer (result, sampleRate, o.sampleRate) : std::move (result);
    const double outRate = o.sampleRate > 0 ? o.sampleRate : sampleRate;
    if (o.normalise)
    {
        const float peak = outBuf.getMagnitude (0, outBuf.getNumSamples());
        if (peak > 1.0e-6f) outBuf.applyGain (juce::Decibels::decibelsToGain (-0.3f) / peak);
    }

    o.file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream (o.file.createOutputStream().release());
    if (stream == nullptr) return "Couldn't write " + o.file.getFullPathName();

    std::unique_ptr<juce::AudioFormat> fmt;
    auto opts = juce::AudioFormatWriterOptions{}.withSampleRate (outRate).withNumChannels (2);
    if (o.format == 1)      { fmt = std::make_unique<juce::FlacAudioFormat>(); opts = opts.withBitsPerSample (juce::jmin (24, o.bitDepth)); }
    else if (o.format == 2) { fmt = std::make_unique<juce::OggVorbisAudioFormat>(); opts = opts.withBitsPerSample (16).withQualityOptionIndex (8); }
    else                    { fmt = std::make_unique<juce::WavAudioFormat>(); opts = opts.withBitsPerSample (o.bitDepth); }

    auto writer = fmt->createWriterFor (stream, opts);
    if (writer == nullptr) return "Couldn't create the " + fmt->getFormatName() + " encoder.";
    writer->writeFromAudioSampleBuffer (outBuf, 0, outBuf.getNumSamples());
    if (progress) progress (1.0f);
    return {};
}

} // namespace wis::daw
