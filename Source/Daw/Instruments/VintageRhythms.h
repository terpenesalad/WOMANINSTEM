#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace wis::daw
{

class Project;
struct Track;
struct Clip;

/** Preset rhythms in the style of 1980s home keyboards: a drum pattern plus an auto-accompaniment
    pattern (bass and chord) for one bar. Written as step strings:
      drums:  'X' accent, 'x' normal, 'o' soft, '.' rest
      bass:   'R' root, '3' third, '5' fifth, '6' sixth, '7' flat seventh, '8' octave, '-' hold, '.' rest
      chord:  'X' hit, 'x' soft hit, '-' hold, '.' rest                                                   */
struct VintageRhythm
{
    juce::String name;
    float tempo;               // suggested BPM
    int beats;                 // quarter notes per bar
    int steps;                 // steps per bar (16 = 16ths in 4/4, 12 = triplets in 4/4 or 16ths in 3/4)
    std::vector<std::pair<int, juce::String>> drums;   // GM note, steps
    juce::String bass, chord;
};

const std::vector<VintageRhythm>& vintageRhythms();

/** The early-80s Yamaha portable keyboard rhythm section (the PS-20's eight rhythms: March, Disco, Waltz,
    Rock, Tango, Swing, Rhumba, Samba), each with variation I and II: index = rhythm * 2 + variation.
    The keyboard's real patterns aren't published anywhere, so these are written in the style of the
    preset patterns of that generation of portables (plain, mostly bass drum / snare / hi-hat). */
const std::vector<VintageRhythm>& portableRhythms();
juce::StringArray portableRhythmNames();

/** A one-bar fill for a rhythm (snare/tom run in the second half, crash handled by the caller). */
std::vector<std::pair<int, juce::String>> vintageFill (const VintageRhythm& r);

/** Velocity (0..1) for a step character, or 0 for a rest. */
float stepVelocity (juce::juce_wchar c);

/** Interval in semitones above the root for a bass step character (minor chords flatten the third). */
int bassInterval (juce::juce_wchar c, bool minor);

/** Writes `bars` bars of a rhythm's drums as a MIDI clip (a fill every 4th bar). */
Clip insertVintageRhythm (Project& p, const Track& t, int rhythmIndex, double startBeat, int bars);

} // namespace wis::daw
