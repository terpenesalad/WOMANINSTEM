#include "TempoDetect.h"
#include <juce_dsp/juce_dsp.h>

namespace wis::daw
{

static constexpr double phaseOffsetFraction = 0.5;

TempoEstimate estimateTempo (const juce::AudioBuffer<float>& audio, double sr)
{
    TempoEstimate est;
    const int n = audio.getNumSamples();
    if (n < (int) (sr * 6.0) || sr <= 0) return est;

    // ---- onset strength: positive spectral flux, 100 frames per second ----
    constexpr int order = 10, fftSize = 1 << order;
    const int hop = (int) std::round (sr / 100.0);
    const double frameRate = sr / hop;
    juce::dsp::FFT fft (order);
    std::vector<float> window (fftSize), buf (2 * fftSize), prevMag (fftSize / 2 + 1, 0.0f);
    for (int i = 0; i < fftSize; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * i / fftSize);

    // analyse at most 3 minutes
    const int frames = juce::jmin ((n - fftSize) / hop, (int) (180.0 * frameRate));
    std::vector<float> onset ((size_t) juce::jmax (0, frames), 0.0f), lowOnset (onset.size(), 0.0f);
    const int lowBins = juce::jmax (2, (int) std::round (250.0 * fftSize / sr));   // kick / bass / snare body
    const int chs = audio.getNumChannels();
    for (int f = 0; f < frames; ++f)
    {
        const int start = f * hop;
        for (int i = 0; i < fftSize; ++i)
        {
            float s = 0;
            for (int c = 0; c < chs; ++c) s += audio.getSample (c, start + i);
            buf[(size_t) i] = s * window[(size_t) i];
        }
        std::fill (buf.begin() + fftSize, buf.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (buf.data(), true);
        float flux = 0, low = 0;
        for (int k = 1; k <= fftSize / 2; ++k)
        {
            const float m = std::log1p (100.0f * buf[(size_t) k]);
            const float d = juce::jmax (0.0f, m - prevMag[(size_t) k]);
            flux += d;
            if (k <= lowBins) low += d;
            prevMag[(size_t) k] = m;
        }
        onset[(size_t) f] = flux;
        lowOnset[(size_t) f] = low;
    }
    if (frames < 300) return est;

    // remove the slowly varying part (local mean) and half-wave rectify
    const int w = (int) (frameRate * 0.5);
    auto detrend = [&] (const std::vector<float>& in)
    {
        std::vector<float> out (in.size());
        double acc = 0;
        for (int i = 0; i < frames; ++i)
        {
            acc += in[(size_t) i];
            if (i >= w) acc -= in[(size_t) (i - w)];
            const float mean = (float) (acc / juce::jmin (i + 1, w));
            out[(size_t) i] = juce::jmax (0.0f, in[(size_t) i] - mean);
        }
        return out;
    };
    const auto o = detrend (onset);
    const auto lo = detrend (lowOnset);

    // the signal we look for periodicity in: mostly low end (kick / snare body land on beats; hi-hats often
    // tick twice per beat and would otherwise make us pick double time)
    double loMax = 1e-9, oMax = 1e-9;
    for (int i = 0; i < frames; ++i) { loMax = juce::jmax (loMax, (double) lo[(size_t) i]); oMax = juce::jmax (oMax, (double) o[(size_t) i]); }
    std::vector<float> beatSig ((size_t) frames);
    for (int i = 0; i < frames; ++i) beatSig[(size_t) i] = (float) (lo[(size_t) i] / loMax + 0.3 * o[(size_t) i] / oMax);

    // ---- autocorrelation over 60..200 BPM, weighted towards ~105 BPM ----
    double bestScore = -1, bestLag = 0, total = 0;
    const int minLag = (int) (frameRate * 60.0 / 200.0), maxLag = (int) (frameRate * 60.0 / 60.0);
    const int acLen = 4 * maxLag + 4;
    std::vector<double> ac ((size_t) acLen + 1, 0.0);
    for (int lag = minLag; lag <= juce::jmin (acLen, frames - 1); ++lag)
    {
        double s = 0;
        for (int i = 0; i + lag < frames; ++i) s += beatSig[(size_t) i] * beatSig[(size_t) (i + lag)];
        ac[(size_t) lag] = s / (frames - lag);   // unbiased, so long lags aren't penalised
    }
    auto acNear = [&] (int l)   // tolerate the multiple not landing on a whole frame
    {
        l = juce::jlimit (1, acLen - 1, l);
        return juce::jmax (ac[(size_t) l - 1], ac[(size_t) l], ac[(size_t) l + 1]);
    };
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        const double bpm = 60.0 * frameRate / lag;
        const double pref = std::exp (-0.5 * std::pow (std::log2 (bpm / 105.0) / 0.8, 2.0));
        // a real beat also repeats every 2 and 4 beats (half bar / bar); off-grid figures (dotted notes, triplets) don't
        const double s = (ac[(size_t) lag] + 0.5 * acNear (2 * lag) + 0.5 * acNear (4 * lag)) * pref;
        total += s;
        if (s > bestScore) { bestScore = s; bestLag = lag; }
    }
    if (bestLag <= 0) return est;

    // parabolic refinement of the lag
    double lag = bestLag;
    if (bestLag > minLag && bestLag < maxLag)
    {
        const double a = ac[(size_t) bestLag - 1], b = ac[(size_t) bestLag], c = ac[(size_t) bestLag + 1];
        const double d = a - 2 * b + c;
        if (std::abs (d) > 1e-12) lag = bestLag + 0.5 * (a - c) / d;
    }
    est.bpm = 60.0 * frameRate / lag;
    // round to 0.5 BPM when close (most music is on whole BPMs)
    if (std::abs (est.bpm - std::round (est.bpm)) < 0.35) est.bpm = std::round (est.bpm);

    // ---- phase: comb offset with the most onset energy, favouring the low end (kick / snare land on beats,
    //      hi-hats often sit between them) ----
    auto strength = [&] (double t)   // linearly interpolated
    {
        const int i = (int) t;
        const double fr = t - i;
        auto at = [&] (int k) { return k + 1 < frames ? (double) beatSig[(size_t) k] : 0.0; };
        return at (i) * (1.0 - fr) + at (i + 1) * fr;
    };
    const double beatLag = 60.0 * frameRate / est.bpm;
    double bestPhase = 0, bestSum = -1;
    for (double ph = 0; ph < beatLag; ph += 0.25)
    {
        double s = 0;
        for (double t = ph; t + 1 < frames; t += beatLag) s += strength (t);
        if (s > bestSum) { bestSum = s; bestPhase = ph; }
    }
    // ---- which of the 4 beats is the downbeat? (4/4 guess: the kick-heaviest one) ----
    {
        double bestBar = -1;
        int bestOffset = 0;
        for (int b = 0; b < 4; ++b)
        {
            double s = 0;
            for (double t = bestPhase + b * beatLag; t + 1 < frames; t += 4 * beatLag)
                s += lo[(size_t) t] / loMax;
            if (s > bestBar) { bestBar = s; bestOffset = b; }
        }
        bestPhase += bestOffset * beatLag;
    }

    // the flux peaks once the attack is well inside the analysis window
    est.firstBeatSeconds = juce::jmax (0.0, (bestPhase * hop + fftSize * phaseOffsetFraction) / sr);
    est.confidence = (float) juce::jlimit (0.0, 1.0, bestScore / (total / (maxLag - minLag + 1)) / 6.0);
    return est;
}

} // namespace wis::daw
