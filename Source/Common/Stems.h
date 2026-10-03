#pragma once

#include <juce_graphics/juce_graphics.h>
#include <array>

namespace wis
{

/** Every stem the app can produce. Order here = order in the mixer. */
enum class StemId : int
{
    drums = 0,
    bass,
    guitar,
    piano,
    leadVocals,
    backingVocals,
    other,
    count
};

constexpr int numStemIds = (int) StemId::count;

struct StemInfo
{
    StemId id;
    const char* key;          // stable file / json key
    const char* displayName;  // shown in the mixer
    const char* hint;         // tooltip
    juce::uint32 colour;
};

inline const std::array<StemInfo, numStemIds>& allStems()
{
    static const std::array<StemInfo, numStemIds> stems { {
        { StemId::drums,         "drums",   "Drums",          "Kit, percussion",                                       0xfff59e0b },
        { StemId::bass,          "bass",    "Bass",           "Bass guitar, synth bass",                               0xff8b5cf6 },
        { StemId::guitar,        "guitar",  "Guitar",         "Electric and acoustic guitars",                         0xffef4444 },
        { StemId::piano,         "piano",   "Keys / Piano",   "Piano and most keyboard parts",                         0xff14b8a6 },
        { StemId::leadVocals,    "vocals",  "Lead Vocals",    "Centre-panned vocal (main singer)",                     0xffec4899 },
        { StemId::backingVocals, "backing", "Backing Vocals", "Wide / doubled vocals separated from the lead by stereo position", 0xfff9a8d4 },
        { StemId::other,         "other",   "Other",          "Everything else: strings, horns, synths, pads, FX",     0xff94a3b8 },
    } };
    return stems;
}

inline const StemInfo& stemInfo (StemId id)          { return allStems()[(size_t) id]; }
inline juce::Colour stemColour (StemId id)           { return juce::Colour (stemInfo (id).colour); }

inline int stemIndexForKey (const juce::String& key)
{
    for (auto& s : allStems())
        if (key == s.key)
            return (int) s.id;
    return -1;
}

/** Demucs works at 44.1 kHz; every stem in the cache is stored at this rate. */
constexpr double stemSampleRate = 44100.0;

} // namespace wis
