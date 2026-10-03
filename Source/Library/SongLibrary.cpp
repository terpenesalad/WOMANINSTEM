#include "SongLibrary.h"
#include "Separation/ModelManager.h"
#include "Separation/AudioFileLoader.h"
#include <juce_cryptography/juce_cryptography.h>

namespace wis
{

juce::File SongLibrary::libraryDirectory()
{
    auto dir = ModelManager::appDataDirectory().getChildFile ("Library");
    dir.createDirectory();
    return dir;
}

juce::String SongLibrary::computeId (const juce::File& source, SeparationQuality quality)
{
    juce::FileInputStream in (source);
    juce::String hash;

    if (in.openedOk())
        hash = juce::MD5 (in).toHexString().substring (0, 20);
    else
        hash = juce::String::toHexString (source.getFullPathName().hashCode64());

    return hash + (quality == SeparationQuality::maximum ? "-max" : "-std");
}

std::optional<SongInfo> SongLibrary::readManifest (const juce::File& folder)
{
    auto manifest = folder.getChildFile ("manifest.json");
    if (! manifest.existsAsFile())
        return std::nullopt;

    auto json = juce::JSON::parse (manifest);
    auto* obj = json.getDynamicObject();
    if (obj == nullptr)
        return std::nullopt;

    SongInfo info;
    info.folder          = folder;
    info.id              = obj->getProperty ("id").toString();
    info.title           = obj->getProperty ("title").toString();
    info.artist          = obj->getProperty ("artist").toString();
    info.sourcePath      = obj->getProperty ("source").toString();
    info.durationSeconds = (double) obj->getProperty ("duration");
    info.added           = juce::Time ((juce::int64) obj->getProperty ("added"));
    info.quality         = (int) obj->getProperty ("quality") == 1 ? SeparationQuality::maximum : SeparationQuality::standard;

    if (auto* stems = obj->getProperty ("stems").getArray())
        for (auto& s : *stems)
        {
            const int idx = stemIndexForKey (s.toString());
            if (idx >= 0 && info.stemFile ((StemId) idx).existsAsFile())
                info.present[(size_t) idx] = true;
        }

    if (info.id.isEmpty())
        return std::nullopt;

    return info;
}

juce::Array<SongInfo> SongLibrary::listSongs()
{
    juce::Array<SongInfo> songs;

    for (auto& entry : juce::RangedDirectoryIterator (libraryDirectory(), false, "*", juce::File::findDirectories))
        if (auto info = readManifest (entry.getFile()))
            songs.add (*info);

    std::sort (songs.begin(), songs.end(), [] (const SongInfo& a, const SongInfo& b) { return a.added > b.added; });
    return songs;
}

std::optional<SongInfo> SongLibrary::findSong (const juce::String& id)
{
    return readManifest (libraryDirectory().getChildFile (id));
}

juce::String SongLibrary::saveSong (SongInfo& info, const SeparatedStems& stems,
                                    const std::function<void (float)>& progress)
{
    info.folder = libraryDirectory().getChildFile (info.id);
    info.folder.deleteRecursively();
    if (! info.folder.createDirectory())
        return "Couldn't create the library folder " + info.folder.getFullPathName();

    juce::FlacAudioFormat flac;
    juce::Array<juce::var> stemKeys;

    int done = 0;
    for (auto& s : allStems())
    {
        const auto idx = (size_t) s.id;
        info.present[idx] = stems.present[idx];

        if (stems.present[idx])
        {
            auto& buffer = stems.audio[idx];
            auto file = info.stemFile (s.id);

            std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());
            if (stream == nullptr)
                return "Couldn't write " + file.getFullPathName();

            auto options = juce::AudioFormatWriterOptions{}
                               .withSampleRate (stemSampleRate)
                               .withNumChannels (2)
                               .withBitsPerSample (24)
                               .withQualityOptionIndex (5);

            auto writer = flac.createWriterFor (stream, options);
            if (writer == nullptr)
                return "FLAC encoder failed for " + file.getFileName();

            // Stems can very slightly exceed 0 dBFS; FLAC is integer so clip softly at the format limit.
            juce::AudioBuffer<float> safe (buffer);
            for (int ch = 0; ch < safe.getNumChannels(); ++ch)
                juce::FloatVectorOperations::clip (safe.getWritePointer (ch), safe.getReadPointer (ch), -0.9999f, 0.9999f, safe.getNumSamples());

            writer->writeFromAudioSampleBuffer (safe, 0, safe.getNumSamples());
            writer.reset();
            stemKeys.add (juce::String (s.key));
        }

        if (progress)
            progress ((float) ++done / (float) numStemIds);
    }

    auto obj = std::make_unique<juce::DynamicObject>();
    obj->setProperty ("id", info.id);
    obj->setProperty ("title", info.title);
    obj->setProperty ("artist", info.artist);
    obj->setProperty ("source", info.sourcePath);
    obj->setProperty ("duration", info.durationSeconds);
    obj->setProperty ("added", info.added.toMilliseconds());
    obj->setProperty ("quality", (int) info.quality);
    obj->setProperty ("sampleRate", stemSampleRate);
    obj->setProperty ("stems", stemKeys);
    obj->setProperty ("app", "WOMANINSTEM " WIS_VERSION_STRING);

    if (! info.folder.getChildFile ("manifest.json").replaceWithText (juce::JSON::toString (juce::var (obj.release()))))
        return "Couldn't write the song manifest.";

    return {};
}

juce::String SongLibrary::loadStems (const SongInfo& info, StemBuffers& out, const std::function<bool()>& shouldCancel)
{
    juce::FlacAudioFormat flac;

    for (auto& s : allStems())
    {
        auto& dest = out[(size_t) s.id];
        dest.setSize (0, 0);

        if (! info.present[(size_t) s.id])
            continue;

        if (shouldCancel && shouldCancel())
            return "Cancelled";

        auto file = info.stemFile (s.id);
        std::unique_ptr<juce::AudioFormatReader> reader (flac.createReaderFor (file.createInputStream().release(), true));
        if (reader == nullptr)
            return "Couldn't read " + file.getFullPathName() + " - try separating the song again.";

        const int len = (int) reader->lengthInSamples;
        dest.setSize (2, len);
        reader->read (&dest, 0, len, 0, true, true);
        if (reader->numChannels == 1)
            dest.copyFrom (1, 0, dest, 0, 0, len);
    }

    return {};
}

bool SongLibrary::removeSong (const juce::String& id)
{
    if (id.isEmpty()) return false;
    return libraryDirectory().getChildFile (id).deleteRecursively();
}

juce::String SongLibrary::exportStems (const SongInfo& info, const juce::File& destFolder,
                                       const std::function<void (float)>& progress)
{
    auto safeName = juce::File::createLegalFileName (info.displayName());
    auto folder = destFolder.getChildFile (safeName + " stems");
    folder.createDirectory();

    StemBuffers stems;
    if (auto err = loadStems (info, stems); err.isNotEmpty())
        return err;

    juce::WavAudioFormat wav;
    int done = 0;

    for (auto& s : allStems())
    {
        auto& buf = stems[(size_t) s.id];
        if (buf.getNumSamples() > 0)
        {
            auto file = folder.getChildFile (safeName + " - " + juce::File::createLegalFileName (s.displayName) + ".wav");
            file.deleteFile();

            std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());
            if (stream == nullptr)
                return "Couldn't write " + file.getFullPathName();

            auto options = juce::AudioFormatWriterOptions{}
                               .withSampleRate (stemSampleRate)
                               .withNumChannels (2)
                               .withBitsPerSample (24);

            auto writer = wav.createWriterFor (stream, options);
            if (writer == nullptr)
                return "WAV writer failed.";
            writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
        }

        if (progress) progress ((float) ++done / (float) numStemIds);
    }

    folder.revealToUser();
    return {};
}

} // namespace wis
