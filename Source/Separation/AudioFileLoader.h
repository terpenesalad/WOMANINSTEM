#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

namespace wis
{

struct LoadedAudio
{
    juce::AudioBuffer<float> audio;   // always stereo
    double sampleRate = 0.0;
    juce::String title;               // from tags when available, else the file name
    juce::String artist;
    juce::String error;               // non-empty on failure

    bool ok() const noexcept { return error.isEmpty() && audio.getNumSamples() > 0; }
};

/** Formats we can open (mp3, flac, wav, aiff, ogg, plus m4a/wma via Windows Media on Windows). */
juce::AudioFormatManager& sharedFormatManager();
juce::String supportedAudioWildcard();

/** Decodes an audio file to a stereo float buffer and converts it to targetRate (0 = keep native rate).
    Mono files are duplicated to both channels, multi-channel files are folded down to stereo. */
LoadedAudio loadAudioFile (const juce::File& file, double targetRate,
                           const std::function<bool()>& shouldCancel = {});

} // namespace wis
