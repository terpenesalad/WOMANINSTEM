#pragma once

#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include "CabSim.h"
#include "IrHandoff.h"

namespace wis
{

/** The amp in a room, recorded by a spaced pair of room mics: what turns a close-miked cab into "an amp in a space".

    Each room is a real-sized box with the cab on the floor facing the mics. Its impulse response is built from the
    image sources of the walls, floor and ceiling (each reflection leaving the speaker at its own angle, so it carries
    the speaker's beaming: bright straight ahead, dull to the sides, only lows from behind a closed back, an inverted
    rear wave from an open back), absorbed a little more in the highs at every bounce and by the air, then a diffuse
    tail whose level against the direct sound follows the room's size and reverb time (Sabine). It's convolved with
    the cabinet's own far-field response. Mono in (the amp, before the cab), stereo out. Rebuilt in the background. */
class AmpRoom
{
public:
    AmpRoom();
    ~AmpRoom();

    static juce::StringArray roomNames();
    static juce::String roomDescription (int room);

    void prepare (double sampleRate, int maxBlock);
    void reset();

    /** Which room and which speaker (any thread; the room is rebuilt in the background when they change). */
    void setRoom (int room, CabType cab);
    /** Audio thread: mono amp signal in, room-mic stereo out (silence until the first room is in). */
    void process (const float* in, float* outL, float* outR, int numSamples);
    /** The room asked for last has been designed and handed over (the audio thread installs it on its next block). */
    bool isDesigned() const noexcept { const int w = wantedKey.load(); return w < 0 || designedKey.load() == w; }

    /** Builds a room's stereo impulse response (any thread; also used by the tests). About unity loudness in the mids. */
    static juce::AudioBuffer<float> designImpulseResponse (int room, CabType cab, double sampleRate);

private:
    struct Builder;
    double sampleRate = 0.0;
    int maxBlockSize = 512;
    std::atomic<int> wantedKey { -1 }, designedKey { -1 }, installedKey { -1 };
    std::unique_ptr<Builder> builder;
    IrHandoff handoff;
    juce::dsp::Convolution convolution { juce::dsp::Convolution::NonUniform { 512 } };
    juce::AudioBuffer<float> stereo;
};

/** The rig's reverb as a convolution with a designed space: a room, a plate, a guitar-amp spring tank or a hall
    (see daw::roomir). Stereo in, wet stereo out. Rebuilt in the background when the type, size, tone or pre-delay change. */
class SpaceReverb
{
public:
    SpaceReverb();
    ~SpaceReverb();

    /** 0 = Classic (the rig's algorithmic reverb, not handled here), then these. */
    static juce::StringArray typeNames() { return { "Classic", "Room", "Plate", "Spring", "Hall" }; }

    void prepare (double sampleRate, int maxBlock);
    void reset();
    /** type 1..4, size 0..1, damping 0..1, pre-delay in ms (any thread). */
    void setSpace (int type, float size01, float damping01, float predelayMs);
    /** Audio thread: replaces the two channels with the wet signal (silence until the first space is in). */
    void process (float* l, float* r, int numSamples);

    static juce::AudioBuffer<float> designImpulseResponse (int type, float size01, float damping01, float predelayMs, double sampleRate);
    bool isDesigned() const noexcept { const int w = wantedKey.load(); return w < 0 || designedKey.load() == w; }

private:
    struct Builder;
    double sampleRate = 0.0;
    std::atomic<int> wantedKey { -1 }, designedKey { -1 }, installedKey { -1 };
    std::unique_ptr<Builder> builder;
    IrHandoff handoff;
    juce::dsp::Convolution convolution { juce::dsp::Convolution::NonUniform { 512 } };
};

} // namespace wis
