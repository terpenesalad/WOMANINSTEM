#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace wis::daw
{

/** Designs stereo impulse responses for real and imagined spaces (no recordings needed):
    early reflections, a band-wise decaying diffuse tail, discrete echoes (canyons), flutter (car parks),
    dispersive springs. The result has unit energy per channel, so the wet level doesn't depend on the space. */
namespace roomir
{
    enum Space
    {
        dry = 0, vocalBooth, livingRoom, woodenStudio, bigLiveRoom, barClub, bathroom, concertHall,
        church, cathedral, forest, canyon, carPark, plate, spring,
        concreteRoom, tinShed, cave, stairwell,   // 3.7
        numSpaces
    };

    juce::StringArray spaceNames();
    juce::String spaceDescription (int space);

    /** size 0.5..2 scales the room, tone 0..1 (0.5 neutral) darkens / brightens the tail, predelayMs shifts it. */
    juce::AudioBuffer<float> design (int space, double sampleRate, float size, float tone, float predelayMs, juce::uint32 seed = 1);

    /** Reverb time (mid band) of a space at size 1, seconds. */
    float decaySeconds (int space);
}

} // namespace wis::daw
