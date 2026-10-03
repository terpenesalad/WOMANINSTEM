#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "Common/Stems.h"
#include "ModelManager.h"
#include <array>
#include <functional>
#include <memory>

namespace demucscpp { struct demucs_model; }

namespace wis
{

enum class SeparationQuality
{
    standard = 0,   // one pass of the 6-stem model
    maximum  = 1    // 6-stem model + fine-tuned drums/bass/vocals specialists (about 4x slower)
};

struct SeparationSettings
{
    SeparationQuality quality = SeparationQuality::standard;
    bool splitBackingVocals = true;
    int numThreads = 0;   // 0 = automatic
};

struct SeparatedStems
{
    std::array<juce::AudioBuffer<float>, numStemIds> audio;   // stereo, 44.1 kHz
    std::array<bool, numStemIds> present {};                  // false if the stem is (near) silent
    std::array<float, numStemIds> rmsDb {};
};

/** Runs Demucs v4 (via demucs.cpp) on a 44.1 kHz stereo mix. All methods are blocking;
    call from a background thread. Thread-safe to use one instance per job. */
class StemSeparator
{
public:
    StemSeparator();
    ~StemSeparator();

    using Progress = std::function<void (float progress01, const juce::String& stage)>;

    /** Returns empty string on success, "Cancelled" if cancelled, else an error message. */
    juce::String separate (const juce::AudioBuffer<float>& mix44k,
                           const SeparationSettings& settings,
                           const Progress& progress,
                           const std::function<bool()>& shouldCancel,
                           SeparatedStems& result);

    /** Models needed for the given quality that aren't installed yet. */
    static juce::Array<ModelId> missingModels (SeparationQuality q);

    static int defaultThreadCount();

    /** Measured with `stemsplit --selftest --long --threads N` (see CI benchmark). */
    static constexpr int baseMemoryMB = 1200;
    static constexpr int perWorkerMemoryMB = 700;

private:
    struct CancelledException {};

    demucscpp::demucs_model* getModel (ModelId id, juce::String& error);

    /** Runs one model over the whole mix with chunk-level multithreading.
        Returns tensor [sources][2][N] as a vector of stereo buffers. */
    bool runModel (ModelId id, const juce::AudioBuffer<float>& mix, int numThreads,
                   float progressStart, float progressEnd, const juce::String& stage,
                   const Progress& progress, const std::function<bool()>& shouldCancel,
                   std::vector<juce::AudioBuffer<float>>& sourcesOut, juce::String& error);

    std::map<ModelId, std::unique_ptr<demucscpp::demucs_model>> models;
};

} // namespace wis
