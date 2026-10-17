#pragma once

#include "Project.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace wis::daw
{

class AudioCache;

/** What the Smart Drummer listens to, copied out of the project on the message thread (cheap) so the
    analysis itself can run on a background thread. */
struct SongSnapshot
{
    double tempo = 120.0, beatsPerBar = 4.0;
    int tsNum = 4, tsDen = 4;
    double endBeat = 0.0;

    struct NoteEvent { double beat; float velocity; int pitch; int track; };
    std::vector<NoteEvent> notes;

    struct AudioPart
    {
        juce::File file;
        double startBeat = 0.0, offsetSeconds = 0.0, lengthSeconds = 0.0, stretchRatio = 1.0;
        bool reversed = false;
        float gain = 1.0f;
        int track = 0;
    };
    std::vector<AudioPart> audio;

    struct Marker { double beat; juce::String name; };
    std::vector<Marker> markers;
    juce::StringArray trackNames;   // the tracks that were listened to

    /** Everything on every track except `excludeTrackId` (the drummer itself) and other Smart Drummers. */
    static SongSnapshot capture (const Project&, int excludeTrackId);
};

/** The song as a drummer hears it: bars, where the accents fall, how busy and loud each bar is, the swing,
    and the sections (from the song's markers, or found from changes in energy and instrumentation). */
struct SongMap
{
    double tempo = 120.0, beatsPerBar = 4.0;
    int tsNum = 4, tsDen = 4;
    int stepsPerBar = 16;        // 16th-note steps
    int bars = 0;

    struct Bar
    {
        bool active = false;
        float energy = 0.0f;     // 0..1 across the song
        float density = 0.0f;    // share of 16th steps with an onset
        std::vector<float> accents, low;   // per 16th step, 0..1: every onset / low-end (bass, kick) onsets
    };
    std::vector<Bar> bar;

    float swing = 0.0f;          // 0 straight .. 1 full triplet swing
    bool tripletFeel = false;    // shuffle / 12-8 feel
    int firstActiveBar = 0, lastActiveBar = -1;

    struct Section
    {
        int startBar = 0, bars = 0;
        juce::String name;
        int intensity = 1;       // 0 quiet .. 3 peak
        float energy = 0.0f;
        bool silent = false;     // nothing playing (a break)
    };
    std::vector<Section> sections;

    int partsAnalysed = 0;
    juce::String summary() const;
    bool isEmpty() const { return bars == 0 || lastActiveBar < firstActiveBar; }
};

/** Analyses a snapshot. Audio parts are decoded through `cache` (null = MIDI only). */
SongMap analyzeSong (const SongSnapshot&, AudioCache* cache, double sampleRate);

/** Finds sections from per-bar features (used by analyzeSong; public for tests). */
void findSections (SongMap&, const std::vector<SongSnapshot::Marker>& markers, const std::vector<std::vector<int>>& activeTracksPerBar);

} // namespace wis::daw
