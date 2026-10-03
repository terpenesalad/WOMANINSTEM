#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#if defined(_MSC_VER)
 #pragma warning (push, 0)
#endif
#include <CDSPResampler.h>
#if defined(_MSC_VER)
 #pragma warning (pop)
#endif

namespace wis
{

/** Offline, high quality (r8brain, 24-bit class) sample-rate conversion of a whole buffer. */
inline juce::AudioBuffer<float> resampleBuffer (const juce::AudioBuffer<float>& in,
                                                double sourceRate, double targetRate)
{
    if (std::abs (sourceRate - targetRate) < 0.5 || in.getNumSamples() == 0)
        return in;

    const int inLen  = in.getNumSamples();
    const int outLen = (int) std::llround ((double) inLen * targetRate / sourceRate);

    juce::AudioBuffer<float> out (in.getNumChannels(), juce::jmax (1, outLen));

    for (int ch = 0; ch < in.getNumChannels(); ++ch)
    {
        r8b::CDSPResampler24 resampler (sourceRate, targetRate, 8192);
        resampler.oneshot (const_cast<float*> (in.getReadPointer (ch)), inLen, out.getWritePointer (ch), outLen);   // r8brain doesn't modify input
    }

    return out;
}

} // namespace wis
