#pragma once

#include <juce_dsp/juce_dsp.h>
#include <atomic>

namespace wis
{

/** Hands an impulse response built on another thread to a juce::dsp::Convolution on the audio thread.
    JUCE wants loadImpulseResponse() called in step with process() (in practice from the audio thread); it is wait-free
    there and takes the buffer over without allocating. This passes the buffer across with two atomic slots, so the
    audio thread never locks, allocates or frees. Call collect() from time to time on the other thread. */
class IrHandoff
{
public:
    ~IrHandoff()
    {
        delete pending.exchange (nullptr);
        delete retired.exchange (nullptr);
    }

    /** Any thread but the audio thread: offers an IR (replacing one that's still waiting). */
    void post (juce::AudioBuffer<float>&& ir, double sampleRate, int key, bool stereo, bool normalise, bool trim = false)
    {
        collect();
        auto* item = new Item { std::move (ir), sampleRate, key, stereo, normalise, trim };
        delete pending.exchange (item);
    }

    /** Not the audio thread: frees what the audio thread has finished with. */
    void collect() { delete retired.exchange (nullptr); }

    /** Drops an IR still waiting (not the audio thread). */
    void discard() { delete pending.exchange (nullptr); }

    bool hasPending() const noexcept { return pending.load() != nullptr; }

    /** Audio thread (or while it's stopped): installs a waiting IR. Returns its key, or -1 if there was none. */
    int install (juce::dsp::Convolution& c) noexcept
    {
        if (retired.load() != nullptr)
            return -1;                               // the last one hasn't been collected yet: try again next block
        auto* item = pending.exchange (nullptr);
        if (item == nullptr)
            return -1;
        using C = juce::dsp::Convolution;
        c.loadImpulseResponse (std::move (item->ir), item->sampleRate, item->stereo ? C::Stereo::yes : C::Stereo::no,
                               item->trim ? C::Trim::yes : C::Trim::no, item->normalise ? C::Normalise::yes : C::Normalise::no);
        const int key = item->key;
        retired.store (item);                        // the (now empty) buffer is freed by collect() on the other thread
        return key;
    }

private:
    struct Item
    {
        juce::AudioBuffer<float> ir;
        double sampleRate;
        int key;
        bool stereo, normalise, trim;
    };
    std::atomic<Item*> pending { nullptr }, retired { nullptr };
};

} // namespace wis
