#include "CreativeEffects.h"

namespace wis::daw
{
using namespace prm;

namespace
{
    using Preset = std::vector<std::pair<const char*, float>>;
    void applyPreset (BuiltinProcessor& p, const std::vector<Preset>& list, int i)
    {
        if (! juce::isPositiveAndBelow (i, (int) list.size())) return;
        for (auto& [id, v] : list[(size_t) i]) p.setParam (id, v);
    }
    constexpr double twoPi = juce::MathConstants<double>::twoPi;
    inline float softClip (float x) { return x / (1.0f + std::abs (x) * 0.3f); }
}

// =====================================================================================================
//  Shared helpers
// =====================================================================================================
namespace fxdsp
{
Transport transport (juce::AudioProcessor& p)
{
    Transport t;
    if (auto* ph = p.getPlayHead())
        if (auto pos = ph->getPosition())
        {
            t.playing = pos->getIsPlaying();
            if (auto q = pos->getPpqPosition()) t.ppq = *q;
            if (auto b = pos->getBpm()) t.bpm = *b;
            if (auto ts = pos->getTimeSignature()) t.bpb = ts->numerator * 4.0 / juce::jmax (1, ts->denominator);
        }
    return t;
}

juce::StringArray syncNames() { return { "4 Bars", "2 Bars", "1 Bar", "1/2", "1/4", "1/4 T", "1/8", "1/8 T", "1/16", "1/16 T", "1/32" }; }
double syncBeats (int i)
{
    static const double b[] = { 16, 8, 4, 2, 1, 2.0 / 3.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
    return b[juce::jlimit (0, (int) std::size (b) - 1, i)];
}
juce::StringArray lfoShapeNames() { return { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Random Steps", "Smooth Random" }; }

float lfoShape (int shape, double ph, float randomHeld)   // -> -1..1
{
    ph -= std::floor (ph);
    switch (shape)
    {
        case 1:  return (float) (ph < 0.5 ? ph * 4.0 - 1.0 : 3.0 - ph * 4.0);
        case 2:  return (float) (ph * 2.0 - 1.0);
        case 3:  return (float) (1.0 - ph * 2.0);
        case 4:  return ph < 0.5 ? 1.0f : -1.0f;
        case 5:
        case 6:  return randomHeld;
        default: return (float) std::sin (twoPi * ph);
    }
}
} // namespace fxdsp

// =====================================================================================================
//  Bitcrusher
// =====================================================================================================
static Layout crushLayout()
{
    Layout l;
    addFloat (l, "bits", "Bits", 1.0f, 16.0f, 8.0f, "bit", 0.0f, 1);
    addFloat (l, "down", "Downsample", 1.0f, 64.0f, 4.0f, "x", 8.0f, 1);
    addPercent (l, "jitter", "Jitter", 0.0f);
    addDb (l, "drive", "Drive", 0.0f, 24.0f, 0.0f);
    addPercent (l, "mix", "Mix", 1.0f);
    return l;
}
Bitcrusher::Bitcrusher() : BuiltinProcessor ("bitcrusher", "Bitcrusher", false, crushLayout()) {}

void Bitcrusher::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const float bits = param ("bits"), down = param ("down"), jitter = param ("jitter"), mix = param ("mix");
    const float drive = juce::Decibels::decibelsToGain (param ("drive"));
    const float levels = std::pow (2.0f, bits - 1.0f);
    const int nch = juce::jmin (2, buffer.getNumChannels()), n = buffer.getNumSamples();
    for (int i = 0; i < n; ++i)
    {
        counter += 1.0;
        const bool take = counter >= down;
        if (take) counter -= down * (1.0 + jitter * (rng.nextFloat() - 0.5));
        for (int ch = 0; ch < nch; ++ch)
        {
            float* d = buffer.getWritePointer (ch);
            const float x = d[i];
            if (take)
            {
                const float v = juce::jlimit (-1.0f, 1.0f, x * drive);
                held[ch] = std::round (v * levels) / levels;
            }
            d[i] = x + (held[ch] - x) * mix;
        }
    }
}

// =====================================================================================================
//  Auto Filter
// =====================================================================================================
static Layout autoFilterLayout()
{
    Layout l;
    addChoice (l, "type", "Filter", { "Low-pass", "Low-pass 24 dB", "Band-pass", "High-pass", "Notch" }, 0);
    addHz (l, "cutoff", "Cutoff", 30.0f, 18000.0f, 900.0f);
    addPercent (l, "reso", "Resonance", 0.35f);
    addFloat (l, "depth", "LFO Depth", 0.0f, 6.0f, 2.0f, "oct", 0.0f, 2);
    addBool (l, "sync", "Sync to Tempo", true);
    addChoice (l, "division", "LFO Note", fxdsp::syncNames(), 4);
    addFloat (l, "rate", "LFO Rate", 0.02f, 20.0f, 1.0f, "Hz", 2.0f, 2);
    addChoice (l, "shape", "LFO Shape", fxdsp::lfoShapeNames(), 0);
    addPercent (l, "stereo", "Stereo Phase", 0.0f);
    addFloat (l, "env", "Envelope", -6.0f, 6.0f, 0.0f, "oct", 0.0f, 2);
    addMs (l, "attack", "Env Attack", 1.0f, 200.0f, 10.0f);
    addMs (l, "release", "Env Release", 10.0f, 1000.0f, 150.0f);
    addDb (l, "drive", "Drive", 0.0f, 24.0f, 0.0f);
    addPercent (l, "mix", "Mix", 1.0f);
    return l;
}
AutoFilter::AutoFilter() : BuiltinProcessor ("autofilter", "Auto Filter", false, autoFilterLayout()) {}

void AutoFilter::prepareToPlay (double sr, int)
{
    sampleRate = sr; env = 0; smooth = -1;
    for (auto& a : f) for (auto& b : a) b.reset();
}

juce::StringArray AutoFilter::getProgramNames() { return { "Slow Sweep", "Wobble 1/8", "Envelope Funk", "Random Steps", "Telephone", "Rising Swell (4 Bars)" }; }

void AutoFilter::loadProgram (int i)
{
    static const std::vector<Preset> p = {
        { { "type", 0 }, { "cutoff", 700 }, { "reso", 0.4f }, { "depth", 3 }, { "sync", 1 }, { "division", 2 }, { "shape", 0 }, { "env", 0 }, { "mix", 1 } },
        { { "type", 1 }, { "cutoff", 400 }, { "reso", 0.6f }, { "depth", 3.5f }, { "sync", 1 }, { "division", 6 }, { "shape", 1 }, { "env", 0 }, { "mix", 1 } },
        { { "type", 2 }, { "cutoff", 300 }, { "reso", 0.65f }, { "depth", 0 }, { "env", 4 }, { "attack", 5 }, { "release", 120 }, { "mix", 1 } },
        { { "type", 0 }, { "cutoff", 900 }, { "reso", 0.55f }, { "depth", 3 }, { "sync", 1 }, { "division", 8 }, { "shape", 5 }, { "env", 0 }, { "mix", 1 } },
        { { "type", 2 }, { "cutoff", 1400 }, { "reso", 0.2f }, { "depth", 0 }, { "env", 0 }, { "drive", 9 }, { "mix", 1 } },
        { { "type", 1 }, { "cutoff", 120 }, { "reso", 0.45f }, { "depth", 6 }, { "sync", 1 }, { "division", 0 }, { "shape", 2 }, { "env", 0 }, { "mix", 1 } },
    };
    applyPreset (*this, p, i);
}

void AutoFilter::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const auto t = fxdsp::transport (*this);
    const int type = (int) param ("type"), shape = (int) param ("shape");
    const float cutoff = param ("cutoff"), depth = param ("depth"), envAmt = param ("env"), stereo = param ("stereo"), mix = param ("mix");
    const float q = 0.5f + std::pow (param ("reso"), 2.0f) * 14.0f;
    const float drive = juce::Decibels::decibelsToGain (param ("drive"));
    const float atk = std::exp (-1.0f / (float) (sampleRate * param ("attack") * 0.001));
    const float rel = std::exp (-1.0f / (float) (sampleRate * param ("release") * 0.001));
    const bool sync = param ("sync") > 0.5f;
    const double beats = fxdsp::syncBeats ((int) param ("division"));
    const double inc = sync ? t.bpm / 60.0 / beats / sampleRate : param ("rate") / sampleRate;
    if (sync && t.playing) phase = t.ppq / beats;   // lock to the song

    const int nch = juce::jmin (2, buffer.getNumChannels()), n = buffer.getNumSamples();
    float* d[2] = { buffer.getWritePointer (0), buffer.getWritePointer (nch > 1 ? 1 : 0) };
    for (int i = 0; i < n; ++i)
    {
        const float lvl = nch > 1 ? juce::jmax (std::abs (d[0][i]), std::abs (d[1][i])) : std::abs (d[0][i]);
        env = lvl > env ? lvl + (env - lvl) * atk : lvl + (env - lvl) * rel;

        const double prev = phase;
        phase += inc;
        if (std::floor (phase) != std::floor (prev)) randHeld = rng.nextFloat() * 2.0f - 1.0f;
        if ((i & 7) == 0)
        {
            for (int ch = 0; ch < nch; ++ch)
            {
                float lfo = fxdsp::lfoShape (shape, phase + (ch == 1 ? stereo * 0.5 : 0.0), randHeld);
                if (shape == 6) { if (smooth < -0.99f) smooth = randHeld; smooth += (randHeld - smooth) * 0.02f; lfo = smooth; }
                const float oct = depth * 0.5f * lfo + envAmt * juce::jmin (1.0f, env * 2.0f);
                const float hz = cutoff * std::pow (2.0f, oct);
                f[ch][0].set (sampleRate, hz, q);
                f[ch][1].set (sampleRate, hz, type == 1 ? 0.7071f : q);
            }
        }
        for (int ch = 0; ch < nch; ++ch)
        {
            const float x = d[ch][i];
            const float xin = drive > 1.001f ? std::tanh (x * drive) : x;
            float bp, hp, y;
            const float lp = f[ch][0].tick (xin, bp, hp);
            switch (type)
            {
                case 1:  { float b2, h2; y = f[ch][1].tick (lp, b2, h2); break; }
                case 2:  y = bp * f[ch][0].k; break;   // unity-gain band-pass
                case 3:  y = hp; break;
                case 4:  y = lp + hp; break;
                default: y = lp; break;
            }
            d[ch][i] = x + (y - x) * mix;
        }
    }
    if (phase > 1.0e7) phase -= std::floor (phase);
}

// =====================================================================================================
//  Flanger
// =====================================================================================================
static Layout flangerLayout()
{
    Layout l;
    addFloat (l, "rate", "Rate", 0.02f, 10.0f, 0.25f, "Hz", 1.0f, 2);
    addBool (l, "sync", "Sync to Tempo", false);
    addChoice (l, "division", "Note", fxdsp::syncNames(), 2);
    addPercent (l, "depth", "Depth", 0.8f);
    addMs (l, "manual", "Delay", 0.5f, 10.0f, 4.0f);
    addFloat (l, "feedback", "Feedback", -95.0f, 95.0f, 50.0f, "%", 0.0f, 0);
    addPercent (l, "stereo", "Stereo", 0.25f);
    addPercent (l, "mix", "Mix", 0.5f);
    return l;
}
Flanger::Flanger() : BuiltinProcessor ("flanger", "Flanger", false, flangerLayout()) {}
void Flanger::prepareToPlay (double sr, int) { sampleRate = sr; for (auto& l : line) l.init ((int) (sr * 0.03)); fb[0] = fb[1] = 0; }

void Flanger::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const auto t = fxdsp::transport (*this);
    const bool sync = param ("sync") > 0.5f;
    const double beats = fxdsp::syncBeats ((int) param ("division"));
    const double inc = sync ? t.bpm / 60.0 / beats / sampleRate : param ("rate") / sampleRate;
    if (sync && t.playing) phase = t.ppq / beats;
    const float depth = param ("depth"), feedback = param ("feedback") * 0.01f, stereo = param ("stereo"), mix = param ("mix");
    const double minD = 0.12e-3 * sampleRate, maxD = param ("manual") * 1.0e-3 * sampleRate;
    const int nch = juce::jmin (2, buffer.getNumChannels()), n = buffer.getNumSamples();
    for (int i = 0; i < n; ++i)
    {
        for (int ch = 0; ch < nch; ++ch)
        {
            float* d = buffer.getWritePointer (ch);
            const double lfo01 = 0.5 + 0.5 * std::sin (twoPi * (phase + (ch == 1 ? stereo * 0.5 : 0.0)));
            const double delay = minD + (maxD - minD) * (1.0 - depth + depth * lfo01);
            const float x = d[i];
            line[ch].push (softClip (x + fb[ch] * feedback));
            const float wet = line[ch].read (juce::jmax (1.5, delay));
            fb[ch] = wet;
            d[i] = x * (1.0f - mix * 0.5f) + wet * mix;
        }
        phase += inc;
    }
    if (phase > 1.0e7) phase -= std::floor (phase);
}

// =====================================================================================================
//  Ring Modulator
// =====================================================================================================
static Layout ringLayout()
{
    Layout l;
    addHz (l, "freq", "Frequency", 1.0f, 5000.0f, 220.0f);
    addChoice (l, "shape", "Carrier", { "Sine", "Triangle", "Square" }, 0);
    addFloat (l, "lfo", "Sweep Depth", 0.0f, 4.0f, 0.0f, "oct", 0.0f, 2);
    addFloat (l, "lforate", "Sweep Rate", 0.02f, 10.0f, 0.3f, "Hz", 1.0f, 2);
    addPercent (l, "mix", "Mix", 0.6f);
    return l;
}
RingMod::RingMod() : BuiltinProcessor ("ringmod", "Ring Modulator", false, ringLayout()) {}

void RingMod::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const float base = param ("freq"), sweep = param ("lfo"), mix = param ("mix");
    const int shape = (int) param ("shape");
    const double lfoInc = param ("lforate") / sampleRate;
    const int nch = juce::jmin (2, buffer.getNumChannels()), n = buffer.getNumSamples();
    for (int i = 0; i < n; ++i)
    {
        const double hz = base * std::pow (2.0, sweep * std::sin (twoPi * lfo));
        carrier += hz / sampleRate; carrier -= std::floor (carrier);
        lfo += lfoInc; lfo -= std::floor (lfo);
        float c;
        if (shape == 1) c = (float) (carrier < 0.5 ? carrier * 4.0 - 1.0 : 3.0 - carrier * 4.0);
        else if (shape == 2) c = carrier < 0.5 ? 0.8f : -0.8f;
        else c = (float) std::sin (twoPi * carrier);
        for (int ch = 0; ch < nch; ++ch)
        {
            float* d = buffer.getWritePointer (ch);
            d[i] = d[i] + (d[i] * c - d[i]) * mix;
        }
    }
}

// =====================================================================================================
//  Grain Cloud
// =====================================================================================================
static Layout grainLayout()
{
    Layout l;
    addMs (l, "size", "Grain Size", 10.0f, 500.0f, 90.0f);
    addFloat (l, "density", "Density", 1.0f, 100.0f, 18.0f, "/s", 15.0f, 1);
    addMs (l, "position", "Position", 0.0f, 2000.0f, 120.0f);
    addMs (l, "spray", "Spray", 0.0f, 1500.0f, 250.0f);
    addFloat (l, "pitch", "Pitch", -24.0f, 24.0f, 0.0f, "st", 0.0f, 1);
    addFloat (l, "detune", "Random Detune", 0.0f, 100.0f, 8.0f, "ct", 0.0f, 0);
    addChoice (l, "jumps", "Random Jumps", { "None", "Octaves", "Fifths & Octaves" }, 0);
    addPercent (l, "reverse", "Reverse Grains", 0.25f);
    addPercent (l, "width", "Width", 0.8f);
    addPercent (l, "feedback", "Feedback", 0.2f);
    addBool (l, "freeze", "Freeze", false);
    addPercent (l, "mix", "Mix", 0.5f);
    return l;
}
GrainCloud::GrainCloud() : BuiltinProcessor ("grains", "Grain Cloud", false, grainLayout()) {}

void GrainCloud::prepareToPlay (double sr, int)
{
    sampleRate = sr;
    ring.setSize (2, (int) (sr * 4.0) + 16);
    ring.clear();
    writePos = 0; untilNext = 0;
    for (auto& g : grains) g.on = false;
    lastWet[0] = lastWet[1] = 0;
}

juce::StringArray GrainCloud::getProgramNames() { return { "Ambient Cloud", "Octave Shimmer", "Glitch Spray", "Reverse Haze", "Frozen Pad", "Underwater" }; }

void GrainCloud::loadProgram (int i)
{
    static const std::vector<Preset> p = {
        { { "size", 120 }, { "density", 20 }, { "position", 150 }, { "spray", 400 }, { "pitch", 0 }, { "detune", 10 }, { "jumps", 0 }, { "reverse", 0.2f }, { "feedback", 0.35f }, { "freeze", 0 }, { "mix", 0.45f } },
        { { "size", 160 }, { "density", 16 }, { "position", 100 }, { "spray", 300 }, { "pitch", 12 }, { "detune", 6 }, { "jumps", 1 }, { "reverse", 0 }, { "feedback", 0.5f }, { "freeze", 0 }, { "mix", 0.4f } },
        { { "size", 25 }, { "density", 60 }, { "position", 30 }, { "spray", 800 }, { "pitch", 0 }, { "detune", 40 }, { "jumps", 2 }, { "reverse", 0.5f }, { "feedback", 0.1f }, { "freeze", 0 }, { "mix", 0.6f } },
        { { "size", 300 }, { "density", 8 }, { "position", 400 }, { "spray", 600 }, { "pitch", 0 }, { "detune", 4 }, { "jumps", 0 }, { "reverse", 1 }, { "feedback", 0.4f }, { "freeze", 0 }, { "mix", 0.55f } },
        { { "size", 250 }, { "density", 40 }, { "position", 200 }, { "spray", 300 }, { "pitch", 0 }, { "detune", 12 }, { "jumps", 0 }, { "reverse", 0.5f }, { "feedback", 0 }, { "freeze", 1 }, { "mix", 1 } },
        { { "size", 200 }, { "density", 25 }, { "position", 250 }, { "spray", 500 }, { "pitch", -12 }, { "detune", 25 }, { "jumps", 0 }, { "reverse", 0.3f }, { "feedback", 0.3f }, { "freeze", 0 }, { "mix", 0.5f } },
    };
    applyPreset (*this, p, i);
}

void GrainCloud::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const int size = juce::jmax (8, (int) (param ("size") * 0.001 * sampleRate));
    const double density = param ("density");
    const double posS = param ("position") * 0.001 * sampleRate, sprayS = param ("spray") * 0.001 * sampleRate;
    const float pitch = param ("pitch"), detune = param ("detune"), reverse = param ("reverse"), width = param ("width");
    const int jumps = (int) param ("jumps");
    const float feedback = param ("feedback") * 0.9f, mix = param ("mix");
    const bool freeze = param ("freeze") > 0.5f;
    const int len = ring.getNumSamples();
    const float overlap = (float) (density * size / sampleRate);
    const float norm = 1.0f / std::sqrt (juce::jmax (1.0f, overlap));

    const int nch = juce::jmin (2, buffer.getNumChannels()), n = buffer.getNumSamples();
    float* d[2] = { buffer.getWritePointer (0), buffer.getWritePointer (nch > 1 ? 1 : 0) };
    const float* r0 = ring.getReadPointer (0);
    const float* r1 = ring.getReadPointer (1);
    auto readAt = [len] (const float* r, double pos)
    {
        pos -= std::floor (pos / len) * len;
        const int a = (int) pos; const float t = (float) (pos - a);
        return r[a] + (r[(a + 1) % len] - r[a]) * t;
    };

    for (int i = 0; i < n; ++i)
    {
        const float inL = d[0][i], inR = nch > 1 ? d[1][i] : inL;
        if (! freeze)
        {
            ring.setSample (0, writePos, softClip (inL + lastWet[0] * feedback));
            ring.setSample (1, writePos, softClip (inR + lastWet[1] * feedback));
            writePos = (writePos + 1) % len;
        }

        untilNext -= 1.0;
        if (untilNext <= 0.0)
        {
            untilNext += sampleRate / density * (0.6 + 0.8 * rng.nextDouble());
            for (auto& g : grains)
                if (! g.on)
                {
                    double semis = pitch + detune * 0.01 * (rng.nextDouble() * 2.0 - 1.0);
                    if (jumps > 0 && rng.nextFloat() < 0.35f)
                    {
                        static const int oct[] = { -12, 12 }, fifth[] = { -12, 7, 12, 19 };
                        semis += jumps == 1 ? oct[rng.nextInt (2)] : fifth[rng.nextInt (4)];
                    }
                    double rate = std::pow (2.0, semis / 12.0);
                    const bool rev = rng.nextFloat() < reverse;
                    const double lead = rate > 1.0 ? size * (rate - 1.0) : 0.0;
                    g.pos = writePos - (posS + rng.nextDouble() * sprayS + lead + 4.0);
                    if (rev) { g.pos += 0; rate = -rate; }
                    g.rate = rate; g.length = size; g.age = 0;
                    const float pan = (rng.nextFloat() * 2.0f - 1.0f) * width;
                    g.panL = std::sqrt (0.5f * (1.0f - pan)) * 1.414f;
                    g.panR = std::sqrt (0.5f * (1.0f + pan)) * 1.414f;
                    g.gain = norm * (0.7f + 0.3f * rng.nextFloat());
                    g.on = true;
                    break;
                }
        }

        float wl = 0, wr = 0;
        for (auto& g : grains)
        {
            if (! g.on) continue;
            const float w = 0.5f - 0.5f * (float) std::cos (twoPi * g.age / g.length);
            wl += readAt (r0, g.pos) * w * g.gain * g.panL;
            wr += readAt (r1, g.pos) * w * g.gain * g.panR;
            g.pos += g.rate;
            if (++g.age >= g.length) g.on = false;
        }
        lastWet[0] = wl; lastWet[1] = wr;
        d[0][i] = inL * (1.0f - mix) + wl * mix;
        if (nch > 1) d[1][i] = inR * (1.0f - mix) + wr * mix;
    }
}

// =====================================================================================================
//  Beat Repeat (stutter)
// =====================================================================================================
static juce::StringArray gridNames() { return { "1/4", "1/8", "1/8 T", "1/16", "1/16 T", "1/32", "1/64" }; }
static double gridBeats (int i) { static const double b[] = { 1, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125, 0.0625 }; return b[juce::jlimit (0, 6, i)]; }

static Layout repeatLayout()
{
    Layout l;
    addChoice (l, "interval", "Every", { "1 Beat", "2 Beats", "1 Bar", "2 Bars", "4 Bars" }, 2);
    addFloat (l, "offset", "Offset", 0.0f, 15.0f, 12.0f, "/16", 0.0f, 0);
    addPercent (l, "chance", "Chance", 0.5f);
    addChoice (l, "grid", "Repeat Size", gridNames(), 3);
    addChoice (l, "length", "Repeat For", { "1/2 Beat", "1 Beat", "2 Beats", "1 Bar" }, 1);
    addPercent (l, "gate", "Gate", 1.0f);
    addFloat (l, "pitch", "Pitch Drop", 0.0f, 12.0f, 0.0f, "st", 0.0f, 1);
    addPercent (l, "decay", "Fade Out", 0.0f);
    addChoice (l, "mode", "Mode", { "Replace", "Mix", "Repeats Only" }, 0);
    addBool (l, "hold", "Repeat Now (Hold)", false);
    return l;
}
BeatRepeat::BeatRepeat() : BuiltinProcessor ("beatrepeat", "Beat Repeat", false, repeatLayout()) {}

void BeatRepeat::prepareToPlay (double sr, int)
{
    sampleRate = sr;
    ring.setSize (2, (int) (sr * 10.0));
    ring.clear();
    writePos = 0; repeating = false; lastInterval = -1; mixSmooth = 0;
}

juce::StringArray BeatRepeat::getProgramNames() { return { "Glitch Fill", "Stutter 1/32", "Tape Stop Drop", "Triplet Roll", "Random Chops" }; }

void BeatRepeat::loadProgram (int i)
{
    static const std::vector<Preset> p = {
        { { "interval", 2 }, { "offset", 12 }, { "chance", 0.6f }, { "grid", 3 }, { "length", 1 }, { "gate", 1 }, { "pitch", 0 }, { "decay", 0 }, { "mode", 0 } },
        { { "interval", 3 }, { "offset", 14 }, { "chance", 1 }, { "grid", 5 }, { "length", 0 }, { "gate", 0.8f }, { "pitch", 0 }, { "decay", 0.1f }, { "mode", 0 } },
        { { "interval", 3 }, { "offset", 12 }, { "chance", 1 }, { "grid", 1 }, { "length", 1 }, { "gate", 1 }, { "pitch", 5 }, { "decay", 0.25f }, { "mode", 0 } },
        { { "interval", 2 }, { "offset", 8 }, { "chance", 0.5f }, { "grid", 4 }, { "length", 2 }, { "gate", 0.7f }, { "pitch", 0 }, { "decay", 0 }, { "mode", 0 } },
        { { "interval", 0 }, { "offset", 2 }, { "chance", 0.25f }, { "grid", 3 }, { "length", 0 }, { "gate", 0.6f }, { "pitch", 0 }, { "decay", 0.1f }, { "mode", 0 } },
    };
    applyPreset (*this, p, i);
}

void BeatRepeat::start (int sliceSamples, int totalSamples)
{
    repeating = true;
    startPos = writePos;
    slice = juce::jmax (16, sliceSamples);
    total = totalSamples;
    elapsed = 0;
}

void BeatRepeat::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const auto t = fxdsp::transport (*this);
    static const double intervals[] = { 1, 2, -1, -2, -4 };   // negative = bars
    const int ii = (int) param ("interval");
    const double interval = intervals[ii] > 0 ? intervals[ii] : -intervals[ii] * t.bpb;
    const double offset = param ("offset") * 0.25;
    const float chance = param ("chance"), gate = param ("gate"), pitch = param ("pitch"), decay = param ("decay");
    static const double lengths[] = { 0.5, 1, 2, -1 };
    const double lenBeats = lengths[(int) param ("length")] > 0 ? lengths[(int) param ("length")] : t.bpb;
    const double samplesPerBeat = 60.0 / t.bpm * sampleRate;
    const int sliceS = (int) (gridBeats ((int) param ("grid")) * samplesPerBeat);
    const int mode = (int) param ("mode");
    const bool hold = param ("hold") > 0.5f;
    const double beatsPerSample = 1.0 / samplesPerBeat;
    const int len = ring.getNumSamples();

    if (hold && ! manualWas) start (sliceS, std::numeric_limits<int>::max());
    if (! hold && manualWas) repeating = false;
    manualWas = hold;

    const int nch = juce::jmin (2, buffer.getNumChannels()), n = buffer.getNumSamples();
    float* d[2] = { buffer.getWritePointer (0), buffer.getWritePointer (nch > 1 ? 1 : 0) };
    if (! t.playing) lastInterval = -1;

    for (int i = 0; i < n; ++i)
    {
        if (t.playing && ! hold && chance > 0.0f)
        {
            const double ppq = t.ppq + i * beatsPerSample;
            const auto idx = (juce::int64) std::floor ((ppq - offset) / interval);
            if (lastInterval >= 0 && idx != lastInterval && rng.nextFloat() < chance)
                start (sliceS, (int) (lenBeats * samplesPerBeat));
            lastInterval = idx;
        }

        const float inL = d[0][i], inR = nch > 1 ? d[1][i] : inL;
        ring.setSample (0, writePos, inL);
        ring.setSample (1, writePos, inR);
        writePos = (writePos + 1) % len;

        float rl = inL, rr = inR, target = 0.0f;
        if (repeating)
        {
            const int k = elapsed / slice;
            const int within = elapsed - k * slice;
            if (k > 0)
            {
                const double rate = std::pow (2.0, -pitch * k / 12.0);
                const double pos = within * rate;
                const int gated = (int) (slice * gate);
                float g = std::pow (1.0f - decay, (float) k);
                const float fade = (float) juce::jmin (1.0, within / 48.0, (gated - within) / 48.0);
                g *= within < gated ? juce::jlimit (0.0f, 1.0f, fade) : 0.0f;
                const double p = startPos + pos;
                const int a = ((int) p) % len; const float fr = (float) (p - std::floor (p));
                const int b = (a + 1) % len;
                rl = (ring.getSample (0, a) + (ring.getSample (0, b) - ring.getSample (0, a)) * fr) * g;
                rr = (ring.getSample (1, a) + (ring.getSample (1, b) - ring.getSample (1, a)) * fr) * g;
                target = 1.0f;
            }
            if (++elapsed >= total || elapsed >= len - 16) repeating = false;
        }
        mixSmooth += (target - mixSmooth) * 0.01f;
        float oL, oR;
        if (mode == 1)      { oL = inL + rl * mixSmooth; oR = inR + rr * mixSmooth; }
        else if (mode == 2) { oL = rl * mixSmooth; oR = rr * mixSmooth; }
        else                { oL = inL + (rl - inL) * mixSmooth; oR = inR + (rr - inR) * mixSmooth; }
        d[0][i] = oL;
        if (nch > 1) d[1][i] = oR;
    }
}

// =====================================================================================================
//  Tape Warble
// =====================================================================================================
static Layout tapeLayout()
{
    Layout l;
    addPercent (l, "wow", "Wow", 0.35f);
    addFloat (l, "wowrate", "Wow Speed", 0.1f, 4.0f, 0.55f, "Hz", 1.0f, 2);
    addPercent (l, "flutter", "Flutter", 0.25f);
    addPercent (l, "saturation", "Saturation", 0.3f);
    addPercent (l, "age", "Age (Tone)", 0.35f);
    addPercent (l, "hiss", "Hiss", 0.15f);
    addPercent (l, "dropouts", "Dropouts", 0.0f);
    addPercent (l, "mix", "Mix", 1.0f);
    return l;
}
TapeWarble::TapeWarble() : BuiltinProcessor ("tape", "Tape Warble", false, tapeLayout()) {}

void TapeWarble::prepareToPlay (double sr, int)
{
    sampleRate = sr;
    for (auto& l : line) l.init ((int) (sr * 0.06));
    lp[0] = lp[1] = lp2[0] = lp2[1] = 0; drift = driftTarget = 0; dropout = dropTarget = 1;
}

juce::StringArray TapeWarble::getProgramNames() { return { "Cassette Dream", "Seasick VHS", "Old Reel-to-Reel", "Warped Record", "Subtle Wobble", "Worn Out Walkman" }; }

void TapeWarble::loadProgram (int i)
{
    static const std::vector<Preset> p = {
        { { "wow", 0.35f }, { "wowrate", 0.55f }, { "flutter", 0.25f }, { "saturation", 0.35f }, { "age", 0.35f }, { "hiss", 0.15f }, { "dropouts", 0 }, { "mix", 1 } },
        { { "wow", 0.8f }, { "wowrate", 0.35f }, { "flutter", 0.4f }, { "saturation", 0.3f }, { "age", 0.55f }, { "hiss", 0.25f }, { "dropouts", 0.2f }, { "mix", 1 } },
        { { "wow", 0.15f }, { "wowrate", 0.8f }, { "flutter", 0.15f }, { "saturation", 0.6f }, { "age", 0.2f }, { "hiss", 0.1f }, { "dropouts", 0 }, { "mix", 1 } },
        { { "wow", 1.0f }, { "wowrate", 0.55f }, { "flutter", 0.1f }, { "saturation", 0.2f }, { "age", 0.4f }, { "hiss", 0.05f }, { "dropouts", 0 }, { "mix", 1 } },
        { { "wow", 0.12f }, { "wowrate", 0.5f }, { "flutter", 0.08f }, { "saturation", 0.15f }, { "age", 0.1f }, { "hiss", 0.0f }, { "dropouts", 0 }, { "mix", 1 } },
        { { "wow", 0.5f }, { "wowrate", 1.2f }, { "flutter", 0.6f }, { "saturation", 0.5f }, { "age", 0.75f }, { "hiss", 0.4f }, { "dropouts", 0.5f }, { "mix", 1 } },
    };
    applyPreset (*this, p, i);
}

void TapeWarble::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const float wow = param ("wow"), flutter = param ("flutter"), sat = param ("saturation"), age = param ("age");
    const float hiss = param ("hiss") * 0.012f, drops = param ("dropouts"), mix = param ("mix");
    const double wowInc = param ("wowrate") / sampleRate, flutInc = 7.3 / sampleRate;
    const double base = 0.012 * sampleRate;
    const double wowDepth = wow * 0.0045 * sampleRate, flutDepth = flutter * 0.00025 * sampleRate;
    const float drive = 1.0f + sat * 5.0f, driveNorm = 1.0f / std::tanh (drive);
    const float cutoff = 19000.0f * std::pow (2.0f, -age * 2.6f);
    const float a = 1.0f - std::exp (-(float) twoPi * cutoff / (float) sampleRate);
    const float hissA = 1.0f - std::exp (-(float) twoPi * 5000.0f / (float) sampleRate);
    const int nch = juce::jmin (2, buffer.getNumChannels()), n = buffer.getNumSamples();

    for (int i = 0; i < n; ++i)
    {
        if (--driftCounter <= 0)
        {
            driftCounter = (int) (sampleRate * 0.25);
            driftTarget = rng.nextFloat() * 2.0f - 1.0f;
            if (drops > 0 && rng.nextFloat() < drops * 0.25f) dropTarget = 0.35f + 0.4f * rng.nextFloat();
            else dropTarget = 1.0f;
        }
        drift += (driftTarget - drift) * 0.00005f;
        dropout += (dropTarget - dropout) * 0.0008f;
        wowPhase += wowInc * (1.0 + 0.3 * drift); flutPhase += flutInc;
        const double delay = base + wowDepth * (std::sin (twoPi * wowPhase) * 0.75 + drift * 0.5)
                                  + flutDepth * (std::sin (twoPi * flutPhase) + 0.5 * std::sin (twoPi * flutPhase * 2.7));
        hissLp += (rng.nextFloat() * 2.0f - 1.0f - hissLp) * hissA;
        for (int ch = 0; ch < nch; ++ch)
        {
            float* d = buffer.getWritePointer (ch);
            const float x = d[i];
            line[ch].push (x);
            float y = line[ch].read (delay + (ch == 1 ? 0.00003 * sampleRate * wow : 0.0));
            if (sat > 0.001f) y = std::tanh (y * drive) * driveNorm;
            lp[ch] += (y - lp[ch]) * a;
            lp2[ch] += (lp[ch] - lp2[ch]) * a;
            y = lp2[ch] * dropout + hissLp * hiss;
            d[i] = x + (y - x) * mix;
        }
    }
    wowPhase -= std::floor (wowPhase); flutPhase -= std::floor (flutPhase);
}

// =====================================================================================================
//  Pitch Shifter / Harmonizer
// =====================================================================================================
static Layout shiftLayout()
{
    Layout l;
    addFloat (l, "semis", "Pitch", -24.0f, 24.0f, 12.0f, "st", 0.0f, 0);
    addFloat (l, "fine", "Fine", -100.0f, 100.0f, 0.0f, "ct", 0.0f, 0);
    addFloat (l, "semis2", "Voice 2 Pitch", -24.0f, 24.0f, -12.0f, "st", 0.0f, 0);
    addPercent (l, "level2", "Voice 2 Level", 0.0f);
    addFloat (l, "spread", "Stereo Detune", 0.0f, 30.0f, 0.0f, "ct", 0.0f, 0);
    addMs (l, "window", "Grain", 20.0f, 120.0f, 50.0f);
    addPercent (l, "mix", "Mix", 0.5f);
    return l;
}
PitchShifter::PitchShifter() : BuiltinProcessor ("pitchshift", "Pitch Shifter", false, shiftLayout()) {}

void PitchShifter::prepareToPlay (double sr, int)
{
    sampleRate = sr;
    for (int ch = 0; ch < 2; ++ch) { a[ch].init (sr, 130.0); b[ch].init (sr, 130.0); }
}

void PitchShifter::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const double win = param ("window") * 0.001 * sampleRate;
    for (int ch = 0; ch < 2; ++ch) { a[ch].window = win; b[ch].window = win; }
    const float spread = param ("spread"), level2 = param ("level2"), mix = param ("mix");
    const double s1 = param ("semis") + param ("fine") * 0.01, s2 = param ("semis2");
    const int nch = juce::jmin (2, buffer.getNumChannels()), n = buffer.getNumSamples();
    for (int ch = 0; ch < nch; ++ch)
    {
        const double det = (ch == 0 ? -1.0 : 1.0) * spread * 0.01;
        const double r1 = std::pow (2.0, (s1 + det) / 12.0), r2 = std::pow (2.0, (s2 - det) / 12.0);
        float* d = buffer.getWritePointer (ch);
        for (int i = 0; i < n; ++i)
        {
            const float x = d[i];
            float y = a[ch].process (x, r1);
            if (level2 > 0.001f) y += b[ch].process (x, r2) * level2;
            d[i] = x * (1.0f - mix) + y * mix;
        }
    }
}

// =====================================================================================================
//  Shimmer Reverb
// =====================================================================================================
static Layout shimmerLayout()
{
    Layout l;
    addChoice (l, "interval", "Shimmer Pitch", { "+1 Octave", "+5th", "+1 Oct + 5th", "+2 Octaves", "-1 Octave" }, 0);
    addPercent (l, "amount", "Shimmer", 0.5f);
    addPercent (l, "size", "Size", 0.85f);
    addPercent (l, "damp", "Damping", 0.4f);
    addHz (l, "tone", "Shimmer Tone", 1000.0f, 16000.0f, 6000.0f);
    addPercent (l, "width", "Width", 1.0f);
    addPercent (l, "mix", "Mix", 0.35f);
    return l;
}
ShimmerVerb::ShimmerVerb() : BuiltinProcessor ("shimmer", "Shimmer Reverb", false, shimmerLayout()) {}

void ShimmerVerb::prepareToPlay (double sr, int block)
{
    sampleRate = sr;
    reverb.setSampleRate (sr);
    reverb.reset();
    wet.setSize (2, juce::jmax (block, 512));
    for (int ch = 0; ch < 2; ++ch) { shift[ch].init (sr, 80.0); fbRing[ch].assign (1 << 16, 0.0f); tone[ch].reset(); }
    fbRead = fbWrite = 0;
}

void ShimmerVerb::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    static const double semis[] = { 12, 7, 19, 24, -12 };
    const double ratio = std::pow (2.0, semis[(int) param ("interval")] / 12.0);
    const float amount = param ("amount") * 0.85f, mix = param ("mix");
    juce::Reverb::Parameters rp;
    rp.roomSize = 0.5f + param ("size") * 0.49f;
    rp.damping = param ("damp");
    rp.width = param ("width");
    rp.wetLevel = 1.0f; rp.dryLevel = 0.0f; rp.freezeMode = 0.0f;
    reverb.setParameters (rp);
    for (auto& t : tone) t.set (sampleRate, param ("tone"), 0.7071f);

    const int nch = juce::jmin (2, buffer.getNumChannels()), n = buffer.getNumSamples();
    if (wet.getNumSamples() < n) wet.setSize (2, n, false, false, true);
    const int mask = (1 << 16) - 1;
    for (int ch = 0; ch < 2; ++ch)
    {
        const float* src = buffer.getReadPointer (juce::jmin (ch, nch - 1));
        float* w = wet.getWritePointer (ch);
        for (int i = 0; i < n; ++i)
        {
            auto& slot = fbRing[ch][(size_t) ((fbRead + i) & mask)];
            w[i] = src[i] * 0.5f + slot * amount;
            slot = 0.0f;
        }
    }
    fbRead = (fbRead + n) & mask;
    reverb.processStereo (wet.getWritePointer (0), wet.getWritePointer (1), n);
    for (int ch = 0; ch < 2; ++ch)
    {
        const float* w = wet.getReadPointer (ch);
        for (int i = 0; i < n; ++i)
        {
            float bp, hp;
            const float s = tone[ch].tick (shift[ch].process (w[i], ratio), bp, hp);
            fbRing[ch][(size_t) ((fbWrite + i) & mask)] = std::tanh (s);
        }
    }
    fbWrite = (fbWrite + n) & mask;
    for (int ch = 0; ch < nch; ++ch)
    {
        float* d = buffer.getWritePointer (ch);
        const float* w = wet.getReadPointer (ch);
        for (int i = 0; i < n; ++i) d[i] = d[i] * (1.0f - mix * 0.5f) + w[i] * mix;
    }
}

// =====================================================================================================
//  Stereo Width
// =====================================================================================================
static Layout widthLayout()
{
    Layout l;
    addFloat (l, "width", "Width", 0.0f, 200.0f, 130.0f, "%", 0.0f, 0);
    addMs (l, "haas", "Haas Delay", 0.0f, 30.0f, 0.0f);
    addHz (l, "bassmono", "Mono Bass Below", 20.0f, 500.0f, 20.0f);
    addDb (l, "gain", "Output", -12.0f, 12.0f, 0.0f);
    return l;
}
StereoWidth::StereoWidth() : BuiltinProcessor ("width", "Stereo Width", false, widthLayout()) {}
void StereoWidth::prepareToPlay (double sr, int) { sampleRate = sr; haas.init ((int) (sr * 0.05)); bass.reset(); }

void StereoWidth::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    if (buffer.getNumChannels() < 2) return;
    const float width = param ("width") * 0.01f, gain = juce::Decibels::decibelsToGain (param ("gain"));
    const double haasS = param ("haas") * 0.001 * sampleRate;
    const float bassHz = param ("bassmono");
    bass.set (sampleRate, bassHz, 0.7071f);
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const float m = (L[i] + R[i]) * 0.5f;
        float s = (L[i] - R[i]) * 0.5f;
        if (bassHz > 21.0f) { float bp, hp; bass.tick (s, bp, hp); s = hp; }
        s *= width;
        float l = m + s, r = m - s;
        haas.push (r);
        if (haasS > 1.0) r = haas.read (haasS);
        L[i] = l * gain; R[i] = r * gain;
    }
}

// =====================================================================================================
//  De-Esser
// =====================================================================================================
static Layout deessLayout()
{
    Layout l;
    addHz (l, "freq", "Frequency", 2000.0f, 12000.0f, 6000.0f);
    addDb (l, "threshold", "Threshold", -60.0f, 0.0f, -30.0f);
    addDb (l, "range", "Max Reduction", 0.0f, 24.0f, 10.0f);
    addBool (l, "listen", "Listen to Esses", false);
    return l;
}
DeEsser::DeEsser() : BuiltinProcessor ("deesser", "De-Esser", false, deessLayout()) {}
void DeEsser::prepareToPlay (double sr, int) { sampleRate = sr; env = 0; gain = 1; for (auto& h : hp) h.reset(); }

void DeEsser::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    for (auto& h : hp) h.set (sampleRate, param ("freq"), 0.7071f);
    const float thr = param ("threshold"), range = param ("range");
    const bool listen = param ("listen") > 0.5f;
    const float atk = std::exp (-1.0f / (float) (sampleRate * 0.0008)), rel = std::exp (-1.0f / (float) (sampleRate * 0.06));
    const float gSmooth = 1.0f - std::exp (-1.0f / (float) (sampleRate * 0.002));
    const int nch = juce::jmin (2, buffer.getNumChannels()), n = buffer.getNumSamples();
    float maxRed = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        float hpv[2] = {}, lvl = 0.0f;
        for (int ch = 0; ch < nch; ++ch)
        {
            float bp;
            hp[ch].tick (buffer.getSample (ch, i), bp, hpv[ch]);
            lvl = juce::jmax (lvl, std::abs (hpv[ch]));
        }
        env = lvl > env ? lvl + (env - lvl) * atk : lvl + (env - lvl) * rel;
        const float over = juce::Decibels::gainToDecibels (env, -100.0f) - thr;
        const float red = over > 0.0f ? juce::jmin (range, over * 0.8f) : 0.0f;
        maxRed = juce::jmax (maxRed, red);
        gain += (juce::Decibels::decibelsToGain (-red) - gain) * gSmooth;
        for (int ch = 0; ch < nch; ++ch)
        {
            float* d = buffer.getWritePointer (ch);
            d[i] = listen ? hpv[ch] * gain : d[i] - hpv[ch] * (1.0f - gain);
        }
    }
    reductionDb = maxRed;
}

// =====================================================================================================
//  Vocoder
// =====================================================================================================
static Layout vocoderLayout()
{
    Layout l;
    addChoice (l, "mode", "Carrier", { "Robot (built-in buzz)", "This Track (voice on side-chain)" }, 0);
    addFloat (l, "note", "Robot Note", 24.0f, 72.0f, 45.0f, {}, 0.0f, 0);
    addChoice (l, "chord", "Robot Chord", { "Single", "Octaves", "Power (5th)", "Minor", "Major", "Sus 4", "Minor 7" }, 1);
    addFloat (l, "q", "Band Sharpness", 2.0f, 20.0f, 7.0f, {}, 0.0f, 1);
    addFloat (l, "shift", "Formant Shift", -12.0f, 12.0f, 0.0f, "st", 0.0f, 1);
    addMs (l, "release", "Release", 5.0f, 300.0f, 35.0f);
    addPercent (l, "sibilance", "Sibilance", 0.35f);
    addDb (l, "output", "Output", -12.0f, 18.0f, 6.0f);
    addPercent (l, "mix", "Mix", 1.0f);
    return l;
}
Vocoder::Vocoder() : BuiltinProcessor ("vocoder", "Vocoder", false, vocoderLayout()) {}

void Vocoder::prepareToPlay (double sr, int)
{
    sampleRate = sr; bandsBuilt = -1;
    for (auto& e : env) e = 0;
    for (auto& e : envC) e = 0;
}

void Vocoder::buildBands (float q, float shift)
{
    for (int b = 0; b < numBands; ++b)
    {
        const float hz = 100.0f * std::pow (8000.0f / 100.0f, (float) b / (numBands - 1));
        for (int s = 0; s < 2; ++s)
        {
            mod[b][s].set (sampleRate, hz, q);
            car[b][s].set (sampleRate, hz * std::pow (2.0f, shift / 12.0f), q);
        }
    }
    sib.set (sampleRate, 6000.0f, 0.7071f);
}

void Vocoder::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const float q = param ("q"), shift = param ("shift");
    const int key = (int) (q * 100) * 1000 + (int) (shift * 10 + 500);
    if (key != bandsBuilt) { buildBands (q, shift); bandsBuilt = key; }

    const bool robot = param ("mode") < 0.5f;
    const float rel = std::exp (-1.0f / (float) (sampleRate * param ("release") * 0.001));
    const float atk = std::exp (-1.0f / (float) (sampleRate * 0.002));
    const float relC = std::exp (-1.0f / (float) (sampleRate * 0.02));
    const float sibAmt = param ("sibilance"), mix = param ("mix");
    const float outGain = juce::Decibels::decibelsToGain (param ("output"));
    static const std::vector<std::vector<int>> chords = { { 0 }, { 0, 12 }, { 0, 7, 12 }, { 0, 3, 7 }, { 0, 4, 7 }, { 0, 5, 7 }, { 0, 3, 7, 10 } };
    const auto& chord = chords[(size_t) juce::jlimit (0, 6, (int) param ("chord"))];
    const double baseHz = 440.0 * std::pow (2.0, (param ("note") - 69.0) / 12.0);

    const int nch = juce::jmin (2, buffer.getNumChannels()), n = buffer.getNumSamples();
    const auto* sc = sidechainInput;
    const bool haveSc = sc != nullptr && sc->getNumSamples() >= n && sc->getNumChannels() > 0;
    float* d[2] = { buffer.getWritePointer (0), buffer.getWritePointer (nch > 1 ? 1 : 0) };

    for (int i = 0; i < n; ++i)
    {
        const float in = nch > 1 ? (d[0][i] + d[1][i]) * 0.5f : d[0][i];
        float modulator, carrier;
        if (robot)
        {
            modulator = in;
            float s = 0.0f;
            for (size_t c = 0; c < chord.size(); ++c)
            {
                const double inc = baseHz * std::pow (2.0, chord[c] / 12.0) / sampleRate;
                double& ph = phases[c];
                ph += inc; ph -= std::floor (ph);
                // polyBLEP saw
                double v = 2.0 * ph - 1.0;
                if (ph < inc) { const double t = ph / inc; v -= t + t - t * t - 1.0; }
                else if (ph > 1.0 - inc) { const double t = (ph - 1.0) / inc; v -= t * t + t + t + 1.0; }
                s += (float) v;
            }
            carrier = s / std::sqrt ((float) chord.size()) + (rng.nextFloat() * 2.0f - 1.0f) * 0.05f;
        }
        else
        {
            modulator = haveSc ? (sc->getNumChannels() > 1 ? (sc->getSample (0, i) + sc->getSample (1, i)) * 0.5f : sc->getSample (0, i)) : 0.0f;
            carrier = in;
        }

        float out = 0.0f;
        for (int b = 0; b < numBands; ++b)
        {
            float bp, hp, bp2;
            mod[b][0].tick (modulator, bp, hp);
            mod[b][1].tick (bp * mod[b][0].k, bp2, hp);
            bp2 *= mod[b][1].k;   // unity gain at the band centre
            const float m = std::abs (bp2);
            env[b] = m > env[b] ? m + (env[b] - m) * atk : m + (env[b] - m) * rel;

            car[b][0].tick (carrier, bp, hp);
            car[b][1].tick (bp * car[b][0].k, bp2, hp);
            bp2 *= car[b][1].k;
            const float c = std::abs (bp2);
            envC[b] = c > envC[b] ? c + (envC[b] - c) * atk : c + (envC[b] - c) * relC;
            out += bp2 * env[b] / (envC[b] + 1.0e-4f);
        }
        // let "s" and "t" sounds through so words stay clear
        float sbp, shp;
        sib.tick (modulator, sbp, shp);
        out = out * 0.5f + shp * sibAmt;
        out *= outGain;

        for (int ch = 0; ch < nch; ++ch)
            d[ch][i] = d[ch][i] * (1.0f - mix) + out * mix;
    }
}

} // namespace wis::daw
