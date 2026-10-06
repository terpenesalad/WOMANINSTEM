#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>

namespace wis::daw
{

/** Analogue-style drum synthesis (no samples): the sounds of 70s/80s rhythm boxes and home keyboards.
    Notes follow the General MIDI drum map (36 kick, 38 snare, 42 closed hat, 46 open hat ...). */
class DrumSynth
{
public:
    enum Kit { homeKeyboard, compuRhythm, eightOhEight, toyBox, numKits };
    static juce::StringArray kitNames() { return { "Home Keyboard '84", "Compu Rhythm", "Eight-Oh-Eight", "Toy Box Lo-Fi" }; }

    enum Voice
    {
        kick, snare, rim, clap, closedHat, pedalHat, openHat, crash, ride, lowTom, midTom, highTom,
        cowbell, clave, maracas, tambourine, highConga, lowConga, highBongo, lowBongo, numVoices
    };
    static juce::String voiceName (Voice v);
    /** GM note -> voice (-1 if the note isn't a drum we make). */
    static int voiceForNote (int note);
    static int noteForVoice (Voice v);

    void prepare (double sampleRate);
    void setKit (int k)            { kit = juce::jlimit (0, (int) numKits - 1, k); }
    int getKit() const             { return kit; }
    /** Global character: tune in semitones, decay multiplier, tone -1 (dark) .. +1 (bright). */
    void setShape (float tuneSemis, float decayScale, float tone) { tune = tuneSemis; decayMul = decayScale; brightness = tone; }

    void trigger (int note, float velocity);
    void trigger (Voice v, float velocity);
    void allOff()                  { for (auto& v : voices) v.active = false; }

    /** Adds `n` samples into the two channels. */
    void render (float* left, float* right, int n);

    /** Renders one hit to a buffer (used to make the default Drum Pads kit). */
    static juce::AudioBuffer<float> renderHit (Voice v, int kit, double sampleRate, float velocity = 0.9f);

private:
    struct Svf
    {
        float ic1 = 0, ic2 = 0, g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0;
        void set (float freq, float q, double sr)
        {
            g = std::tan (juce::MathConstants<float>::pi * juce::jlimit (20.0f, (float) sr * 0.45f, freq) / (float) sr);
            k = 1.0f / juce::jmax (0.05f, q);
            a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
        }
        void process (float x, float& lp, float& bp, float& hp)
        {
            const float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2;
            lp = v2; bp = v1; hp = x - k * v1 - v2;
        }
    };

    struct Active
    {
        bool active = false;
        Voice type = kick;
        float vel = 1.0f, t = 0.0f;   // seconds since trigger
        float phase[6] {};
        float pan = 0.0f;
        Svf f1, f2;
        float decay = 0.2f, pitch = 60.0f, gain = 1.0f;
    };

    float noise() { seed = seed * 1664525u + 1013904223u; return (float) (int) seed * (1.0f / 2147483648.0f); }
    float renderVoice (Active& a);
    void setupVoice (Active& a);

    std::array<Active, 32> voices;
    double sr = 48000.0;
    int kit = homeKeyboard;
    float tune = 0.0f, decayMul = 1.0f, brightness = 0.0f;
    juce::uint32 seed = 22222;
};

} // namespace wis::daw
