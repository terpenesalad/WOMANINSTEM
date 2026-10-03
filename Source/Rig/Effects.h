#pragma once

#include <juce_dsp/juce_dsp.h>

namespace wis
{

/** Noise gate with hysteresis and hold - opens fast, closes smoothly, doesn't chatter on decaying notes. */
class NoiseGate
{
public:
    void prepare (double sr)
    {
        sampleRate = sr;
        envAttack  = std::exp (-1.0f / (float) (0.0005 * sr));
        envRelease = std::exp (-1.0f / (float) (0.030 * sr));
        gainAttack = std::exp (-1.0f / (float) (0.0008 * sr));
        holdSamples = (int) (0.040 * sr);
        reset();
    }

    void reset() { env = 0.0f; gain = 0.0f; open = false; holdCounter = 0; }

    void setParameters (float thresholdDb, float releaseMs)
    {
        openThreshold  = juce::Decibels::decibelsToGain (thresholdDb);
        closeThreshold = juce::Decibels::decibelsToGain (thresholdDb - 6.0f);
        gainRelease = std::exp (-1.0f / (float) (juce::jmax (1.0f, releaseMs) * 0.001 * sampleRate));
    }

    void process (float* data, int n, const float* sidechain = nullptr) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const float a = std::abs (sidechain != nullptr ? sidechain[i] : data[i]);
            env = a > env ? envAttack * env + (1.0f - envAttack) * a
                          : envRelease * env + (1.0f - envRelease) * a;

            if (env > openThreshold)       { open = true; holdCounter = holdSamples; }
            else if (env < closeThreshold) { if (holdCounter > 0) --holdCounter; else open = false; }

            const float target = open ? 1.0f : 0.0f;
            const float coeff = target > gain ? gainAttack : gainRelease;
            gain = coeff * gain + (1.0f - coeff) * target;
            data[i] *= gain;
        }
    }

    float getGainReduction() const noexcept { return gain; }

private:
    double sampleRate = 48000.0;
    float env = 0, gain = 0, envAttack = 0, envRelease = 0, gainAttack = 0, gainRelease = 0.99f;
    float openThreshold = 0.001f, closeThreshold = 0.0005f;
    bool open = false;
    int holdSamples = 0, holdCounter = 0;
};

/** Stereo delay: tape-style smoothed time changes, filtered feedback, optional ping-pong. */
class StereoDelay
{
public:
    void prepare (double sr, int maxBlock)
    {
        sampleRate = sr;
        const int maxDelay = (int) (sr * 2.1) + maxBlock + 4;
        for (auto& line : lines)
            line.assign ((size_t) maxDelay, 0.0f);
        size = maxDelay;
        writeIndex = 0;
        delaySamples.reset (sr, 0.25);
        delaySamples.setCurrentAndTargetValue ((float) (0.38 * sr));
        for (auto& f : fbFilter) { f.coefficients->coefficients.ensureStorageAllocated (8); f.reset(); }
        for (auto& f : fbHighPass) { f.coefficients->coefficients.ensureStorageAllocated (8); f.reset(); }
        setTone (4500.0f);
    }

    void reset()
    {
        for (auto& line : lines) std::fill (line.begin(), line.end(), 0.0f);
        for (auto& f : fbFilter) f.reset();
        for (auto& f : fbHighPass) f.reset();
    }

    void setParameters (float timeMs, float fb, float toneHz, float mixAmount, bool pingPongOn)
    {
        delaySamples.setTargetValue (juce::jlimit (1.0f, (float) (size - 4), timeMs * 0.001f * (float) sampleRate));
        feedback = juce::jlimit (0.0f, 0.95f, fb);
        mix = juce::jlimit (0.0f, 1.0f, mixAmount);
        pingPong = pingPongOn;
        if (toneHz != currentTone) setTone (toneHz);
    }

    /** Processes stereo in place (wet added to dry). */
    void process (float* left, float* right, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const float d = delaySamples.getNextValue();
            float readPos = (float) writeIndex - d;
            if (readPos < 0) readPos += (float) size;
            const int i0 = (int) readPos;
            const int i1 = (i0 + 1) % size;
            const float frac = readPos - (float) i0;

            const float wetL = lines[0][(size_t) i0] + frac * (lines[0][(size_t) i1] - lines[0][(size_t) i0]);
            const float wetR = lines[1][(size_t) i0] + frac * (lines[1][(size_t) i1] - lines[1][(size_t) i0]);

            const float inL = left[i], inR = right[i];
            float fbL = fbHighPass[0].processSample (fbFilter[0].processSample (wetL)) * feedback;
            float fbR = fbHighPass[1].processSample (fbFilter[1].processSample (wetR)) * feedback;

            if (pingPong)
            {
                const float mono = 0.5f * (inL + inR);
                lines[0][(size_t) writeIndex] = std::tanh (mono + fbR);   // L <- input + R feedback
                lines[1][(size_t) writeIndex] = std::tanh (fbL);          // R <- L feedback only
            }
            else
            {
                lines[0][(size_t) writeIndex] = std::tanh (inL + fbL);
                lines[1][(size_t) writeIndex] = std::tanh (inR + fbR);
            }

            left[i]  = inL + wetL * mix;
            right[i] = inR + wetR * mix;
            writeIndex = (writeIndex + 1) % size;
        }
    }

private:
    void setTone (float hz)
    {
        currentTone = hz;
        for (auto& f : fbFilter)
            *f.coefficients = juce::dsp::IIR::ArrayCoefficients<float>::makeLowPass (sampleRate, juce::jlimit (500.0f, (float) (sampleRate * 0.45), hz), 0.6f);
        for (auto& f : fbHighPass)
            *f.coefficients = juce::dsp::IIR::ArrayCoefficients<float>::makeHighPass (sampleRate, 120.0f, 0.6f);
    }

    double sampleRate = 48000.0;
    std::array<std::vector<float>, 2> lines;
    int size = 1, writeIndex = 0;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> delaySamples;
    float feedback = 0.3f, mix = 0.2f, currentTone = -1.0f;
    bool pingPong = false;
    std::array<juce::dsp::IIR::Filter<float>, 2> fbFilter, fbHighPass;
};

/** Simple pre-delay line used in front of the reverb. */
class PreDelay
{
public:
    void prepare (double sr)
    {
        sampleRate = sr;
        size = (int) (sr * 0.26) + 2;
        for (auto& l : lines) l.assign ((size_t) size, 0.0f);
        pos = 0;
    }
    void setDelayMs (float ms) { delay = juce::jlimit (0, size - 1, (int) (ms * 0.001 * sampleRate)); }
    void process (float* l, float* r, int n) noexcept
    {
        if (delay == 0) return;
        for (int i = 0; i < n; ++i)
        {
            const int rd = (pos - delay + size) % size;
            const float ol = lines[0][(size_t) rd], orr = lines[1][(size_t) rd];
            lines[0][(size_t) pos] = l[i];
            lines[1][(size_t) pos] = r[i];
            l[i] = ol; r[i] = orr;
            pos = (pos + 1) % size;
        }
    }
    void reset() { for (auto& l : lines) std::fill (l.begin(), l.end(), 0.0f); }

private:
    double sampleRate = 48000.0;
    std::array<std::vector<float>, 2> lines;
    int size = 1, pos = 0, delay = 0;
};

} // namespace wis
