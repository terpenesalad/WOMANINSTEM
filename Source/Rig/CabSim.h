#pragma once

#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include "Convolver.h"

namespace wis
{

/** Speaker cabinets. (Order saved in presets and songs: only append. Rig state version 2 remaps the old list.) */
enum class CabType : int
{
    brit4x12 = 0,       // British 4x12, greenback-style: woody, mid-forward, classic rock
    modern4x12,         // modern 4x12: tight lows, aggressive upper mids
    american2x12,       // American 2x12 open back: clean, scooped, sparkly
    britBlue2x12,       // British 2x12 with alnico "blue" speakers: chimey, bright, 60s jangle
    openBack1x12,       // 1x12 open back combo: airy, focused
    tweed1x12,          // 50s tweed 1x12: warm, loose, papery
    bass1x15Vintage,    // vintage 1x15 bass: deep, round, no highs (60s studio)
    bass2x15Sixties,    // 60s British 2x15 bass column: warm, boomy, thumpy
    bass4x10Horn,       // 4x10 with horn: punchy, modern, clear top
    bass8x10,           // 8x10 "fridge": huge low mids, the rock bass cab
    bass2x10Modern,     // modern lightweight 2x10: tight, hi-fi
    impulseResponse,    // user-loaded .wav IR
    count
};

juce::StringArray cabTypeNames();
bool cabTypeIsBass (CabType t);
/** Maps a cabinet index saved by WOMANINSTEM 1.x / 2.x / 3.0 to the current list. */
int migrateCabTypeV1 (int oldIndex);

enum class MicType : int { dynamic = 0, ribbon, condenser, dynamicPlusRibbon, count };
juce::StringArray micTypeNames();

/** Speaker cabinet + microphone. Built-in cabinets are minimum-phase impulse responses designed from each
    speaker's response, cone break-up and the chosen mic / position / room, rebuilt in the background when
    you change them. You can also load any cabinet impulse response. */
class CabSim
{
public:
    CabSim();
    ~CabSim();

    void prepare (double sampleRate, int maxBlock);
    void reset();

    /** micPosition 0 = centre of the cone (bright) .. 1 = edge (dark); room 0..1. */
    void setParameters (CabType type, float lowCutHz, float highCutHz, MicType mic = MicType::dynamic, float micPosition = 0.3f, float room = 0.1f);
    void process (float* data, int numSamples);
    /** Delay (samples) before the cabinet's sound arrives, for lining up a DI. */
    int getArrivalSamples() const noexcept { return type == CabType::impulseResponse ? 0 : conv.currentPeak(); }

    /** Loads a .wav IR (thread-safe). Returns false if the file can't be read. */
    bool loadImpulseResponse (const juce::File& file);
    void clearImpulseResponse();
    juce::File getImpulseResponseFile() const { return irFile; }
    bool hasImpulseResponse() const { return irReady.load(); }

    /** Builds a built-in cabinet's impulse response (any thread). Also used by the tests. */
    static juce::AudioBuffer<float> designImpulseResponse (CabType type, MicType mic, float micPosition, float room, double sampleRate);

    /** Rebuilds the current cabinet right now (not real-time safe: tests, offline rendering). */
    void rebuildNow();

private:
    struct Builder;
    static int keyFor (CabType t, MicType m, float pos, float room);
    void updateFilters();

    double sampleRate = 48000.0;
    CabType type = CabType::brit4x12;
    float lowCut = 70.0f, highCut = 9000.0f;
    bool dirty = true;

    juce::dsp::IIR::Filter<float> userLowCut, userHighCut, userHighCut2;

    // built-in cabinets: impulse responses designed on a background thread, crossfaded in by the convolver
    Convolver conv;
    std::atomic<int> wantedKey { -1 }, builtKey { -1 };
    std::unique_ptr<Builder> builder;

    juce::dsp::Convolution convolution { juce::dsp::Convolution::NonUniform { 128 } };   // user IR
    juce::File irFile;                    // message thread only
    std::atomic<bool> irReady { false };  // read by the audio thread
};

} // namespace wis
