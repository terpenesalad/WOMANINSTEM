#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>
#include <set>

namespace wis::daw
{

/** Decoded audio for one file at the engine's sample rate, plus a peak overview for drawing. Immutable. */
struct AudioData : public juce::ReferenceCountedObject
{
    using Ptr = juce::ReferenceCountedObjectPtr<AudioData>;

    juce::File file;
    juce::AudioBuffer<float> buffer;   // 1 or 2 channels
    double sampleRate = 0.0;
    double lengthSeconds() const { return sampleRate > 0 ? buffer.getNumSamples() / sampleRate : 0.0; }

    static constexpr int peakStep = 256;   // samples per peak value
    std::vector<float> peaks;              // max |x| over both channels
};

/** Loads audio files on a background thread and keeps them in memory, keyed by file + sample rate.
    Broadcasts a change whenever a file finishes loading (the engine then rebuilds, the UI repaints). */
class AudioCache : public juce::ChangeBroadcaster, private juce::Thread, private juce::AsyncUpdater
{
public:
    AudioCache();
    ~AudioCache() override;

    /** Returns the data if loaded (else null, and queues a background load). Any thread except audio. */
    AudioData::Ptr get (const juce::File& f, double sampleRate);

    /** Loads synchronously (for export, tests and tools). */
    AudioData::Ptr getBlocking (const juce::File& f, double sampleRate);

    /** A time-stretched / pitch-shifted / reversed rendering of the whole file (ratio = new length / old length).
        Rendered in the background with Signalsmith Stretch; null until ready. */
    AudioData::Ptr getVariant (const juce::File& f, double sampleRate, double ratio, double semitones, bool reverse);
    AudioData::Ptr getVariantBlocking (const juce::File& f, double sampleRate, double ratio, double semitones, bool reverse);
    /** Length of the original file, given a variant and the ratio it was made with. */
    static double sourceLengthSeconds (const AudioData::Ptr& variant, double ratio) { return variant != nullptr ? variant->lengthSeconds() / ratio : 0.0; }
    /** Marks the start of a round of requests (the engine calls this when rebuilding); variants that are no longer
        wanted by the time their turn comes are skipped (e.g. while dragging the tempo). */
    void beginRequestRound();
    /** True while variants are being rendered (for a "processing" hint in the UI). */
    bool isRenderingVariants() const { return variantJobs.load() > 0; }

    /** Duration of a file in seconds without decoding it. */
    static double fileLengthSeconds (const juce::File& f);

    /** Frees files nobody references any more. */
    void purgeUnused();

    /** Files that failed to load (missing or undecodable). */
    juce::StringArray getFailedFiles() const;

private:
    void run() override;
    void handleAsyncUpdate() override { sendChangeMessage(); }
    static juce::String keyFor (const juce::File& f, double sr);
    static juce::String variantKey (const juce::File& f, double sr, double ratio, double semitones, bool reverse);
    static AudioData::Ptr load (const juce::File& f, double sr);
    static AudioData::Ptr render (const AudioData& source, double ratio, double semitones, bool reverse);
    static void computePeaks (AudioData& d);
    struct VariantJob;

    mutable juce::CriticalSection lock;
    std::map<juce::String, AudioData::Ptr> cache;
    juce::StringArray failed;
    struct Job { juce::File file; double rate; };
    std::vector<Job> queue;
    juce::WaitableEvent wake;

    juce::ThreadPool pool { juce::ThreadPoolOptions().withThreadName ("Time-stretch").withNumberOfThreads (juce::jlimit (1, 4, juce::SystemStats::getNumCpus() / 2)) };
    std::map<juce::String, int> wanted;     // variant key -> request round it was last asked for
    std::set<juce::String> variantsQueued;
    int round = 0;
    std::atomic<int> variantJobs { 0 };
};

} // namespace wis::daw
