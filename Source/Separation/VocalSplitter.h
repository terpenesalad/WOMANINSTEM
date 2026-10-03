#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <functional>

namespace wis
{

/** Splits a separated vocal stem into "lead" (phantom-centre content: identical in L and R)
    and "backing" (wide, doubled or harmonised parts that differ between L and R).

    This works because almost every mix pans the lead vocal dead centre, while backing vocals,
    doubles and harmonies are recorded separately and spread across the stereo field.
    lead + backing == input exactly, so nothing is lost. If the "backing" part turns out to be
    only a faint reverb tail, it's folded back into the lead and backing is returned silent. */
struct VocalSplitter
{
    static void split (const juce::AudioBuffer<float>& vocals,
                       juce::AudioBuffer<float>& lead,
                       juce::AudioBuffer<float>& backing,
                       const std::function<bool()>& shouldCancel = {});
};

} // namespace wis
