#pragma once

#include "BuiltinProcessor.h"

namespace wis::daw
{

/** Hidden helper used by the automated tests: a gain with a fixed, reported latency (exercises delay compensation
    and plugin-parameter automation). Never shown in the UI. */
class LatencyTestFx : public BuiltinProcessor
{
public:
    static constexpr int latencySamples = 1000;
    LatencyTestFx();
    void prepareToPlay (double sr, int block) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
private:
    juce::AudioBuffer<float> line;
    int pos = 0;
};

} // namespace wis::daw
