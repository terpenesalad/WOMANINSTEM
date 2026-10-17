#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>
#include <array>
#include <map>

namespace wis::daw
{

/** Chord Keys: whole chords on the computer keyboard. 40 keys (the number row and the three letter rows)
    each hold a chord you choose: root, type, inversion, octave, voicing and an optional bass note. Banks
    of 40 are written in a home key and can follow the song's key. */
namespace chordkeys
{
    constexpr int numKeys = 40;
    constexpr int keysPerRow = 10;
    /** The keys, row by row: 1..0, Q..P, A..;, Z../ */
    const char* keyChars();
    /** Which chord key a typed character is (shifted characters and capitals count), or -1. */
    int keyIndexForChar (juce::juce_wchar c);
    juce::String keyLabel (int index);

    struct ChordType { const char* name; const char* suffix; std::vector<int> intervals; const char* group; };
    const std::vector<ChordType>& types();
    int typeForSuffix (const juce::String& suffix);   // -1 if unknown

    enum Voicing { close = 0, open, spread, wide, numVoicings };
    juce::StringArray voicingNames();
    /** keyTonic -1: the everyday spelling (C Db D Eb E F F# G Ab A Bb B). Otherwise the note is spelled as a
        degree of that major key, so borrowed chords come out right (in D: C, F and Bb, not B# or A#). */
    juce::String pitchName (int pitchClass, int keyTonic = -1);

    struct Slot
    {
        bool used = false;
        int root = 0, type = 0, inversion = 0, octave = 0, bass = -1, voicing = close;
        bool operator== (const Slot& o) const
        {
            return used == o.used && root == o.root && type == o.type && inversion == o.inversion && octave == o.octave && bass == o.bass && voicing == o.voicing;
        }
    };

    struct Bank
    {
        juce::String name;
        int tonic = 0;          // the home key it was written in (pitch class)
        bool minor = false;
        std::array<Slot, numKeys> slots {};
    };

    /** "Am7/G", transposed by some semitones. Empty slots give "". */
    juce::String chordName (const Slot&, int transpose = 0, int keyTonic = -1);
    /** Parses "C", "F#m7", "Bbmaj9", "Am/G", "Dsus4"... (for the factory banks and typed entries). */
    Slot parse (const juce::String& text);

    struct PlayOptions
    {
        int baseNote = 60;       // where C of the chord octave sits (C4 = 60)
        int transpose = 0;       // semitones, -6..+5 after normalising
        bool voiceLeading = false;
        bool bassNote = false;   // add the root an octave below
    };
    /** The MIDI notes a slot plays (sorted). With voice leading the inversion / octave closest to `previous` is used. */
    std::vector<int> notesFor (const Slot&, const PlayOptions&, const std::vector<int>& previous = {});

    std::vector<Bank> factoryBanks();
    /** A row (0..3) of a bank filled with the chords of a major key: kind 0 triads, 1 sevenths, 2 sus / add9. */
    void fillRow (Bank&, int row, int tonic, bool minor, int kind);

    juce::ValueTree toTree (const Bank&);
    Bank fromTree (const juce::ValueTree&);
}

/** The user's chord banks and playing options, kept in the app settings. Message thread only. */
class ChordKeysState : public juce::ChangeBroadcaster
{
public:
    explicit ChordKeysState (juce::PropertiesFile* settings);

    std::vector<chordkeys::Bank> banks;
    int current = 0;
    bool enabled = false;
    int octave = 4;              // C4 = middle C
    int transpose = 0;           // your own shift, on top of following the song
    bool followKey = true;
    int velocity = 100;
    float strumMs = 0.0f;
    int strumDir = 0;            // 0 up, 1 down, 2 alternate
    bool voiceLeading = true;
    bool bassNote = false;
    bool latch = false;

    chordkeys::Bank& bank();
    /** The shift applied to the current bank right now (your transpose + the song key if followed), -6..+5. */
    int effectiveTranspose (int songKey, int songScale) const;
    chordkeys::PlayOptions options (int songKey, int songScale) const;
    /** The major key (tonic) the current bank sounds in right now, for spelling chord names. */
    int spelling (int songKey, int songScale) const;

    void save();
    void load();
    void resetFactory();
    juce::ValueTree toTree() const;
    void fromTree (const juce::ValueTree&);
    void changed() { save(); sendChangeMessage(); }

private:
    juce::PropertiesFile* settings;
};

/** Sends chords to a MidiKeyboardState: notes shared between chords are counted (a note only stops when the
    last chord using it is let go), strums are spread over time on a high-resolution timer. */
class ChordPlayer : private juce::HighResolutionTimer
{
public:
    explicit ChordPlayer (juce::MidiKeyboardState& state);
    ~ChordPlayer() override;

    /** A chord key went down (owner = the key's code, or any id). Strum: ms between notes, dir 0 up / 1 down. */
    void press (int owner, const std::vector<int>& notes, float velocity, float strumMs, int strumDir);
    void release (int owner);
    void releaseAll();
    bool isHeld (int owner) const;
    std::vector<int> owners() const;

private:
    void hiResTimerCallback() override;
    void noteOnNow (int note, float vel);
    void noteOffNow (int note);

    juce::MidiKeyboardState& keys;
    mutable juce::CriticalSection lock;
    struct Pending { double due; int note; float velocity; int owner; };
    std::vector<Pending> pending;
    std::map<int, std::vector<int>> sounding;   // owner -> notes switched on
    std::array<int, 128> refs {};
};

} // namespace wis::daw
