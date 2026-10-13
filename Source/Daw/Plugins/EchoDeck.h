#pragma once

#include "BuiltinProcessor.h"

namespace wis::daw
{

/** "Echo Deck": an analog-style echo processor.

    Every echo passes through a model of the machine it's set to (the "style"): tape heads with wow and flutter
    and soft saturation, a valve preamp, a bucket-brigade chip whose clock slows (and darkens) as the echo gets
    longer, an oil can, early 12-bit digital... Each repeat goes round the loop again, so it gets darker, dirtier
    and wobblier the way a real unit does, and feedback above 100% runs away into a held, saturated wash
    instead of exploding.

    Time is set in milliseconds or as a note (straight, dotted, triplet) locked to the song's tempo or a tapped
    tempo. Modes: Single (true stereo), Dual (two echo times, left and right), Ping-Pong and Rhythm (an 8-step
    multi-tap pattern that repeats). Groove swings or pushes every other repeat, Feel places the echoes ahead of
    or behind the beat, Duck lowers the echoes while you play, and Hold freezes what's in the loop.

    The loop is two read heads in series (A then B): with Groove at zero they're equal and it's a plain echo;
    with Groove they alternate long / short, which is how a swing is made in a feedback loop. */
class EchoDeck : public BuiltinProcessor
{
public:
    EchoDeck();
    ~EchoDeck() override;

    void prepareToPlay (double sr, int block) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    double getTailLengthSeconds() const override { return 12.0; }
    juce::StringArray getProgramNames() override;
    void loadProgram (int) override;

    juce::AudioProcessorEditor* createCustomEditor() override { return editorFactory ? editorFactory (*this) : nullptr; }
    static std::function<juce::AudioProcessorEditor* (EchoDeck&)> editorFactory;

    enum Mode { single, dual, pingPong, rhythm };
    enum TimeMode { milliseconds, note, dotted, triplet };
    static constexpr int numTaps = 8;

    static juce::StringArray styleNames();
    static juce::StringArray styleBlurbs();
    static juce::StringArray noteNames();
    static double noteBeats (int noteIndex, int timeMode);

    /** Tap tempo (message thread). In ms mode a tap sets the time; otherwise it sets the tapped tempo. */
    void tap();
    /** The same with the tap's time given (ms on any steady clock): used by tap() and the tests. */
    void tapAt (double ms);

    // read by the editor
    float getInputLevel() const   { return inLevel.load(); }
    float getEchoLevel() const    { return echoLevel.load(); }
    float getEchoMs() const       { return echoMs.load(); }       // current main echo time (target)
    float getTempo() const        { return tempoNow.load(); }     // tempo the notes follow
    bool isHolding() const        { return param ("hold") > 0.5f; }
    /** 0..1 position of the tape travel (for the reels): advances at the tape speed. */
    double getTapeTravel() const  { return tapeTravel.load(); }

    /** The echo time in ms the current settings ask for, at a given tempo (pure; used by the tests too). */
    double targetMs (double bpm) const;

private:
    struct Svf
    {
        float g = 0.1f, k = 1.4f, a1 = 0, a2 = 0, a3 = 0, ic1 = 0, ic2 = 0;
        void set (double sr, float hz, float q);
        float lp (float x);
        float hp (float x);
        void reset() { ic1 = ic2 = 0; }
        void copyCoefs (const Svf& o) { g = o.g; k = o.k; a1 = o.a1; a2 = o.a2; a3 = o.a3; }
    };
    struct Allpass
    {
        std::vector<float> buf; int pos = 0, len = 1;
        void prepare (int samples) { buf.assign ((size_t) juce::jmax (1, samples), 0.0f); len = (int) buf.size(); pos = 0; }
        float process (float x, float g);
    };
    /** The non-filter part of what a pass round the loop does (recomputed every few samples). */
    struct Shape
    {
        int sat = 1;              // 0 clean, 1 tape, 2 valve, 3 bucket brigade (compander), 4 clip
        float drive = 1.0f;
        float holdInc = 1.0f;     // sample-and-hold rate / sample rate (>= 1: off)
        float diffusion = 0.0f;   // all-pass gain
        bool styleHpOn = true, userHpOn = true, styleLpOn = true, userLpOn = true;   // off at the ends of their range
        Svf styleHp, userHp, styleLp, userLp;   // coefficient templates
    };
    /** Everything one pass round the loop does to the signal (one per head per channel). */
    struct Pass
    {
        Svf dc, styleHp, userHp, styleLp, recon, userLp;
        Allpass ap1, ap2;
        float holdPhase = 1.0f, held = 0.0f;
        float run (float x, const Shape& s);
        void setFilters (const Shape& s);
        void reset();
    };
    struct Line
    {
        std::vector<float> a, b;
        Pass passA, passB;
        void prepare (int size, double sr);
        void clear();
    };

    void updateShape (double ms, double sr);
    float readHermite (const std::vector<float>& buf, double delaySamples) const;

    double sampleRate = 48000.0;
    Line lines[2];
    int bufMask = 0, writePos = 0;

    // smoothed times (samples)
    double tA = 0, tB = 0, tA2 = 0, tB2 = 0, feelOffset = 0, stepT = 0;
    bool firstBlock = true;
    juce::SmoothedValue<float> dryGain, wetGain, inGain, outGain, fbGain, holdMix, widthS;
    float duckEnv = 0.0f, duckGain = 1.0f, hissEnv = 0.0f;
    double wowPhase[2] { 0.0, 0.37 }, flutPhase[2] { 0.0, 0.61 };
    float drift[2] {}, driftTarget[2] {};
    int driftCount = 0;
    juce::Random rng { 0x5eed };
    std::array<float, numTaps> tapLevel {};

    std::atomic<float> inLevel { 0.0f }, echoLevel { 0.0f }, echoMs { 375.0f }, tempoNow { 120.0f };
    std::atomic<double> tapeTravel { 0.0 };
    std::vector<double> taps;   // message thread (tap tempo)

    Shape shape;
    Svf dcTemplate;
};

} // namespace wis::daw
