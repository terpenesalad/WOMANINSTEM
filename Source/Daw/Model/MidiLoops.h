#pragma once

#include "Project.h"

namespace wis::daw
{

/** Ready-made MIDI loops (chords, bass lines, arpeggios) that are written in the song's key:
    a chord progression played in one of several styles. */
struct LoopProgression
{
    juce::String name;
    bool minor;                                     // the progression's own mode
    std::vector<std::pair<int, int>> chords;        // (semitones above the key root, chord type), one bar each
};

struct LoopStyle
{
    juce::String name, category;                    // category: "Chords", "Bass", "Arpeggio"
    PluginRef (*instrument)();                       // a suitable sound when a new track is needed
};

const std::vector<LoopProgression>& loopProgressions();
const std::vector<LoopStyle>& loopStyles();

/** Chord type names / intervals. */
juce::String chordTypeSuffix (int type);
const std::vector<int>& chordIntervals (int type);

/** Writes 8 bars (the 4-bar progression twice) as a MIDI clip in the project's key. */
Clip insertMidiLoop (Project& p, const Track& t, int progression, int style, double startBeat);

/** e.g. "Am - F - C - G" for the song's key. */
juce::String describeProgression (const Project& p, int progression);

} // namespace wis::daw
