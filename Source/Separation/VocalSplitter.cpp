#include "VocalSplitter.h"
#include <juce_dsp/juce_dsp.h>

namespace wis
{

void VocalSplitter::split (const juce::AudioBuffer<float>& vocalsIn,
                           juce::AudioBuffer<float>& lead,
                           juce::AudioBuffer<float>& backing,
                           const std::function<bool()>& shouldCancel)
{
    // Copy first: callers may pass the same buffer as input and lead output.
    juce::AudioBuffer<float> vocals (vocalsIn);
    const int n = vocals.getNumSamples();

    backing.setSize (2, n);
    backing.clear();

    juce::AudioBuffer<float> leadOut (2, n);
    leadOut.clear();

    if (vocals.getNumChannels() < 2 || n < 8192)
    {
        lead = std::move (vocals);
        return;
    }

    constexpr int order = 12;               // 4096-point FFT (93 ms at 44.1k) - good frequency resolution for voices
    constexpr int fftSize = 1 << order;
    constexpr int hop = fftSize / 4;         // 75% overlap
    constexpr int bins = fftSize / 2 + 1;

    juce::dsp::FFT fft (order);

    // Periodic Hann used for analysis and synthesis; sum of squares at 75% overlap = 1.5
    std::vector<float> window (fftSize);
    for (int i = 0; i < fftSize; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) fftSize);
    const float olaNorm = 1.0f / 1.5f;

    std::vector<float> bufL (2 * fftSize), bufR (2 * fftSize);
    std::vector<float> mask (bins, 1.0f), smoothMask (bins, 1.0f), rawMask (bins, 1.0f);

    auto* inL = vocals.getReadPointer (0);
    auto* inR = vocals.getReadPointer (1);
    auto* outL = leadOut.getWritePointer (0);
    auto* outR = leadOut.getWritePointer (1);

    const int numFrames = (n + fftSize) / hop + 1;

    for (int f = 0; f < numFrames; ++f)
    {
        if ((f & 255) == 0 && shouldCancel && shouldCancel())
            return;

        const int start = f * hop - fftSize;   // start before 0 so the first samples get full overlap

        // windowed frame
        for (int i = 0; i < fftSize; ++i)
        {
            const int idx = start + i;
            const bool inside = idx >= 0 && idx < n;
            bufL[(size_t) i] = inside ? inL[idx] * window[(size_t) i] : 0.0f;
            bufR[(size_t) i] = inside ? inR[idx] * window[(size_t) i] : 0.0f;
        }
        std::fill (bufL.begin() + fftSize, bufL.end(), 0.0f);
        std::fill (bufR.begin() + fftSize, bufR.end(), 0.0f);

        fft.performRealOnlyForwardTransform (bufL.data(), true);
        fft.performRealOnlyForwardTransform (bufR.data(), true);

        // Per-bin "centredness": 2 Re(L R*) / (|L|^2 + |R|^2) is 1 when L == R, 0 for unrelated signals,
        // negative for out-of-phase content.
        for (int k = 0; k < bins; ++k)
        {
            const float lr = bufL[(size_t) (2 * k)], li = bufL[(size_t) (2 * k + 1)];
            const float rr = bufR[(size_t) (2 * k)], ri = bufR[(size_t) (2 * k + 1)];
            const float cross = lr * rr + li * ri;
            const float pow   = lr * lr + li * li + rr * rr + ri * ri;
            const float c = pow > 1.0e-12f ? 2.0f * cross / pow : 1.0f;
            // map similarity to a soft mask: >0.93 -> lead, <0.6 -> backing
            const float m = juce::jlimit (0.0f, 1.0f, (c - 0.6f) / 0.33f);
            rawMask[(size_t) k] = m * m * (3.0f - 2.0f * m);   // smoothstep
        }

        // smooth across frequency (reduces "musical noise")
        for (int k = 0; k < bins; ++k)
        {
            float acc = 0.0f, wsum = 0.0f;
            for (int d = -2; d <= 2; ++d)
            {
                const int kk = juce::jlimit (0, bins - 1, k + d);
                const float wgt = d == 0 ? 2.0f : 1.0f;
                acc += rawMask[(size_t) kk] * wgt;
                wsum += wgt;
            }
            mask[(size_t) k] = acc / wsum;
        }

        // smooth across time: quick to open (lead onsets), slower to close
        for (int k = 0; k < bins; ++k)
        {
            auto& s = smoothMask[(size_t) k];
            const float target = mask[(size_t) k];
            s += (target > s ? 0.6f : 0.35f) * (target - s);
        }

        // apply mask
        for (int k = 0; k < bins; ++k)
        {
            const float g = smoothMask[(size_t) k];
            bufL[(size_t) (2 * k)] *= g; bufL[(size_t) (2 * k + 1)] *= g;
            bufR[(size_t) (2 * k)] *= g; bufR[(size_t) (2 * k + 1)] *= g;
        }

        fft.performRealOnlyInverseTransform (bufL.data());
        fft.performRealOnlyInverseTransform (bufR.data());

        for (int i = 0; i < fftSize; ++i)
        {
            const int idx = start + i;
            if (idx >= 0 && idx < n)
            {
                const float w = window[(size_t) i] * olaNorm;
                outL[idx] += bufL[(size_t) i] * w;
                outR[idx] += bufR[(size_t) i] * w;
            }
        }
    }

    // backing = vocals - lead (exact complement)
    for (int ch = 0; ch < 2; ++ch)
    {
        backing.copyFrom (ch, 0, vocals, ch, 0, n);
        backing.addFrom (ch, 0, leadOut, ch, 0, n, -1.0f);
    }

    // If "backing" is just a faint ambience/reverb residue, keep the lead intact instead.
    const float leadRms    = juce::jmax (leadOut.getRMSLevel (0, 0, n), leadOut.getRMSLevel (1, 0, n));
    const float backingRms = juce::jmax (backing.getRMSLevel (0, 0, n), backing.getRMSLevel (1, 0, n));

    if (leadRms <= 0.0f || juce::Decibels::gainToDecibels (backingRms / juce::jmax (1.0e-9f, leadRms)) < -16.0f)
    {
        lead = std::move (vocals);
        backing.clear();
        return;
    }

    lead = std::move (leadOut);
}

} // namespace wis
