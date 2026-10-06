#include "TestEffects.h"

namespace wis::daw
{

static prm::Layout latencyTestLayout()
{
    prm::Layout l;
    prm::addFloat (l, "gain", "Gain", 0.0f, 1.0f, 1.0f);
    return l;
}

LatencyTestFx::LatencyTestFx() : BuiltinProcessor ("testlatency", "Latency Test", false, latencyTestLayout()) {}

void LatencyTestFx::prepareToPlay (double, int)
{
    line.setSize (2, latencySamples);
    line.clear();
    pos = 0;
    setLatencySamples (latencySamples);
}

void LatencyTestFx::processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&)
{
    const float g = param ("gain");
    const int n = b.getNumSamples();
    const int size = line.getNumSamples();
    for (int i = 0; i < n; ++i)
    {
        for (int ch = 0; ch < juce::jmin (2, b.getNumChannels()); ++ch)
        {
            const float x = b.getSample (ch, i);
            b.setSample (ch, i, line.getSample (ch, pos) * g);
            line.setSample (ch, pos, x);
        }
        if (++pos >= size) pos = 0;
    }
}

} // namespace wis::daw
