#pragma once

#include <juce_dsp/juce_dsp.h>
#include <atomic>

namespace wis
{

enum class CabType : int
{
    brit4x12 = 0,
    american2x12,
    openBack1x12,
    bass4x10,
    bass8x10,
    impulseResponse,   // user-loaded .wav IR
    count
};

juce::StringArray cabTypeNames();

/** Speaker cabinet + microphone simulation. Built-in cabinets are modelled with a resonant filter
    network (cheap, zero latency); for studio realism the user can load any cabinet impulse response. */
class CabSim
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset();

    void setParameters (CabType type, float lowCutHz, float highCutHz);
    void process (float* data, int numSamples);

    /** Loads a .wav IR in the background (thread-safe). Returns false if the file can't be read. */
    bool loadImpulseResponse (const juce::File& file);
    void clearImpulseResponse();
    juce::File getImpulseResponseFile() const { return irFile; }
    bool hasImpulseResponse() const { return irReady.load(); }

private:
    void updateFilters();

    double sampleRate = 48000.0;
    CabType type = CabType::brit4x12;
    float lowCut = 70.0f, highCut = 9000.0f;
    bool dirty = true;

    static constexpr int numBands = 6;
    std::array<juce::dsp::IIR::Filter<float>, numBands> bands;
    juce::dsp::IIR::Filter<float> userLowCut, userHighCut, userHighCut2;

    juce::dsp::Convolution convolution { juce::dsp::Convolution::NonUniform { 128 } };   // zero latency, efficient tail
    juce::File irFile;                 // message thread only
    std::atomic<bool> irReady { false };  // read by the audio thread
};

} // namespace wis
