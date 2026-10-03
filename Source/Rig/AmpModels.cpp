#include "AmpModels.h"

namespace wis
{

using AC = juce::dsp::IIR::ArrayCoefficients<float>;

/** Assigns coefficients without heap allocation (safe on the audio thread once storage is reserved). */
template <size_t N>
static void assign (juce::dsp::IIR::Filter<float>& f, const std::array<float, N>& c)
{
    *f.coefficients = c;
}

static void reserve (std::initializer_list<juce::dsp::IIR::Filter<float>*> filters)
{
    for (auto* f : filters)
    {
        f->coefficients->coefficients.ensureStorageAllocated (8);
        f->reset();
    }
}

juce::StringArray ampTypeNames()
{
    return { "Clean Combo", "Brit Crunch", "Hot Lead", "Bass Tube", "Bass Modern", "Flat / DI", "NAM Capture" };
}

bool ampTypeIsBass (AmpType t)
{
    return t == AmpType::bassTube || t == AmpType::bassModern;
}

// ---- helpers -----------------------------------------------------------------------------------

/** Triode-ish stage: soft, asymmetric (bias shifts the curve so it clips harder on one side). */
static inline float triode (float x, float bias) noexcept
{
    return std::tanh (x + bias) - std::tanh (bias);
}

static inline float knobToDb (float knob010, float rangeDb) noexcept
{
    return (knob010 - 5.0f) / 5.0f * rangeDb;
}

// ---- BuiltInAmp ----------------------------------------------------------------------------------

BuiltInAmp::Voicing BuiltInAmp::voicingFor (AmpType t)
{
    //                stages  minDb maxDb  inHP   interLP  bright bias  bassHz midHz  midQ  trebHz  scoop  pDrive sag   trim
    switch (t)
    {
        case AmpType::cleanCombo: return { 1, -6.0f, 22.0f,  60.0f, 9000.0f, 3.0f, 0.10f, 110.0f, 600.0f, 0.6f, 3200.0f, -2.5f, 1.2f, 0.15f,  -0.8f,   0.0f };
        case AmpType::britCrunch: return { 2,  0.0f, 40.0f,  90.0f, 7000.0f, 2.0f, 0.25f, 120.0f, 750.0f, 0.7f, 3000.0f,  1.0f, 1.8f, 0.30f,  -4.2f,   0.0f };
        case AmpType::hotLead:    return { 3, 10.0f, 62.0f, 140.0f, 5500.0f, 1.0f, 0.35f, 100.0f, 650.0f, 0.8f, 2800.0f, -4.0f, 1.5f, 0.20f,  -4.2f,   0.0f };
        case AmpType::bassTube:   return { 2, -6.0f, 34.0f,  30.0f, 6000.0f, 0.0f, 0.20f,  60.0f, 500.0f, 0.7f, 2500.0f,  0.0f, 1.4f, 0.35f,  -5.2f,   0.0f };
        case AmpType::bassModern: return { 2,  0.0f, 38.0f, 250.0f, 8000.0f, 3.0f, 0.15f,  50.0f, 800.0f, 0.9f, 3000.0f, -3.0f, 1.0f, 0.10f,  -2.0f, 220.0f };
        case AmpType::flatDi:
        case AmpType::namCapture:
        case AmpType::count:
        default:                  return { 0,  0.0f,  0.0f,  20.0f, 20000.0f, 0.0f, 0.0f, 100.0f, 700.0f, 0.7f, 3000.0f,  0.0f, 0.0f, 0.00f,   4.7f,   0.0f };
    }
}

void BuiltInAmp::prepare (double sr, int maxBlock)
{
    sampleRate = sr;
    maxBlockSize = maxBlock;
    oversampling.initProcessing ((size_t) maxBlock);
    preGain.reset (sr, 0.03);
    masterGain.reset (sr, 0.03);
    sagCoeff = std::exp (-1.0f / (float) (0.08 * sr));   // 80 ms sag recovery
    cleanLow.assign ((size_t) maxBlock + 16, 0.0f);
    reserve ({ &cleanLowLp, &cleanLowLp2 });
    reserve ({ &inputHp, &brightShelf, &bassF, &midF, &trebleF, &voiceMid, &presenceF, &outputLp, &dcBlock,
               &couplingHp[0], &couplingHp[1], &couplingHp[2], &stageLp[0], &stageLp[1], &stageLp[2] });
    filtersDirty = true;
    updateFilters();
    reset();
}

void BuiltInAmp::reset()
{
    oversampling.reset();
    for (auto* f : { &inputHp, &brightShelf, &bassF, &midF, &trebleF, &voiceMid, &presenceF, &outputLp, &dcBlock, &cleanLowLp, &cleanLowLp2 })
        f->reset();
    for (auto& f : couplingHp) f.reset();
    for (auto& f : stageLp) f.reset();
    sagEnv = 0.0f;
}

void BuiltInAmp::setParameters (AmpType t, float gain, float bass, float mid, float treble, float presence, float master)
{
    if (t != type || bass != bassKnob || mid != midKnob || treble != trebleKnob || presence != presenceKnob)
        filtersDirty = true;

    if (t != type)
        v = voicingFor (t);

    type = t; gainKnob = gain; bassKnob = bass; midKnob = mid; trebleKnob = treble; presenceKnob = presence; masterKnob = master;

    // Gain knob is roughly log-tapered like a real pot.
    const float g01 = std::pow (juce::jlimit (0.0f, 10.0f, gain) / 10.0f, 1.2f);
    preGain.setTargetValue (juce::Decibels::decibelsToGain (v.minGainDb + (v.maxGainDb - v.minGainDb) * g01));

    // Master: 7 = unity, 10 = +12 dB, 2 = -20 dB, 0 = off. Automatic trim so more gain doesn't just mean "louder".
    const float masterDb = master <= 0.01f ? -100.0f : (master - 7.0f) * 4.0f;
    const float autoTrim = -(v.maxGainDb - v.minGainDb) * g01 * (v.stages > 0 ? 0.18f : 0.0f);
    masterGain.setTargetValue (juce::Decibels::decibelsToGain (masterDb + v.outputTrimDb + autoTrim));
}

void BuiltInAmp::updateFilters()
{
    if (! filtersDirty) return;
    filtersDirty = false;

    const double osRate = sampleRate * 4.0;

    assign (inputHp, AC::makeHighPass (osRate, v.inputHpHz, 0.707f));
    assign (brightShelf, AC::makeHighShelf (osRate, 2500.0, 0.6f, juce::Decibels::decibelsToGain (v.brightDb)));

    for (int i = 0; i < 3; ++i)
    {
        // coupling caps: progressively tighter lows in later stages (prevents flubby high gain)
        assign (couplingHp[(size_t) i], AC::makeFirstOrderHighPass (osRate, v.inputHpHz * (0.4f + 0.5f * (float) i) + 10.0f));
        assign (stageLp[(size_t) i], AC::makeLowPass (osRate, juce::jmin (20000.0f, v.interStageLpHz * (1.0f + 0.25f * (float) (2 - i))), 0.6f));
    }

    const bool bassAmp = ampTypeIsBass (type);

    assign (bassF, AC::makeLowShelf (sampleRate, v.bassHz,   0.7f, juce::Decibels::decibelsToGain (knobToDb (bassKnob, bassAmp ? 15.0f : 12.0f))));
    assign (midF, AC::makePeakFilter (sampleRate, v.midHz,    v.midQ, juce::Decibels::decibelsToGain (knobToDb (midKnob, 10.0f))));
    assign (trebleF, AC::makeHighShelf (sampleRate, v.trebleHz, 0.7f, juce::Decibels::decibelsToGain (knobToDb (trebleKnob, 12.0f))));
    assign (voiceMid, AC::makePeakFilter (sampleRate, v.midHz * 0.8f, 0.5f, juce::Decibels::decibelsToGain (v.midScoopDb)));
    assign (presenceF, AC::makeHighShelf (sampleRate, 4500.0, 0.7f, juce::Decibels::decibelsToGain (knobToDb (presenceKnob, 8.0f))));
    assign (outputLp, AC::makeLowPass (sampleRate, juce::jmin (sampleRate * 0.45, 16000.0), 0.707f));
    assign (dcBlock, AC::makeFirstOrderHighPass (sampleRate, bassAmp ? 18.0f : 30.0f));
    if (v.cleanBlendHz > 0.0f)
    {
        assign (cleanLowLp,  AC::makeLowPass (sampleRate, v.cleanBlendHz, 0.54f));
        assign (cleanLowLp2, AC::makeLowPass (sampleRate, v.cleanBlendHz, 1.31f));
    }
}

void BuiltInAmp::processToneOnly (float* data, int numSamples)
{
    updateFilters();
    for (int i = 0; i < numSamples; ++i)
    {
        float x = data[i];
        x = bassF.processSample (x);
        x = midF.processSample (x);
        x = trebleF.processSample (x);
        x = presenceF.processSample (x);
        data[i] = x * masterGain.getNextValue() * juce::Decibels::decibelsToGain (-v.outputTrimDb);
    }
}

void BuiltInAmp::process (float* data, int numSamples)
{
    updateFilters();

    if (v.stages == 0)   // flat DI: tone controls only
    {
        for (int i = 0; i < numSamples; ++i)
        {
            float x = data[i] * preGain.getNextValue();
            x = bassF.processSample (x);
            x = midF.processSample (x);
            x = trebleF.processSample (x);
            x = presenceF.processSample (x);
            data[i] = x * masterGain.getNextValue();
        }
        return;
    }

    // Parallel clean low end (modern bass rigs blend a clean sub path under the drive).
    const bool blendLows = v.cleanBlendHz > 0.0f;
    if (blendLows)
        for (int i = 0; i < numSamples; ++i)
            cleanLow[(size_t) i] = cleanLowLp2.processSample (cleanLowLp.processSample (data[i]));

    // ---- preamp (4x oversampled) ----
    // The total preamp gain is split evenly (in dB) across the stages, like a real cascaded preamp:
    // stage gain is applied before each triode stage.
    float* channels[] = { data };
    juce::dsp::AudioBlock<float> block (channels, 1, (size_t) numSamples);

    const int stages = v.stages;
    const float startStageGain = lastStageGain;
    for (int i = 0; i < numSamples; ++i)
    {
        const float total = preGain.getNextValue();
        lastStageGain = std::pow (total, 1.0f / (float) stages);
        data[i] *= lastStageGain;                      // stage 1 gain at base rate (smoothed)
    }

    auto up = oversampling.processSamplesUp (block);
    auto* os = up.getChannelPointer (0);
    const int osN = (int) up.getNumSamples();
    const float gainStep = (lastStageGain - startStageGain) / (float) juce::jmax (1, osN);
    float stageGain = startStageGain;

    for (int i = 0; i < osN; ++i)
    {
        stageGain += gainStep;
        float s = inputHp.processSample (os[i]);
        s = brightShelf.processSample (s);

        for (int st = 0; st < stages; ++st)
        {
            if (st > 0)
                s *= stageGain * 1.6f;                 // later stages also get the inter-stage gain
            s = triode (s, v.bias * (st % 2 == 0 ? 1.0f : -1.0f));   // alternating polarity like real stages
            s = couplingHp[(size_t) st].processSample (s);
            s = stageLp[(size_t) st].processSample (s);
        }
        os[i] = s;
    }

    oversampling.processSamplesDown (block);

    // ---- tone stack, power amp, presence (base rate) ----
    for (int i = 0; i < numSamples; ++i)
    {
        float x = data[i];
        if (blendLows)
            x += cleanLow[(size_t) i] * 2.5f;
        x = voiceMid.processSample (x);
        x = bassF.processSample (x);
        x = midF.processSample (x);
        x = trebleF.processSample (x);

        // power amp: supply sag lowers gain on loud transients, then soft saturation
        const float a = std::abs (x);
        sagEnv = a > sagEnv ? a : sagEnv * sagCoeff + a * (1.0f - sagCoeff);
        const float sagGain = 1.0f / (1.0f + v.sag * sagEnv);
        x = std::tanh (x * v.powerDrive * sagGain) / v.powerDrive;

        x = presenceF.processSample (x);
        x = outputLp.processSample (x);
        x = dcBlock.processSample (x);
        data[i] = x * masterGain.getNextValue() * 2.0f;
    }
}

// ---- DrivePedal ----------------------------------------------------------------------------------

juce::StringArray driveTypeNames()
{
    return { "Overdrive", "Distortion", "Fuzz" };
}

void DrivePedal::prepare (double sr, int maxBlock)
{
    sampleRate = sr;
    oversampling.initProcessing ((size_t) maxBlock);
    driveGain.reset (sr, 0.03);
    outGain.reset (sr, 0.03);
    cleanCopy.assign ((size_t) maxBlock * 2 + 16, 0.0f);
    reserve ({ &preHp, &clipHp, &toneLp, &toneHp, &postHp, &fuzzMidHp, &fuzzMidLp });
    dirty = true;
    updateFilters();
    reset();
}

void DrivePedal::reset()
{
    oversampling.reset();
    for (auto* f : { &preHp, &clipHp, &toneLp, &toneHp, &postHp, &fuzzMidHp, &fuzzMidLp })
        f->reset();
}

void DrivePedal::setParameters (DriveType t, float d, float tn, float lv)
{
    if (t != type || tn != tone)
        dirty = true;
    type = t; drive = d; tone = tn; level = lv;

    const float d01 = juce::jlimit (0.0f, 10.0f, d) / 10.0f;
    float gainDb = 0.0f, trimDb = 0.0f;
    switch (t)
    {
        case DriveType::overdrive:  gainDb = 6.0f + 36.0f * d01;  trimDb = -2.0f;  break;
        case DriveType::distortion: gainDb = 10.0f + 50.0f * d01; trimDb = -12.0f; break;
        case DriveType::fuzz:       gainDb = 20.0f + 40.0f * d01; trimDb = -14.0f; break;
        default: break;
    }
    driveGain.setTargetValue (juce::Decibels::decibelsToGain (gainDb));
    const float levelDb = lv <= 0.01f ? -100.0f : juce::jmap (lv, 0.0f, 10.0f, -24.0f, 12.0f);
    outGain.setTargetValue (juce::Decibels::decibelsToGain (levelDb + trimDb));
}

void DrivePedal::updateFilters()
{
    if (! dirty) return;
    dirty = false;

    const double os = sampleRate * 2.0;
    const float t01 = juce::jlimit (0.0f, 10.0f, tone) / 10.0f;

    switch (type)
    {
        case DriveType::overdrive:
            // classic "green" overdrive: clip only the mids/highs, lows pass clean -> tight
            assign (clipHp, AC::makeFirstOrderHighPass (os, 720.0));
            assign (preHp, AC::makeFirstOrderHighPass (os, 30.0));
            assign (toneLp, AC::makeLowPass (sampleRate, 700.0 * std::pow (12.0, t01), 0.5));
            break;
        case DriveType::distortion:
            assign (preHp, AC::makeFirstOrderHighPass (os, 80.0));
            assign (clipHp, AC::makeFirstOrderHighPass (os, 30.0));
            assign (toneLp, AC::makeLowPass (sampleRate, 500.0 * std::pow (24.0, t01), 0.707f));
            break;
        case DriveType::fuzz:
        default:
            assign (preHp, AC::makeFirstOrderHighPass (os, 50.0));
            assign (clipHp, AC::makeFirstOrderHighPass (os, 25.0));
            assign (fuzzMidLp, AC::makeFirstOrderLowPass (sampleRate, 600.0));
            assign (fuzzMidHp, AC::makeFirstOrderHighPass (sampleRate, 1100.0));
            assign (toneLp, AC::makeLowPass (sampleRate, 12000.0, 0.707));
            break;
    }
    assign (postHp, AC::makeFirstOrderHighPass (sampleRate, 25.0));
}

void DrivePedal::process (float* data, int numSamples)
{
    updateFilters();

    if ((int) cleanCopy.size() < numSamples)
        cleanCopy.resize ((size_t) numSamples);
    std::copy (data, data + numSamples, cleanCopy.begin());

    for (int i = 0; i < numSamples; ++i)
        data[i] *= driveGain.getNextValue();

    float* channels[] = { data };
    juce::dsp::AudioBlock<float> block (channels, 1, (size_t) numSamples);
    auto up = oversampling.processSamplesUp (block);
    auto* os = up.getChannelPointer (0);
    const int osN = (int) up.getNumSamples();

    switch (type)
    {
        case DriveType::overdrive:
            for (int i = 0; i < osN; ++i)
            {
                float x = preHp.processSample (os[i]);
                x = clipHp.processSample (x);
                os[i] = std::tanh (x) * 0.9f;            // symmetric soft diodes
            }
            break;
        case DriveType::distortion:
            for (int i = 0; i < osN; ++i)
            {
                float x = preHp.processSample (os[i]);
                x = clipHp.processSample (x);
                const float ax = std::abs (x);
                os[i] = x / std::pow (1.0f + std::pow (ax, 2.5f), 1.0f / 2.5f);   // harder knee
            }
            break;
        case DriveType::fuzz:
        default:
            for (int i = 0; i < osN; ++i)
            {
                float x = preHp.processSample (os[i]);
                x = std::tanh (x * 1.5f + 0.1f) - std::tanh (0.1f);
                x = clipHp.processSample (x * 6.0f);
                os[i] = std::tanh (x);
            }
            break;
    }

    oversampling.processSamplesDown (block);

    const float t01 = juce::jlimit (0.0f, 10.0f, tone) / 10.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        float x = data[i];
        if (type == DriveType::fuzz)
        {
            // big-muff style tone: blend a low-passed and a high-passed copy -> mid scoop
            const float lo = fuzzMidLp.processSample (x);
            const float hi = fuzzMidHp.processSample (x);
            x = lo * (1.0f - t01) + hi * t01;
            x = toneLp.processSample (x) * 1.6f;
        }
        else
        {
            x = toneLp.processSample (x);
        }

        if (type == DriveType::overdrive)
            x += cleanCopy[(size_t) i];                  // clean lows/body blended back in

        data[i] = postHp.processSample (x) * outGain.getNextValue();
    }
}

} // namespace wis
