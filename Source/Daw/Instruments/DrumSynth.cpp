#include "DrumSynth.h"

namespace wis::daw
{

static constexpr float twoPi = juce::MathConstants<float>::twoPi;

juce::String DrumSynth::voiceName (Voice v)
{
    static const char* names[] = { "Kick", "Snare", "Rim", "Clap", "Closed Hat", "Pedal Hat", "Open Hat", "Crash", "Ride",
                                   "Low Tom", "Mid Tom", "High Tom", "Cowbell", "Clave", "Maracas", "Tambourine",
                                   "High Conga", "Low Conga", "High Bongo", "Low Bongo" };
    return names[(int) v];
}

int DrumSynth::voiceForNote (int note)
{
    switch (note)
    {
        case 35: case 36: return kick;
        case 37: return rim;
        case 38: case 40: return snare;
        case 39: return clap;
        case 41: case 43: return lowTom;
        case 45: case 47: return midTom;
        case 48: case 50: return highTom;
        case 42: return closedHat;
        case 44: return pedalHat;
        case 46: return openHat;
        case 49: case 52: case 55: case 57: return crash;
        case 51: case 53: case 59: return ride;
        case 54: return tambourine;
        case 56: return cowbell;
        case 60: return highBongo;
        case 61: return lowBongo;
        case 62: case 63: return highConga;
        case 64: return lowConga;
        case 69: case 70: case 82: return maracas;
        case 75: case 76: case 77: return clave;
        default: return -1;
    }
}

int DrumSynth::noteForVoice (Voice v)
{
    static const int notes[] = { 36, 38, 37, 39, 42, 44, 46, 49, 51, 41, 45, 48, 56, 75, 70, 54, 63, 64, 60, 61 };
    return notes[(int) v];
}

void DrumSynth::prepare (double sampleRate)
{
    sr = sampleRate;
    allOff();
}

void DrumSynth::trigger (int note, float velocity)
{
    const int v = voiceForNote (note);
    if (v >= 0) trigger ((Voice) v, velocity);
}

void DrumSynth::trigger (Voice v, float velocity)
{
    // hats choke each other
    if (v == closedHat || v == pedalHat)
        for (auto& a : voices) if (a.active && a.type == openHat) a.active = false;
    // retriggering the same voice restarts it (like the analogue circuits)
    Active* slot = nullptr;
    for (auto& a : voices) if (a.active && a.type == v) { slot = &a; break; }
    if (slot == nullptr) for (auto& a : voices) if (! a.active) { slot = &a; break; }
    if (slot == nullptr)
    {
        slot = &voices[0];
        for (auto& a : voices) if (a.t > slot->t) slot = &a;   // steal the oldest
    }
    *slot = Active();
    slot->active = true;
    slot->type = v;
    slot->vel = juce::jlimit (0.05f, 1.0f, velocity);
    setupVoice (*slot);
}

void DrumSynth::setupVoice (Active& a)
{
    const float tuneF = std::pow (2.0f, tune / 12.0f);
    const float bright = std::pow (2.0f, brightness);
    const bool k808 = kit == eightOhEight, cr = kit == rhythmUnit, home = kit == homeKeyboard, toy = kit == toyBox;
    a.gain = 1.0f;
    a.pan = 0.0f;
    switch (a.type)
    {
        case kick:
            a.pitch = (k808 ? 47.0f : cr ? 60.0f : home ? 68.0f : 82.0f) * tuneF;
            a.decay = (k808 ? 0.42f : cr ? 0.14f : home ? 0.10f : 0.07f);
            a.gain = 1.1f;
            break;
        case snare:
            a.pitch = (k808 ? 185.0f : cr ? 230.0f : home ? 240.0f : 300.0f) * tuneF;
            a.decay = k808 ? 0.17f : cr ? 0.12f : home ? 0.09f : 0.07f;
            a.f1.set ((k808 ? 1800.0f : cr ? 2400.0f : 3200.0f) * bright, 0.7f, sr);
            a.gain = 0.7f;
            break;
        case rim:
            a.pitch = (k808 ? 1700.0f : cr ? 1200.0f : 1500.0f) * tuneF;
            a.decay = 0.012f;
            a.f1.set (a.pitch * 1.4f, 3.0f, sr);
            a.gain = 0.55f; a.pan = -0.15f;
            break;
        case clap:
            a.decay = k808 ? 0.11f : 0.08f;
            a.f1.set (1150.0f * bright, 1.4f, sr);
            a.gain = 0.9f; a.pan = 0.1f;
            break;
        case closedHat: case pedalHat: case openHat:
            a.pitch = (k808 ? 40.0f : cr ? 52.0f : 60.0f) * tuneF;
            a.decay = a.type == closedHat ? (k808 ? 0.045f : home ? 0.03f : 0.04f)
                    : a.type == pedalHat ? 0.07f : (k808 ? 0.42f : home ? 0.24f : 0.32f);
            a.f1.set ((toy ? 6000.0f : 9500.0f) * bright, 1.0f, sr);
            a.f2.set (6500.0f * juce::jmin (1.5f, bright), 0.7f, sr);
            a.gain = a.type == pedalHat ? 0.25f : 0.38f; a.pan = 0.22f;
            break;
        case crash:
            a.pitch = 46.0f * tuneF;
            a.decay = home ? 0.7f : toy ? 0.4f : 1.3f;
            a.f1.set (6500.0f * bright, 0.8f, sr);
            a.f2.set (3800.0f, 0.7f, sr);
            a.gain = 0.42f; a.pan = -0.3f;
            break;
        case ride:
            a.pitch = 70.0f * tuneF;
            a.decay = home ? 0.8f : 1.5f;
            a.f1.set (4200.0f * bright, 2.2f, sr);
            a.f2.set (2500.0f, 0.7f, sr);
            a.gain = 0.3f; a.pan = 0.3f;
            break;
        case lowTom: case midTom: case highTom:
        {
            const float base = k808 ? 85.0f : cr ? 110.0f : home ? 120.0f : 160.0f;
            const float mul = a.type == lowTom ? 1.0f : a.type == midTom ? 1.38f : 1.9f;
            a.pitch = base * mul * tuneF;
            a.decay = k808 ? 0.38f : home ? 0.2f : 0.26f;
            a.gain = 0.85f; a.pan = a.type == lowTom ? 0.3f : a.type == midTom ? 0.0f : -0.3f;
            break;
        }
        case cowbell:
            a.pitch = 540.0f * tuneF;
            a.decay = 0.28f;
            a.f1.set (2600.0f * bright, 3.0f, sr);
            a.gain = 0.4f; a.pan = -0.2f;
            break;
        case clave:
            a.pitch = (k808 ? 2500.0f : cr ? 2100.0f : 2300.0f) * tuneF;
            a.decay = 0.035f;
            a.gain = 0.5f; a.pan = 0.25f;
            break;
        case maracas:
            a.decay = cr ? 0.045f : 0.03f;
            a.f1.set (6000.0f * bright, 0.8f, sr);
            a.gain = 0.35f; a.pan = 0.35f;
            break;
        case tambourine:
            a.pitch = 95.0f * tuneF;
            a.decay = 0.22f;
            a.f1.set (8000.0f * bright, 1.2f, sr);
            a.gain = 0.3f; a.pan = -0.35f;
            break;
        case highConga: case lowConga: case highBongo: case lowBongo:
        {
            const float f = a.type == highConga ? 330.0f : a.type == lowConga ? 220.0f : a.type == highBongo ? 490.0f : 390.0f;
            a.pitch = f * tuneF;
            a.decay = a.type == highConga || a.type == lowConga ? 0.17f : 0.1f;
            a.gain = 0.6f; a.pan = (a.type == highConga || a.type == highBongo) ? -0.25f : 0.25f;
            break;
        }
        default: break;
    }
    a.decay *= decayMul;
}

float DrumSynth::renderVoice (Active& a)
{
    const float t = a.t;
    const float dt = 1.0f / (float) sr;
    a.t += dt;
    float out = 0.0f;
    float lp, bp, hp;

    auto env = [] (float time, float tau) { return std::exp (-time / juce::jmax (0.0005f, tau)); };
    auto osc = [&] (int i, float freq)
    {
        a.phase[i] += freq * dt;
        a.phase[i] -= std::floor (a.phase[i]);
        return std::sin (twoPi * a.phase[i]);
    };
    auto square = [&] (int i, float freq)
    {
        a.phase[i] += freq * dt;
        a.phase[i] -= std::floor (a.phase[i]);
        return a.phase[i] < 0.5f ? 1.0f : -1.0f;
    };
    auto metal = [&] (float base)
    {
        // six detuned square waves (the classic analogue cymbal/hat circuit)
        static const float ratios[] = { 5.13f, 7.61f, 9.24f, 13.07f, 13.5f, 20.0f };
        float m = 0.0f;
        for (int i = 0; i < 6; ++i) m += square (i, base * ratios[i]);
        return m * (1.0f / 6.0f);
    };

    switch (a.type)
    {
        case kick:
        {
            const float bend = kit == eightOhEight ? 2.6f : kit == rhythmUnit ? 1.3f : 1.0f;
            const float f = a.pitch * (1.0f + bend * env (t, 0.012f));
            out = osc (0, f) * env (t, a.decay);
            out += noise() * env (t, 0.0018f) * 0.25f;                  // beater click
            if (kit == toyBox) out = std::tanh (out * 3.0f) * 0.6f;
            break;
        }
        case snare:
        {
            const float tone = (osc (0, a.pitch) + 0.55f * osc (1, a.pitch * 1.78f)) * env (t, 0.045f);
            a.f1.process (noise(), lp, bp, hp);
            const float noiseAmt = kit == rhythmUnit ? 1.1f : kit == homeKeyboard ? 0.9f : 0.8f;
            out = tone * (kit == eightOhEight ? 0.6f : 0.35f) + hp * noiseAmt * env (t, a.decay);
            break;
        }
        case rim:
        {
            a.f1.process (noise(), lp, bp, hp);
            out = osc (0, a.pitch) * env (t, a.decay) + bp * env (t, 0.004f) * 0.8f;
            break;
        }
        case clap:
        {
            a.f1.process (noise(), lp, bp, hp);
            float e = 0.0f;
            for (float burst : { 0.0f, 0.011f, 0.022f })
                if (t >= burst) e = juce::jmax (e, env (t - burst, 0.005f));
            if (t >= 0.03f) e = juce::jmax (e, 0.8f * env (t - 0.03f, a.decay));
            out = bp * e * 2.2f;
            break;
        }
        case closedHat: case pedalHat: case openHat:
        {
            const float src = (kit == eightOhEight) ? metal (a.pitch) : 0.35f * metal (a.pitch) + 0.65f * noise();
            a.f1.process (src, lp, bp, hp);
            float lp2, bp2, hp2;
            a.f2.process (bp, lp2, bp2, hp2);
            out = hp2 * env (t, a.decay) * 2.0f;
            break;
        }
        case crash: case ride:
        {
            const float src = metal (a.pitch) * 0.6f + noise() * (a.type == crash ? 0.5f : 0.2f);
            a.f1.process (src, lp, bp, hp);
            float lp2, bp2, hp2;
            a.f2.process (bp, lp2, bp2, hp2);
            out = hp2 * env (t, a.decay) * 1.6f;
            if (a.type == ride) out += osc (5, a.pitch * 41.0f) * env (t, 0.25f) * 0.15f;   // bell
            break;
        }
        case lowTom: case midTom: case highTom:
        {
            const float f = a.pitch * (1.0f + 0.35f * env (t, 0.06f));
            out = osc (0, f) * env (t, a.decay) + noise() * env (t, 0.006f) * 0.15f;
            break;
        }
        case cowbell:
        {
            const float sq = square (0, a.pitch) + square (1, a.pitch * 1.48f);
            a.f1.process (sq, lp, bp, hp);
            out = bp * (0.6f * env (t, 0.02f) + 0.5f * env (t, a.decay)) * 1.2f;
            break;
        }
        case clave:
            out = osc (0, a.pitch) * env (t, a.decay);
            break;
        case maracas:
        {
            a.f1.process (noise(), lp, bp, hp);
            const float attack = juce::jmin (1.0f, t / 0.004f);
            out = hp * attack * env (t, a.decay) * 1.4f;
            break;
        }
        case tambourine:
        {
            a.f1.process (metal (a.pitch) * 0.5f + noise() * 0.5f, lp, bp, hp);
            float e = env (t, a.decay);
            for (float jingle : { 0.012f, 0.03f }) if (t >= jingle) e = juce::jmax (e, 0.7f * env (t - jingle, a.decay * 0.6f));
            out = hp * e * 1.6f;
            break;
        }
        case highConga: case lowConga: case highBongo: case lowBongo:
        {
            const float f = a.pitch * (1.0f + 0.12f * env (t, 0.02f));
            out = osc (0, f) * env (t, a.decay) + noise() * env (t, 0.003f) * 0.2f;
            break;
        }
        default: break;
    }

    if (kit == toyBox)
    {
        // 6-bit grit
        out = std::round (out * 32.0f) / 32.0f;
    }
    if (t > a.decay * 7.0f + 0.05f) a.active = false;
    return out * a.vel * a.gain;
}

void DrumSynth::render (float* left, float* right, int n)
{
    for (auto& a : voices)
    {
        if (! a.active) continue;
        const float angle = (a.pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        const float gl = std::cos (angle) * juce::MathConstants<float>::sqrt2;
        const float gr = std::sin (angle) * juce::MathConstants<float>::sqrt2;
        for (int i = 0; i < n && a.active; ++i)
        {
            const float s = renderVoice (a);
            left[i] += s * gl;
            right[i] += s * gr;
        }
    }
}

juce::AudioBuffer<float> DrumSynth::renderHit (Voice v, int kit, double sampleRate, float velocity)
{
    DrumSynth ds;
    ds.prepare (sampleRate);
    ds.setKit (kit);
    ds.trigger (v, velocity);
    const int maxLen = (int) (sampleRate * 2.5);
    juce::AudioBuffer<float> b (2, maxLen);
    b.clear();
    const int chunk = 256;
    int done = 0;
    for (; done < maxLen; done += chunk)
    {
        const int n = juce::jmin (chunk, maxLen - done);
        ds.render (b.getWritePointer (0, done), b.getWritePointer (1, done), n);
        bool any = false;
        for (auto& a : ds.voices) any = any || a.active;
        if (! any) { done += n; break; }
    }
    b.setSize (2, juce::jmax (1, juce::jmin (done, maxLen)), true);
    return b;
}

} // namespace wis::daw
