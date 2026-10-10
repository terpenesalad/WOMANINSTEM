#pragma once

#include "Project.h"

namespace wis::daw
{

/** Ready-made effect chains for recording a voice through a microphone, from a clean studio vocal and folk
    rooms to 60s double tracking, phaser haze, demons, robots and extreme-metal shrieks. Each one is just
    the Studio's own effects with their knobs set, so everything stays editable on the track. */
struct VocalChain
{
    juce::String name, group, description;    // group: "Studio & Pop", "Folk & Roots", "Psychedelic", "Weird & Extreme"
    struct Fx { juce::String id; std::vector<std::pair<juce::String, float>> values; };
    std::vector<Fx> fx;
};

const std::vector<VocalChain>& vocalChains();

/** The chain as plugin references (each with its settings in its state). */
std::vector<PluginRef> vocalChainRefs (int index);

/** Replaces the track's effects with the chain (undoable). */
void applyVocalChain (Project& project, const Track& track, int index);

/** Index of the chain a new "Microphone / Vocals" track starts with. */
constexpr int defaultVocalChain = 0;

} // namespace wis::daw
