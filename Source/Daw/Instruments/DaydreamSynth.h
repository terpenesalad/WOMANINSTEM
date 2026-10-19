#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>

namespace wis::daw::daydream
{

/** Everything a voice needs from the parameters, read once per block. */
struct SoundParams
{
    int engine = 0;                 // 0 analogue, 1 FM, 2 reed piano
    // analogue
    int waveA = 0, waveB = 0;       // saw, square, pulse, triangle, sine
    float semiB = 0, fineB = 7, mixB = 0.5f, sub = 0, noise = 0, pw = 0.5f, detune = 10;
    int unison = 1;
    // FM
    int algorithm = 0;
    float ratio[4] { 1, 1, 2, 3 }, level[4] { 1, 0.5f, 0.2f, 0 }, opAttack[4] {}, opDecay[4] { 1, 0.6f, 0.4f, 0.3f }, opSustain[4] { 1, 0.4f, 0.2f, 0 };
    float feedback = 0, fmVelocity = 0.5f;
    // reed piano
    float bark = 0.45f, reedTone = 0.55f, reedDecay = 1.0f, mechanics = 0.4f;
    // shared
    int filterType = 0;             // LP 24, LP 12, band pass, high pass
    float cutoff = 6000, reso = 0.1f, envAmount = 0.3f, keyTrack = 0.5f, velCutoff = 0.3f, drive = 0;
    float attack = 0.005f, decay = 0.6f, sustain = 0.7f, release = 0.4f;
    float fAttack = 0.005f, fDecay = 0.5f, fSustain = 0.3f, fRelease = 0.4f;
    float lfoRate = 5, lfoDelay = 0.3f, vibrato = 0, lfoFilter = 0, lfoPwm = 0, lfoAmp = 0, drift = 3;
    float velSens = 0.6f, width = 0.5f, glide = 0;
    float bend = 0;                 // pitch bend, semitones
    double sampleRate = 48000.0;
};

juce::StringArray engineNames();
juce::StringArray waveNames();
juce::StringArray filterNames();
juce::StringArray algorithmNames();

/** One voice of the chord synth (any of the three engines). */
class Voice
{
public:
    void start (int owner, int note, float pitch, float velocity, const SoundParams&, juce::uint32 order, float pan);
    void release (const SoundParams&);
    void kill() { active = false; }
    void render (const SoundParams&, float* left, float* right, int n);

    bool active = false, released = false, sustained = false;
    int owner = -1, note = -1;
    juce::uint32 order = 0;          // start order (oldest gets stolen)
    float level() const { return amp.value; }

private:
    struct Env
    {
        enum Stage { idle, attackS, decayS, sustainS, releaseS } stage = idle;
        float value = 0, rateA = 0, coefD = 0, coefR = 0, target = 0;
        void set (float a, float d, float s, float r, double sr);
        void start() { stage = attackS; }
        void off()   { if (stage != idle) stage = releaseS; }
        float next();
    };
    struct Svf
    {
        float ic1 = 0, ic2 = 0;
        float lp (float x, float g, float k)
        {
            const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
            const float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2;
            lastBp = v1; lastHp = x - k * v1 - v2;
            return v2;
        }
        float lastBp = 0, lastHp = 0;
    };

    float pitch = 60, vel = 1, pan = 0;
    Env amp, filt, op[4];
    double phase[2][4] {};           // analogue: osc A/B x unison
    double subPhase = 0;
    double fmPhase[4] {};
    float fmPrev[2] {};
    float lfoPhase = 0, lfoAge = 0, driftCt = 0, driftVel = 0;
    Svf f1, f2;
    float g = 0.1f, k = 1.4f;        // filter coefficients (control rate)
    int ctl = 0;
    float freq = 261.6f, pwNow = 0.5f, vibMul = 1.0f, ampLfo = 1.0f;
    juce::uint32 seed = 1;
    // reed piano
    struct Mode { float y1 = 0, y2 = 0, c = 0, r = 0, rDamped = 0, gain = 0; };
    Mode modes[3];
    int hammer = 0, hammerLen = 1;
    float thump = 0, thumpLp = 0, chuff = 0, chuffBp1 = 0, chuffBp2 = 0, toneLp = 0, dc = 0, dcIn = 0;
    float pickup = 0.3f;
    float noise() { seed = seed * 1664525u + 1013904223u; return (float) (int) seed * (1.0f / 2147483648.0f); }
    float renderAnalogue (const SoundParams&);
    float renderFm (const SoundParams&);
    float renderReed (const SoundParams&);
    void control (const SoundParams&);
};

/** The separate bass synth: a few voices with glide, one of eight bass sounds. */
juce::StringArray bassSoundNames();
class BassSynth
{
public:
    void prepare (double sampleRate);
    void noteOn (int owner, int note, float velocity, int sound, float glideMs);
    void noteOff (int owner, int note);
    void allOff (int owner = -1);
    void render (float* left, float* right, int n, int sound, float tone, float gain);
    int soundingNote() const;   // for the display (-1 = none)

private:
    struct BVoice
    {
        bool active = false, released = false;
        int owner = -1, note = -1;
        float pitch = 36, target = 36, glide = 0, vel = 1, env = 0, fenv = 0, age = 0;
        double ph[3] {};
        float ic1 = 0, ic2 = 0, ic3 = 0, ic4 = 0;
        juce::uint32 order = 0;
    };
    std::array<BVoice, 4> v;
    double sr = 48000.0;
    juce::uint32 counter = 0;
};

/** The effects: drive, tremolo, phaser, flanger, chorus, ensemble, delay and reverb, in that order. */
struct FxParams
{
    float drive = 0, driveTone = 0.6f;
    float tremolo = 0, tremRate = 5; bool tremSync = false, tremPan = false;
    float phaser = 0, phaserRate = 0.4f, phaserFeedback = 0.5f;
    float flanger = 0, flangerRate = 0.2f, flangerFeedback = 0.5f;
    float chorus = 0, chorusRate = 0.6f, chorusDepth = 0.5f;
    float ensemble = 0;
    float delay = 0, delayFeedback = 0.35f, delayTone = 0.6f; int delayDivision = 3; bool pingPong = true;
    float reverb = 0, reverbSize = 0.6f, reverbDamp = 0.4f;
};
juce::StringArray fxNames();          // the eight effects, for the FX focus
juce::StringArray delayDivisionNames();
double delayDivisionBeats (int division);

class Effects
{
public:
    void prepare (double sampleRate, int blockSize);
    void reset();
    void process (float* left, float* right, int n, const FxParams&, double bpm);

private:
    double sr = 48000.0;
    // drive
    float dLp[2] {};
    // tremolo
    double tremPhase = 0;
    // phaser
    float apState[2][6] {}, phaserFb[2] {};
    double phaserPhase = 0;
    // modulated delays (flanger, chorus, ensemble)
    std::vector<float> mod[2];
    int modPos = 0;
    double flPhase = 0, chPhase = 0, enPhase = 0, enFast = 0;
    std::vector<float> fl[2];
    int flPos = 0;
    float flOut[2] {};
    float phaserCoef[2] { -0.9f, -0.9f };
    int ctl = 0;
    float readMod (int ch, float delaySamples) const;
    // delay
    std::vector<float> echo[2];
    int echoPos = 0;
    float echoLp[2] {}, echoTime = 0;
    juce::Reverb reverb;
    juce::Reverb::Parameters lastRev;
    std::vector<float> wetL, wetR;
};

} // namespace wis::daw::daydream
