#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace wis::daw
{

/** Chord logic for Daydream (the chord machine): which notes a set of chord buttons makes over a root,
    in-key chords, chord names, how a chord is placed on the keyboard (position, spread, the voicing
    ladder), the bass note, and the rhythm patterns and drum grooves it plays. Pure functions, no audio. */
namespace daydream
{
    /** The eight chord buttons: four chord types, then four colours (extensions). */
    enum Button { bMaj, bMin, bSus, bDim, b6, bm7, bM7, b9, numButtons };
    constexpr unsigned typeMask = 0x0f, extMask = 0xf0;
    juce::String buttonName (int b);        // "Maj", "Min", "Sus", "Dim", "6", "m7", "M7", "9"
    juce::String buttonLongName (int b);    // "Major", "Minor"... "Add 9"

    /** Semitones above the root (ascending, the 9th is 14) for the buttons held. Several types held
        together combine their notes. No type held = empty (a colour on its own isn't a chord). */
    std::vector<int> intervalsFor (unsigned buttons, bool sus2 = false);

    juce::StringArray scaleNames();
    /** Scale steps (semitones from the tonic, 7 of them). */
    const std::vector<int>& scaleSteps (int scale);

    struct Chord { int root = 0; std::vector<int> intervals; };
    /** Key Mode: the chord the scale makes on this key (out-of-key notes snap to the nearest scale note,
        down on a tie). Colour buttons add the scale's 6th, 7th (m7 or M7: the key's own 7th) and 9th. */
    Chord inKey (int pitchClass, int tonic, int scale, unsigned buttons);

    /** A chord symbol: C, Cm, Csus4, Cdim, C6, Cmaj7, C7, Cm7b5, Cdim7, C7sus4, Cadd9, Cm9, C6/9...
        Spelled for the key when keyTonic >= 0. An optional bass note gives a slash chord. */
    juce::String chordName (int rootPc, const std::vector<int>& intervals, int keyTonic = -1, int bassPc = -1);
    juce::String noteName (int midiNote, int keyTonic = -1);   // "C4"

    enum Position { followKey = 0, stayCentred };
    enum Spread { close = 0, drop2, drop3, wide };
    juce::StringArray positionNames();
    juce::StringArray spreadNames();

    /** One step of the voicing ladder: up moves the lowest note an octave up to the top,
        down moves the highest note an octave down to the bottom. */
    void voicingStep (std::vector<int>& notes, int direction);

    /** The chord's notes on the keyboard (sorted). Follow the key: the root sits in C3..B3.
        Stay centred: the inversion whose average is nearest `centre` (chords stay in one place).
        Then the spread, then `voicing` steps up (+) or down (-) the ladder. */
    std::vector<int> place (const Chord&, int position, int centre, int spread, int voicing);

    /** The bass note: the root in the bass octave (2 = C2..B2), then `bassVoicing` steps up or down
        through the chord's notes (1 = the next chord note above the root, -1 the one below). */
    int bassNote (const Chord&, int bassOctave, int bassVoicing);

    // ---- performance patterns -------------------------------------------------------------------------
    /** A rhythm pattern in 16ths. Each step: notes to start (chord-note indices, lowest = 0; index n and
        above go up an octave; -1 = the bass note; 99 = the whole chord) and how many 16ths they last. */
    struct PatternStep { std::vector<int> notes; int length = 1; float velocity = 1.0f; };
    struct Pattern { const char* name; int steps; std::vector<std::pair<int, PatternStep>> events; };
    const std::vector<Pattern>& patterns();
    juce::StringArray patternNames();

    // ---- drum grooves ---------------------------------------------------------------------------------
    /** A groove: 16 (or 12) steps per bar, lanes as text: 'X' accent, 'x' normal, 'o' soft, '.' rest. */
    struct GrooveLane { int note; const char* steps; };
    struct Groove { const char* name; int stepsPerBar; float swing; std::vector<GrooveLane> lanes; };
    const std::vector<Groove>& grooves();
    juce::StringArray grooveNames();   // "Off" first
}

} // namespace wis::daw
