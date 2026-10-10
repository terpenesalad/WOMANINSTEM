#pragma once

#include <juce_dsp/juce_dsp.h>

namespace wis
{

/** Built-in amp models. NAM captures are handled separately by NamAmp.
    (The order is saved in presets and songs: only ever append. Rig state version 2 remaps the old list.) */
enum class AmpType : int
{
    // guitar
    americanClean = 0,   // blackface-style twin: huge headroom, scooped sparkle
    tweedBreakup,        // 50s tweed: simple tone circuit, breaks up early, loose and hairy
    britishChime,        // 60s British class-A combo: jangly upper mids, chimey clean to raunchy crunch
    britishCrunch,       // plexi-style: the classic rock crunch, cleans up with the guitar volume
    britishLead,         // hot-rodded British: tighter, more gain, singing leads
    modernHighGain,      // modern high gain: four stages, tight lows, scooped or mid-forward
    smoothOverdrive,     // boutique overdrive: fat, smooth, vocal leads
    // bass
    bassSixties,         // 60s British valve bass: warm, round, thumpy (the Merseybeat bass sound)
    bassClassicTube,     // classic all-tube 300 W head into an 8x10: the rock bass sound
    bassFlipTop,         // 60s flip-top 1x15 studio combo: deep, round, fat (Motown records)
    bassModernGrowl,     // modern pedal-style preamp: clean lows + gritty mids/highs, punchy
    bassStudioDi,        // studio DI / preamp: hi-fi, tight, a touch of tube warmth
    // utility
    flatDi,              // no amp colour: tone controls only
    namCapture,          // use the loaded .nam model
    // 3.7 (appended: saved presets keep their indices)
    bassPsychPop,        // psych-pop bass: a 60s hollow-body DI'd into a cooking valve preamp and squashed flat,
                         // mids pushed into a round, rubbery honk, top rolled off (Tame Impala-ish)
    count
};

juce::StringArray ampTypeNames();
bool ampTypeIsBass (AmpType t);
/** Maps an amp model index saved by WOMANINSTEM 1.x / 2.x / 3.0 to the current list. */
int migrateAmpTypeV1 (int oldIndex);

/** The classic passive treble/middle/bass tone stack (as in most tweed, blackface and British amps),
    solved analytically from its six components (Yeh & Smith) and discretised with the bilinear transform. */
class ToneStack
{
public:
    struct Components { double C1, C2, C3, R1, R2, R3, R4; };
    static Components tweed()      { return { 250e-12, 20e-9, 20e-9, 250e3, 1e6, 25e3, 56e3 }; }
    static Components blackface()  { return { 250e-12, 100e-9, 47e-9, 250e3, 250e3, 10e3, 100e3 }; }
    static Components british()    { return { 470e-12, 22e-9, 22e-9, 220e3, 1e6, 22e3, 33e3 }; }
    static Components chime()      { return { 100e-12, 47e-9, 22e-9, 1e6, 1e6, 10e3, 100e3 }; }
    static Components modern()     { return { 470e-12, 22e-9, 22e-9, 250e3, 1e6, 25e3, 47e3 }; }
    static Components bassman()    { return { 250e-12, 20e-9, 20e-9, 250e3, 1e6, 25e3, 56e3 }; }

    /** Pots 0..1 (bass is given a log taper here). */
    void design (double sampleRate, const Components& c, double treble01, double mid01, double bass01);
    void reset() { z1 = z2 = z3 = 0.0; }
    inline float process (float xIn) noexcept
    {
        const double x = xIn;
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y + z3;
        z3 = b3 * x - a3 * y;
        return (float) y;
    }
    /** |H| at a frequency (for tests / auto make-up gain). */
    double magnitudeAt (double hz, double sampleRate) const;

private:
    double b0 = 1, b1 = 0, b2 = 0, b3 = 0, a1 = 0, a2 = 0, a3 = 0;
    double z1 = 0, z2 = 0, z3 = 0;
};

/** Virtual-analogue amp: oversampled asymmetric triode stages with dynamic bias shift (bloom / blocking),
    the model's tone stack in its real position in the circuit, a sagging push-pull power amp and the
    speaker-load interaction of its negative feedback. Mono. */
class BuiltInAmp
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset();

    /** All knobs 0..10 like a real amp. */
    void setParameters (AmpType type, float gain, float bass, float mid, float treble, float presence, float master);

    void process (float* data, int numSamples);

    /** Tone controls applied after a NAM capture (so the knobs still do something useful). */
    void processToneOnly (float* data, int numSamples);
    float getLatencySamples() const { return v.stages > 0 ? oversampling.getLatencyInSamples() : 0.0f; }

    struct Model
    {
        int stages;                    // preamp triode stages
        float minGainDb, maxGainDb;    // total preamp gain range (gain knob 0..10)
        float inputHpHz;               // coupling / cathode bypass: how much lows reach the first stage
        float brightDb;                // bright cap: treble boost at low gain settings
        float bias;                    // stage asymmetry (even harmonics)
        float bloom;                   // dynamic bias shift: compression / "blocking" when pushed
        float stageLpHz;               // Miller capacitance between stages
        int stack;                     // 0 tweed, 1 blackface, 2 british, 3 chime, 4 modern, 5 active bass, 6 bassman, 7 none
        int stackAfter;                // tone stack after this many stages (stages = at the end of the preamp)
        float stackMakeupDb;
        float activeBassHz, activeMidHz, activeMidQ, activeTrebleHz, activeRangeDb;
        float powerDrive, sag, asym;   // power amp
        float nfb;                     // negative feedback 0..1 (less = more speaker interaction, looser)
        float resonanceHz;             // speaker / transformer low resonance the power amp follows
        float voiceLowHz, voiceLowDb, voiceMidHz, voiceMidQ, voiceMidDb, voiceHighHz, voiceHighDb;
        float outputTrimDb;            // loudness matching between models
        float cleanBlendHz, cleanBlendDb;   // > 0: clean low end blended under the drive (modern bass)
    };
    static Model modelFor (AmpType t);

private:
    void updateFilters();

    double sampleRate = 48000.0;
    int maxBlockSize = 512;

    AmpType type = AmpType::americanClean;
    float gainKnob = 5, bassKnob = 5, midKnob = 5, trebleKnob = 5, presenceKnob = 5, masterKnob = 5;
    Model v = modelFor (AmpType::americanClean);
    bool filtersDirty = true;

    static constexpr int osFactorLog2 = 2;   // 4x oversampling
    juce::dsp::Oversampling<float> oversampling { 1, osFactorLog2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false };

    // oversampled-rate processing
    juce::dsp::IIR::Filter<float> inputHp, brightShelf, voiceLow, voiceMid, voiceHigh, activeBass, activeMid, activeTreble;
    std::array<juce::dsp::IIR::Filter<float>, 4> couplingHp, stageLp;
    std::array<float, 4> stageEnv {};
    ToneStack stack;
    float envAtk = 0, envRel = 0;

    // base-rate processing
    juce::dsp::IIR::Filter<float> resonance, nfbHigh, presenceF, outputLp, dcBlock, cleanLowLp, cleanLowLp2, toneOnlyBass, toneOnlyMid, toneOnlyTreble;
    std::vector<float> cleanLow;

    juce::SmoothedValue<float> preGain, masterGain;
    float sagEnv = 0.0f, sagCoeff = 0.0f;
    float lastStageGain = 1.0f;
};

/** Stomp box drive in front of the amp. (Order saved in presets: only append.) */
enum class DriveType : int { overdrive = 0, distortion, fuzz, bassDrive, count };
juce::StringArray driveTypeNames();

class DrivePedal
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset();
    void setParameters (DriveType t, float drive010, float tone010, float level010);
    void process (float* data, int numSamples);
    float getLatencySamples() const { return oversampling.getLatencyInSamples(); }

private:
    void updateFilters();

    double sampleRate = 48000.0;
    DriveType type = DriveType::overdrive;
    float drive = 5, tone = 5, level = 5;
    bool dirty = true;

    juce::dsp::Oversampling<float> oversampling { 1, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false };
    juce::dsp::IIR::Filter<float> preHp, clipHp, toneLp, toneHp, postHp, fuzzMidHp, fuzzMidLp, blendLp, blendLp2;
    juce::SmoothedValue<float> driveGain, outGain;
    std::vector<float> cleanCopy;
};

/** "Strings & Pickups": shapes the instrument before the amp, so a modern bass can sound like a 60s
    hollow-body with flatwounds (or the other way round) without stacking EQs. */
enum class CharacterType : int
{
    off = 0,
    flatwounds,          // flatwound strings: thumpy attack, highs fade fast
    violinBass,          // 60s hollow-body short-scale bass with flats: woody, round, short sustain
    foamMute,            // foam mute under the strings: dead thud, very short notes (Motown / 60s pop)
    roundwoundsBright,   // fresh roundwounds + active electronics: piano-like clarity, zing
    precisionPickup,     // split-coil bass pickup: growly mids, punchy
    singleCoils,         // vintage single coils: glassy, scooped
    humbuckers,          // humbuckers: thick mids, smoother top
    count
};
juce::StringArray characterTypeNames();

class InstrumentCharacter
{
public:
    void prepare (double sampleRate);
    void reset();
    void setParameters (CharacterType t, float amount010);
    void process (float* data, int numSamples);

private:
    double sampleRate = 48000.0;
    CharacterType type = CharacterType::off;
    float amount = 0.0f;
    bool dirty = true;
    int used = 0;
    juce::dsp::IIR::Filter<float> eq1, eq2, eq3, eq4;
    // dynamic low-pass (strings that lose their highs after the pluck)
    float lpState1 = 0, lpState2 = 0, envFast = 0, envSlow = 0, brightEnv = 0, sustainGain = 1.0f, sinceAttack = 0.0f;
    float fastA = 0, fastR = 0, slowA = 0, slowR = 0;
};

/** 60s studio console + tape machine: transformer warmth, tape compression, head bump, soft top end. */
class ConsoleTape
{
public:
    void prepare (double sampleRate);
    void reset();
    void setDrive (float drive010) { drive = drive010; }
    void process (float* data, int numSamples);
private:
    double sampleRate = 48000.0;
    float drive = 5.0f;
    juce::dsp::IIR::Filter<float> preEmph, deEmph, headBump, topRoll, lowCut;
};

} // namespace wis
