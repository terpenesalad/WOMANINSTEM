#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include "Common/Stems.h"
#include "Separation/StemSeparator.h"
#include <array>
#include <optional>

namespace wis
{

/** A separated song stored in the local library (one folder of FLAC stems + manifest.json). */
struct SongInfo
{
    juce::String id;
    juce::String title;
    juce::String artist;
    juce::String sourcePath;
    double durationSeconds = 0.0;
    juce::Time added;
    SeparationQuality quality = SeparationQuality::standard;
    std::array<bool, numStemIds> present {};
    juce::File folder;

    juce::String displayName() const { return artist.isNotEmpty() ? artist + " - " + title : title; }
    juce::File stemFile (StemId s) const { return folder.getChildFile (juce::String (stemInfo (s).key) + ".flac"); }
};

using StemBuffers = std::array<juce::AudioBuffer<float>, numStemIds>;

class SongLibrary
{
public:
    static juce::File libraryDirectory();

    /** Content hash of the source file + quality - identical files are only separated once. */
    static juce::String computeId (const juce::File& source, SeparationQuality quality);

    static juce::Array<SongInfo> listSongs();
    static std::optional<SongInfo> findSong (const juce::String& id);

    /** Writes stems (24-bit FLAC) and the manifest. Returns empty string on success. */
    static juce::String saveSong (SongInfo& info, const SeparatedStems& stems,
                                  const std::function<void (float)>& progress = {});

    /** Loads all stems of a song (44.1 kHz stereo). Absent stems come back empty. */
    static juce::String loadStems (const SongInfo& info, StemBuffers& out,
                                   const std::function<bool()>& shouldCancel = {});

    static bool removeSong (const juce::String& id);

    /** Exports every present stem as 24-bit WAV into destFolder/<song name> stems/. */
    static juce::String exportStems (const SongInfo& info, const juce::File& destFolder,
                                     const std::function<void (float)>& progress = {});

private:
    static std::optional<SongInfo> readManifest (const juce::File& folder);
};

} // namespace wis
