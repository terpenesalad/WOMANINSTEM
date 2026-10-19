#include "DaydreamSynth.h"
#include <cmath>

namespace wis::daw::daydream
{

namespace
{
    constexpr float twoPi = juce::MathConstants<float>::twoPi;
    constexpr int ctlRate = 16;

    struct SineTable
    {
        static constexpr int size = 4096;
        float t[size + 1];
        SineTable() { for (int i = 0; i <= size; ++i) t[i] = (float) std::sin (2.0 * juce::MathConstants<double>::pi * i / size); }
    };
    const SineTable& sineTable() { static const SineTable s; return s; }

    /** sin(2 pi x), x in cycles (any value). */
    inline float sinCycles (double x) noexcept
    {
        const auto& s = sineTable();
        x -= std::floor (x);
        const double p = x * SineTable::size;
        const int i = (int) p;
        const float f = (float) (p - i);
        return s.t[i] + f * (s.t[i + 1] - s.t[i]);
    }

    inline float mtof (float n) noexcept { return 440.0f * std::exp2 ((n - 69.0f) / 12.0f); }

    inline float blep (double t, double dt) noexcept
    {
        if (t < dt) { t /= dt; return (float) (t + t - t * t - 1.0); }
        if (t > 1.0 - dt) { t = (t - 1.0) / dt; return (float) (t * t + t + t + 1.0); }
        return 0.0f;
    }

    inline float oscillator (int wave, double t, double dt, float pw) noexcept
    {
        switch (wave)
        {
            case 0: return (float) (2.0 * t - 1.0) - blep (t, dt);
            case 1: case 2:
            {
                const float w = wave == 1 ? 0.5f : pw;
                float s = t < w ? 1.0f : -1.0f;
                s += blep (t, dt);
                double t2 = t + 1.0 - w; t2 -= std::floor (t2);
                s -= blep (t2, dt);
                return s;
            }
            case 3: return 1.0f - 4.0f * std::abs ((float) t - 0.5f);
            default: return sinCycles (t);
        }
    }

    inline void advance (double& ph, double inc) noexcept { ph += inc; if (ph >= 1.0) ph -= std::floor (ph); }

    // FM routing: which ops modulate each op (bit j = op j), and which ops are heard.
    struct Algorithm { int mods[4]; int carriers; };
    const Algorithm algorithms[] = {
        { { 0b0010, 0b0100, 0b1000, 0 }, 0b0001 },   // stack 4 > 3 > 2 > 1
        { { 0b0010, 0b1100, 0, 0 },      0b0001 },   // 3 + 4 > 2 > 1
        { { 0b0010, 0, 0b1000, 0 },      0b0101 },   // two pairs
        { { 0b1110, 0, 0, 0 },           0b0001 },   // three into one
        { { 0b1000, 0b1000, 0b1000, 0 }, 0b0111 },   // one into three
        { { 0, 0, 0, 0 },                0b1111 },   // four sines (organ)
        { { 0b0010, 0b0100, 0, 0 },      0b1001 },   // stack + a sine
        { { 0b0110, 0, 0b1000, 0 },      0b0001 },   // 2 and (4 > 3) into 1
    };
}

juce::StringArray engineNames()    { return { "Analogue", "FM", "Reed Piano" }; }
juce::StringArray waveNames()      { return { "Saw", "Square", "Pulse", "Triangle", "Sine" }; }
juce::StringArray filterNames()    { return { "Low Pass 24", "Low Pass 12", "Band Pass", "High Pass" }; }
juce::StringArray algorithmNames()
{
    return { "4>3>2>1 Stack", "3+4>2>1", "2>1 + 4>3", "2+3+4>1", "4>1+2+3", "1+2+3+4 Organ", "3>2>1 + 4", "2+(4>3)>1" };
}

// ---- envelope ---------------------------------------------------------------------------------------------

void Voice::Env::set (float a, float d, float s, float r, double sr)
{
    rateA = 1.0f / juce::jmax (1.0f, a * (float) sr);
    coefD = std::exp (-4.6f / juce::jmax (1.0f, d * (float) sr));
    coefR = std::exp (-4.6f / juce::jmax (1.0f, r * (float) sr));
    target = juce::jlimit (0.0f, 1.0f, s);
}

float Voice::Env::next()
{
    switch (stage)
    {
        case attackS:  value += rateA; if (value >= 1.0f) { value = 1.0f; stage = decayS; } break;
        case decayS:   value = target + (value - target) * coefD; if (std::abs (value - target) < 1.0e-4f) { value = target; stage = sustainS; } break;
        case sustainS: value = target; break;
        case releaseS: value *= coefR; if (value < 1.0e-4f) { value = 0.0f; stage = idle; } break;
        case idle: default: value = 0.0f; break;
    }
    return value;
}

// ---- voice ------------------------------------------------------------------------------------------------

void Voice::start (int ownerIn, int noteIn, float pitchIn, float velocity, const SoundParams& p, juce::uint32 orderIn, float panIn)
{
    const bool wasActive = active && amp.value > 0.001f;
    active = true; released = false; sustained = false;
    owner = ownerIn; note = noteIn; pitch = pitchIn; order = orderIn; pan = panIn;
    vel = juce::jlimit (0.0f, 1.0f, velocity);
    seed = (juce::uint32) (noteIn * 7919 + orderIn * 104729 + 1);
    amp.set (p.attack, p.decay, p.sustain, p.release, p.sampleRate);
    filt.set (p.fAttack, p.fDecay, p.fSustain, p.fRelease, p.sampleRate);
    if (! wasActive) { amp.value = 0.0f; filt.value = 0.0f; f1 = {}; f2 = {}; }
    amp.start(); filt.start();
    for (int i = 0; i < 4; ++i)
    {
        op[i].set (p.opAttack[i], p.opDecay[i], p.opSustain[i], p.release, p.sampleRate);
        if (! wasActive) op[i].value = 0.0f;
        op[i].start();
    }
    lfoAge = 0.0f;
    lfoPhase = (float) ((seed >> 8) & 1023) / 1024.0f;
    driftCt = (noise()) * p.drift * 0.5f;
    driftVel = 0.0f;
    for (auto& ph : phase) for (auto& x : ph) x = (double) ((seed = seed * 1664525u + 1013904223u) >> 8) / 16777216.0;
    if (p.engine == 1) { for (auto& x : fmPhase) x = 0.0; fmPrev[0] = fmPrev[1] = 0.0f; }
    ctl = 0;

    if (p.engine == 2)
    {
        // the reed: a cantilever's first three bending modes (1 : 6.27 : 17.55), struck by a felt hammer
        const float f0 = mtof (pitch);
        const float base = juce::jlimit (0.6f, 11.0f, 5.0f * std::pow (f0 / 261.6f, -0.55f)) * p.reedDecay;
        const float ratios[3] = { 1.0f, 6.267f, 17.55f };
        const float decays[3] = { base, base * 0.12f, base * 0.035f };
        const float v = vel;
        const float gains[3] = { 1.0f, (0.10f + 0.32f * p.reedTone) * v * std::sqrt (v), (0.02f + 0.1f * p.reedTone) * v * v };
        for (int m = 0; m < 3; ++m)
        {
            const float f = f0 * ratios[m];
            auto& md = modes[m];
            const float w = twoPi * f / (float) p.sampleRate;
            const bool ok = f < 0.45f * (float) p.sampleRate;
            md.r = std::exp (-6.9f / (decays[m] * (float) p.sampleRate));
            md.rDamped = std::exp (-6.9f / (juce::jmin (decays[m], 0.12f / (1.0f + m * 2.0f)) * (float) p.sampleRate));
            md.c = 2.0f * md.r * std::cos (w);
            md.gain = ok ? gains[m] : 0.0f;
            // start the sine at zero with the right amplitude (y[-1] = 0, y[0] = A sin w)
            md.y2 = 0.0f;
            md.y1 = md.gain * std::sin (w);
        }
        hammer = 0;
        hammerLen = juce::jmax (1, (int) ((0.0022f - 0.0016f * v) * (float) p.sampleRate));
        thump = p.mechanics * v * 0.10f;
        chuff = 0.0f;
        pickup = juce::jlimit (0.0f, 0.95f, p.bark * (0.3f + 0.7f * v));
        dc = dcIn = 0.0f;
        toneLp = 0.0f;
    }
}

void Voice::release (const SoundParams& p)
{
    if (released) return;
    released = true;
    amp.off(); filt.off();
    for (auto& o : op) o.off();
    if (p.engine == 2)
    {
        for (auto& md : modes) { md.c = md.c / md.r * md.rDamped; md.r = md.rDamped; }
        chuff = p.mechanics * 0.035f;
        amp.coefR = std::exp (-4.6f / juce::jmax (1.0f, juce::jmax (0.25f, p.release) * (float) p.sampleRate));
    }
}

void Voice::control (const SoundParams& p)
{
    const float sr = (float) p.sampleRate;
    const float dtc = (float) ctlRate / sr;
    lfoAge += dtc;
    lfoPhase += p.lfoRate * dtc;
    if (lfoPhase >= 1.0f) lfoPhase -= 1.0f;
    const float fade = p.lfoDelay <= 0.001f ? 1.0f : juce::jlimit (0.0f, 1.0f, lfoAge / p.lfoDelay);
    const float s = sinCycles (lfoPhase) * fade;

    // a slow, wandering analogue drift
    driftVel = driftVel * 0.995f + noise() * 0.004f;
    driftCt = juce::jlimit (-p.drift, p.drift, driftCt + driftVel * p.drift);

    const float semis = pitch + p.bend + (p.vibrato * s + driftCt) * 0.01f;
    freq = mtof (semis);
    pwNow = juce::jlimit (0.06f, 0.94f, p.pw + p.lfoPwm * 0.42f * s);
    ampLfo = 1.0f - p.lfoAmp * 0.5f * (1.0f + s);

    float fe = 0.0f;
    for (int i = 0; i < ctlRate; ++i) fe = filt.next();
    const float oct = p.envAmount * 5.0f * fe + p.keyTrack * (pitch - 60.0f) / 12.0f
                    + p.velCutoff * (vel - 0.7f) * 2.5f + p.lfoFilter * s;
    const float fc = juce::jlimit (20.0f, 0.45f * sr, p.cutoff * std::exp2 (oct));
    g = std::tan (juce::MathConstants<float>::pi * fc / sr);
    k = 2.0f - 1.94f * juce::jlimit (0.0f, 1.0f, p.reso);
}

float Voice::renderAnalogue (const SoundParams& p)
{
    const double sr = p.sampleRate;
    const int u = juce::jlimit (1, 4, p.unison);
    const float fB = freq * std::exp2 ((p.semiB + p.fineB * 0.01f) / 12.0f);
    float a = 0.0f, b = 0.0f;
    for (int i = 0; i < u; ++i)
    {
        const float spread = u == 1 ? 0.0f : ((float) i / (float) (u - 1) * 2.0f - 1.0f) * p.detune;
        const float m = std::exp2 (spread / 1200.0f);
        const double dA = freq * m / sr, dB = fB * m / sr;
        a += oscillator (p.waveA, phase[0][i], dA, pwNow);
        b += oscillator (p.waveB, phase[1][i], dB, pwNow);
        advance (phase[0][i], dA);
        advance (phase[1][i], dB);
    }
    const float norm = 1.0f / std::sqrt ((float) u);
    float x = (a * (1.0f - p.mixB) + b * p.mixB) * norm;
    if (p.sub > 0.0f)
    {
        const double ds = freq * 0.5 / sr;
        x += p.sub * oscillator (1, subPhase, ds, 0.5f);
        advance (subPhase, ds);
    }
    if (p.noise > 0.0f) x += p.noise * noise();
    return x * 0.5f;
}

float Voice::renderFm (const SoundParams& p)
{
    const auto& alg = algorithms[juce::jlimit (0, 7, p.algorithm)];
    const float velIdx = 1.0f - p.fmVelocity + p.fmVelocity * vel * 1.4f;
    float out[4] {};
    float envs[4];
    for (int i = 0; i < 4; ++i) envs[i] = op[i].next();
    for (int i = 3; i >= 0; --i)
    {
        float m = 0.0f;
        for (int j = i + 1; j < 4; ++j)
            if (alg.mods[i] & (1 << j)) m += out[j] * p.level[j] * 0.9f * velIdx;   // index in cycles (0.9 = ~5.7 rad)
        if (i == 3 && p.feedback > 0.0f) m += p.feedback * 0.25f * (fmPrev[0] + fmPrev[1]);
        out[i] = sinCycles (fmPhase[i] + m) * envs[i];
        if (i == 3) { fmPrev[1] = fmPrev[0]; fmPrev[0] = out[3]; }
        advance (fmPhase[i], freq * p.ratio[i] / p.sampleRate);
    }
    float x = 0.0f;
    int n = 0;
    for (int i = 0; i < 4; ++i) if (alg.carriers & (1 << i)) { x += out[i] * p.level[i]; ++n; }
    return x * 0.62f / std::sqrt ((float) juce::jmax (1, n));
}

float Voice::renderReed (const SoundParams& p)
{
    float x = 0.0f;
    for (auto& md : modes)
    {
        const float y = md.c * md.y1 - md.r * md.r * md.y2;
        md.y2 = md.y1; md.y1 = y;
        x += y;
    }
    // the hammer's felt softens the first moments
    if (hammer < hammerLen) { x *= (float) hammer / (float) hammerLen; ++hammer; }
    x *= 0.55f + 0.45f * vel;
    // the electrostatic pickup: output ~ 1 / gap, so a hard hit "barks" (strong even harmonics)
    const float q = juce::jlimit (-0.86f, 0.86f, pickup * x);
    float y = x / (1.0f - q);
    dc += 0.0015f * (y - dc);
    y -= dc;
    // mechanics: the key's thump and the damper's release
    if (thump > 1.0e-5f)
    {
        thumpLp += 0.06f * (noise() - thumpLp);
        y += thumpLp * thump * 4.0f;
        thump *= 0.9975f;
    }
    if (released && chuff > 1.0e-5f)
    {
        chuffBp1 += 0.18f * (noise() - chuffBp1);
        chuffBp2 += 0.05f * (chuffBp1 - chuffBp2);
        y += (chuffBp1 - chuffBp2) * chuff * 3.0f;
        chuff *= 0.9992f;
    }
    // tone: the amp's top end
    const float fc = 1200.0f * std::exp2 (p.reedTone * 3.6f);
    const float a = 1.0f - std::exp (-twoPi * fc / (float) p.sampleRate);
    toneLp += a * (y - toneLp);
    return toneLp * 0.42f;
}

void Voice::render (const SoundParams& p, float* left, float* right, int n)
{
    if (! active) return;
    const float velGain = (1.0f - p.velSens) + p.velSens * vel * std::sqrt (vel);
    const float pp = juce::jlimit (-1.0f, 1.0f, pan * p.width);
    const float gl = std::cos ((pp + 1.0f) * juce::MathConstants<float>::pi * 0.25f) * 1.4142f;
    const float gr = std::sin ((pp + 1.0f) * juce::MathConstants<float>::pi * 0.25f) * 1.4142f;
    const float driveGain = 1.0f + p.drive * 6.0f;
    const float driveNorm = 1.0f / std::sqrt (driveGain);

    for (int i = 0; i < n; ++i)
    {
        if (ctl-- <= 0) { control (p); ctl = ctlRate - 1; }
        const float e = amp.next();
        if (amp.stage == Env::idle) { active = false; return; }
        float x = p.engine == 0 ? renderAnalogue (p) : p.engine == 1 ? renderFm (p) : renderReed (p);

        float y;
        switch (p.filterType)
        {
            case 1:  y = f1.lp (x, g, k); break;
            case 2:  f1.lp (x, g, k); y = f1.lastBp; break;
            case 3:  f1.lp (x, g, k); y = f1.lastHp; break;
            default: y = f2.lp (f1.lp (x, g, k), g, 1.41f); break;
        }
        if (p.drive > 0.001f) y = std::tanh (y * driveGain) * driveNorm;
        y *= e * velGain * ampLfo;
        left[i] += y * gl;
        right[i] += y * gr;
    }
}

// ---- bass -------------------------------------------------------------------------------------------------

namespace
{
    struct BassDef { const char* name; float saw, square, sine, sub, cutoff, reso, env, fDecay, aDecay, sustain, drive, fm, wobble; };
    const BassDef bassDefs[] = {
        { "Deep Sub",        0.0f, 0.0f, 1.0f, 0.0f,  300, 0.0f, 0.0f, 0.2f, 2.0f, 1.0f,  0.15f, 0, 0 },
        { "Round Analogue",  0.6f, 0.4f, 0.0f, 0.3f,  420, 0.3f, 0.55f, 0.25f, 2.0f, 0.9f, 0.1f, 0, 0 },
        { "Warm Fingers",    0.3f, 0.0f, 0.8f, 0.0f,  700, 0.1f, 0.45f, 0.12f, 1.1f, 0.5f, 0.15f, 0, 0 },
        { "Rubber Band",     0.3f, 0.5f, 0.5f, 0.0f,  560, 0.25f, 0.6f, 0.1f, 1.4f, 0.65f, 0.45f, 0, 0 },
        { "Saw Growl",       1.0f, 0.0f, 0.0f, 0.4f,  900, 0.45f, 0.5f, 0.3f, 2.0f, 0.85f, 0.5f, 0, 0 },
        { "Hollow Square",   0.0f, 1.0f, 0.0f, 0.0f, 1100, 0.15f, 0.25f, 0.2f, 2.0f, 0.9f, 0.1f, 0, 0 },
        { "Bell Bass",       0.0f, 0.0f, 1.0f, 0.0f, 2400, 0.0f, 0.0f, 0.2f, 1.6f, 0.45f, 0.05f, 1.2f, 0 },
        { "Tape Wobble",     0.5f, 0.0f, 0.6f, 0.0f,  520, 0.2f, 0.35f, 0.2f, 2.0f, 0.8f, 0.25f, 0, 1 },
    };
    constexpr int numBassDefs = (int) (sizeof (bassDefs) / sizeof (bassDefs[0]));
}

juce::StringArray bassSoundNames()
{
    juce::StringArray n;
    for (auto& d : bassDefs) n.add (d.name);
    return n;
}

void BassSynth::prepare (double sampleRate)
{
    sr = sampleRate;
    for (auto& x : v) x = {};
}

void BassSynth::noteOn (int owner, int note, float velocity, int, float glideMs)
{
    // one bass line per owner: a new note glides (or jumps) from the last
    for (auto& x : v)
        if (x.active && x.owner == owner && ! x.released)
        {
            x.note = note; x.target = (float) note; x.vel = velocity;
            x.glide = glideMs > 0.5f ? 1.0f - std::exp (-1.0f / (glideMs * 0.001f * (float) sr / 4.0f)) : 1.0f;
            if (glideMs <= 0.5f) { x.pitch = (float) note; x.age = 0; x.fenv = 1.0f; }
            return;
        }
    BVoice* pick = nullptr;
    for (auto& x : v) if (! x.active) { pick = &x; break; }
    if (pick == nullptr)
    {
        pick = &v[0];
        for (auto& x : v) if (x.order < pick->order) pick = &x;
    }
    *pick = {};
    pick->active = true;
    pick->owner = owner; pick->note = note; pick->pitch = pick->target = (float) note;
    pick->vel = velocity; pick->glide = 1.0f; pick->fenv = 1.0f;
    pick->order = ++counter;
}

void BassSynth::noteOff (int owner, int note)
{
    for (auto& x : v) if (x.active && x.owner == owner && x.note == note) x.released = true;
}

void BassSynth::allOff (int owner)
{
    for (auto& x : v) if (x.active && (owner < 0 || x.owner == owner)) x.released = true;
}

int BassSynth::soundingNote() const
{
    int best = -1; juce::uint32 o = 0;
    for (auto& x : v) if (x.active && ! x.released && x.order >= o) { o = x.order; best = x.note; }
    return best;
}

void BassSynth::render (float* left, float* right, int n, int sound, float tone, float gain)
{
    const auto& d = bassDefs[juce::jlimit (0, numBassDefs - 1, sound)];
    const float fsr = (float) sr;
    const float aRel = std::exp (-4.6f / (0.09f * fsr));
    const float aDec = std::exp (-4.6f / (d.aDecay * fsr));
    const float fDec = std::exp (-4.6f / (d.fDecay * fsr));
    const float k = 2.0f - 1.9f * d.reso;
    const float driveGain = 1.0f + d.drive * 5.0f, driveNorm = 1.0f / std::sqrt (driveGain);
    for (auto& x : v)
    {
        if (! x.active) continue;
        for (int i = 0; i < n; ++i)
        {
            x.pitch += (x.target - x.pitch) * x.glide;
            x.age += 1.0f / fsr;
            const float wob = d.wobble > 0 ? 0.12f * std::sin (twoPi * 0.7f * x.age) + 0.05f * std::sin (twoPi * 5.3f * x.age) : 0.0f;
            const float f = mtof (x.pitch + wob);
            const double dt = f / sr;
            // attack 3 ms, decay to sustain, release
            if (x.released) x.env *= aRel;
            else if (x.age < 0.003f) x.env = juce::jmax (x.env, x.age / 0.003f);
            else x.env = d.sustain + (x.env - d.sustain) * aDec;
            x.fenv *= fDec;
            if (x.released && x.env < 1.0e-4f) { x.active = false; break; }

            float s = 0.0f;
            if (d.saw > 0) s += d.saw * oscillator (0, x.ph[0], dt, 0.5f);
            if (d.square > 0) s += d.square * oscillator (1, x.ph[0], dt, 0.5f);
            if (d.sine > 0)
            {
                const float fm = d.fm > 0 ? d.fm * x.fenv * sinCycles (x.ph[2]) : 0.0f;
                s += d.sine * sinCycles (x.ph[0] + fm * 0.5f);
            }
            if (d.sub > 0) s += d.sub * oscillator (1, x.ph[1], dt * 0.5, 0.5f);
            advance (x.ph[0], dt);
            advance (x.ph[1], dt * 0.5);
            advance (x.ph[2], dt * 2.0);

            const float fc = juce::jlimit (30.0f, 0.4f * fsr, d.cutoff * std::exp2 (d.env * 4.0f * x.fenv + (tone - 0.5f) * 4.0f + (x.pitch - 36.0f) / 24.0f));
            const float g = std::tan (juce::MathConstants<float>::pi * fc / fsr);
            const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
            auto svf = [&] (float in, float& ic1, float& ic2, float kk)
            {
                const float b1 = 1.0f / (1.0f + g * (g + kk)), b2 = g * b1, b3 = g * b2;
                const float v3 = in - ic2, v1 = b1 * ic1 + b2 * v3, v2 = ic2 + b2 * ic1 + b3 * v3;
                ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2;
                return v2;
            };
            juce::ignoreUnused (a1, a2, a3);
            float y = svf (svf (s, x.ic1, x.ic2, k), x.ic3, x.ic4, 1.41f);
            y = std::tanh (y * driveGain) * driveNorm;
            y *= x.env * (0.55f + 0.45f * x.vel) * gain * 0.55f;
            left[i] += y;
            right[i] += y;
        }
    }
}

// ---- effects ----------------------------------------------------------------------------------------------

juce::StringArray fxNames() { return { "Drive", "Tremolo", "Phaser", "Flanger", "Chorus", "Ensemble", "Delay", "Reverb" }; }
juce::StringArray delayDivisionNames() { return { "1/16", "1/8 Triplet", "1/8", "Dotted 1/8", "1/4 Triplet", "1/4", "Dotted 1/4", "1/2" }; }
double delayDivisionBeats (int d)
{
    static const double b[] = { 0.25, 1.0 / 3.0, 0.5, 0.75, 2.0 / 3.0, 1.0, 1.5, 2.0 };
    return b[juce::jlimit (0, 7, d)];
}

void Effects::prepare (double sampleRate, int blockSize)
{
    sr = sampleRate;
    for (auto& m : mod) m.assign (8192, 0.0f);
    for (auto& f : fl) f.assign (2048, 0.0f);
    for (auto& e : echo) e.assign ((size_t) (sr * 3.0) + 8, 0.0f);
    wetL.assign ((size_t) juce::jmax (1, blockSize), 0.0f);
    wetR.assign ((size_t) juce::jmax (1, blockSize), 0.0f);
    reverb.setSampleRate (sr);
    reset();
}

void Effects::reset()
{
    for (auto& m : mod) std::fill (m.begin(), m.end(), 0.0f);
    for (auto& f : fl) std::fill (f.begin(), f.end(), 0.0f);
    for (auto& e : echo) std::fill (e.begin(), e.end(), 0.0f);
    modPos = flPos = echoPos = 0;
    echoTime = 0.0f;
    for (auto& s : apState) for (auto& x : s) x = 0.0f;
    phaserFb[0] = phaserFb[1] = flOut[0] = flOut[1] = echoLp[0] = echoLp[1] = dLp[0] = dLp[1] = 0.0f;
    reverb.reset();
    lastRev.roomSize = -1.0f;   // forces the first setParameters
}

float Effects::readMod (int ch, float d) const
{
    const auto& b = mod[(size_t) ch];
    const int mask = (int) b.size() - 1;
    const float rp = (float) modPos - d;
    const int i = (int) std::floor (rp);
    const float f = rp - (float) i;
    return b[(size_t) (i & mask)] + f * (b[(size_t) ((i + 1) & mask)] - b[(size_t) (i & mask)]);
}

void Effects::process (float* L, float* R, int n, const FxParams& p, double bpm)
{
    const float fsr = (float) sr;
    float* ch[2] = { L, R };

    // drive
    if (p.drive > 0.001f)
    {
        const float pre = 1.0f + p.drive * 12.0f, norm = 1.0f / std::sqrt (pre);
        const float fc = 1500.0f * std::exp2 (p.driveTone * 3.0f);
        const float a = 1.0f - std::exp (-twoPi * fc / fsr);
        const float toneMix = juce::jmin (1.0f, p.drive * 5.0f);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
            {
                const float y = std::tanh (ch[c][i] * pre) * norm;
                dLp[c] += a * (y - dLp[c]);
                ch[c][i] = y + (dLp[c] - y) * toneMix;
            }
    }

    // tremolo (or auto-pan)
    if (p.tremolo > 0.001f)
    {
        const double inc = p.tremRate / sr;
        for (int i = 0; i < n; ++i)
        {
            const float s = sinCycles (tremPhase);
            const float gl = 1.0f - p.tremolo * 0.5f * (1.0f - s);
            const float gr = 1.0f - p.tremolo * 0.5f * (1.0f - (p.tremPan ? -s : s));
            L[i] *= gl; R[i] *= gr;
            tremPhase += inc; if (tremPhase >= 1.0) tremPhase -= 1.0;
        }
    }

    // phaser: six first-order all-passes swept between 200 Hz and 3.2 kHz
    if (p.phaser > 0.001f)
    {
        const double inc = p.phaserRate / sr;
        for (int i = 0; i < n; ++i)
        {
            if ((ctl++ & 15) == 0)
                for (int c = 0; c < 2; ++c)
                {
                    const float s = 0.5f + 0.5f * sinCycles (phaserPhase + c * 0.25);
                    const float fc = 200.0f * std::exp2 (4.0f * s);
                    const float t = std::tan (juce::MathConstants<float>::pi * fc / fsr);
                    phaserCoef[c] = (t - 1.0f) / (t + 1.0f);
                }
            phaserPhase += inc; if (phaserPhase >= 1.0) phaserPhase -= 1.0;
            for (int c = 0; c < 2; ++c)
            {
                float x = ch[c][i] + phaserFb[c] * p.phaserFeedback * 0.7f;
                const float a = phaserCoef[c];
                for (auto& st : apState[c]) { const float y = a * x + st; st = x - a * y; x = y; }
                phaserFb[c] = x;
                ch[c][i] = ch[c][i] * (1.0f - 0.5f * p.phaser) + x * 0.5f * p.phaser;
            }
        }
    }

    // flanger
    if (p.flanger > 0.001f)
    {
        const double inc = p.flangerRate / sr;
        const int mask = (int) fl[0].size() - 1;
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < 2; ++c)
            {
                const float s = 0.5f + 0.5f * sinCycles (flPhase + c * 0.18);
                const float d = (0.0004f + 0.0045f * s) * fsr;
                const float rp = (float) flPos - d;
                const int ii = (int) std::floor (rp);
                const float f = rp - (float) ii;
                const auto& b = fl[(size_t) c];
                const float y = b[(size_t) (ii & mask)] + f * (b[(size_t) ((ii + 1) & mask)] - b[(size_t) (ii & mask)]);
                fl[(size_t) c][(size_t) flPos] = ch[c][i] + y * p.flangerFeedback * 0.85f;
                ch[c][i] = ch[c][i] * (1.0f - 0.45f * p.flanger) + y * 0.65f * p.flanger;
            }
            flPos = (flPos + 1) & mask;
            flPhase += inc; if (flPhase >= 1.0) flPhase -= 1.0;
        }
    }

    // chorus and ensemble share one modulated delay line
    if (p.chorus > 0.001f || p.ensemble > 0.001f)
    {
        const int mask = (int) mod[0].size() - 1;
        const double chInc = p.chorusRate / sr, enInc = 0.62 / sr, enFastInc = 6.1 / sr;
        for (int i = 0; i < n; ++i)
        {
            mod[0][(size_t) modPos] = L[i];
            mod[1][(size_t) modPos] = R[i];
            float outL = L[i], outR = R[i];
            if (p.chorus > 0.001f)
            {
                const float dl = (0.012f + 0.0055f * p.chorusDepth * sinCycles (chPhase)) * fsr;
                const float dr = (0.012f + 0.0055f * p.chorusDepth * sinCycles (chPhase + 0.25)) * fsr;
                const float wl = readMod (0, dl), wr = readMod (1, dr);
                outL = outL * (1.0f - 0.3f * p.chorus) + wl * 0.62f * p.chorus;
                outR = outR * (1.0f - 0.3f * p.chorus) + wr * 0.62f * p.chorus;
            }
            if (p.ensemble > 0.001f)
            {
                float sumL = 0.0f, sumR = 0.0f;
                for (int t = 0; t < 3; ++t)
                {
                    const double ph = enPhase + t / 3.0, phf = enFast + t / 3.0;
                    const float d = (0.009f + 0.0035f * sinCycles (ph) + 0.00035f * sinCycles (phf)) * fsr;
                    const float d2 = (0.009f + 0.0035f * sinCycles (ph + 0.5) + 0.00035f * sinCycles (phf + 0.5)) * fsr;
                    sumL += readMod (0, d);
                    sumR += readMod (1, d2);
                }
                outL = outL * (1.0f - 0.5f * p.ensemble) + sumL * 0.42f * p.ensemble;
                outR = outR * (1.0f - 0.5f * p.ensemble) + sumR * 0.42f * p.ensemble;
            }
            L[i] = outL; R[i] = outR;
            modPos = (modPos + 1) & mask;
            chPhase += chInc; if (chPhase >= 1.0) chPhase -= 1.0;
            enPhase += enInc; if (enPhase >= 1.0) enPhase -= 1.0;
            enFast += enFastInc; if (enFast >= 1.0) enFast -= 1.0;
        }
    }

    // tempo delay
    if (p.delay > 0.001f)
    {
        const int size = (int) echo[0].size();
        const float target = (float) juce::jlimit (0.02, 2.8, delayDivisionBeats (p.delayDivision) * 60.0 / juce::jmax (20.0, bpm)) * fsr;
        if (echoTime <= 0.0f) echoTime = target;
        const float a = 1.0f - std::exp (-twoPi * (800.0f * std::exp2 (p.delayTone * 4.0f)) / fsr);
        for (int i = 0; i < n; ++i)
        {
            echoTime += (target - echoTime) * 0.0005f;
            float rp = (float) echoPos - echoTime;
            if (rp < 0) rp += (float) size;
            const int ii = (int) rp;
            const float f = rp - (float) ii;
            float y[2];
            for (int c = 0; c < 2; ++c)
            {
                const auto& b = echo[(size_t) c];
                y[c] = b[(size_t) ii] + f * (b[(size_t) ((ii + 1) % size)] - b[(size_t) ii]);
                echoLp[c] += a * (y[c] - echoLp[c]);
            }
            const float fb = juce::jlimit (0.0f, 0.95f, p.delayFeedback);
            if (p.pingPong)
            {
                echo[0][(size_t) echoPos] = 0.5f * (L[i] + R[i]) + echoLp[1] * fb;
                echo[1][(size_t) echoPos] = echoLp[0] * fb;
            }
            else
            {
                echo[0][(size_t) echoPos] = L[i] + echoLp[0] * fb;
                echo[1][(size_t) echoPos] = R[i] + echoLp[1] * fb;
            }
            L[i] += y[0] * p.delay * 0.8f;
            R[i] += y[1] * p.delay * 0.8f;
            echoPos = (echoPos + 1) % size;
        }
    }

    // reverb
    if (p.reverb > 0.001f && n <= (int) wetL.size())
    {
        juce::Reverb::Parameters rp;
        rp.roomSize = juce::jlimit (0.0f, 1.0f, 0.35f + 0.64f * p.reverbSize);
        rp.damping = p.reverbDamp;
        rp.wetLevel = 0.33f;
        rp.dryLevel = 0.0f;
        rp.width = 1.0f;
        if (rp.roomSize != lastRev.roomSize || rp.damping != lastRev.damping) { reverb.setParameters (rp); lastRev = rp; }
        std::copy (L, L + n, wetL.begin());
        std::copy (R, R + n, wetR.begin());
        reverb.processStereo (wetL.data(), wetR.data(), n);
        const float wet = p.reverb * 1.1f, dry = 1.0f - 0.35f * p.reverb;
        for (int i = 0; i < n; ++i) { L[i] = L[i] * dry + wetL[(size_t) i] * wet; R[i] = R[i] * dry + wetR[(size_t) i] * wet; }
    }
}

} // namespace wis::daw::daydream
