#pragma once

#include "Project.h"

namespace wis::daw
{

/** MIDI loops for Junkyard Percussion's kit: finger snaps and body percussion, junkyard grooves and
    8-bar sound spaces (atmospheres for games and film). Being MIDI they follow the song's tempo: change the
    tempo and the loop speeds up or slows down with it. */
struct JunkLoop
{
    juce::String name, group;          // group: "Finger Snaps", "Junkyard Grooves", "Sound Spaces"
    juce::String description;
    int stepsPerBeat = 4;              // 4 = 16ths, 3 = triplets / shuffle
    double suggestedTempo = 100.0;
    float swing = 0.0f;                // 0..1: pushes the off-beat 16ths late
    std::vector<std::pair<int, juce::String>> lanes;   // kit key -> one bar of steps ('X' accent, 'x' normal, 'o' ghost)
    struct Event { int note; double bar, lengthBars; int velocity; };
    std::vector<Event> events;         // sound spaces: held textures and single hits, positioned in bars
    int preset = 0;                    // the Junkyard preset for a new track
};

const std::vector<JunkLoop>& junkLoops();

/** Writes `bars` bars of the loop (sound spaces are always 8 bars) as a new MIDI clip on the track. */
Clip insertJunkLoop (Project& project, const Track& track, int index, double startBeat, int bars = 8);

} // namespace wis::daw
