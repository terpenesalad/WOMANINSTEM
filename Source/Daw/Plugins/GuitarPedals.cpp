#include "GuitarPedals.h"

namespace wis::daw
{

using namespace prm;

namespace
{
    bool sameChannels (const juce::AudioBuffer<float>& b, int n)
    {
        if (b.getNumChannels() < 2) return true;
        const float* l = b.getReadPointer (0);
        const float* r = b.getReadPointer (1);
        for (int i = 0; i < n; ++i)
            if (l[i] != r[i]) return false;
        return true;
    }

    inline float softLimit (float x) noexcept
    {
        // transparent below ~0.8, then a smooth knee: a boost shouldn't fold over on its own
        const float a = std::abs (x);
        if (a < 0.8f) return x;
        const float y = 0.8f + 0.2f * std::tanh ((a - 0.8f) / 0.2f);
        return x < 0.0f ? -y : y;
    }
}

// =====================================================================================================
//  Drive
// =====================================================================================================
static Layout driveLayout()
{
    Layout l;
    addChoice (l, "type", "Type", driveTypeNames(), 0);
    addFloat  (l, "drive", "Drive", 0.0f, 10.0f, 5.0f, "", 0.0f, 1);
    addFloat  (l, "tone", "Tone", 0.0f, 10.0f, 5.5f, "", 0.0f, 1);
    addFloat  (l, "level", "Level", 0.0f, 10.0f, 6.0f, "", 0.0f, 1);
    return l;
}

DriveStomp::DriveStomp() : BuiltinProcessor ("stompdrive", "Drive Pedal", false, driveLayout()) {}

void DriveStomp::prepareToPlay (double sr, int block)
{
    for (auto& d : drive) d.prepare (sr, block);
    setLatencySamples (juce::roundToInt (drive[0].getLatencySamples()));
}

void DriveStomp::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const int n = buffer.getNumSamples();
    const auto type = (DriveType) juce::jlimit (0, (int) DriveType::count - 1, (int) param ("type"));
    for (auto& d : drive) d.setParameters (type, param ("drive"), param ("tone"), param ("level"));
    if (sameChannels (buffer, n))
    {
        drive[0].process (buffer.getWritePointer (0), n);
        for (int c = 1; c < buffer.getNumChannels(); ++c) buffer.copyFrom (c, 0, buffer, 0, 0, n);
    }
    else
    {
        for (int c = 0; c < juce::jmin (2, buffer.getNumChannels()); ++c)
            drive[c].process (buffer.getWritePointer (c), n);
    }
}

juce::StringArray DriveStomp::getProgramNames() { return { "Edge of Breakup", "Classic Overdrive", "Hot Distortion", "Thick Fuzz", "Bass Grit", "Bass Fuzz Wall" }; }

void DriveStomp::loadProgram (int i)
{
    struct P { int type; float drive, tone, level; };
    static const P ps[] = { { 0, 2.0f, 5.5f, 6.5f }, { 0, 5.0f, 6.0f, 6.0f }, { 1, 6.5f, 5.5f, 5.5f },
                            { 2, 7.5f, 5.0f, 5.5f }, { 3, 4.0f, 5.5f, 6.0f }, { 3, 8.5f, 4.5f, 5.5f } };
    const auto& p = ps[juce::jlimit (0, 5, i)];
    setParam ("type", (float) p.type); setParam ("drive", p.drive); setParam ("tone", p.tone); setParam ("level", p.level);
}

// =====================================================================================================
//  Boost
// =====================================================================================================
static Layout boostLayout()
{
    Layout l;
    addChoice (l, "type", "Type", { "Clean", "Treble", "Mid" }, 0);
    addDb     (l, "gain", "Boost", 0.0f, 24.0f, 9.0f);
    addFloat  (l, "tone", "Tone", 0.0f, 10.0f, 5.0f, "", 0.0f, 1);
    return l;
}

BoostStomp::BoostStomp() : BuiltinProcessor ("boost", "Boost", false, boostLayout()) {}

void BoostStomp::prepareToPlay (double sr, int)
{
    sampleRate = sr;
    for (auto* arr : { &shape, &lowCut })
        for (auto& f : *arr) { f.coefficients = new juce::dsp::IIR::Coefficients<float> (1, 0, 1, 0); f.reset(); }
    lastType = -1;
    lastTone = -1.0f;
    gain.reset (sr, 0.02);
    gain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (param ("gain")));
}

void BoostStomp::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const int n = buffer.getNumSamples(), nch = juce::jmin (2, buffer.getNumChannels());
    const int type = (int) param ("type");
    const float tone = param ("tone");
    if (type != lastType || std::abs (tone - lastTone) > 0.01f)
    {
        lastType = type;
        lastTone = tone;
        using C = juce::dsp::IIR::Coefficients<float>;
        const float t = tone / 10.0f;
        for (int c = 0; c < 2; ++c)
        {
            switch (type)
            {
                case 1:  // treble booster: thins the lows, lifts the highs
                    shape[(size_t) c].coefficients = C::makeHighShelf (sampleRate, 2500.0, 0.7, juce::Decibels::decibelsToGain (2.0f + 6.0f * t));
                    lowCut[(size_t) c].coefficients = C::makeHighPass (sampleRate, 200.0 + 900.0 * t, 0.6);
                    break;
                case 2:  // mid boost: pushes the amp with a fat hump
                    shape[(size_t) c].coefficients = C::makePeakFilter (sampleRate, 400.0 + 1100.0 * t, 0.9, juce::Decibels::decibelsToGain (7.0f));
                    lowCut[(size_t) c].coefficients = C::makeHighPass (sampleRate, 50.0, 0.7);
                    break;
                default: // clean: flat, the tone knob tilts the top a little
                    shape[(size_t) c].coefficients = C::makeHighShelf (sampleRate, 3000.0, 0.7, juce::Decibels::decibelsToGain ((t - 0.5f) * 8.0f));
                    lowCut[(size_t) c].coefficients = C::makeHighPass (sampleRate, 25.0, 0.7);
                    break;
            }
        }
    }
    gain.setTargetValue (juce::Decibels::decibelsToGain (param ("gain")));
    for (int i = 0; i < n; ++i)
    {
        const float g = gain.getNextValue();
        for (int c = 0; c < nch; ++c)
        {
            auto* d = buffer.getWritePointer (c);
            d[i] = softLimit (shape[(size_t) c].processSample (lowCut[(size_t) c].processSample (d[i])) * g);
        }
    }
}

juce::StringArray BoostStomp::getProgramNames() { return { "Clean Boost", "Treble Booster", "Mid Push", "Bass Clean Lift" }; }

void BoostStomp::loadProgram (int i)
{
    struct P { int type; float gain, tone; };
    static const P ps[] = { { 0, 9.0f, 5.0f }, { 1, 14.0f, 5.0f }, { 2, 10.0f, 4.0f }, { 0, 6.0f, 6.5f } };
    const auto& p = ps[juce::jlimit (0, 3, i)];
    setParam ("type", (float) p.type); setParam ("gain", p.gain); setParam ("tone", p.tone);
}

// =====================================================================================================
//  Wah
// =====================================================================================================
static Layout wahLayout()
{
    Layout l;
    addChoice  (l, "mode", "Mode", { "Pedal", "Auto (touch)", "LFO" }, 0);
    addPercent (l, "position", "Pedal", 0.5f);
    addPercent (l, "sens", "Sensitivity", 0.6f);
    addFloat   (l, "rate", "Rate", 0.1f, 8.0f, 1.5f, "Hz", 1.0f, 2);
    addChoice  (l, "range", "Range", { "Guitar", "Bass" }, 0);
    addFloat   (l, "q", "Resonance", 1.5f, 12.0f, 5.0f, "", 4.0f, 1);
    addPercent (l, "mix", "Mix", 1.0f);
    return l;
}

WahStomp::WahStomp() : BuiltinProcessor ("wah", "Wah", false, wahLayout()) {}

void WahStomp::prepareToPlay (double sr, int)
{
    sampleRate = sr;
    for (auto& f : svf) f = {};
    env = 0.0f;
    position = param ("position");
    lfoPhase = 0.0;
}

void WahStomp::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const int n = buffer.getNumSamples(), nch = juce::jmin (2, buffer.getNumChannels());
    const int mode = (int) param ("mode");
    const bool bass = param ("range") > 0.5f;
    const float lo = bass ? 120.0f : 350.0f, hi = bass ? 1100.0f : 2200.0f;
    const float k = 1.0f / param ("q"), mix = param ("mix"), sens = param ("sens");
    const float att = 1.0f - std::exp (-1.0f / (0.004f * (float) sampleRate));
    const float rel = 1.0f - std::exp (-1.0f / (0.15f * (float) sampleRate));
    const float posSmooth = 1.0f - std::exp (-1.0f / (0.012f * (float) sampleRate));
    const double lfoInc = param ("rate") / sampleRate;
    const float manual = param ("position");

    float a1 = 0, a2 = 0, a3 = 0;
    for (int i = 0; i < n; ++i)
    {
        float in = 0.0f;
        for (int c = 0; c < nch; ++c) in = juce::jmax (in, std::abs (buffer.getSample (c, i)));
        env += (in > env ? att : rel) * (in - env);

        float target = manual;
        if (mode == 1)
        {
            const float db = juce::Decibels::gainToDecibels (env, -100.0f);
            target = juce::jlimit (0.0f, 1.0f, (db + 60.0f - (1.0f - sens) * 40.0f) / 40.0f);
        }
        else if (mode == 2)
        {
            target = 0.5f - 0.5f * (float) std::cos (juce::MathConstants<double>::twoPi * lfoPhase);
            lfoPhase += lfoInc;
            if (lfoPhase >= 1.0) lfoPhase -= 1.0;
        }
        position += posSmooth * (target - position);

        if ((i & 7) == 0)
        {
            const float fc = lo * std::pow (hi / lo, position);
            const float g = std::tan (juce::MathConstants<float>::pi * juce::jmin (fc, 0.45f * (float) sampleRate) / (float) sampleRate);
            a1 = 1.0f / (1.0f + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
        }
        for (int c = 0; c < nch; ++c)
        {
            auto& s = svf[(size_t) c];
            float* d = buffer.getWritePointer (c);
            const float x = d[i];
            const float v3 = x - s.ic2;
            const float v1 = a1 * s.ic1 + a2 * v3;
            const float v2 = s.ic2 + a2 * s.ic1 + a3 * v3;
            s.ic1 = 2.0f * v1 - s.ic1;
            s.ic2 = 2.0f * v2 - s.ic2;
            const float wet = k * v1 * 2.2f + v2 * 0.08f;   // resonant band-pass with a whisper of the lows, like the real thing
            d[i] = x + mix * (wet - x);
        }
    }
    for (auto& s : svf)
        if (! std::isfinite (s.ic1) || ! std::isfinite (s.ic2)) s = {};
}

juce::StringArray WahStomp::getProgramNames() { return { "Rocker Pedal (half open)", "Funk Auto-Wah", "Slow Sweep", "Bass Envelope Filter", "Cocked Wah (toe down)" }; }

void WahStomp::loadProgram (int i)
{
    struct P { int mode; float pos, sens, rate; int range; float q, mix; };
    static const P ps[] = { { 0, 0.5f, 0.6f, 1.5f, 0, 5.0f, 1.0f }, { 1, 0.5f, 0.7f, 1.5f, 0, 6.0f, 1.0f }, { 2, 0.5f, 0.6f, 0.4f, 0, 5.0f, 1.0f },
                            { 1, 0.5f, 0.65f, 1.5f, 1, 4.0f, 0.85f }, { 0, 0.85f, 0.6f, 1.5f, 0, 7.0f, 1.0f } };
    const auto& p = ps[juce::jlimit (0, 4, i)];
    setParam ("mode", (float) p.mode); setParam ("position", p.pos); setParam ("sens", p.sens); setParam ("rate", p.rate);
    setParam ("range", (float) p.range); setParam ("q", p.q); setParam ("mix", p.mix);
}

// =====================================================================================================
//  Octaver
// =====================================================================================================
static Layout octaverLayout()
{
    Layout l;
    addPercent (l, "dry", "Dry", 1.0f);
    addPercent (l, "sub1", "Octave Down", 0.7f);
    addPercent (l, "sub2", "Two Down", 0.0f);
    addPercent (l, "up", "Octave Up", 0.0f);
    addHz      (l, "tone", "Sub Tone", 150.0f, 2500.0f, 600.0f);
    return l;
}

OctaverStomp::OctaverStomp() : BuiltinProcessor ("octaver", "Octaver", false, octaverLayout()) {}

void OctaverStomp::prepareToPlay (double sr, int)
{
    sampleRate = sr;
    using C = juce::dsp::IIR::Coefficients<float>;
    trackLp1.coefficients = C::makeLowPass (sr, 500.0, 0.6);
    trackLp2.coefficients = C::makeLowPass (sr, 500.0, 0.6);
    upHp.coefficients = C::makeHighPass (sr, 90.0, 0.7);
    upLp.coefficients = C::makeLowPass (sr, 5000.0, 0.7);
    for (auto* f : { &trackLp1, &trackLp2, &subLp1, &subLp2, &upHp, &upLp }) f->reset();
    lastTone = -1.0f;
    env = 0.0f; armed = true; ff1 = ff2 = false;
}

void OctaverStomp::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const int n = buffer.getNumSamples(), nch = juce::jmin (2, buffer.getNumChannels());
    if (nch == 0) return;
    const float tone = param ("tone");
    if (std::abs (tone - lastTone) > 1.0f)
    {
        lastTone = tone;
        using C = juce::dsp::IIR::Coefficients<float>;
        subLp1.coefficients = C::makeLowPass (sampleRate, tone, 0.6);
        subLp2.coefficients = C::makeLowPass (sampleRate, tone, 0.6);
    }
    const float dry = param ("dry"), s1 = param ("sub1"), s2 = param ("sub2"), up = param ("up");
    const float att = 1.0f - std::exp (-1.0f / (0.002f * (float) sampleRate));
    const float rel = 1.0f - std::exp (-1.0f / (0.06f * (float) sampleRate));

    for (int i = 0; i < n; ++i)
    {
        float m = 0.0f;
        for (int c = 0; c < nch; ++c) m += buffer.getSample (c, i);
        m /= (float) nch;

        // track the fundamental: low-passed signal, flip-flops on its rising zero crossings (with hysteresis)
        const float t = trackLp2.processSample (trackLp1.processSample (m));
        const float a = std::abs (t);
        env += (a > env ? att : rel) * (a - env);
        const float h = env * 0.15f;
        if (armed && t > h)
        {
            armed = false;
            ff1 = ! ff1;
            if (ff1) ff2 = ! ff2;
        }
        else if (t < -h)
        {
            armed = true;
        }
        const float gate = env > 1.0e-4f ? 1.0f : env * 1.0e4f;
        const float sub = ((ff1 ? 1.0f : -1.0f) * s1 + (ff2 ? 1.0f : -1.0f) * s2) * env * 1.6f * gate;
        const float subOut = subLp2.processSample (subLp1.processSample (sub));
        const float upOut = up > 0.0f ? upLp.processSample (upHp.processSample (std::abs (m))) * up * 2.0f : 0.0f;

        for (int c = 0; c < nch; ++c)
        {
            float* d = buffer.getWritePointer (c);
            d[i] = d[i] * dry + subOut + upOut;
        }
    }
}

juce::StringArray OctaverStomp::getProgramNames() { return { "Classic Sub Octave", "Synth Bass Floor", "Octave Fuzz Up", "Big Organ (down + up)" }; }

void OctaverStomp::loadProgram (int i)
{
    struct P { float dry, s1, s2, up, tone; };
    static const P ps[] = { { 1.0f, 0.7f, 0.0f, 0.0f, 600.0f }, { 0.6f, 0.8f, 0.5f, 0.0f, 350.0f }, { 0.8f, 0.0f, 0.0f, 0.7f, 600.0f }, { 0.8f, 0.6f, 0.0f, 0.4f, 900.0f } };
    const auto& p = ps[juce::jlimit (0, 3, i)];
    setParam ("dry", p.dry); setParam ("sub1", p.s1); setParam ("sub2", p.s2); setParam ("up", p.up); setParam ("tone", p.tone);
}

// =====================================================================================================
//  Swell
// =====================================================================================================
static Layout swellLayout()
{
    Layout l;
    addMs (l, "attack", "Swell Time", 50.0f, 3000.0f, 600.0f);
    addDb (l, "thresh", "Sensitivity", -70.0f, -20.0f, -46.0f);
    return l;
}

SwellStomp::SwellStomp() : BuiltinProcessor ("swell", "Volume Swell", false, swellLayout()) {}

void SwellStomp::prepareToPlay (double sr, int)
{
    sampleRate = sr;
    env = 0.0f; gainNow = 0.0f; open = false;
}

void SwellStomp::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const int n = buffer.getNumSamples(), nch = juce::jmin (2, buffer.getNumChannels());
    const float thr = juce::Decibels::decibelsToGain (param ("thresh"));
    const float rise = 1.0f / juce::jmax (1.0f, param ("attack") * 0.001f * (float) sampleRate);
    const float fall = 1.0f / (0.03f * (float) sampleRate);
    const float att = 1.0f - std::exp (-1.0f / (0.001f * (float) sampleRate));
    const float rel = 1.0f - std::exp (-1.0f / (0.04f * (float) sampleRate));
    for (int i = 0; i < n; ++i)
    {
        float in = 0.0f;
        for (int c = 0; c < nch; ++c) in = juce::jmax (in, std::abs (buffer.getSample (c, i)));
        env += (in > env ? att : rel) * (in - env);
        // a note only swells in after the previous one has died away (like a violin bow, or a volume knob rolled up)
        if (! open && env > thr) open = true;
        else if (open && env < thr * 0.5f) open = false;
        gainNow = open ? juce::jmin (1.0f, gainNow + rise) : juce::jmax (0.0f, gainNow - fall);
        const float g = gainNow * gainNow * (3.0f - 2.0f * gainNow);
        for (int c = 0; c < nch; ++c) buffer.getWritePointer (c)[i] *= g;
    }
}

} // namespace wis::daw
