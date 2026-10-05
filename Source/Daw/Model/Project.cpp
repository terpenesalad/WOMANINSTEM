#include "Project.h"
#include "Daw/Instruments/InstrumentRefs.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace wis::daw
{

juce::Colour trackColourForIndex (int index)
{
    static const juce::uint32 palette[] = {
        0xff5b8def, 0xff4fc3a1, 0xfff2b84b, 0xffef6f6c, 0xffa98bf5, 0xff47b8e0,
        0xfff58ec1, 0xff8bc34a, 0xffff8a50, 0xff26a69a, 0xffba68c8, 0xffd4c35a
    };
    return juce::Colour (palette[(size_t) (std::abs (index) % (int) std::size (palette))]);
}

double Clip::lengthBeats (double tempo) const
{
    return isMidi() ? midiLength() : lengthSeconds() * tempo / 60.0;
}

// =====================================================================================================

Project::Project()
{
    initialiseTree ("Untitled");
}

void Project::initialiseTree (const juce::String& name)
{
    root = juce::ValueTree (ids::PROJECT);
    root.setProperty (ids::name, name, nullptr);
    root.setProperty (ids::version, 1, nullptr);
    root.setProperty (ids::tempo, 120.0, nullptr);
    root.setProperty (ids::tsNum, 4, nullptr);
    root.setProperty (ids::tsDen, 4, nullptr);
    root.setProperty (ids::cycleOn, false, nullptr);
    root.setProperty (ids::cycleStart, 0.0, nullptr);
    root.setProperty (ids::cycleEnd, 16.0, nullptr);
    root.setProperty (ids::metronome, false, nullptr);
    root.setProperty (ids::countIn, true, nullptr);
    root.setProperty (ids::masterVolume, 0.0, nullptr);
    root.setProperty (ids::playhead, 0.0, nullptr);
    root.setProperty (ids::zoom, 24.0, nullptr);
    root.setProperty (ids::nextId, 1, nullptr);
    ensureStructure();
}

void Project::ensureStructure()
{
    if (! root.getChildWithName (ids::TRACKS).isValid())
        root.appendChild (juce::ValueTree (ids::TRACKS), nullptr);
    auto master = root.getChildWithName (ids::MASTER);
    if (! master.isValid())
    {
        master = juce::ValueTree (ids::MASTER);
        root.appendChild (master, nullptr);
    }
    if (! master.getChildWithName (ids::INSERTS).isValid())
        master.appendChild (juce::ValueTree (ids::INSERTS), nullptr);

    for (auto t : tracks())
        for (auto childId : { ids::INSERTS, ids::CLIPS, ids::AUTOMATION })
            if (! t.getChildWithName (childId).isValid())
                t.appendChild (juce::ValueTree (childId), nullptr);
}

int Project::allocateId()
{
    const int id = juce::jmax (1, (int) root.getProperty (ids::nextId, 1));
    root.setProperty (ids::nextId, id + 1, nullptr);   // never undone: ids stay unique
    return id;
}

// ---- file ------------------------------------------------------------------------------------------------

juce::File Project::defaultProjectsFolder()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("WOMANINSTEM Projects");
    dir.createDirectory();
    return dir;
}

juce::File Project::audioFolder() const
{
    auto d = getFolder().getChildFile ("Audio Files");
    d.createDirectory();
    return d;
}

void Project::createNew (const juce::String& name)
{
    auto clean = juce::File::createLegalFileName (name.isNotEmpty() ? name : juce::String ("Untitled"));
    auto folder = defaultProjectsFolder().getChildFile (clean);
    int n = 2;
    while (folder.exists())
        folder = defaultProjectsFolder().getChildFile (clean + " " + juce::String (n++));
    folder.createDirectory();

    file = folder.getChildFile (folder.getFileName() + fileExtension());
    initialiseTree (folder.getFileName());
    undoManager.clearUndoHistory();
    saved = false;
    dirty = false;
    if (onLoaded) onLoaded();
}

juce::String Project::save()
{
    if (file == juce::File())
        return "The project has no location.";

    getFolder().createDirectory();
    auto xml = root.createXml();
    if (xml == nullptr)
        return "Couldn't serialise the project.";

    // write atomically: temp file then replace, and keep one backup
    auto temp = file.getSiblingFile (file.getFileName() + ".tmp");
    if (! xml->writeTo (temp))
        return "Couldn't write " + temp.getFullPathName();
    if (file.existsAsFile())
        file.copyFileTo (file.getSiblingFile (file.getFileNameWithoutExtension() + ".backup" + fileExtension()));
    if (! temp.moveFileTo (file))
        return "Couldn't save " + file.getFullPathName();

    saved = true;
    dirty = false;
    return {};
}

juce::String Project::saveAs (const juce::File& newFile)
{
    auto target = newFile.withFileExtension (fileExtension());
    auto newFolder = target.getParentDirectory();

    // Put each project in its own folder.
    if (newFolder.getFileName() != target.getFileNameWithoutExtension())
    {
        newFolder = newFolder.getChildFile (target.getFileNameWithoutExtension());
        target = newFolder.getChildFile (target.getFileName());
    }
    newFolder.createDirectory();

    auto oldAudio = getFolder().getChildFile ("Audio Files");
    if (oldAudio.isDirectory() && newFolder != getFolder())
        oldAudio.copyDirectoryTo (newFolder.getChildFile ("Audio Files"));

    file = target;
    root.setProperty (ids::name, target.getFileNameWithoutExtension(), nullptr);
    return save();
}

juce::String Project::load (const juce::File& f)
{
    auto xml = juce::XmlDocument::parse (f);
    if (xml == nullptr)
        return "Couldn't read " + f.getFileName() + " (not a WOMANINSTEM project?)";

    auto tree = juce::ValueTree::fromXml (*xml);
    if (! tree.hasType (ids::PROJECT))
        return f.getFileName() + " isn't a WOMANINSTEM project.";

    root = tree;
    file = f;
    ensureStructure();
    undoManager.clearUndoHistory();
    saved = true;
    dirty = false;
    if (onLoaded) onLoaded();
    return {};
}

juce::File Project::resolve (const juce::String& ref) const
{
    if (ref.isEmpty()) return {};
    if (juce::File::isAbsolutePath (ref)) return juce::File (ref);
    return getFolder().getChildFile (ref);
}

juce::String Project::makeRef (const juce::File& f) const
{
    if (f.isAChildOf (getFolder()))
        return f.getRelativePathFrom (getFolder()).replaceCharacter ('\\', '/');
    return f.getFullPathName();
}

juce::File Project::importIntoProject (const juce::File& source)
{
    if (source.isAChildOf (getFolder()))
        return source;

    auto dest = audioFolder().getNonexistentChildFile (source.getFileNameWithoutExtension(), source.getFileExtension(), false);
    if (source.copyFileTo (dest))
        return dest;
    return source;   // fall back to referencing it in place
}

juce::File Project::newRecordingFile (const juce::String& trackName)
{
    auto base = juce::File::createLegalFileName (trackName.isNotEmpty() ? trackName : juce::String ("Audio"));
    return audioFolder().getNonexistentChildFile (base + " #1", ".wav", false);
}

// ---- settings -------------------------------------------------------------------------------------------

void Project::setTempo (double bpm)
{
    root.setProperty (ids::tempo, juce::jlimit (20.0, 300.0, bpm), um());
}

void Project::setTimeSignature (int num, int den)
{
    root.setProperty (ids::tsNum, juce::jlimit (1, 16, num), um());
    root.setProperty (ids::tsDen, den == 2 || den == 8 || den == 16 ? den : 4, um());
}

// ---- tracks ---------------------------------------------------------------------------------------------

Track Project::trackById (int id) const
{
    for (auto t : tracks())
        if ((int) t[ids::id] == id)
            return Track (t);
    return Track();
}

Track Project::trackForClip (const juce::ValueTree& clip) const
{
    auto clips = clip.getParent();
    return Track (clips.isValid() ? clips.getParent() : juce::ValueTree());
}

Clip Project::clipById (int id) const
{
    for (auto t : tracks())
        for (auto c : t.getChildWithName (ids::CLIPS))
            if ((int) c[ids::id] == id)
                return Clip (c);
    return Clip();
}

Track Project::addTrack (const juce::String& kind, const juce::String& name, int insertIndex)
{
    juce::ValueTree t (ids::TRACK);
    t.setProperty (ids::id, allocateId(), nullptr);
    t.setProperty (ids::name, name, nullptr);
    t.setProperty (ids::kind, kind, nullptr);
    t.setProperty (ids::colour, (juce::int64) trackColourForIndex (numTracks()).getARGB(), nullptr);
    t.setProperty (ids::volume, 0.0, nullptr);
    t.setProperty (ids::pan, 0.0, nullptr);
    t.setProperty (ids::mute, false, nullptr);
    t.setProperty (ids::solo, false, nullptr);
    t.setProperty (ids::arm, false, nullptr);
    t.setProperty (ids::monitor, kind == kindInstrument, nullptr);
    t.setProperty (ids::input, 0, nullptr);
    t.setProperty (ids::inputStereo, false, nullptr);
    t.setProperty (ids::height, 72, nullptr);
    if (kind == kindInstrument)
    {
        juce::ValueTree inst (ids::INSTRUMENT);
        t.appendChild (inst, nullptr);
    }
    t.appendChild (juce::ValueTree (ids::INSERTS), nullptr);
    t.appendChild (juce::ValueTree (ids::CLIPS), nullptr);
    t.appendChild (juce::ValueTree (ids::AUTOMATION), nullptr);

    tracks().addChild (t, insertIndex, um());
    return Track (t);
}

void Project::removeTrack (const Track& t)
{
    if (t.isValid())
        tracks().removeChild (t.v, um());
}

void Project::moveTrack (int from, int to)
{
    const int n = numTracks();
    if (juce::isPositiveAndBelow (from, n) && juce::isPositiveAndBelow (to, n) && from != to)
        tracks().moveChild (from, to, um());
}

Track Project::duplicateTrack (const Track& t)
{
    auto copy = t.v.createCopy();
    copy.setProperty (ids::id, allocateId(), nullptr);
    copy.setProperty (ids::name, t.name() + " copy", nullptr);
    copy.setProperty (ids::arm, false, nullptr);
    for (auto c : copy.getChildWithName (ids::CLIPS))
        c.setProperty (ids::id, allocateId(), nullptr);
    tracks().addChild (copy, tracks().indexOf (t.v) + 1, um());
    return Track (copy);
}

juce::ValueTree Project::makePluginNode (const PluginRef& ref)
{
    juce::ValueTree p (ids::PLUGIN);
    p.setProperty (ids::type, ref.type, nullptr);
    p.setProperty (ids::uid, ref.uid, nullptr);
    p.setProperty (ids::name, ref.name, nullptr);
    p.setProperty (ids::bypass, false, nullptr);
    p.setProperty (ids::id, juce::Uuid().toString(), nullptr);   // slot identity for the engine
    if (ref.descXml.isNotEmpty()) p.setProperty (ids::desc, ref.descXml, nullptr);
    if (ref.state.isNotEmpty())   p.setProperty (ids::state, ref.state, nullptr);
    return p;
}

void Project::setPlugin (juce::ValueTree parent, int index, const PluginRef& ref)
{
    auto node = makePluginNode (ref);
    if (index >= 0 && index < parent.getNumChildren())
    {
        parent.removeChild (index, um());
        parent.addChild (node, index, um());
    }
    else
    {
        parent.appendChild (node, um());
    }
}

void Project::setInstrument (const Track& t, const PluginRef& ref)
{
    auto inst = t.v.getChildWithName (ids::INSTRUMENT);
    if (! inst.isValid())
    {
        inst = juce::ValueTree (ids::INSTRUMENT);
        t.v.appendChild (inst, um());
    }
    inst.removeAllChildren (um());
    inst.appendChild (makePluginNode (ref), um());
}

// ---- clips ---------------------------------------------------------------------------------------------

Clip Project::addAudioClip (const Track& t, const juce::File& f, double startBeats, double offsetSeconds, double lengthSeconds,
                            const juce::String& name)
{
    juce::ValueTree c (ids::CLIP);
    c.setProperty (ids::id, allocateId(), nullptr);
    c.setProperty (ids::kind, kindAudio, nullptr);
    c.setProperty (ids::name, name.isNotEmpty() ? name : f.getFileNameWithoutExtension(), nullptr);
    c.setProperty (ids::start, juce::jmax (0.0, startBeats), nullptr);
    c.setProperty (ids::file, makeRef (f), nullptr);
    c.setProperty (ids::offset, offsetSeconds, nullptr);
    c.setProperty (ids::length, lengthSeconds, nullptr);
    c.setProperty (ids::gain, 0.0, nullptr);
    c.setProperty (ids::fadeIn, 0.0, nullptr);
    c.setProperty (ids::fadeOut, 0.0, nullptr);
    t.clips().appendChild (c, um());
    return Clip (c);
}

Clip Project::addMidiClip (const Track& t, double startBeats, double lengthBeats, const juce::String& name)
{
    juce::ValueTree c (ids::CLIP);
    c.setProperty (ids::id, allocateId(), nullptr);
    c.setProperty (ids::kind, kindMidi, nullptr);
    c.setProperty (ids::name, name.isNotEmpty() ? name : t.name(), nullptr);
    c.setProperty (ids::start, juce::jmax (0.0, startBeats), nullptr);
    c.setProperty (ids::length, juce::jmax (0.25, lengthBeats), nullptr);
    t.clips().appendChild (c, um());
    return Clip (c);
}

void Project::addNote (const Clip& c, int pitch, double startBeats, double lengthBeats, int velocity)
{
    juce::ValueTree n (ids::NOTE);
    n.setProperty (ids::p, juce::jlimit (0, 127, pitch), nullptr);
    n.setProperty (ids::s, startBeats, nullptr);
    n.setProperty (ids::l, juce::jmax (1.0 / 64.0, lengthBeats), nullptr);
    n.setProperty (ids::v, juce::jlimit (1, 127, velocity), nullptr);
    c.v.appendChild (n, um());
}

void Project::addController (const Clip& c, int number, double beat, int value)
{
    juce::ValueTree n (ids::CC);
    n.setProperty (ids::n, number, nullptr);
    n.setProperty (ids::b, beat, nullptr);
    n.setProperty (ids::v, value, nullptr);
    c.v.appendChild (n, um());
}

void Project::moveClip (const Clip& c, const Track& toTrack, double newStart)
{
    c.v.setProperty (ids::start, juce::jmax (0.0, newStart), um());
    auto from = trackForClip (c.v);
    if (toTrack.isValid() && from.v != toTrack.v && toTrack.isInstrument() == c.isMidi())
    {
        auto node = c.v;
        from.clips().removeChild (node, um());
        toTrack.clips().appendChild (node, um());
    }
}

Clip Project::duplicateClip (const Clip& c, double newStart, const Track& toTrack)
{
    auto copy = c.v.createCopy();
    copy.setProperty (ids::id, allocateId(), nullptr);
    copy.setProperty (ids::start, juce::jmax (0.0, newStart), nullptr);
    auto dest = toTrack.isValid() && toTrack.isInstrument() == c.isMidi() ? toTrack : trackForClip (c.v);
    dest.clips().appendChild (copy, um());
    return Clip (copy);
}

void Project::deleteClip (const Clip& c)
{
    auto parent = c.v.getParent();
    if (parent.isValid())
        parent.removeChild (c.v, um());
}

Clip Project::splitClip (const Clip& c, double at)
{
    const double t = tempo();
    const double start = c.start(), end = c.endBeats (t);
    if (at <= start + 1.0e-6 || at >= end - 1.0e-6)
        return Clip();

    auto right = c.v.createCopy();
    right.setProperty (ids::id, allocateId(), nullptr);
    right.setProperty (ids::start, at, nullptr);

    const double leftBeats = at - start;

    if (c.isAudio())
    {
        const double leftSecs = leftBeats * 60.0 / t;
        right.setProperty (ids::offset, c.offsetSeconds() + leftSecs, nullptr);
        right.setProperty (ids::length, c.lengthSeconds() - leftSecs, nullptr);
        right.setProperty (ids::fadeIn, 0.0, nullptr);
        c.v.setProperty (ids::length, leftSecs, um());
        c.v.setProperty (ids::fadeOut, 0.0, um());
        // takes keep working: each take offset moves with the split
        for (auto take : right)
            if (take.hasType (ids::TAKE))
                take.setProperty (ids::offset, (double) take[ids::offset] + leftSecs, nullptr);
    }
    else
    {
        right.setProperty (ids::length, c.midiLength() - leftBeats, nullptr);
        // right-hand clip: shift everything left; notes that started before the split stay in the left clip
        for (int i = right.getNumChildren(); --i >= 0;)
        {
            auto e = right.getChild (i);
            if (e.hasType (ids::NOTE))
            {
                const double s = (double) e[ids::s];
                if (s < leftBeats) right.removeChild (i, nullptr);
                else e.setProperty (ids::s, s - leftBeats, nullptr);
            }
            else if (e.hasType (ids::CC))
            {
                const double b = (double) e[ids::b];
                if (b < leftBeats) right.removeChild (i, nullptr);
                else e.setProperty (ids::b, b - leftBeats, nullptr);
            }
        }
        // left clip: drop notes that start after the split, shorten ones that cross it
        for (int i = c.v.getNumChildren(); --i >= 0;)
        {
            auto e = c.v.getChild (i);
            if (e.hasType (ids::NOTE))
            {
                const double s = (double) e[ids::s], l = (double) e[ids::l];
                if (s >= leftBeats) c.v.removeChild (i, um());
                else if (s + l > leftBeats) e.setProperty (ids::l, leftBeats - s, um());
            }
            else if (e.hasType (ids::CC) && (double) e[ids::b] >= leftBeats)
                c.v.removeChild (i, um());
        }
        c.v.setProperty (ids::length, leftBeats, um());
    }

    c.v.getParent().appendChild (right, um());
    return Clip (right);
}

void Project::trimClipStart (const Clip& c, double newStart)
{
    const double t = tempo();
    newStart = juce::jlimit (0.0, c.endBeats (t) - 1.0 / 16.0, newStart);
    double delta = newStart - c.start();

    if (c.isAudio())
    {
        double deltaSecs = delta * 60.0 / t;
        if (c.offsetSeconds() + deltaSecs < 0.0)   // can't reveal audio before the file start
        {
            deltaSecs = -c.offsetSeconds();
            delta = deltaSecs * t / 60.0;
        }
        c.v.setProperty (ids::offset, c.offsetSeconds() + deltaSecs, um());
        c.v.setProperty (ids::length, c.lengthSeconds() - deltaSecs, um());
        for (auto take : c.v)
            if (take.hasType (ids::TAKE))
                take.setProperty (ids::offset, (double) take[ids::offset] + deltaSecs, um());
    }
    else
    {
        // non-destructive: notes keep their absolute position (they may now sit at negative offsets)
        for (auto e : c.v)
        {
            if (e.hasType (ids::NOTE)) e.setProperty (ids::s, (double) e[ids::s] - delta, um());
            else if (e.hasType (ids::CC)) e.setProperty (ids::b, (double) e[ids::b] - delta, um());
        }
        c.v.setProperty (ids::length, c.midiLength() - delta, um());
    }
    c.v.setProperty (ids::start, c.start() + delta, um());
}

void Project::trimClipEnd (const Clip& c, double newEnd)
{
    const double t = tempo();
    newEnd = juce::jmax (c.start() + 1.0 / 16.0, newEnd);
    const double beats = newEnd - c.start();
    if (c.isAudio())
        c.v.setProperty (ids::length, beats * 60.0 / t, um());
    else
        c.v.setProperty (ids::length, beats, um());
}

void Project::quantize (const Clip& c, double grid, float strength, bool selectedOnly, const juce::Array<juce::ValueTree>& selection)
{
    if (! c.isMidi() || grid <= 0.0) return;
    const double clipStart = c.start();
    for (auto n : c.v)
    {
        if (! n.hasType (ids::NOTE)) continue;
        if (selectedOnly && ! selection.contains (n)) continue;
        const double abs = clipStart + (double) n[ids::s];
        const double q = std::round (abs / grid) * grid;
        n.setProperty (ids::s, (double) n[ids::s] + (q - abs) * strength, um());
    }
}

void Project::setTake (const Clip& c, int takeIndex)
{
    int i = 0;
    for (auto take : c.v)
    {
        if (! take.hasType (ids::TAKE)) continue;
        if (i++ == takeIndex)
        {
            c.v.setProperty (ids::offset, (double) take[ids::offset], um());
            c.v.setProperty (ids::take, takeIndex, um());
            return;
        }
    }
}

double Project::contentEndBeats() const
{
    double end = 0.0;
    const double t = tempo();
    for (auto tr : tracks())
        for (auto c : tr.getChildWithName (ids::CLIPS))
            end = juce::jmax (end, Clip (c).endBeats (t));
    return end;
}

// ---- automation ------------------------------------------------------------------------------------------

juce::ValueTree Project::getOrCreateLane (const Track& t, const juce::String& param)
{
    auto lane = t.lane (param);
    if (! lane.isValid())
    {
        lane = juce::ValueTree (ids::LANE);
        lane.setProperty (ids::param, param, nullptr);
        t.automation().appendChild (lane, um());
    }
    return lane;
}

float Project::automationValueAt (const juce::ValueTree& lane, double beat, float fallback)
{
    if (! lane.isValid() || lane.getNumChildren() == 0)
        return fallback;

    // points are kept sorted by the editor
    juce::ValueTree prev, next;
    for (auto p : lane)
    {
        if ((double) p[ids::b] <= beat) prev = p;
        else { next = p; break; }
    }
    if (! prev.isValid()) return (float) (double) next[ids::v];
    if (! next.isValid()) return (float) (double) prev[ids::v];
    const double b0 = prev[ids::b], b1 = next[ids::b];
    const double t = b1 > b0 ? (beat - b0) / (b1 - b0) : 0.0;
    return (float) ((double) prev[ids::v] + ((double) next[ids::v] - (double) prev[ids::v]) * t);
}

// ---- MIDI files -----------------------------------------------------------------------------------------

juce::String Project::importMidiFile (const juce::File& f, double atBeats, bool setTempoFromFile)
{
    juce::FileInputStream in (f);
    juce::MidiFile mf;
    if (! in.openedOk() || ! mf.readFrom (in))
        return "Couldn't read " + f.getFileName() + " as a MIDI file.";

    const short format = mf.getTimeFormat();
    if (format <= 0)
        return "SMPTE-timed MIDI files aren't supported.";
    const double tpq = format;

    // tempo
    if (setTempoFromFile)
    {
        juce::MidiMessageSequence tempos;
        mf.findAllTempoEvents (tempos);
        if (tempos.getNumEvents() > 0)
        {
            const double secsPerQuarter = tempos.getEventPointer (0)->message.getTempoSecondsPerQuarterNote();
            if (secsPerQuarter > 0) setTempo (60.0 / secsPerQuarter);
        }
        juce::MidiMessageSequence sigs;
        mf.findAllTimeSigEvents (sigs);
        if (sigs.getNumEvents() > 0)
        {
            int num = 4, den = 4;
            sigs.getEventPointer (0)->message.getTimeSignatureInfo (num, den);
            setTimeSignature (num, den);
        }
    }

    int created = 0;
    for (int ti = 0; ti < mf.getNumTracks(); ++ti)
    {
        auto* seq = mf.getTrack (ti);
        // split by channel: each channel with notes becomes its own track
        for (int ch = 1; ch <= 16; ++ch)
        {
            juce::MidiMessageSequence notes;
            juce::String trackName;
            int program = 0;
            for (auto* e : *seq)
            {
                auto& m = e->message;
                if (m.isTrackNameEvent() && trackName.isEmpty()) trackName = m.getTextFromTextMetaEvent();
                if (m.getChannel() != ch) continue;
                if (m.isProgramChange()) program = m.getProgramChangeNumber();
                notes.addEvent (m);
            }
            notes.updateMatchedPairs();

            bool hasNotes = false;
            double firstBeat = 1.0e9, lastBeat = 0.0;
            for (auto* e : notes)
                if (e->message.isNoteOn())
                {
                    hasNotes = true;
                    firstBeat = juce::jmin (firstBeat, e->message.getTimeStamp() / tpq);
                    const double off = e->noteOffObject != nullptr ? e->noteOffObject->message.getTimeStamp() / tpq
                                                                   : e->message.getTimeStamp() / tpq + 0.25;
                    lastBeat = juce::jmax (lastBeat, off);
                }
            if (! hasNotes) continue;

            const bool drums = ch == 10;
            auto name = trackName.isNotEmpty() ? trackName : (drums ? juce::String ("Drums") : gmProgramName (program));
            auto t = addTrack (kindInstrument, name);
            setInstrument (t, drums ? soundFontRef (128, 0) : soundFontRef (0, program));

            const double clipStart = atBeats;   // keep the file's own timing
            const double length = std::ceil (lastBeat / beatsPerBar()) * beatsPerBar();
            auto clip = addMidiClip (t, clipStart, juce::jmax (beatsPerBar(), length), name);

            for (auto* e : notes)
            {
                auto& m = e->message;
                const double b = m.getTimeStamp() / tpq;
                if (m.isNoteOn())
                {
                    const double off = e->noteOffObject != nullptr ? e->noteOffObject->message.getTimeStamp() / tpq : b + 0.25;
                    juce::ValueTree n (ids::NOTE);
                    n.setProperty (ids::p, m.getNoteNumber(), nullptr);
                    n.setProperty (ids::s, b, nullptr);
                    n.setProperty (ids::l, juce::jmax (1.0 / 64.0, off - b), nullptr);
                    n.setProperty (ids::v, (int) m.getVelocity(), nullptr);
                    clip.v.appendChild (n, nullptr);
                }
                else if (m.isController() || m.isPitchWheel() || m.isChannelPressure())
                {
                    juce::ValueTree c (ids::CC);
                    c.setProperty (ids::n, m.isController() ? m.getControllerNumber() : m.isPitchWheel() ? ccPitchBend : ccAftertouch, nullptr);
                    c.setProperty (ids::b, b, nullptr);
                    c.setProperty (ids::v, m.isController() ? m.getControllerValue() : m.isPitchWheel() ? m.getPitchWheelValue() : m.getChannelPressureValue(), nullptr);
                    clip.v.appendChild (c, nullptr);
                }
            }
            ++created;
        }
    }

    return created > 0 ? juce::String() : juce::String ("No notes found in " + f.getFileName());
}

juce::String Project::exportMidiFile (const juce::File& f) const
{
    const int tpq = 960;
    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (tpq);

    juce::MidiMessageSequence meta;
    meta.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::round (60000000.0 / tempo())), 0);
    meta.addEvent (juce::MidiMessage::timeSignatureMetaEvent (tsNum(), tsDen()), 0);
    mf.addTrack (meta);

    int channel = 1;
    for (auto tv : tracks())
    {
        Track t (tv);
        if (! t.isInstrument()) continue;

        juce::MidiMessageSequence seq;
        seq.addEvent (juce::MidiMessage::textMetaEvent (3, t.name()), 0);
        const int ch = channel;
        for (auto c : t.clips())
        {
            Clip clip (c);
            const double cs = clip.start(), len = clip.midiLength();
            for (auto n : c)
            {
                if (n.hasType (ids::NOTE))
                {
                    double s = n[ids::s], l = n[ids::l];
                    if (s + l <= 0.0 || s >= len) continue;
                    const double on = juce::jmax (0.0, s), off = juce::jmin (len, s + l);
                    seq.addEvent (juce::MidiMessage::noteOn (ch, (int) n[ids::p], (juce::uint8) (int) n[ids::v]), (cs + on) * tpq);
                    seq.addEvent (juce::MidiMessage::noteOff (ch, (int) n[ids::p]), (cs + off) * tpq);
                }
                else if (n.hasType (ids::CC))
                {
                    const double b = n[ids::b];
                    if (b < 0.0 || b >= len) continue;
                    const int num = n[ids::n], val = n[ids::v];
                    auto m = num == ccPitchBend ? juce::MidiMessage::pitchWheel (ch, val)
                           : num == ccAftertouch ? juce::MidiMessage::channelPressureChange (ch, val)
                           : juce::MidiMessage::controllerEvent (ch, num, val);
                    seq.addEvent (m, (cs + b) * tpq);
                }
            }
        }
        seq.updateMatchedPairs();
        seq.sort();
        mf.addTrack (seq);
        channel = channel % 16 + 1;
        if (channel == 10) channel = 11;
    }

    f.deleteFile();
    juce::FileOutputStream out (f);
    if (! out.openedOk() || ! mf.writeTo (out))
        return "Couldn't write " + f.getFullPathName();
    return {};
}

// ---- formatting -----------------------------------------------------------------------------------------

juce::String formatBarsBeats (double beats, double beatsPerBar, int tsDen, bool withTicks)
{
    if (beats < 0) beats = 0;
    const double beatUnit = 4.0 / tsDen;   // in quarter notes
    const int bar = (int) std::floor (beats / beatsPerBar + 1.0e-9);
    const double inBar = beats - bar * beatsPerBar;
    const int beat = (int) std::floor (inBar / beatUnit + 1.0e-9);
    const double inBeat = inBar - beat * beatUnit;
    const int sixteenth = (int) std::floor (inBeat / 0.25 + 1.0e-9);
    juce::String s;
    s << (bar + 1) << "." << (beat + 1);
    if (withTicks) s << "." << (sixteenth + 1);
    return s;
}

juce::String formatSeconds (double seconds)
{
    if (seconds < 0) seconds = 0;
    const int m = (int) (seconds / 60.0);
    const double s = seconds - m * 60.0;
    return juce::String (m) + ":" + juce::String (s, 1).paddedLeft ('0', 4);
}

} // namespace wis::daw
