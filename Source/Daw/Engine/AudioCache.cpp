#include "AudioCache.h"
#include "Separation/AudioFileLoader.h"
#include "Common/Resample.h"
#include <signalsmith-stretch/signalsmith-stretch.h>

namespace wis::daw
{

AudioCache::AudioCache() : juce::Thread ("Audio loader")
{
    startThread (juce::Thread::Priority::normal);
}

AudioCache::~AudioCache()
{
    pool.removeAllJobs (true, 10000);
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
    {
        auto w = wanted.find (it->first);
        const bool recentlyWanted = w != wanted.end() && w->second >= round - 1;
        it = it->second->getReferenceCount() == 1 && ! recentlyWanted ? cache.erase (it) : std::next (it);
    }
    // forget old requests
    for (auto it = wanted.begin(); it != wanted.end();)
        it = it->second < round - 2 ? wanted.erase (it) : std::next (it);
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

    computePeaks (*d);
    return d;
}

void AudioCache::computePeaks (AudioData& data)
{
    auto* d = &data;
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
}

// ---- variants (time-stretch / pitch / reverse) -------------------------------------------------------------

juce::String AudioCache::variantKey (const juce::File& f, double sr, double ratio, double semitones, bool reverse)
{
    return keyFor (f, sr) + "#" + juce::String (ratio, 4) + "#" + juce::String (semitones, 2) + (reverse ? "#r" : "");
}

AudioData::Ptr AudioCache::render (const AudioData& source, double ratio, double semitones, bool reverse)
{
    AudioData::Ptr d = new AudioData();
    d->file = source.file;
    d->sampleRate = source.sampleRate;
    const int channels = source.buffer.getNumChannels();
    const int inLen = source.buffer.getNumSamples();

    juce::AudioBuffer<float> in (channels, inLen);
    for (int c = 0; c < channels; ++c)
    {
        in.copyFrom (c, 0, source.buffer, c, 0, inLen);
        if (reverse) std::reverse (in.getWritePointer (c), in.getWritePointer (c) + inLen);
    }

    const bool stretch = std::abs (ratio - 1.0) > 1.0e-4 || std::abs (semitones) > 1.0e-3;
    if (! stretch)
    {
        d->buffer = std::move (in);
    }
    else
    {
        const int outLen = juce::jmax (1, (int) std::llround (inLen * ratio));
        d->buffer.setSize (channels, outLen);
        signalsmith::stretch::SignalsmithStretch<float> st;
        st.presetDefault (channels, (float) source.sampleRate);
        st.setTransposeSemitones ((float) semitones);
        std::vector<const float*> ins;
        std::vector<float*> outs;
        for (int c = 0; c < channels; ++c) { ins.push_back (in.getReadPointer (c)); outs.push_back (d->buffer.getWritePointer (c)); }
        if (! st.exact (ins.data(), inLen, outs.data(), outLen))
        {
            // very short sounds: simple resample instead
            for (int c = 0; c < channels; ++c)
                for (int i = 0; i < outLen; ++i)
                {
                    const double x = i / ratio;
                    const int a = juce::jlimit (0, inLen - 1, (int) x);
                    const int b = juce::jmin (inLen - 1, a + 1);
                    const float f = (float) (x - a);
                    outs[(size_t) c][i] = ins[(size_t) c][a] * (1.0f - f) + ins[(size_t) c][b] * f;
                }
        }
    }
    computePeaks (*d);
    return d;
}

struct AudioCache::VariantJob : public juce::ThreadPoolJob
{
    VariantJob (AudioCache& c, juce::File f, double r, double rt, double st, bool rev, juce::String k)
        : ThreadPoolJob ("stretch"), cache (c), file (std::move (f)), rate (r), ratio (rt), semis (st), reverse (rev), key (std::move (k)) {}

    JobStatus runJob() override
    {
        bool stillWanted;
        {
            const juce::ScopedLock sl (cache.lock);
            auto it = cache.wanted.find (key);
            stillWanted = it != cache.wanted.end() && it->second >= cache.round - 1;
        }
        if (stillWanted && ! shouldExit())
        {
            if (auto base = cache.getBlocking (file, rate))
            {
                auto v = render (*base, ratio, semis, reverse);
                const juce::ScopedLock sl (cache.lock);
                cache.cache[key] = v;
            }
            else
            {
                const juce::ScopedLock sl (cache.lock);
                cache.failed.addIfNotAlreadyThere (key);
            }
        }
        {
            const juce::ScopedLock sl (cache.lock);
            cache.variantsQueued.erase (key);
        }
        --cache.variantJobs;
        cache.triggerAsyncUpdate();
        return jobHasFinished;
    }

    AudioCache& cache;
    juce::File file;
    double rate, ratio, semis;
    bool reverse;
    juce::String key;
};

void AudioCache::beginRequestRound()
{
    const juce::ScopedLock sl (lock);
    ++round;
}

AudioData::Ptr AudioCache::getVariant (const juce::File& f, double sr, double ratio, double semitones, bool reverse)
{
    const auto key = variantKey (f, sr, ratio, semitones, reverse);
    const juce::ScopedLock sl (lock);
    wanted[key] = round;
    if (auto it = cache.find (key); it != cache.end())
        return it->second;
    if (failed.contains (key) || variantsQueued.count (key) > 0)
        return nullptr;
    variantsQueued.insert (key);
    ++variantJobs;
    pool.addJob (new VariantJob (*this, f, sr, ratio, semitones, reverse, key), true);
    return nullptr;
}

AudioData::Ptr AudioCache::getVariantBlocking (const juce::File& f, double sr, double ratio, double semitones, bool reverse)
{
    const auto key = variantKey (f, sr, ratio, semitones, reverse);
    {
        const juce::ScopedLock sl (lock);
        if (auto it = cache.find (key); it != cache.end())
            return it->second;
    }
    auto base = getBlocking (f, sr);
    if (base == nullptr) return nullptr;
    auto v = render (*base, ratio, semitones, reverse);
    const juce::ScopedLock sl (lock);
    cache[key] = v;
    return v;
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
