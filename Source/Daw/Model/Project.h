#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_graphics/juce_graphics.h>

namespace wis::daw
{

/*  The project ("song") is one juce::ValueTree, edited through an UndoManager, so every change in
    the Studio is undoable and the engine / UI simply listen for changes.

    PROJECT  name tempo tsNum tsDen cycleOn cycleStart cycleEnd metronome countIn masterVolume playhead
     ├─ TRACKS
     │   └─ TRACK  id name kind(audio|instrument) colour volume pan mute solo arm monitor input(chan) inputStereo height
     │        ├─ INSTRUMENT ─ PLUGIN          (instrument tracks)
     │        ├─ INSERTS    ─ PLUGIN*         (effects chain)
     │        ├─ CLIPS      ─ CLIP*
     │        │     audio: id name start(beats) file offset(s) length(s) gain(dB) fadeIn(s) fadeOut(s) take ─ TAKE*(offset)
     │        │     midi:  id name start(beats) length(beats) ─ NOTE*(p s l v)  CC*(n b v)
     │        └─ AUTOMATION ─ LANE(param) ─ POINT*(b v)
     └─ MASTER ─ INSERTS ─ PLUGIN*

    PLUGIN  type(builtin|external) uid name bypass state(base64) desc(xml, external only)
*/
namespace ids
{
    #define WIS_ID(x) inline const juce::Identifier x { #x };
    WIS_ID (PROJECT) WIS_ID (TRACKS) WIS_ID (TRACK) WIS_ID (INSTRUMENT) WIS_ID (INSERTS) WIS_ID (CLIPS) WIS_ID (CLIP)
    WIS_ID (NOTE) WIS_ID (CC) WIS_ID (TAKE) WIS_ID (AUTOMATION) WIS_ID (LANE) WIS_ID (POINT) WIS_ID (MASTER) WIS_ID (PLUGIN)

    WIS_ID (name) WIS_ID (tempo) WIS_ID (tsNum) WIS_ID (tsDen) WIS_ID (cycleOn) WIS_ID (cycleStart) WIS_ID (cycleEnd)
    WIS_ID (metronome) WIS_ID (countIn) WIS_ID (masterVolume) WIS_ID (playhead) WIS_ID (version) WIS_ID (zoom) WIS_ID (nextId)

    WIS_ID (id) WIS_ID (kind) WIS_ID (colour) WIS_ID (volume) WIS_ID (pan) WIS_ID (mute) WIS_ID (solo) WIS_ID (arm)
    WIS_ID (monitor) WIS_ID (input) WIS_ID (inputStereo) WIS_ID (height) WIS_ID (showAutomation)

    WIS_ID (start) WIS_ID (length) WIS_ID (file) WIS_ID (offset) WIS_ID (gain) WIS_ID (fadeIn) WIS_ID (fadeOut) WIS_ID (take)
    WIS_ID (p) WIS_ID (s) WIS_ID (l) WIS_ID (v) WIS_ID (n) WIS_ID (b)
    WIS_ID (param) WIS_ID (type) WIS_ID (uid) WIS_ID (bypass) WIS_ID (state) WIS_ID (desc)
    #undef WIS_ID
}

inline const juce::String kindAudio      { "audio" };
inline const juce::String kindInstrument { "instrument" };
inline const juce::String kindMidi       { "midi" };

constexpr int ccPitchBend = -1;       // CC node "n" values for non-controller events
constexpr int ccAftertouch = -2;

juce::Colour trackColourForIndex (int index);

/** Thin typed views over the ValueTree. Cheap to copy; always refer to the live tree. */
struct Clip
{
    mutable juce::ValueTree v;   // a handle: editing through a const Clip& is intended
    explicit Clip (juce::ValueTree t = {}) : v (std::move (t)) {}

    bool isValid() const              { return v.isValid(); }
    bool isMidi() const               { return v[ids::kind].toString() == kindMidi; }
    bool isAudio() const              { return ! isMidi(); }
    int id() const                    { return (int) v[ids::id]; }
    double start() const              { return (double) v[ids::start]; }          // beats
    juce::String name() const         { return v[ids::name].toString(); }
    /** Clip length in beats (audio clips are converted with the given tempo). */
    double lengthBeats (double tempo) const;
    double endBeats (double tempo) const { return start() + lengthBeats (tempo); }

    // audio
    juce::String file() const         { return v[ids::file].toString(); }
    double offsetSeconds() const      { return (double) v[ids::offset]; }
    double lengthSeconds() const      { return (double) v[ids::length]; }
    float gainDb() const              { return (float) (double) v.getProperty (ids::gain, 0.0); }
    double fadeIn() const             { return (double) v.getProperty (ids::fadeIn, 0.0); }
    double fadeOut() const            { return (double) v.getProperty (ids::fadeOut, 0.0); }

    // midi
    double midiLength() const         { return (double) v[ids::length]; }
};

struct Track
{
    mutable juce::ValueTree v;
    explicit Track (juce::ValueTree t = {}) : v (std::move (t)) {}

    bool isValid() const              { return v.isValid(); }
    int id() const                    { return (int) v[ids::id]; }
    bool isInstrument() const         { return v[ids::kind].toString() == kindInstrument; }
    juce::String name() const         { return v[ids::name].toString(); }
    juce::Colour colour() const       { return juce::Colour ((juce::uint32) (juce::int64) v[ids::colour]); }
    juce::ValueTree clips() const     { return v.getChildWithName (ids::CLIPS); }
    juce::ValueTree inserts() const   { return v.getChildWithName (ids::INSERTS); }
    juce::ValueTree instrument() const{ return v.getChildWithName (ids::INSTRUMENT).getChildWithName (ids::PLUGIN); }
    juce::ValueTree automation() const{ return v.getChildWithName (ids::AUTOMATION); }
    juce::ValueTree lane (const juce::String& param) const { return automation().getChildWithProperty (ids::param, param); }
};

/** Generic description of a plugin to put in a slot (built-in or VST3). */
struct PluginRef
{
    juce::String type;   // "builtin" | "external"
    juce::String uid;    // built-in id, or PluginDescription::createIdentifierString()
    juce::String name;
    juce::String descXml;
    juce::String state;  // optional initial state (base64)
};

class Project
{
public:
    Project();

    juce::ValueTree& tree() { return root; }
    juce::UndoManager& undo() { return undoManager; }
    juce::UndoManager* um() { return &undoManager; }

    // ---- file -------------------------------------------------------------------------------------
    static juce::File defaultProjectsFolder();
    static juce::String fileExtension() { return ".wisproj"; }
    juce::File getFile() const           { return file; }
    juce::File getFolder() const         { return file.getParentDirectory(); }
    juce::File audioFolder() const;
    bool hasBeenSaved() const            { return saved; }
    bool isDirty() const                 { return dirty; }
    void markClean()                     { dirty = false; }
    void markDirty()                     { dirty = true; }

    /** Creates a fresh, empty song in Documents/WOMANINSTEM Projects/<name>/ (folder is created now so recordings have a home). */
    void createNew (const juce::String& name);
    juce::String save();
    juce::String saveAs (const juce::File& newProjectFile);   // copies audio files into the new folder
    juce::String load (const juce::File& projectFile);

    /** Resolve a clip's file reference (relative to the project folder, or absolute). */
    juce::File resolve (const juce::String& ref) const;
    juce::String makeRef (const juce::File& f) const;

    /** Copies an external audio file into the project's Audio Files folder (unless already inside). */
    juce::File importIntoProject (const juce::File& source);
    juce::File newRecordingFile (const juce::String& trackName);

    // ---- song settings ----------------------------------------------------------------------------
    double tempo() const        { return (double) root.getProperty (ids::tempo, 120.0); }
    int tsNum() const           { return (int) root.getProperty (ids::tsNum, 4); }
    int tsDen() const           { return (int) root.getProperty (ids::tsDen, 4); }
    /** Beats (quarter notes) per bar. */
    double beatsPerBar() const  { return tsNum() * 4.0 / tsDen(); }
    double secondsPerBeat() const { return 60.0 / tempo(); }
    double beatsToSeconds (double b) const { return b * secondsPerBeat(); }
    double secondsToBeats (double s) const { return s / secondsPerBeat(); }
    void setTempo (double bpm);
    void setTimeSignature (int num, int den);

    // ---- tracks -------------------------------------------------------------------------------------
    juce::ValueTree tracks() const { return root.getChildWithName (ids::TRACKS); }
    juce::ValueTree masterInserts() const { return root.getChildWithName (ids::MASTER).getChildWithName (ids::INSERTS); }
    int numTracks() const { return tracks().getNumChildren(); }
    Track track (int index) const { return Track (tracks().getChild (index)); }
    Track trackById (int id) const;
    Track trackForClip (const juce::ValueTree& clip) const;
    Clip clipById (int id) const;

    int allocateId();

    Track addTrack (const juce::String& kind, const juce::String& name, int insertIndex = -1);
    void removeTrack (const Track& t);
    void moveTrack (int from, int to);
    Track duplicateTrack (const Track& t);

    void setPlugin (juce::ValueTree parent, int index, const PluginRef& ref);   // index -1 = append
    static juce::ValueTree makePluginNode (const PluginRef& ref);
    void setInstrument (const Track& t, const PluginRef& ref);

    // ---- clips --------------------------------------------------------------------------------------
    Clip addAudioClip (const Track& t, const juce::File& file, double startBeats, double offsetSeconds, double lengthSeconds,
                       const juce::String& name = {});
    Clip addMidiClip (const Track& t, double startBeats, double lengthBeats, const juce::String& name = {});
    void addNote (const Clip& c, int pitch, double startBeats, double lengthBeats, int velocity);
    void addController (const Clip& c, int number, double beat, int value);

    void moveClip (const Clip& c, const Track& toTrack, double newStartBeats);
    Clip duplicateClip (const Clip& c, double newStartBeats, const Track& toTrack = Track());
    void deleteClip (const Clip& c);
    /** Split at an absolute beat position. Returns the right-hand part. */
    Clip splitClip (const Clip& c, double atBeats);
    void trimClipStart (const Clip& c, double newStartBeats);
    void trimClipEnd (const Clip& c, double newEndBeats);
    void quantize (const Clip& c, double gridBeats, float strength = 1.0f, bool selectedOnly = false,
                   const juce::Array<juce::ValueTree>& selection = {});
    void setTake (const Clip& c, int takeIndex);

    /** End of the last clip, in beats. */
    double contentEndBeats() const;

    // ---- automation ---------------------------------------------------------------------------------
    juce::ValueTree getOrCreateLane (const Track& t, const juce::String& param);
    static float automationValueAt (const juce::ValueTree& lane, double beat, float fallback);

    // ---- MIDI files -----------------------------------------------------------------------------------
    juce::String importMidiFile (const juce::File& f, double atBeats, bool setTempoFromFile);
    juce::String exportMidiFile (const juce::File& f) const;

    // ---- listeners ------------------------------------------------------------------------------------
    std::function<void()> onLoaded;    // after load()/createNew() replaced the tree

private:
    void initialiseTree (const juce::String& name);
    void ensureStructure();

    juce::ValueTree root;
    juce::UndoManager undoManager { 0, 300 };
    juce::File file;
    bool saved = false, dirty = false;
};

/** Formatting helpers. */
juce::String formatBarsBeats (double beats, double beatsPerBar, int tsDen, bool withTicks = true);
juce::String formatSeconds (double seconds);

} // namespace wis::daw
