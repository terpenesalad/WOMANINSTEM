#pragma once

#include "Project.h"

namespace wis::daw
{

/** A one-bar groove written as step strings ('X' accent, 'x' normal, 'o' ghost, anything else = rest). */
struct DrumPattern
{
    juce::String name, genre;
    int stepsPerBeat = 4;                                   // 4 = straight 16ths, 3 = triplet / shuffle
    std::vector<std::pair<int, juce::String>> lanes;       // GM drum note -> steps for one bar
    double suggestedTempo = 100.0;
};

const juce::Array<DrumPattern>& drumPatterns();

/** Writes `bars` bars of the pattern into a new MIDI clip on `track` (crash on the downbeat,
    a fill every 4th bar if requested). Returns the clip. */
Clip insertDrumPattern (Project& project, const Track& track, int patternIndex, double startBeat, int bars, bool fills);

} // namespace wis::daw
