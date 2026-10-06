#pragma once

#include "BuiltinProcessor.h"

namespace wis::daw
{

namespace fxdsp
{
    /** Zero-delay-feedback state variable filter (one channel). */
    struct Svf
    {
        float g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0, ic1 = 0, ic2 = 0;
        void set (double sr, float hz, float q)
        {
            g = (float) std::tan (juce::MathConstants<double>::pi * juce::jlimit (10.0, sr * 0.49, (double) hz) / sr);
            k = 1.0f / juce::jmax (0.05f, q);
            a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
        }
        void reset() { ic1 = ic2 = 0; }
        // returns lp, writes bp/hp
        inline float tick (float x, float& bp, float& hp)
        {
            const float v3 = x - ic2;
            const float v1 = a1 * ic1 + a2 * v3;
            const float v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2;
            bp = v1; hp = x - k * v1 - v2;
            return v2;
        }
    };

    /** Mono ring delay line with fractional (cubic) reads. */
    struct Line
    {
        std::vector<float> d; int w = 0, mask = 0;
        void init (int minSize) { int s = 1; while (s < minSize) s <<= 1; d.assign ((size_t) s, 0.0f); mask = s - 1; w = 0; }
        void clear() { std::fill (d.begin(), d.end(), 0.0f); }
        inline void push (float x) { d[(size_t) w] = x; w = (w + 1) & mask; }
        /** delay in samples behind the most recent push (>= 1). */
        inline float read (double delay) const
        {
            const double pos = (double) w - delay;
            const int i = (int) std::floor (pos);
            const float t = (float) (pos - i);
            const float y0 = d[(size_t) ((i - 1) & mask)], y1 = d[(size_t) (i & mask)], y2 = d[(size_t) ((i + 1) & mask)], y3 = d[(size_t) ((i + 2) & mask)];
            const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
            return ((c3 * t + c2) * t + c1) * t + y1;
        }
    };

    /** Classic two-tap rotating delay pitch shifter (smooth, low latency, a little "warbly" on big shifts). */
    struct PitchShift
    {
        Line line; double phase = 0, window = 2048;
        void init (double sr, double windowMs = 60.0) { window = sr * windowMs / 1000.0; line.init ((int) window * 2 + 8); phase = 0; }
        void clear() { line.clear(); }
        inline float process (float x, double ratio)
        {
            line.push (x);
            phase += (1.0 - ratio) / window;
            phase -= std::floor (phase);
            const double p2 = phase + 0.5 - std::floor (phase + 0.5);
            const float g1 = (float) std::sin (juce::MathConstants<double>::pi * phase);
            const float g2 = (float) std::sin (juce::MathConstants<double>::pi * p2);
            return line.read (2.0 + phase * window) * g1 * g1 + line.read (2.0 + p2 * window) * g2 * g2;
        }
    };

    struct Transport { double ppq = 0, bpm = 120, bpb = 4; bool playing = false; };
    Transport transport (juce::AudioProcessor& p);
    juce::StringArray syncNames();
    double syncBeats (int index);
    float lfoShape (int shape, double phase01, float randomHeld);
    juce::StringArray lfoShapeNames();
}

class Bitcrusher : public BuiltinProcessor
{
public:
    Bitcrusher();
    void prepareToPlay (double, int) override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    double getTailLengthSeconds() const override { return 0.0; }
private:
    float held[2] {}; double counter = 0; juce::Random rng;
};

class AutoFilter : public BuiltinProcessor
{
public:
    AutoFilter();
    void prepareToPlay (double sr, int) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::StringArray getProgramNames() override;
    void loadProgram (int) override;
    double getTailLengthSeconds() const override { return 0.2; }
private:
    double sampleRate = 48000, phase = 0; float env = 0, randHeld = 0, smooth = -1;
    fxdsp::Svf f[2][2]; juce::Random rng;
};

class Flanger : public BuiltinProcessor
{
public:
    Flanger();
    void prepareToPlay (double sr, int) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    double getTailLengthSeconds() const override { return 0.2; }
private:
    double sampleRate = 48000, phase = 0; fxdsp::Line line[2]; float fb[2] {};
};

class RingMod : public BuiltinProcessor
{
public:
    RingMod();
    void prepareToPlay (double sr, int) override { sampleRate = sr; }
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    double getTailLengthSeconds() const override { return 0.0; }
private:
    double sampleRate = 48000, carrier = 0, lfo = 0;
};

class GrainCloud : public BuiltinProcessor
{
public:
    GrainCloud();
    void prepareToPlay (double sr, int) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::StringArray getProgramNames() override;
    void loadProgram (int) override;
    double getTailLengthSeconds() const override { return 3.0; }
private:
    struct Grain { bool on = false; double pos = 0, rate = 1; int age = 0, length = 1; float panL = 1, panR = 1, gain = 1; };
    std::array<Grain, 64> grains;
    juce::AudioBuffer<float> ring; int writePos = 0;
    double sampleRate = 48000, untilNext = 0;
    float lastWet[2] {};
    juce::Random rng;
};

class BeatRepeat : public BuiltinProcessor
{
public:
    BeatRepeat();
    void prepareToPlay (double sr, int) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::StringArray getProgramNames() override;
    void loadProgram (int) override;
    double getTailLengthSeconds() const override { return 0.0; }
private:
    void start (int sliceSamples, int totalSamples);
    juce::AudioBuffer<float> ring; int writePos = 0;
    double sampleRate = 48000;
    bool repeating = false, manualWas = false;
    int startPos = 0, slice = 1, elapsed = 0, total = 0;
    double readPhase = 0; int repeatIndex = 0;
    juce::int64 lastInterval = -1;
    float mixSmooth = 0;
    juce::Random rng;
};

class TapeWarble : public BuiltinProcessor
{
public:
    TapeWarble();
    void prepareToPlay (double sr, int) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::StringArray getProgramNames() override;
    void loadProgram (int) override;
    double getTailLengthSeconds() const override { return 0.05; }
private:
    double sampleRate = 48000, wowPhase = 0, flutPhase = 0;
    float drift = 0, driftTarget = 0, lp[2] {}, lp2[2] {}, hissLp = 0, dropout = 1, dropTarget = 1;
    int driftCounter = 0;
    fxdsp::Line line[2];
    juce::Random rng;
};

class PitchShifter : public BuiltinProcessor
{
public:
    PitchShifter();
    void prepareToPlay (double sr, int) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    double getTailLengthSeconds() const override { return 0.1; }
private:
    fxdsp::PitchShift a[2], b[2]; double sampleRate = 48000;
};

class ShimmerVerb : public BuiltinProcessor
{
public:
    ShimmerVerb();
    void prepareToPlay (double sr, int block) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    double getTailLengthSeconds() const override { return 12.0; }
private:
    juce::Reverb reverb;
    fxdsp::PitchShift shift[2];
    juce::AudioBuffer<float> wet;
    std::vector<float> fbRing[2]; int fbRead = 0, fbWrite = 0;
    fxdsp::Svf tone[2];
    double sampleRate = 48000;
};

class StereoWidth : public BuiltinProcessor
{
public:
    StereoWidth();
    void prepareToPlay (double sr, int) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    double getTailLengthSeconds() const override { return 0.0; }
private:
    double sampleRate = 48000; fxdsp::Line haas; fxdsp::Svf bass;
};

class DeEsser : public BuiltinProcessor
{
public:
    DeEsser();
    void prepareToPlay (double sr, int) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    double getTailLengthSeconds() const override { return 0.0; }
    std::atomic<float> reductionDb { 0.0f };
private:
    double sampleRate = 48000; fxdsp::Svf hp[2]; float env = 0, gain = 1;
};

/** Vocoder: a voice shapes a synth. Either the track's own audio is the voice and a built-in buzz is the
    carrier ("robot voice"), or the track (a synth) is the carrier and a side-chained vocal is the voice. */
class Vocoder : public BuiltinProcessor
{
public:
    Vocoder();
    void prepareToPlay (double sr, int) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool wantsSidechain() const override { return true; }
    double getTailLengthSeconds() const override { return 0.2; }
    static constexpr int numBands = 20;
private:
    double sampleRate = 48000, phases[8] {};
    fxdsp::Svf mod[numBands][2], car[numBands][2], sib;
    float env[numBands] {}, envC[numBands] {};
    juce::Random rng;
    int bandsBuilt = -1;
    void buildBands (float q, float shift);
};

} // namespace wis::daw
