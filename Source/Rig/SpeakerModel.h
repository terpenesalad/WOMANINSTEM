#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "CabSim.h"
#include <vector>

namespace wis::speaker
{

/** The close-mic impulse response of a built-in cabinet, from a physical model of the speaker and the microphone:
    - the sound of every patch of the cone at the mic's real position and distance (Rayleigh integral), averaged over
      the mic's diaphragm. Above break-up the cone stops moving as one piece: bending waves run out from the voice coil
      and reflect off the surround, so the outer cone goes quiet and the response depends on where the mic points;
    - discrete cone break-up modes whose strength follows the mode shape under the mic (the peaks, notches and short
      ringing that make a speaker sound like a speaker rather than an EQ curve);
    - the cab's other speakers (heard off axis), the box's internal modes, the floor and the room's first reflections;
    - the mic itself: far-field response, pattern and proximity effect at that distance.
    The result is shaped to each speaker's measured voicing at the reference position (mic on the cap edge, 3 cm back),
    so the physics decides how it changes as the mic moves. Not real-time safe (runs on the cab's builder thread). */
juce::AudioBuffer<float> designCloseMic (CabType type, MicType mic, float position01, float distance01, double sampleRate);

/** Mic distance from the grille in metres for the Distance knob (0..1): 2 cm .. 1 m. */
float micDistanceMetres (float distance01) noexcept;

struct Geometry
{
    double radius = 0.125;      // effective cone radius (m)
    bool closedBack = true;
    double height = 0.5;        // centre of the miked speaker above the floor (m)
    int drivers = 1;
};
Geometry geometry (CabType type);

/** The speaker's own on-axis far-field magnitude (its voicing, no mic), at bins k * sampleRate / fftSize, k = 0..fftSize/2. */
std::vector<double> farFieldMagnitude (CabType type, double sampleRate, int fftSize);

/** Piston directivity 2 J1(x) / x. */
double jinc (double x) noexcept;

/** The radius of the part of the cone that still radiates at a frequency: all of it below break-up, then less and
    less (down to the dust cap) as the outer cone decouples. A real cone beams far less than a rigid piston. */
double radiatingRadius (CabType type, double hz) noexcept;

/** Directivity of the speaker at an angle off its axis (0 .. pi/2), relative to on axis. */
double offAxis (CabType type, double hz, double angle) noexcept;

} // namespace wis::speaker
