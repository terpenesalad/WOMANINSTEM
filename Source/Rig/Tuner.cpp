#include "Tuner.h"

namespace wis
{

void Tuner::prepare (double sr)
{
    sampleRate = sr;
    decimation = sr > 60000.0 ? 4 : 2;   // analyse at ~22-24 kHz: plenty for fundamentals, half the work
    std::fill (ring.begin(), ring.end(), 0.0f);
    writePos = 0;
    decimAcc = 0.0f;
    decimCount = 0;
}

void Tuner::push (const float* data, int numSamples) noexcept
{
    int w = writePos.load (std::memory_order_relaxed);
    for (int i = 0; i < numSamples; ++i)
    {
        decimAcc += data[i];
        if (++decimCount >= decimation)
        {
            ring[(size_t) w] = decimAcc / (float) decimation;   // box-car average = crude anti-alias
            w = (w + 1) & (ringSize - 1);
            decimAcc = 0.0f;
            decimCount = 0;
        }
    }
    writePos.store (w, std::memory_order_release);
}

float Tuner::yin (const float* x, int n, double sr)
{
    const int maxLag = juce::jmin (n / 2, (int) (sr / 30.0));
    const int minLag = juce::jmax (2, (int) (sr / 1400.0));
    const int w = n - maxLag;
    if (w <= minLag) return 0.0f;

    diff.assign ((size_t) maxLag + 1, 0.0f);

    // difference function
    for (int tau = 1; tau <= maxLag; ++tau)
    {
        float s = 0.0f;
        for (int j = 0; j < w; ++j)
        {
            const float d = x[j] - x[j + tau];
            s += d * d;
        }
        diff[(size_t) tau] = s;
    }

    // cumulative mean normalised difference
    diff[0] = 1.0f;
    float running = 0.0f;
    for (int tau = 1; tau <= maxLag; ++tau)
    {
        running += diff[(size_t) tau];
        diff[(size_t) tau] = running > 0.0f ? diff[(size_t) tau] * (float) tau / running : 1.0f;
    }

    // first dip below threshold
    const float threshold = 0.12f;
    int tauEst = -1;
    for (int tau = minLag; tau < maxLag; ++tau)
    {
        if (diff[(size_t) tau] < threshold)
        {
            while (tau + 1 < maxLag && diff[(size_t) tau + 1] < diff[(size_t) tau])
                ++tau;
            tauEst = tau;
            break;
        }
    }

    if (tauEst < 0)
    {
        // No clear dip below the threshold (common on bass with strong harmonics):
        // fall back to the global minimum if it's still reasonably periodic.
        float best = 1.0f;
        for (int tau = minLag; tau < maxLag; ++tau)
            best = juce::jmin (best, diff[(size_t) tau]);
        if (best > 0.35f)
            return 0.0f;

        // take the first (shortest-period) local minimum that is nearly as deep -> avoids octave errors
        for (int tau = minLag + 1; tau < maxLag - 1; ++tau)
        {
            const float d = diff[(size_t) tau];
            if (d <= best * 1.15f + 0.02f && d <= diff[(size_t) tau - 1] && d <= diff[(size_t) tau + 1])
            {
                tauEst = tau;
                break;
            }
        }
        if (tauEst < 0)
            return 0.0f;
    }

    // parabolic interpolation for sub-sample accuracy
    float better = (float) tauEst;
    if (tauEst > 0 && tauEst < maxLag)
    {
        const float s0 = diff[(size_t) tauEst - 1], s1 = diff[(size_t) tauEst], s2 = diff[(size_t) tauEst + 1];
        const float denom = 2.0f * (2.0f * s1 - s2 - s0);
        if (std::abs (denom) > 1.0e-9f)
            better = (float) tauEst + (s2 - s0) / denom;
    }

    return (float) (sr / better);
}

Tuner::Reading Tuner::analyse()
{
    Reading r;
    const double sr = sampleRate / decimation;
    const int n = juce::jmin (ringSize, (int) (sr * 0.12) + (int) (sr / 30.0));   // ~120 ms window + max period

    frame.resize ((size_t) n);
    const int end = writePos.load (std::memory_order_acquire);
    for (int i = 0; i < n; ++i)
        frame[(size_t) i] = ring[(size_t) ((end - n + i + ringSize) & (ringSize - 1))];

    float rms = 0.0f;
    for (auto v : frame) rms += v * v;
    rms = std::sqrt (rms / (float) n);
    r.level = rms;

    if (rms < 0.0015f)   // ~ -56 dBFS: too quiet to read
    {
        stableCount = 0;
        return r;
    }

    const float f = yin (frame.data(), n, sr);
    if (f <= 0.0f)
    {
        stableCount = 0;
        return r;
    }

    // smooth (but jump immediately on a new note)
    if (smoothedFreq <= 0.0f || std::abs (12.0f * std::log2 (f / smoothedFreq)) > 0.6f)
        smoothedFreq = f;
    else
        smoothedFreq += 0.35f * (f - smoothedFreq);

    const float midi = 69.0f + 12.0f * std::log2 (smoothedFreq / referenceA4);
    const int note = (int) std::lround (midi);
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    r.valid = true;
    r.frequency = smoothedFreq;
    r.midiNote = note;
    r.cents = (midi - (float) note) * 100.0f;
    r.noteName = names[((note % 12) + 12) % 12];
    r.octave = note / 12 - 1;
    return r;
}

} // namespace wis
