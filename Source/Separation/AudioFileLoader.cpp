#include "AudioFileLoader.h"
#include "Common/Resample.h"
#include <mutex>

namespace wis
{

juce::AudioFormatManager& sharedFormatManager()
{
    static juce::AudioFormatManager manager;
    static std::once_flag once;
    std::call_once (once, [] { manager.registerBasicFormats(); });   // wav, aiff, flac, ogg, mp3 (+ Windows Media)
    return manager;
}

juce::String supportedAudioWildcard()
{
    return "*.mp3;*.flac;*.wav;*.aif;*.aiff;*.ogg;*.m4a;*.aac;*.wma";
}

static juce::String firstNonEmptyTag (const juce::StringPairArray& meta, std::initializer_list<const char*> keys)
{
    for (auto* k : keys)
    {
        for (auto& stored : meta.getAllKeys())
            if (stored.equalsIgnoreCase (k) && meta[stored].trim().isNotEmpty())
                return meta[stored].trim();
    }
    return {};
}

LoadedAudio loadAudioFile (const juce::File& file, double targetRate, const std::function<bool()>& shouldCancel)
{
    LoadedAudio result;
    result.title = file.getFileNameWithoutExtension();

    if (! file.existsAsFile())
    {
        result.error = "File not found: " + file.getFullPathName();
        return result;
    }

    std::unique_ptr<juce::AudioFormatReader> reader (sharedFormatManager().createReaderFor (file));

    if (reader == nullptr)
    {
        result.error = "Can't decode \"" + file.getFileName() + "\". Supported: MP3, FLAC, WAV, AIFF, OGG (and M4A on Windows).";
        return result;
    }

    if (reader->lengthInSamples <= 0 || reader->sampleRate <= 0)
    {
        result.error = "\"" + file.getFileName() + "\" contains no audio.";
        return result;
    }

    const auto maxSeconds = 30.0 * 60.0;
    if ((double) reader->lengthInSamples / reader->sampleRate > maxSeconds)
    {
        result.error = "That file is longer than 30 minutes. Please trim it first.";
        return result;
    }

    auto tagTitle  = firstNonEmptyTag (reader->metadataValues, { "title", "TITLE", "TIT2", "INAM" });
    auto tagArtist = firstNonEmptyTag (reader->metadataValues, { "artist", "ARTIST", "TPE1", "IART" });
    if (tagTitle.isNotEmpty())  result.title  = tagTitle;
    if (tagArtist.isNotEmpty()) result.artist = tagArtist;

    const int numSamples   = (int) reader->lengthInSamples;
    const int fileChannels = (int) reader->numChannels;

    juce::AudioBuffer<float> raw (juce::jmax (2, fileChannels), numSamples);
    raw.clear();

    // Read in chunks so a cancel request is honoured quickly on long files.
    const int chunk = 1 << 18;
    for (int pos = 0; pos < numSamples; pos += chunk)
    {
        if (shouldCancel && shouldCancel())
        {
            result.error = "Cancelled";
            return result;
        }

        const int n = juce::jmin (chunk, numSamples - pos);
        juce::AudioBuffer<float> tmp (fileChannels, n);
        reader->read (&tmp, 0, n, pos, true, true);

        for (int ch = 0; ch < fileChannels; ++ch)
            raw.copyFrom (ch, pos, tmp, ch, 0, n);
    }

    juce::AudioBuffer<float> stereo (2, numSamples);

    if (fileChannels == 1)
    {
        stereo.copyFrom (0, 0, raw, 0, 0, numSamples);
        stereo.copyFrom (1, 0, raw, 0, 0, numSamples);
    }
    else if (fileChannels == 2)
    {
        stereo.copyFrom (0, 0, raw, 0, 0, numSamples);
        stereo.copyFrom (1, 0, raw, 1, 0, numSamples);
    }
    else
    {
        // Simple fold-down: odd channels left, even channels right, centre (ch 2) to both.
        stereo.clear();
        for (int ch = 0; ch < fileChannels; ++ch)
        {
            const float g = 1.0f / (float) ((fileChannels + 1) / 2);
            if (ch == 2) { stereo.addFrom (0, 0, raw, ch, 0, numSamples, g * 0.707f); stereo.addFrom (1, 0, raw, ch, 0, numSamples, g * 0.707f); }
            else          stereo.addFrom (ch % 2, 0, raw, ch, 0, numSamples, g);
        }
    }

    result.sampleRate = reader->sampleRate;

    if (targetRate > 0.0 && std::abs (targetRate - reader->sampleRate) > 0.5)
    {
        result.audio = resampleBuffer (stereo, reader->sampleRate, targetRate);
        result.sampleRate = targetRate;
    }
    else
    {
        result.audio = std::move (stereo);
    }

    return result;
}

} // namespace wis
