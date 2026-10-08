#include "AmpModels.h"
#include <complex>

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

static inline float dbToGain (float db) noexcept { return juce::Decibels::decibelsToGain (db); }
static inline float knobToDb (float knob010, float rangeDb) noexcept { return (knob010 - 5.0f) / 5.0f * rangeDb; }

juce::StringArray ampTypeNames()
{
    return { "American Clean", "Tweed Breakup", "British Chime", "British Crunch", "British Lead", "Modern High Gain", "Smooth Overdrive",
             "Bass: 60s British Valve", "Bass: Classic Tube 8x10", "Bass: Vintage Flip-Top", "Bass: Modern Growl", "Bass: Studio DI",
             "Flat / DI", "NAM Capture" };
}

bool ampTypeIsBass (AmpType t)
{
    return t >= AmpType::bassSixties && t <= AmpType::bassStudioDi;
}

int migrateAmpTypeV1 (int old)
{
    // 1.x - 3.0: Clean Combo, Brit Crunch, Hot Lead, Bass Tube, Bass Modern, Flat / DI, NAM Capture
    static const AmpType map[] = { AmpType::americanClean, AmpType::britishCrunch, AmpType::modernHighGain, AmpType::bassClassicTube,
                                   AmpType::bassModernGrowl, AmpType::flatDi, AmpType::namCapture };
    return (int) map[juce::jlimit (0, 6, old)];
}

// =====================================================================================================
//  Tone stack
// =====================================================================================================
void ToneStack::design (double fs, const Components& k, double t, double m, double bassPot)
{
    t = juce::jlimit (0.0, 1.0, t);
    m = juce::jlimit (0.0, 1.0, m);
    const double l = std::exp ((juce::jlimit (0.0, 1.0, bassPot) - 1.0) * 3.4);   // log-taper bass pot
    const double C1 = k.C1, C2 = k.C2, C3 = k.C3, R1 = k.R1, R2 = k.R2, R3 = k.R3, R4 = k.R4;


    const double nb1 = t * C1 * R1 + m * C3 * R3 + l * (C1 * R2 + C2 * R2) + (C1 * R3 + C2 * R3);
    const double nb2 = t * (C1 * C2 * R1 * R4 + C1 * C3 * R1 * R4) - m * m * (C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
                     + m * (C1 * C3 * R1 * R3 + C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
                     + l * (C1 * C2 * R1 * R2 + C1 * C2 * R2 * R4 + C1 * C3 * R2 * R4)
                     + l * m * (C1 * C3 * R2 * R3 + C2 * C3 * R2 * R3)
                     + (C1 * C2 * R1 * R3 + C1 * C2 * R3 * R4 + C1 * C3 * R3 * R4);
    const double nb3 = l * m * (C1 * C2 * C3 * R1 * R2 * R3 + C1 * C2 * C3 * R2 * R3 * R4)
                     - m * m * (C1 * C2 * C3 * R1 * R3 * R3 + C1 * C2 * C3 * R3 * R3 * R4)
                     + m * (C1 * C2 * C3 * R1 * R3 * R3 + C1 * C2 * C3 * R3 * R3 * R4)
                     + t * C1 * C2 * C3 * R1 * R3 * R4 - t * m * C1 * C2 * C3 * R1 * R3 * R4
                     + t * l * C1 * C2 * C3 * R1 * R2 * R4;
    const double na0 = 1.0;
    const double na1 = (C1 * R1 + C1 * R3 + C2 * R3 + C2 * R4 + C3 * R4) + m * C3 * R3 + l * (C1 * R2 + C2 * R2);
    const double na2 = m * (C1 * C3 * R1 * R3 - C2 * C3 * R3 * R4 + C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
                     + l * m * (C1 * C3 * R2 * R3 + C2 * C3 * R2 * R3)
                     - m * m * (C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
                     + l * (C1 * C2 * R2 * R4 + C1 * C2 * R1 * R2 + C1 * C3 * R2 * R4 + C2 * C3 * R2 * R4)
                     + (C1 * C2 * R1 * R4 + C1 * C3 * R1 * R4 + C1 * C2 * R3 * R4 + C1 * C2 * R1 * R3 + C1 * C3 * R3 * R4 + C2 * C3 * R3 * R4);
    const double na3 = l * m * (C1 * C2 * C3 * R1 * R2 * R3 + C1 * C2 * C3 * R2 * R3 * R4)
                     - m * m * (C1 * C2 * C3 * R1 * R3 * R3 + C1 * C2 * C3 * R3 * R3 * R4)
                     + m * (C1 * C2 * C3 * R3 * R3 * R4 + C1 * C2 * C3 * R1 * R3 * R3 - C1 * C2 * C3 * R1 * R3 * R4)
                     + l * C1 * C2 * C3 * R1 * R2 * R4 + C1 * C2 * C3 * R1 * R3 * R4;

    // bilinear transform
    const double c = 2.0 * fs, c2 = c * c, c3 = c2 * c;
    const double B0 = -nb1 * c - nb2 * c2 - nb3 * c3;
    const double B1 = -nb1 * c + nb2 * c2 + 3.0 * nb3 * c3;
    const double B2 =  nb1 * c + nb2 * c2 - 3.0 * nb3 * c3;
    const double B3 =  nb1 * c - nb2 * c2 + nb3 * c3;
    const double A0 = -na0 - na1 * c - na2 * c2 - na3 * c3;
    const double A1 = -3.0 * na0 - na1 * c + na2 * c2 + 3.0 * na3 * c3;
    const double A2 = -3.0 * na0 + na1 * c + na2 * c2 - 3.0 * na3 * c3;
    const double A3 = -na0 + na1 * c - na2 * c2 + na3 * c3;

    b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; b3 = B3 / A0;
    a1 = A1 / A0; a2 = A2 / A0; a3 = A3 / A0;
}

double ToneStack::magnitudeAt (double hz, double fs) const
{
    const std::complex<double> z1c = std::polar (1.0, -juce::MathConstants<double>::twoPi * hz / fs);
    const auto z2c = z1c * z1c, z3c = z2c * z1c;
    const auto num = b0 + b1 * z1c + b2 * z2c + b3 * z3c;
    const auto den = 1.0 + a1 * z1c + a2 * z2c + a3 * z3c;
    return std::abs (num / den);
}

// =====================================================================================================
//  Amp models
// =====================================================================================================
static inline float triodeCurve (float x) noexcept
{
    // grid conduction (positive swing) clips harder than cut-off (negative swing): even harmonics, real-amp feel
    return x >= 0.0f ? std::tanh (1.3f * x) * (1.0f / 1.3f) : x / (1.0f + 0.55f * -x);
}

BuiltInAmp::Model BuiltInAmp::modelFor (AmpType t)
{
    Model m {};
    // defaults: a neutral clean preamp
    m.stages = 2; m.minGainDb = -6; m.maxGainDb = 26; m.inputHpHz = 40; m.brightDb = 0; m.bias = 0.1f; m.bloom = 0.2f; m.stageLpHz = 10000;
    m.stack = 7; m.stackAfter = 1; m.stackMakeupDb = 0;
    m.activeBassHz = 60; m.activeMidHz = 600; m.activeMidQ = 0.7f; m.activeTrebleHz = 3500; m.activeRangeDb = 12;
    m.powerDrive = 1.2f; m.sag = 0.2f; m.asym = 0.0f; m.nfb = 0.6f; m.resonanceHz = 100;
    m.voiceLowHz = 100; m.voiceLowDb = 0; m.voiceMidHz = 800; m.voiceMidQ = 0.8f; m.voiceMidDb = 0; m.voiceHighHz = 4000; m.voiceHighDb = 0;
    m.outputTrimDb = 0; m.cleanBlendHz = 0; m.cleanBlendDb = 0;

    switch (t)
    {
        case AmpType::americanClean:
            m.stages = 2; m.minGainDb = -8; m.maxGainDb = 24; m.inputHpHz = 45; m.brightDb = 6; m.bias = 0.05f; m.bloom = 0.1f; m.stageLpHz = 13000;
            m.stack = 1; m.stackAfter = 1; m.powerDrive = 0.9f; m.sag = 0.15f; m.nfb = 0.8f; m.resonanceHz = 95;
            m.voiceHighDb = 1.0f; m.voiceHighHz = 5000;
            break;
        case AmpType::tweedBreakup:
            m.stages = 2; m.minGainDb = 2; m.maxGainDb = 38; m.inputHpHz = 25; m.brightDb = 0; m.bias = 0.28f; m.bloom = 0.55f; m.stageLpHz = 7000;
            m.stack = 0; m.stackAfter = 1; m.powerDrive = 2.2f; m.sag = 0.5f; m.asym = 0.15f; m.nfb = 0.0f; m.resonanceHz = 110;
            m.voiceMidDb = 1.5f; m.voiceMidHz = 900; m.voiceHighDb = -2.0f; m.voiceHighHz = 6000;
            break;
        case AmpType::britishChime:
            m.stages = 2; m.minGainDb = -2; m.maxGainDb = 34; m.inputHpHz = 70; m.brightDb = 3; m.bias = 0.15f; m.bloom = 0.35f; m.stageLpHz = 11000;
            m.stack = 3; m.stackAfter = 2; m.powerDrive = 1.8f; m.sag = 0.35f; m.asym = 0.25f; m.nfb = 0.0f; m.resonanceHz = 120;
            m.voiceMidDb = 3.0f; m.voiceMidHz = 1900; m.voiceMidQ = 0.9f; m.voiceLowDb = -1.5f; m.voiceLowHz = 120;
            break;
        case AmpType::britishCrunch:
            m.stages = 3; m.minGainDb = 0; m.maxGainDb = 42; m.inputHpHz = 85; m.brightDb = 3; m.bias = 0.25f; m.bloom = 0.4f; m.stageLpHz = 7500;
            m.stack = 2; m.stackAfter = 3; m.powerDrive = 1.8f; m.sag = 0.3f; m.asym = 0.05f; m.nfb = 0.4f; m.resonanceHz = 105;
            m.voiceMidDb = 1.0f; m.voiceMidHz = 900;
            break;
        case AmpType::britishLead:
            m.stages = 3; m.minGainDb = 8; m.maxGainDb = 52; m.inputHpHz = 115; m.brightDb = 2; m.bias = 0.3f; m.bloom = 0.3f; m.stageLpHz = 6500;
            m.stack = 2; m.stackAfter = 3; m.powerDrive = 1.6f; m.sag = 0.25f; m.asym = 0.05f; m.nfb = 0.5f; m.resonanceHz = 100;
            m.voiceMidDb = 2.0f; m.voiceMidHz = 1000;
            break;
        case AmpType::modernHighGain:
            m.stages = 4; m.minGainDb = 14; m.maxGainDb = 64; m.inputHpHz = 150; m.brightDb = 1; m.bias = 0.35f; m.bloom = 0.25f; m.stageLpHz = 5500;
            m.stack = 4; m.stackAfter = 4; m.powerDrive = 1.5f; m.sag = 0.2f; m.nfb = 0.55f; m.resonanceHz = 85;
            m.voiceLowDb = 1.0f; m.voiceLowHz = 90; m.voiceMidDb = -3.0f; m.voiceMidHz = 700; m.voiceHighDb = 1.0f; m.voiceHighHz = 3200;
            break;
        case AmpType::smoothOverdrive:
            m.stages = 3; m.minGainDb = 6; m.maxGainDb = 46; m.inputHpHz = 70; m.brightDb = 1; m.bias = 0.2f; m.bloom = 0.3f; m.stageLpHz = 5000;
            m.stack = 0; m.stackAfter = 1; m.powerDrive = 1.4f; m.sag = 0.3f; m.asym = 0.05f; m.nfb = 0.6f; m.resonanceHz = 100;
            m.voiceMidDb = 2.5f; m.voiceMidHz = 750; m.voiceMidQ = 0.7f; m.voiceHighDb = -1.5f; m.voiceHighHz = 5000;
            break;

        case AmpType::bassSixties:
            m.stages = 2; m.minGainDb = -4; m.maxGainDb = 30; m.inputHpHz = 28; m.brightDb = 0; m.bias = 0.2f; m.bloom = 0.45f; m.stageLpHz = 5500;
            m.stack = 6; m.stackAfter = 1; m.powerDrive = 2.0f; m.sag = 0.5f; m.asym = 0.15f; m.nfb = 0.15f; m.resonanceHz = 72;
            m.voiceLowDb = 2.5f; m.voiceLowHz = 110; m.voiceMidDb = 2.0f; m.voiceMidHz = 450; m.voiceHighDb = -4.0f; m.voiceHighHz = 2500;
            break;
        case AmpType::bassClassicTube:
            m.stages = 2; m.minGainDb = -4; m.maxGainDb = 32; m.inputHpHz = 18; m.brightDb = 1; m.bias = 0.15f; m.bloom = 0.3f; m.stageLpHz = 8000;
            m.stack = 5; m.stackAfter = 1; m.activeBassHz = 45; m.activeMidHz = 500; m.activeMidQ = 0.6f; m.activeTrebleHz = 4000; m.activeRangeDb = 15;
            m.powerDrive = 1.6f; m.sag = 0.35f; m.asym = 0.05f; m.nfb = 0.35f; m.resonanceHz = 60;
            m.voiceLowDb = 1.0f; m.voiceLowHz = 60; m.voiceMidDb = 2.0f; m.voiceMidHz = 700;
            break;
        case AmpType::bassFlipTop:
            m.stages = 2; m.minGainDb = -6; m.maxGainDb = 28; m.inputHpHz = 22; m.brightDb = 0; m.bias = 0.1f; m.bloom = 0.35f; m.stageLpHz = 4500;
            m.stack = 5; m.stackAfter = 1; m.activeBassHz = 80; m.activeMidHz = 600; m.activeMidQ = 0.6f; m.activeTrebleHz = 2500; m.activeRangeDb = 12;
            m.powerDrive = 1.6f; m.sag = 0.45f; m.asym = 0.1f; m.nfb = 0.3f; m.resonanceHz = 65;
            m.voiceLowDb = 3.0f; m.voiceLowHz = 90; m.voiceMidDb = -1.0f; m.voiceMidHz = 1000; m.voiceHighDb = -5.0f; m.voiceHighHz = 3000;
            break;
        case AmpType::bassModernGrowl:
            m.stages = 2; m.minGainDb = 4; m.maxGainDb = 40; m.inputHpHz = 280; m.brightDb = 4; m.bias = 0.1f; m.bloom = 0.1f; m.stageLpHz = 9000;
            m.stack = 5; m.stackAfter = 2; m.activeBassHz = 60; m.activeMidHz = 500; m.activeMidQ = 0.8f; m.activeTrebleHz = 3000; m.activeRangeDb = 15;
            m.powerDrive = 1.0f; m.sag = 0.1f; m.nfb = 0.8f; m.resonanceHz = 55;
            m.voiceMidDb = 2.0f; m.voiceMidHz = 1200; m.voiceHighDb = 2.0f; m.voiceHighHz = 4000;
            m.cleanBlendHz = 200; m.cleanBlendDb = 6;
            break;
        case AmpType::bassStudioDi:
            m.stages = 1; m.minGainDb = -10; m.maxGainDb = 20; m.inputHpHz = 15; m.brightDb = 0; m.bias = 0.05f; m.bloom = 0.05f; m.stageLpHz = 16000;
            m.stack = 5; m.stackAfter = 1; m.activeBassHz = 70; m.activeMidHz = 800; m.activeMidQ = 0.7f; m.activeTrebleHz = 5000; m.activeRangeDb = 12;
            m.powerDrive = 1.0f; m.sag = 0.05f; m.nfb = 0.9f; m.resonanceHz = 50;
            m.voiceLowDb = 1.0f; m.voiceLowHz = 80; m.voiceHighDb = 1.5f; m.voiceHighHz = 6000;
            break;
        case AmpType::flatDi:
        case AmpType::namCapture:
        case AmpType::count:
        default:
            m.stages = 0; m.minGainDb = 0; m.maxGainDb = 0; m.nfb = 1.0f;
            break;
    }
    m.outputTrimDb = [t]
    {
        // loudness matching (measured by rigtest with a plucked note at default knobs)
        switch (t)
        {
            case AmpType::americanClean:   return -1.2f;
            case AmpType::tweedBreakup:    return -2.9f;
            case AmpType::britishChime:    return -3.0f;
            case AmpType::britishCrunch:   return -0.9f;
            case AmpType::britishLead:     return -1.5f;
            case AmpType::modernHighGain:  return -0.5f;
            case AmpType::smoothOverdrive: return -1.3f;
            case AmpType::bassSixties:     return -0.8f;
            case AmpType::bassClassicTube: return 0.6f;
            case AmpType::bassFlipTop:     return 1.8f;
            case AmpType::bassModernGrowl: return 1.0f;
            case AmpType::bassStudioDi:    return 5.8f;
            default:                       return 4.7f;
        }
    }();
    return m;
}

static ToneStack::Components stackComponents (int stack)
{
    switch (stack)
    {
        case 0:  return ToneStack::tweed();
        case 1:  return ToneStack::blackface();
        case 2:  return ToneStack::british();
        case 3:  return ToneStack::chime();
        case 4:  return ToneStack::modern();
        default: return ToneStack::bassman();
    }
}

void BuiltInAmp::prepare (double sr, int maxBlock)
{
    sampleRate = sr;
    maxBlockSize = maxBlock;
    oversampling.initProcessing ((size_t) maxBlock);
    preGain.reset (sr, 0.03);
    masterGain.reset (sr, 0.03);
    const double osRate = sr * (1 << osFactorLog2);
    sagCoeff = std::exp (-1.0f / (float) (0.08 * osRate));   // 80 ms sag recovery
    envAtk = std::exp (-1.0f / (float) (0.003 * osRate));
    envRel = std::exp (-1.0f / (float) (0.08 * osRate));
    cleanLow.assign ((size_t) maxBlock + 16, 0.0f);
    reserve ({ &inputHp, &brightShelf, &voiceLow, &voiceMid, &voiceHigh, &activeBass, &activeMid, &activeTreble,
               &couplingHp[0], &couplingHp[1], &couplingHp[2], &couplingHp[3], &stageLp[0], &stageLp[1], &stageLp[2], &stageLp[3],
               &resonance, &nfbHigh, &presenceF, &outputLp, &dcBlock, &cleanLowLp, &cleanLowLp2, &toneOnlyBass, &toneOnlyMid, &toneOnlyTreble });
    filtersDirty = true;
    updateFilters();
    reset();
}

void BuiltInAmp::reset()
{
    oversampling.reset();
    for (auto* f : { &inputHp, &brightShelf, &voiceLow, &voiceMid, &voiceHigh, &activeBass, &activeMid, &activeTreble,
                     &resonance, &nfbHigh, &presenceF, &outputLp, &dcBlock, &cleanLowLp, &cleanLowLp2, &toneOnlyBass, &toneOnlyMid, &toneOnlyTreble })
        f->reset();
    for (auto& f : couplingHp) f.reset();
    for (auto& f : stageLp) f.reset();
    stack.reset();
    stageEnv.fill (0.0f);
    sagEnv = 0.0f;
}

void BuiltInAmp::setParameters (AmpType t, float gain, float bass, float mid, float treble, float presence, float master)
{
    if (t != type || bass != bassKnob || mid != midKnob || treble != trebleKnob || presence != presenceKnob || std::abs (gain - gainKnob) > 0.05f)
        filtersDirty = true;
    if (t != type)
    {
        v = modelFor (t);
        stack.reset();
        stageEnv.fill (0.0f);
    }

    type = t; gainKnob = gain; bassKnob = bass; midKnob = mid; trebleKnob = treble; presenceKnob = presence; masterKnob = master;

    // gain knob: log-tapered like a real pot
    const float g01 = std::pow (juce::jlimit (0.0f, 10.0f, gain) / 10.0f, 1.2f);
    preGain.setTargetValue (dbToGain (v.minGainDb + (v.maxGainDb - v.minGainDb) * g01));

    // master: 7 = unity, 10 = +12 dB, 2 = -20 dB, 0 = off. Automatic trim so more gain doesn't just mean "louder".
    const float masterDb = master <= 0.01f ? -100.0f : (master - 7.0f) * 4.0f;
    const float autoTrim = -(v.maxGainDb - v.minGainDb) * g01 * (v.stages > 0 ? 0.2f : 0.0f);
    masterGain.setTargetValue (dbToGain (masterDb + v.outputTrimDb + autoTrim));
}

void BuiltInAmp::updateFilters()
{
    if (! filtersDirty) return;
    filtersDirty = false;

    const double osRate = sampleRate * (1 << osFactorLog2);
    const float nyq = (float) (osRate * 0.45);

    assign (inputHp, AC::makeHighPass (osRate, v.inputHpHz, 0.6f));
    // the bright cap matters most with the volume low, like on a real amp
    const float brightNow = v.brightDb * (1.0f - juce::jlimit (0.0f, 10.0f, gainKnob) / 12.0f);
    assign (brightShelf, AC::makeHighShelf (osRate, 2200.0, 0.6f, dbToGain (brightNow)));

    for (int i = 0; i < 4; ++i)
    {
        // coupling caps: tighter lows in later stages (keeps high gain from going flubby)
        assign (couplingHp[(size_t) i], AC::makeFirstOrderHighPass (osRate, juce::jmax (8.0f, v.inputHpHz * (0.3f + 0.45f * (float) i))));
        assign (stageLp[(size_t) i], AC::makeFirstOrderLowPass (osRate, juce::jmin (nyq, v.stageLpHz * (1.0f + 0.3f * (float) (3 - i)))));
    }

    if (v.stack <= 4 || v.stack == 6)
    {
        const auto comps = stackComponents (v.stack);
        // make-up: the passive stack loses ~15-25 dB; normalise so noon = roughly unity in the mids
        ToneStack noon;
        noon.design (osRate, comps, 0.5, 0.5, 0.5);
        const double ref = juce::jmax (1.0e-4, noon.magnitudeAt (700.0, osRate));
        stack.design (osRate, comps, trebleKnob / 10.0, midKnob / 10.0, bassKnob / 10.0);
        v.stackMakeupDb = (float) -juce::Decibels::gainToDecibels (ref);
    }
    assign (activeBass, AC::makeLowShelf (osRate, v.activeBassHz, 0.7f, dbToGain (knobToDb (bassKnob, v.activeRangeDb))));
    assign (activeMid, AC::makePeakFilter (osRate, v.activeMidHz, v.activeMidQ, dbToGain (knobToDb (midKnob, v.activeRangeDb * 0.8f))));
    assign (activeTreble, AC::makeHighShelf (osRate, v.activeTrebleHz, 0.7f, dbToGain (knobToDb (trebleKnob, v.activeRangeDb))));

    assign (voiceLow, AC::makePeakFilter (osRate, v.voiceLowHz, 0.8f, dbToGain (v.voiceLowDb)));
    assign (voiceMid, AC::makePeakFilter (osRate, v.voiceMidHz, v.voiceMidQ, dbToGain (v.voiceMidDb)));
    assign (voiceHigh, AC::makeHighShelf (osRate, v.voiceHighHz, 0.7f, dbToGain (v.voiceHighDb)));

    // base rate: the power amp's negative feedback flattens the speaker's impedance peaks; less feedback = more bump and bite
    const float looseness = 1.0f - v.nfb;
    assign (resonance, AC::makePeakFilter (sampleRate, v.resonanceHz, 1.6f, dbToGain (4.5f * looseness)));
    assign (nfbHigh, AC::makeHighShelf (sampleRate, 3000.0, 0.6f, dbToGain (3.0f * looseness)));
    assign (presenceF, AC::makeHighShelf (sampleRate, 4500.0, 0.7f, dbToGain (knobToDb (presenceKnob, 8.0f))));
    assign (outputLp, AC::makeLowPass (sampleRate, juce::jmin (sampleRate * 0.45, 16000.0), 0.707f));
    assign (dcBlock, AC::makeFirstOrderHighPass (sampleRate, ampTypeIsBass (type) ? 16.0f : 28.0f));
    if (v.cleanBlendHz > 0.0f)
    {
        assign (cleanLowLp,  AC::makeLowPass (sampleRate, v.cleanBlendHz, 0.54f));
        assign (cleanLowLp2, AC::makeLowPass (sampleRate, v.cleanBlendHz, 1.31f));
    }
    assign (toneOnlyBass, AC::makeLowShelf (sampleRate, 100.0, 0.7f, dbToGain (knobToDb (bassKnob, 12.0f))));
    assign (toneOnlyMid, AC::makePeakFilter (sampleRate, 700.0, 0.7f, dbToGain (knobToDb (midKnob, 10.0f))));
    assign (toneOnlyTreble, AC::makeHighShelf (sampleRate, 3200.0, 0.7f, dbToGain (knobToDb (trebleKnob, 12.0f))));
}

void BuiltInAmp::processToneOnly (float* data, int numSamples)
{
    updateFilters();
    for (int i = 0; i < numSamples; ++i)
    {
        float x = data[i];
        x = toneOnlyBass.processSample (x);
        x = toneOnlyMid.processSample (x);
        x = toneOnlyTreble.processSample (x);
        x = presenceF.processSample (x);
        data[i] = x * masterGain.getNextValue() * dbToGain (-v.outputTrimDb);
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
            x = toneOnlyBass.processSample (x);
            x = toneOnlyMid.processSample (x);
            x = toneOnlyTreble.processSample (x);
            x = presenceF.processSample (x);
            data[i] = x * masterGain.getNextValue();
        }
        return;
    }

    // parallel clean low end (modern bass rigs blend a clean sub path under the drive)
    const bool blendLows = v.cleanBlendHz > 0.0f;
    if (blendLows)
        for (int i = 0; i < numSamples; ++i)
            cleanLow[(size_t) i] = cleanLowLp2.processSample (cleanLowLp.processSample (data[i]));

    // ---- preamp + tone stack + power amp, 4x oversampled ----
    float* channels[] = { data };
    juce::dsp::AudioBlock<float> block (channels, 1, (size_t) numSamples);

    const int stages = v.stages;
    const float startStageGain = lastStageGain;
    for (int i = 0; i < numSamples; ++i)
    {
        lastStageGain = std::pow (preGain.getNextValue(), 1.0f / (float) stages);
        data[i] *= lastStageGain;                      // first stage's gain (smoothed)
    }

    auto up = oversampling.processSamplesUp (block);
    auto* os = up.getChannelPointer (0);
    const int osN = (int) up.getNumSamples();
    const float gainStep = (lastStageGain - startStageGain) / (float) juce::jmax (1, osN);
    float stageGain = startStageGain;

    const bool passiveStack = v.stack <= 4 || v.stack == 6;
    const bool activeStack = v.stack == 5;
    const float stackMakeup = dbToGain (v.stackMakeupDb);
    auto toneStack = [&] (float s)
    {
        if (passiveStack) return stack.process (s) * stackMakeup;
        if (activeStack) return activeTreble.processSample (activeMid.processSample (activeBass.processSample (s)));
        return s;
    };

    for (int i = 0; i < osN; ++i)
    {
        stageGain += gainStep;
        float s = inputHp.processSample (os[i]);
        s = brightShelf.processSample (s);

        for (int st = 0; st < stages; ++st)
        {
            if (st > 0)
                s *= stageGain * 1.6f;                 // later stages also get the inter-stage gain
            // dynamic bias: a hard-driven stage shifts its operating point (bloom, sag-like compression, "blocking")
            auto& env = stageEnv[(size_t) st];
            const float a = std::abs (s);
            env = a > env ? a + (env - a) * envAtk : a + (env - a) * envRel;
            const float b = (v.bias + v.bloom * juce::jmin (env, 2.0f) * 0.25f) * (st % 2 == 0 ? 1.0f : -1.0f);
            s = triodeCurve (s + b) - triodeCurve (b);
            s = couplingHp[(size_t) st].processSample (s);
            s = stageLp[(size_t) st].processSample (s);
            if (st + 1 == v.stackAfter && v.stackAfter < stages) s = toneStack (s);
        }
        if (v.stackAfter >= stages) s = toneStack (s);

        s = voiceLow.processSample (s);
        s = voiceMid.processSample (s);
        s = voiceHigh.processSample (s);

        // power amp: supply sag lowers the drive on loud transients, then push-pull saturation
        const float a = std::abs (s);
        sagEnv = a > sagEnv ? a : sagEnv * sagCoeff + a * (1.0f - sagCoeff);
        const float drive = v.powerDrive / (1.0f + v.sag * sagEnv);
        const float off = v.asym * 0.4f;
        s = (std::tanh (s * drive + off) - std::tanh (off)) / v.powerDrive;
        os[i] = s;
    }

    oversampling.processSamplesDown (block);

    // ---- speaker interaction, presence, output (base rate) ----
    const float blendGain = dbToGain (v.cleanBlendDb);
    for (int i = 0; i < numSamples; ++i)
    {
        float x = data[i];
        if (blendLows)
            x += cleanLow[(size_t) i] * blendGain;
        x = resonance.processSample (x);
        x = nfbHigh.processSample (x);
        x = presenceF.processSample (x);
        x = outputLp.processSample (x);
        x = dcBlock.processSample (x);
        data[i] = x * masterGain.getNextValue() * 2.0f;
    }
}

// =====================================================================================================
//  Drive pedals
// =====================================================================================================
juce::StringArray driveTypeNames()
{
    return { "Overdrive", "Distortion", "Fuzz", "Bass Drive (clean blend)" };
}

void DrivePedal::prepare (double sr, int maxBlock)
{
    sampleRate = sr;
    oversampling.initProcessing ((size_t) maxBlock);
    driveGain.reset (sr, 0.03);
    outGain.reset (sr, 0.03);
    cleanCopy.assign ((size_t) maxBlock * 2 + 16, 0.0f);
    reserve ({ &preHp, &clipHp, &toneLp, &toneHp, &postHp, &fuzzMidHp, &fuzzMidLp, &blendLp, &blendLp2 });
    dirty = true;
    updateFilters();
    reset();
}

void DrivePedal::reset()
{
    oversampling.reset();
    for (auto* f : { &preHp, &clipHp, &toneLp, &toneHp, &postHp, &fuzzMidHp, &fuzzMidLp, &blendLp, &blendLp2 })
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
        case DriveType::bassDrive:  gainDb = 8.0f + 40.0f * d01;  trimDb = -6.0f;  break;
        default: break;
    }
    driveGain.setTargetValue (dbToGain (gainDb));
    const float levelDb = lv <= 0.01f ? -100.0f : juce::jmap (lv, 0.0f, 10.0f, -24.0f, 12.0f);
    outGain.setTargetValue (dbToGain (levelDb + trimDb));
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
        case DriveType::bassDrive:
            // only the mids and highs are distorted; the low end is kept clean underneath
            assign (preHp, AC::makeHighPass (os, 250.0, 0.707f));
            assign (clipHp, AC::makeFirstOrderHighPass (os, 30.0));
            assign (toneLp, AC::makeLowPass (sampleRate, 1200.0 * std::pow (8.0, t01), 0.707f));
            assign (blendLp, AC::makeLowPass (sampleRate, 250.0, 0.54f));
            assign (blendLp2, AC::makeLowPass (sampleRate, 250.0, 1.31f));
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
        case DriveType::bassDrive:
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
        if (type == DriveType::bassDrive)
            x = x * 0.7f + blendLp2.processSample (blendLp.processSample (cleanCopy[(size_t) i])) * 2.0f;

        data[i] = postHp.processSample (x) * outGain.getNextValue();
    }
}

// =====================================================================================================
//  Strings & pickups
// =====================================================================================================
juce::StringArray characterTypeNames()
{
    return { "Off", "Flatwound Strings", "60s Violin Bass (flats)", "Foam Mute (thud)", "Bright Roundwounds", "Split-Coil Bass Pickup",
             "Vintage Single Coils", "Humbuckers" };
}

void InstrumentCharacter::prepare (double sr)
{
    sampleRate = sr;
    reserve ({ &eq1, &eq2, &eq3, &eq4 });
    fastA = std::exp (-1.0f / (float) (0.001 * sr));
    fastR = std::exp (-1.0f / (float) (0.03 * sr));
    slowA = std::exp (-1.0f / (float) (0.04 * sr));
    slowR = std::exp (-1.0f / (float) (0.3 * sr));
    dirty = true;
    reset();
}

void InstrumentCharacter::reset()
{
    for (auto* f : { &eq1, &eq2, &eq3, &eq4 }) f->reset();
    lpState1 = lpState2 = envFast = envSlow = brightEnv = 0.0f;
    sustainGain = 1.0f;
    sinceAttack = 10.0f;
}

void InstrumentCharacter::setParameters (CharacterType t, float amt)
{
    const float a = juce::jlimit (0.0f, 10.0f, amt) / 10.0f;
    if (t != type || std::abs (a - amount) > 0.01f) dirty = true;
    type = t;
    amount = a;
}

void InstrumentCharacter::process (float* data, int n)
{
    if (type == CharacterType::off || amount <= 0.001f) return;
    const double sr = sampleRate;
    const float a = amount;
    // the amount scales the filters themselves (mixing dry and filtered copies would comb-filter)
    auto peak = [&] (float hz, float q, float db) { return AC::makePeakFilter (sr, hz, q, dbToGain (db * a)); };
    auto lowShelf = [&] (float hz, float db) { return AC::makeLowShelf (sr, hz, 0.7f, dbToGain (db * a)); };
    auto highShelf = [&] (float hz, float db) { return AC::makeHighShelf (sr, hz, 0.7f, dbToGain (db * a)); };
    auto highPass = [&] (float hz) { return AC::makeHighPass (sr, 15.0f + (hz - 15.0f) * a, 0.7f); };
    auto lowPass = [&] (float hz) { return AC::makeLowPass (sr, juce::jmin (sr * 0.45, 20000.0 * std::pow (hz / 20000.0, (double) a)), 0.6f); };
    if (dirty)
    {
        dirty = false;
        used = 0;
        switch (type)
        {
            case CharacterType::flatwounds:
                assign (eq1, lowShelf (120.0f, 2.0f)); assign (eq2, peak (2500.0f, 0.8f, -3.0f)); used = 2; break;
            case CharacterType::violinBass:
                assign (eq1, highPass (45.0f)); assign (eq2, peak (140.0f, 1.2f, 4.0f)); assign (eq3, peak (320.0f, 1.0f, 2.0f)); assign (eq4, lowPass (2800.0f)); used = 4; break;
            case CharacterType::foamMute:
                assign (eq1, peak (120.0f, 1.0f, 3.0f)); assign (eq2, lowPass (1400.0f)); used = 2; break;
            case CharacterType::roundwoundsBright:
                assign (eq1, highPass (30.0f)); assign (eq2, peak (2800.0f, 0.8f, 3.0f)); assign (eq3, highShelf (6000.0f, 3.0f)); assign (eq4, peak (400.0f, 0.8f, -2.5f)); used = 4; break;
            case CharacterType::precisionPickup:
                assign (eq1, peak (700.0f, 0.9f, 3.0f)); assign (eq2, peak (120.0f, 1.0f, 2.0f)); assign (eq3, highShelf (5000.0f, -2.0f)); used = 3; break;
            case CharacterType::singleCoils:
                assign (eq1, peak (600.0f, 0.8f, -2.5f)); assign (eq2, peak (3000.0f, 1.2f, 3.0f)); assign (eq3, highShelf (8000.0f, 1.0f)); used = 3; break;
            case CharacterType::humbuckers:
                assign (eq1, peak (500.0f, 0.8f, 3.0f)); assign (eq2, highShelf (4000.0f, -3.0f)); assign (eq3, lowShelf (120.0f, 1.0f)); used = 3; break;
            default: break;
        }
    }

    // strings that lose their top after the pluck (flats, foam mute) get a dynamic low-pass
    const bool dynamic = type == CharacterType::flatwounds || type == CharacterType::violinBass || type == CharacterType::foamMute;
    const float baseHz0 = type == CharacterType::flatwounds ? 1100.0f : type == CharacterType::violinBass ? 900.0f : 500.0f;
    const float openHz0 = type == CharacterType::flatwounds ? 4000.0f : type == CharacterType::violinBass ? 3200.0f : 1600.0f;
    const float baseHz = 20000.0f * std::pow (baseHz0 / 20000.0f, a), openHz = 20000.0f * std::pow (openHz0 / 20000.0f, a);
    const float brightTau = type == CharacterType::flatwounds ? 0.12f : type == CharacterType::violinBass ? 0.09f : 0.05f;
    const float sustainTau = type == CharacterType::violinBass ? 1.1f : type == CharacterType::foamMute ? 0.22f : 0.0f;
    const float sustainFloor = 1.0f - (1.0f - (type == CharacterType::foamMute ? 0.1f : 0.45f)) * a;
    const float brightDecay = std::exp (-1.0f / (brightTau * (float) sr));
    const float dt = 1.0f / (float) sr;
    const float gainSmooth = 1.0f - std::exp (-1.0f / (0.003f * (float) sr));

    for (int i = 0; i < n; ++i)
    {
        const float dry = data[i];
        float x = dry;
        if (used > 0) x = eq1.processSample (x);
        if (used > 1) x = eq2.processSample (x);
        if (used > 2) x = eq3.processSample (x);
        if (used > 3) x = eq4.processSample (x);
        if (dynamic)
        {
            const float ad = std::abs (dry);
            envFast = ad > envFast ? ad + (envFast - ad) * fastA : ad + (envFast - ad) * fastR;
            envSlow = ad > envSlow ? ad + (envSlow - ad) * slowA : ad + (envSlow - ad) * slowR;
            sinceAttack += dt;
            if (envFast > envSlow * 1.6f && envFast > 0.003f && sinceAttack > 0.04f)
            {
                brightEnv = 1.0f;
                sinceAttack = 0.0f;
            }
            brightEnv *= brightDecay;
            const float hz = baseHz + (openHz - baseHz) * brightEnv;
            const float g = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * juce::jmin (hz, 0.45f * (float) sr) / (float) sr);
            lpState1 += (x - lpState1) * g;
            lpState2 += (lpState1 - lpState2) * g;
            x = lpState2;
            if (sustainTau > 0.0f)
            {
                const float target = sustainFloor + (1.0f - sustainFloor) * std::exp (-sinceAttack / sustainTau);
                sustainGain += (target - sustainGain) * gainSmooth;
                x *= sustainGain;
            }
        }
        data[i] = x;
    }
}

// =====================================================================================================
//  60s console + tape
// =====================================================================================================
void ConsoleTape::prepare (double sr)
{
    sampleRate = sr;
    reserve ({ &preEmph, &deEmph, &headBump, &topRoll, &lowCut });
    assign (preEmph, AC::makeHighShelf (sr, 3000.0, 0.7f, dbToGain (6.0f)));
    assign (deEmph, AC::makeHighShelf (sr, 3000.0, 0.7f, dbToGain (-6.0f)));
    assign (headBump, AC::makePeakFilter (sr, 75.0, 1.0f, dbToGain (2.0f)));
    assign (topRoll, AC::makeLowPass (sr, juce::jmin (sr * 0.45, 14000.0), 0.6f));
    assign (lowCut, AC::makeHighPass (sr, 25.0, 0.7f));
    reset();
}

void ConsoleTape::reset()
{
    for (auto* f : { &preEmph, &deEmph, &headBump, &topRoll, &lowCut }) f->reset();
}

void ConsoleTape::process (float* data, int n)
{
    const float g = 1.0f + juce::jlimit (0.0f, 10.0f, drive) * 0.45f;
    for (int i = 0; i < n; ++i)
    {
        float x = preEmph.processSample (data[i]);
        x = std::tanh (x * g) / g;                     // tape compression: soft, more on the highs
        x = deEmph.processSample (x);
        x = headBump.processSample (x);
        x = topRoll.processSample (x);
        data[i] = lowCut.processSample (x);
    }
}

} // namespace wis
