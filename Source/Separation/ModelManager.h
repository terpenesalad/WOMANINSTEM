#pragma once

#include <juce_core/juce_core.h>
#include <functional>

namespace wis
{

/** The neural network weight files used for separation (Demucs v4, ggml format from demucs.cpp). */
enum class ModelId
{
    sixStem,        // htdemucs_6s: drums, bass, other, vocals, guitar, piano
    ftDrums,        // htdemucs_ft fine-tuned specialists, used by "Maximum" quality
    ftBass,
    ftVocals
};

struct ModelSpec
{
    ModelId id;
    const char* fileName;
    const char* description;
    juce::int64 approxBytes;
};

const ModelSpec& modelSpec (ModelId id);

class ModelManager
{
public:
    /** %APPDATA%/WOMANINSTEM on Windows, ~/.config/WOMANINSTEM on Linux, etc. */
    static juce::File appDataDirectory();

    /** Where downloaded models go. */
    static juce::File userModelsDirectory();

    /** Returns the model file if it exists in any known location (next to the exe, /models next to the exe,
        or the user models folder). */
    static juce::File findModel (ModelId id);

    static bool isInstalled (ModelId id) { return findModel (id).existsAsFile(); }

    static juce::URL downloadUrl (ModelId id);

    /** Downloads a model (blocking - call from a background thread).
        progress(bytesDone, bytesTotal) is called periodically; return false from shouldContinue to abort.
        Returns an empty string on success, else an error message. */
    static juce::String download (ModelId id,
                                  const std::function<void (juce::int64, juce::int64)>& progress,
                                  const std::function<bool()>& shouldContinue);

    /** A path that the C runtime's fopen() can open on every platform (Windows narrow-char fopen can't open
        paths with non-ASCII characters, so we fall back to the 8.3 short path there). */
    static std::string pathForFopen (const juce::File& f);
};

} // namespace wis
