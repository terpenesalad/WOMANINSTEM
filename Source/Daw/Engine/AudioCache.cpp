#include "AudioCache.h"
#include "Separation/AudioFileLoader.h"
#include "Common/Resample.h"

namespace wis::daw
{

AudioCache::AudioCache() : juce::Thread ("Audio loader")
{
    startThread (juce::Thread::Priority::normal);
}

AudioCache::~AudioCache()
{
    signalThreadShouldExit();
    wake.signal();
    stopThread (5000);
    cancelPendingUpdate();
}

juce::String AudioCache::keyFor (const juce::File& f, double sr)
{
    return f.getFullPathName() + "@" + juce::String ((int) std::round (sr));
}

AudioData::Ptr AudioCache::get (const juce::File& f, double sr)
{
    const juce::ScopedLock sl (lock);
    const auto key = keyFor (f, sr);
    if (auto it = cache.find (key); it != cache.end())
        return it->second;

    if (! failed.contains (key))
    {
        bool queued = false;
        for (auto& j : queue) queued = queued || keyFor (j.file, j.rate) == key;
        if (! queued)
        {
            queue.push_back ({ f, sr });
            wake.signal();
        }
    }
    return nullptr;
}

AudioData::Ptr AudioCache::getBlocking (const juce::File& f, double sr)
{
    {
        const juce::ScopedLock sl (lock);
        if (auto it = cache.find (keyFor (f, sr)); it != cache.end())
            return it->second;
    }
    auto d = load (f, sr);
    const juce::ScopedLock sl (lock);
    if (d != nullptr) cache[keyFor (f, sr)] = d;
    return d;
}

double AudioCache::fileLengthSeconds (const juce::File& f)
{
    std::unique_ptr<juce::AudioFormatReader> r (sharedFormatManager().createReaderFor (f));
    return r != nullptr && r->sampleRate > 0 ? (double) r->lengthInSamples / r->sampleRate : 0.0;
}

void AudioCache::purgeUnused()
{
    const juce::ScopedLock sl (lock);
    for (auto it = cache.begin(); it != cache.end();)
        it = it->second->getReferenceCount() == 1 ? cache.erase (it) : std::next (it);
}

juce::StringArray AudioCache::getFailedFiles() const
{
    const juce::ScopedLock sl (lock);
    return failed;
}

AudioData::Ptr AudioCache::load (const juce::File& f, double sr)
{
    std::unique_ptr<juce::AudioFormatReader> reader (sharedFormatManager().createReaderFor (f));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return nullptr;

    const int channels = juce::jlimit (1, 2, (int) reader->numChannels);
    const int len = (int) juce::jmin ((juce::int64) std::numeric_limits<int>::max() / 2, reader->lengthInSamples);
    juce::AudioBuffer<float> raw (channels, len);
    if (reader->numChannels > 2)
    {
        juce::AudioBuffer<float> all ((int) reader->numChannels, len);
        reader->read (&all, 0, len, 0, true, true);
        raw.copyFrom (0, 0, all, 0, 0, len);
        raw.copyFrom (1, 0, all, 1, 0, len);
    }
    else
    {
        reader->read (&raw, 0, len, 0, true, channels > 1);
    }

    AudioData::Ptr d = new AudioData();
    d->file = f;
    d->sampleRate = sr;
    d->buffer = std::abs (reader->sampleRate - sr) > 0.5 ? wis::resampleBuffer (raw, reader->sampleRate, sr) : std::move (raw);

    const int n = d->buffer.getNumSamples();
    d->peaks.resize ((size_t) (n / AudioData::peakStep + 1));
    for (size_t p = 0; p < d->peaks.size(); ++p)
    {
        const int a = (int) p * AudioData::peakStep, b = juce::jmin (n, a + AudioData::peakStep);
        float pk = 0.0f;
        for (int ch = 0; ch < d->buffer.getNumChannels(); ++ch)
        {
            auto* s = d->buffer.getReadPointer (ch);
            for (int i = a; i < b; ++i) pk = juce::jmax (pk, std::abs (s[i]));
        }
        d->peaks[p] = pk;
    }
    return d;
}

void AudioCache::run()
{
    while (! threadShouldExit())
    {
        Job job;
        bool have = false;
        {
            const juce::ScopedLock sl (lock);
            if (! queue.empty())
            {
                job = queue.front();
                queue.erase (queue.begin());
                have = true;
            }
        }

        if (! have)
        {
            wake.wait (500);
            continue;
        }

        auto d = load (job.file, job.rate);
        {
            const juce::ScopedLock sl (lock);
            if (d != nullptr) cache[keyFor (job.file, job.rate)] = d;
            else failed.addIfNotAlreadyThere (keyFor (job.file, job.rate));
        }
        triggerAsyncUpdate();
    }
}

} // namespace wis::daw
