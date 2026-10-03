#include "StemPlayer.h"

#if defined(_MSC_VER)
 #pragma warning (push, 0)
#endif
#include <signalsmith-stretch/signalsmith-stretch.h>
#if defined(_MSC_VER)
 #pragma warning (pop)
#endif

namespace wis
{

// ---- PlayableSong -----------------------------------------------------------------------------------

void PlayableSong::buildOverviews()
{
    const int buckets = overviewBuckets;
    mixOverview.assign ((size_t) buckets, 0.0f);

    if (length <= 0)
        return;

    const double perBucket = (double) length / (double) buckets;

    for (int s = 0; s < numStemIds; ++s)
    {
        auto& ov = overview[(size_t) s];
        ov.assign ((size_t) buckets, 0.0f);
        if (! present[(size_t) s] || stems[(size_t) s].getNumSamples() == 0)
            continue;

        auto* l = stems[(size_t) s].getReadPointer (0);
        auto* r = stems[(size_t) s].getReadPointer (1);
        const int len = juce::jmin (length, stems[(size_t) s].getNumSamples());

        for (int b = 0; b < buckets; ++b)
        {
            const int a = (int) (b * perBucket);
            const int e = juce::jmin (len, (int) ((b + 1) * perBucket));
            float pk = 0.0f;
            for (int i = a; i < e; i += 2)
                pk = juce::jmax (pk, std::abs (l[i]), std::abs (r[i]));
            ov[(size_t) b] = pk;
        }
    }

    // mix overview: true peak of the summed stems
    std::vector<float> sum ((size_t) 4096);
    for (int b = 0; b < buckets; ++b)
    {
        const int a = (int) (b * perBucket);
        const int e = juce::jmin (length, (int) ((b + 1) * perBucket));
        float pk = 0.0f;
        for (int i = a; i < e; i += 2)
        {
            float l = 0.0f, r = 0.0f;
            for (int s = 0; s < numStemIds; ++s)
            {
                auto& buf = stems[(size_t) s];
                if (present[(size_t) s] && i < buf.getNumSamples())
                {
                    l += buf.getReadPointer (0)[i];
                    r += buf.getReadPointer (1)[i];
                }
            }
            pk = juce::jmax (pk, std::abs (l), std::abs (r));
        }
        mixOverview[(size_t) b] = pk;
    }
}

// ---- StemPlayer ------------------------------------------------------------------------------------

StemPlayer::StemPlayer() = default;
StemPlayer::~StemPlayer() = default;

void StemPlayer::prepare (double sampleRate, int maxBlock)
{
    deviceRate = sampleRate;
    maxBlockSize = juce::jmax (16, maxBlock);

    stretcher = std::make_unique<Stretcher>();
    stretcher->presetDefault (2, (float) sampleRate, true);   // split computation = smoother CPU load
    stretchActive = false;
    inputAccumulator = 0.0;

    const int maxIn = (int) (maxBlockSize * 1.6f) + 8;
    stretchInL.assign ((size_t) maxIn, 0.0f);
    stretchInR.assign ((size_t) maxIn, 0.0f);
    tmpL.assign ((size_t) maxBlockSize, 0.0f);
    tmpR.assign ((size_t) maxBlockSize, 0.0f);
    fadeStep = 1.0f / (float) juce::jmax (64, (int) (sampleRate * 0.006));
}

void StemPlayer::setSong (PlayableSong::Ptr newSong)
{
    PlayableSong::Ptr old;
    {
        const juce::SpinLock::ScopedLockType sl (songLock);
        old = song;
        song = newSong;
    }
    if (old != nullptr)
        graveyard.add (old);   // freed later on the message thread, never on the audio thread

    playing = false;
    position = 0.0;
    seekRequest = 0.0;
    loopEnabled = false;
}

PlayableSong::Ptr StemPlayer::getSong() const
{
    const juce::SpinLock::ScopedLockType sl (songLock);
    return song;
}

void StemPlayer::releaseOldSongs()
{
    for (int i = graveyard.size(); --i >= 0;)
        if (graveyard.getObjectPointerUnchecked (i)->getReferenceCount() == 1)
            graveyard.remove (i);
}

void StemPlayer::seekSeconds (double seconds)
{
    const double rate = deviceRate.load();
    seekRequest = juce::jmax (0.0, seconds * rate);
    position = juce::jmax (0.0, seconds * rate);   // UI sees the new position immediately
}

double StemPlayer::getPositionSeconds() const   { return position.load() / juce::jmax (1.0, deviceRate.load()); }

double StemPlayer::getLengthSeconds() const
{
    auto s = getSong();
    return s != nullptr && s->sampleRate > 0 ? (double) s->length / s->sampleRate : 0.0;
}

void StemPlayer::setLoop (bool enabled, double startSeconds, double endSeconds)
{
    if (endSeconds < startSeconds) std::swap (startSeconds, endSeconds);
    loopStart = startSeconds;
    loopEnd = endSeconds;
    loopEnabled = enabled && (endSeconds - startSeconds) > 0.05;
}

double StemPlayer::getLoopStartSeconds() const { return loopStart.load(); }
double StemPlayer::getLoopEndSeconds() const   { return loopEnd.load(); }

void StemPlayer::computeGains (std::array<float, numStemIds>& gl, std::array<float, numStemIds>& gr) const noexcept
{
    bool anySolo = false;
    for (auto& c : controls) anySolo = anySolo || c.solo.load();

    const float master = juce::Decibels::decibelsToGain (masterGainDb.load(), -60.0f);

    for (int i = 0; i < numStemIds; ++i)
    {
        auto& c = controls[(size_t) i];
        const bool audible = ! c.mute.load() && (! anySolo || c.solo.load());
        const float g = audible ? juce::Decibels::decibelsToGain (c.gainDb.load(), -60.0f) * master : 0.0f;
        const float bal = juce::jlimit (-1.0f, 1.0f, c.balance.load());
        gl[(size_t) i] = g * (bal > 0.0f ? 1.0f - bal : 1.0f);
        gr[(size_t) i] = g * (bal < 0.0f ? 1.0f + bal : 1.0f);
    }
}

int StemPlayer::mixInto (const PlayableSong& s, double& posD, float* L, float* R, int numSamples, bool& reachedEnd,
                         const std::array<float, numStemIds>& gl0, const std::array<float, numStemIds>& gr0,
                         const std::array<float, numStemIds>& gl1, const std::array<float, numStemIds>& gr1,
                         int rampOffset, int rampTotal) noexcept
{
    const double rate = s.sampleRate;
    const bool loopOn = loopEnabled.load();
    const int ls = juce::jlimit (0, s.length, (int) (loopStart.load() * rate));
    const int le = juce::jlimit (0, s.length, (int) (loopEnd.load() * rate));
    const bool loopValid = loopOn && le - ls > (int) (0.05 * rate);

    int pos = (int) posD;
    int written = 0;
    int guard = 0;

    while (written < numSamples && guard++ < 64)
    {
        if (loopValid && pos == le)
        {
            pos = ls;          // reached the loop end: jump back
            fadeIn = 0.0f;     // de-click the jump
        }

        // only loop if the playhead is inside the loop (seeking past it plays on, like a DAW)
        const int segEnd = loopValid && pos < le ? le : s.length;

        if (pos >= segEnd)
        {
            reachedEnd = true;
            break;
        }

        const int count = juce::jmin (numSamples - written, segEnd - pos);

        for (int st = 0; st < numStemIds; ++st)
        {
            if (! s.present[(size_t) st])
                continue;

            auto& buf = s.stems[(size_t) st];
            if (pos + count > buf.getNumSamples())
                continue;

            auto* srcL = buf.getReadPointer (0, pos);
            auto* srcR = buf.getReadPointer (1, pos);

            float pk = 0.0f;
            const float a0 = gl0[(size_t) st], b0 = gr0[(size_t) st];
            const float da = (gl1[(size_t) st] - a0) / (float) juce::jmax (1, rampTotal);
            const float db = (gr1[(size_t) st] - b0) / (float) juce::jmax (1, rampTotal);

            for (int i = 0; i < count; ++i)
            {
                const int k = rampOffset + written + i;
                const float gL = a0 + da * (float) k;
                const float gR = b0 + db * (float) k;
                const float l = srcL[i], r = srcR[i];
                L[written + i] += l * gL;
                R[written + i] += r * gR;
                pk = juce::jmax (pk, std::abs (l), std::abs (r));
            }

            auto& meter = controls[(size_t) st].meter;
            if (pk > meter.load (std::memory_order_relaxed))
                meter.store (pk, std::memory_order_relaxed);
        }

        written += count;
        pos += count;
    }

    posD = (double) pos;
    return written;
}

void StemPlayer::process (float* L, float* R, int n) noexcept
{
    if (n > maxBlockSize)   // some drivers deliver bigger blocks than announced
    {
        for (int off = 0; off < n; off += maxBlockSize)
            process (L + off, R + off, juce::jmin (maxBlockSize, n - off));
        return;
    }

    PlayableSong::Ptr s;
    {
        const juce::SpinLock::ScopedTryLockType sl (songLock);
        if (! sl.isLocked())
            return;
        s = song;
    }

    if (s == nullptr || s->length == 0 || std::abs (s->sampleRate - deviceRate.load()) > 1.0 || stretcher == nullptr)
        return;

    double pos = position.load();

    if (auto req = seekRequest.exchange (-1.0); req >= 0.0)
    {
        pos = juce::jlimit (0.0, (double) s->length, req);
        fadeIn = 0.0f;
        if (stretchActive)
        {
            stretcher->reset();
            inputAccumulator = 0.0;
        }
    }

    if (! playing.load())
    {
        position = pos;
        return;
    }

    if (pos >= s->length && ! loopEnabled.load())
        pos = 0.0;   // pressing play at the end restarts the song

    std::array<float, numStemIds> gl1 {}, gr1 {};
    computeGains (gl1, gr1);

    const float sp = speed.load();
    const float tr = transpose.load();
    const bool wantStretch = std::abs (sp - 1.0f) > 0.001f || std::abs (tr) > 0.01f;

    if (wantStretch != stretchActive)
    {
        stretchActive = wantStretch;
        fadeIn = 0.0f;
        if (stretchActive)
        {
            stretcher->reset();
            stretcher->setTransposeSemitones (tr);
            lastTranspose = tr;
            inputAccumulator = 0.0;
        }
    }

    bool reachedEnd = false;
    const int firstOut = 0;

    if (! stretchActive)
    {
        // direct path: mix into temp so we can fade, then add to output
        std::fill (tmpL.begin(), tmpL.begin() + juce::jmin (n, (int) tmpL.size()), 0.0f);
        std::fill (tmpR.begin(), tmpR.begin() + juce::jmin (n, (int) tmpR.size()), 0.0f);
        const int nn = juce::jmin (n, (int) tmpL.size());
        mixInto (*s, pos, tmpL.data(), tmpR.data(), nn, reachedEnd, lastGainL, lastGainR, gl1, gr1, 0, nn);
    }
    else
    {
        if (tr != lastTranspose)
        {
            stretcher->setTransposeSemitones (tr);
            lastTranspose = tr;
        }

        const int nn = juce::jmin (n, (int) tmpL.size());
        inputAccumulator += (double) nn * sp;
        int inN = (int) inputAccumulator;
        inputAccumulator -= inN;
        inN = juce::jmin (inN, (int) stretchInL.size());

        std::fill (stretchInL.begin(), stretchInL.begin() + inN, 0.0f);
        std::fill (stretchInR.begin(), stretchInR.begin() + inN, 0.0f);
        mixInto (*s, pos, stretchInL.data(), stretchInR.data(), inN, reachedEnd, lastGainL, lastGainR, gl1, gr1, 0, juce::jmax (1, inN));

        float* ins[]  = { stretchInL.data(), stretchInR.data() };
        float* outs[] = { tmpL.data(), tmpR.data() };
        stretcher->process (ins, inN, outs, nn);
    }

    // add to the output with de-click fade
    const int nn = juce::jmin (n, (int) tmpL.size());
    for (int i = firstOut; i < nn; ++i)
    {
        float g = 1.0f;
        if (fadeIn < 1.0f)
        {
            fadeIn = juce::jmin (1.0f, fadeIn + fadeStep);
            g = fadeIn * fadeIn;
        }
        L[i] += tmpL[(size_t) i] * g;
        R[i] += tmpR[(size_t) i] * g;
    }

    lastGainL = gl1;
    lastGainR = gr1;

    if (reachedEnd)
        playing = false;

    position = pos;
}

} // namespace wis
