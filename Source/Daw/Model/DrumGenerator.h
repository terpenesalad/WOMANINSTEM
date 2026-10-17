#pragma once

#include "SongAnalysis.h"
#include <map>

namespace wis::daw
{

/** The Smart Drummer's playing: a style library (grooves at four intensities, fills, swing, kit) and a
    generator that plays a whole song from a SongMap. Everything comes out as ordinary MIDI notes on the
    General MIDI drum map, so the result can be edited like any other part. */
namespace drummer
{
    namespace note
    {
        enum { kick = 36, stick = 37, snare = 38, clap = 39, floorTom = 41, closedHat = 42, pedalHat = 44, lowTom = 45,
               openHat = 46, midTom = 47, crash = 49, highTom = 50, ride = 51, rideBell = 53, tambourine = 54, cowbell = 56,
               crash2 = 57, shaker = 70 };
    }

    enum FillStyle { fillToms = 0, fillSnareBuild, fillFunk, fillMinimal, fillTriplet, fillJazz, fillLatin };

    struct Lane { int note; const char* steps; };
    struct Style
    {
        const char* name;
        const char* description;
        bool triplet = false;            // 12 steps per 4/4 bar (shuffle / swing grid)
        float swing = 0.0f;              // default swing when the song doesn't say
        bool swing16 = false;            // swing the 16ths (hip-hop, lo-fi) rather than the 8ths
        FillStyle fill = fillToms;
        float ghosts = 0.3f;             // how many ghost notes complexity adds
        float kickFollow = 0.6f;         // how freely the kick follows the band
        int kitSource = 0;               // the kit it suggests (DrumKit source index)
        std::vector<Lane> levels[4];     // quiet, verse, chorus, peak
    };
    const std::vector<Style>& styles();
    juce::StringArray styleNames();

    struct Settings
    {
        int style = 0;
        float complexity = 0.5f;         // 0 simple .. 1 busy
        float energy = 0.0f;             // -1 softer .. +1 harder
        float follow = 0.6f;             // how much the kick locks to the band
        float fills = 0.5f;
        bool swingAuto = true;
        float swing = 0.0f;
        float humanize = 0.5f;
        float feel = 0.0f;               // -1 pushes ahead .. +1 lays back
        int timeFeel = 0;                // 0 normal, 1 half-time, 2 double-time
        int cymbals = 0;                 // 0 the style's choice, 1 hi-hat, 2 ride
        bool endHit = true;
        int seed = 1;
        std::map<int, int> sectionLevel; // section start bar -> 0..3, or 4 = rest (otherwise from the song)
    };

    struct Hit { double beat; int note; int velocity; };
    struct PlayedSection
    {
        SongMap::Section section;
        int level = 1;                   // the intensity it was played at (-1 rest)
        std::vector<Hit> hits;           // beats from the start of the song
    };
    struct Beat
    {
        std::vector<PlayedSection> sections;
        std::vector<Hit> ending;
        double endingBeat = 0.0;
        double beatsPerBar = 4.0;
    };

    /** A style that suits what was heard (shuffle -> blues, very fast -> punk, very slow -> dream pop, else rock). */
    int suggestStyle (const SongMap&);

    /** Plays the song. An empty map gets 8 bars of the style. */
    Beat generate (const SongMap&, const Settings&);

    /** Replaces the drummer's clips on `track` (clips it wrote before) with the new beat, one clip per section. */
    int write (Project&, const Track& track, const Beat&, const juce::String& styleName);
}

} // namespace wis::daw
