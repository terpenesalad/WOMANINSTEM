#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <vector>

namespace wis
{

/** Chromatic tuner (YIN pitch detection). The audio thread pushes samples with push();
    the UI calls analyse() from a timer. Works from low B on a 5-string bass (31 Hz) up to ~1.4 kHz. */
class Tuner
{
public:
    struct Reading
    {
        bool valid = false;
        float frequency = 0.0f;
        int midiNote = 0;
        float cents = 0.0f;           // -50..+50
        juce::String noteName;        // e.g. "E", "F#"
        int octave = 0;
        float level = 0.0f;           // input RMS
    };

    void prepare (double sampleRate);

    /** Audio thread: lock-free, never blocks. */
    void push (const float* data, int numSamples) noexcept;

    /** UI thread. */
    Reading analyse();

    void setReferenceA4 (float hz) { referenceA4 = hz; }

private:
    float yin (const float* x, int n, double sr);

    double sampleRate = 48000.0;
    int decimation = 2;
    float referenceA4 = 440.0f;

    static constexpr int ringSize = 16384;
    std::vector<float> ring = std::vector<float> (ringSize, 0.0f);
    std::atomic<int> writePos { 0 };
    float decimAcc = 0.0f;
    int decimCount = 0;

    std::vector<float> frame, diff;
    float smoothedFreq = 0.0f;
    int stableCount = 0;
};

} // namespace wis
