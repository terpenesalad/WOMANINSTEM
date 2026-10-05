#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace wis::daw
{

struct TempoEstimate
{
    double bpm = 120.0;
    double firstBeatSeconds = 0.0;   // where a beat falls (phase) - the likeliest downbeat
    float confidence = 0.0f;         // 0..1
};

/** Estimates tempo and beat phase from a rhythmic signal (ideally a drum stem).
    Onset-strength envelope -> autocorrelation with a preference for 80-160 BPM -> phase by comb alignment. */
TempoEstimate estimateTempo (const juce::AudioBuffer<float>& audio, double sampleRate);

} // namespace wis::daw
