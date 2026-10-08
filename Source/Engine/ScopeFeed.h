#pragma once

#include <atomic>
#include <cstdint>
#include <vector>
#include <algorithm>

namespace wis
{

/** A lock-free tap from the audio thread to the oscilloscope.

    The audio engines (Play Along and Studio) ask `wants (tap)` once per block and, if the scope is watching
    that signal, push two channels of it. The scope window pulls whatever arrived since its last frame.
    One writer (the audio thread), one reader (the message thread). Costs nothing while no scope is open. */
class ScopeFeed
{
public:
    /** Which signal the scope is watching. Play Along taps 1-4, Studio taps 5-7. */
    enum Tap : int
    {
        off = 0,
        instrument,      // Play Along: your instrument through the rig (stereo)
        song,            // Play Along: the stems you hear (stereo)
        everything,      // Play Along: the final mix (stereo)
        duet,            // Play Along: a = you (mono), b = the song (mono)
        studioMaster,    // Studio: master bus (stereo)
        studioTrack,     // Studio: the selected track, after its effects (stereo)
        studioDuet       // Studio: a = selected track (mono), b = master (mono)
    };

    static constexpr int capacity = 1 << 16;   // frames (~1.4 s at 48 kHz)

    std::atomic<int> tap { off };

    bool wants (Tap t) const noexcept { return tap.load (std::memory_order_relaxed) == (int) t; }

    // ---- audio thread ------------------------------------------------------------------------------
    /** Pushes two channels (b may equal a). */
    void push (const float* a, const float* b, int n, double sampleRate) noexcept
    {
        if (a == nullptr || n <= 0) return;
        if (b == nullptr) b = a;
        rate.store (sampleRate, std::memory_order_relaxed);
        const auto w = writePos.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
        {
            const auto k = (size_t) ((w + (uint64_t) i) & (capacity - 1)) * 2;
            ring[k]     = sanitise (a[i]);
            ring[k + 1] = sanitise (b[i]);
        }
        writePos.store (w + (uint64_t) n, std::memory_order_release);
    }

    /** Pushes two stereo signals as a mono pair: a = (aL + aR) / 2, b = (bL + bR) / 2. */
    void pushPair (const float* aL, const float* aR, const float* bL, const float* bR, int n, double sampleRate) noexcept
    {
        if (n <= 0) return;
        rate.store (sampleRate, std::memory_order_relaxed);
        const auto w = writePos.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
        {
            const float a = 0.5f * ((aL != nullptr ? aL[i] : 0.0f) + (aR != nullptr ? aR[i] : (aL != nullptr ? aL[i] : 0.0f)));
            const float b = 0.5f * ((bL != nullptr ? bL[i] : 0.0f) + (bR != nullptr ? bR[i] : (bL != nullptr ? bL[i] : 0.0f)));
            const auto k = (size_t) ((w + (uint64_t) i) & (capacity - 1)) * 2;
            ring[k]     = sanitise (a);
            ring[k + 1] = sanitise (b);
        }
        writePos.store (w + (uint64_t) n, std::memory_order_release);
    }

    // ---- message thread ----------------------------------------------------------------------------
    /** Copies the frames written since `cursor` (at most maxFrames, newest kept) into a / b. Returns the count. */
    int read (uint64_t& cursor, std::vector<float>& a, std::vector<float>& b, int maxFrames) const
    {
        const auto w = writePos.load (std::memory_order_acquire);
        if (w < cursor) cursor = w;                     // feed was reset
        auto pending = w - cursor;
        const auto limit = (uint64_t) std::min (maxFrames, capacity / 2);
        if (pending > limit) { cursor = w - limit; pending = limit; }
        const int n = (int) pending;
        a.resize ((size_t) n);
        b.resize ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const auto k = (size_t) ((cursor + (uint64_t) i) & (capacity - 1)) * 2;
            a[(size_t) i] = ring[k];
            b[(size_t) i] = ring[k + 1];
        }
        cursor = w;
        return n;
    }

    uint64_t getWritePosition() const noexcept { return writePos.load (std::memory_order_acquire); }
    double getSampleRate() const noexcept      { const double r = rate.load (std::memory_order_relaxed); return r > 1000.0 ? r : 48000.0; }

private:
    static float sanitise (float x) noexcept
    {
        // NaN fails both comparisons and becomes 0; Inf is clamped
        return x > -4.0f && x < 4.0f ? x : (x >= 4.0f ? 4.0f : (x <= -4.0f ? -4.0f : 0.0f));
    }

    std::vector<float> ring = std::vector<float> ((size_t) capacity * 2, 0.0f);
    std::atomic<uint64_t> writePos { 0 };
    std::atomic<double> rate { 48000.0 };
};

} // namespace wis
