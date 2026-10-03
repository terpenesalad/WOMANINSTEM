#pragma once

#include <juce_dsp/juce_dsp.h>

namespace wis
{

/** Built-in amp voicings. NAM captures are handled separately by NamAmp. */
enum class AmpType : int
{
    cleanCombo = 0,   // sparkly American clean, lots of headroom
    britCrunch,       // plexi-style crunch, cleans up with your guitar volume
    hotLead,          // three cascaded gain stages for modern high gain
    bassTube,         // big tube bass head: warm, round, grinds when pushed
    bassModern,       // tight modern bass: clean lows, gritty top
    flatDi,           // no amp colour: for acoustic, keys, or when using an IR only
    namCapture,       // use the loaded .nam model
    count
};

juce::StringArray ampTypeNames();
bool ampTypeIsBass (AmpType t);

/** Virtual-analogue amp: oversampled cascaded triode stages, tone stack, sagging power amp. Mono. */
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

private:
    struct Voicing
    {
        int stages;
        float minGainDb, maxGainDb;   // total preamp gain range
        float inputHpHz;              // tightens the lows before clipping
        float interStageLpHz;         // tames fizz between stages
        float brightDb;               // treble boost before clipping (bright cap)
        float bias;                   // stage asymmetry (even harmonics)
        float bassHz, midHz, midQ, trebleHz;
        float midScoopDb;             // fixed mid voicing at noon
        float powerDrive, sag;
        float outputTrimDb;           // loudness matching between models
        float cleanBlendHz;           // >0: blend a clean low-passed signal under the drive (modern bass)
    };

    static Voicing voicingFor (AmpType t);
    void updateFilters();

    double sampleRate = 48000.0;
    int maxBlockSize = 512;

    AmpType type = AmpType::cleanCombo;
    float gainKnob = 5, bassKnob = 5, midKnob = 5, trebleKnob = 5, presenceKnob = 5, masterKnob = 5;
    Voicing v = voicingFor (AmpType::cleanCombo);
    bool filtersDirty = true;

    juce::dsp::Oversampling<float> oversampling { 1, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false };

    // oversampled-rate filters
    juce::dsp::IIR::Filter<float> inputHp, brightShelf;
    std::array<juce::dsp::IIR::Filter<float>, 3> couplingHp, stageLp;

    // base-rate filters
    juce::dsp::IIR::Filter<float> bassF, midF, trebleF, voiceMid, presenceF, outputLp, dcBlock, cleanLowLp, cleanLowLp2;
    std::vector<float> cleanLow;

    juce::SmoothedValue<float> preGain, masterGain;
    float sagEnv = 0.0f;
    float sagCoeff = 0.0f;
    float lastStageGain = 1.0f;
};

/** Stomp box drive in front of the amp. */
enum class DriveType : int { overdrive = 0, distortion, fuzz, count };
juce::StringArray driveTypeNames();

class DrivePedal
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset();
    void setParameters (DriveType t, float drive010, float tone010, float level010);
    void process (float* data, int numSamples);

private:
    void updateFilters();

    double sampleRate = 48000.0;
    DriveType type = DriveType::overdrive;
    float drive = 5, tone = 5, level = 5;
    bool dirty = true;

    juce::dsp::Oversampling<float> oversampling { 1, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false };
    juce::dsp::IIR::Filter<float> preHp, clipHp, toneLp, toneHp, postHp, fuzzMidHp, fuzzMidLp;
    juce::SmoothedValue<float> driveGain, outGain;
    std::vector<float> cleanCopy;
};

} // namespace wis
