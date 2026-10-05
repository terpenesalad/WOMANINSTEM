#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

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
    static AudioData::Ptr load (const juce::File& f, double sr);

    mutable juce::CriticalSection lock;
    std::map<juce::String, AudioData::Ptr> cache;
    juce::StringArray failed;
    struct Job { juce::File file; double rate; };
    std::vector<Job> queue;
    juce::WaitableEvent wake;
};

} // namespace wis::daw
