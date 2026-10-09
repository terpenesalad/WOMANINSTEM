#include "HomeKeys.h"
#include <bitset>
#include <set>

namespace wis::daw
{

std::function<juce::AudioProcessorEditor* (HomeKeys&)> HomeKeys::editorFactory;

// =====================================================================================================
//  Voices
// =====================================================================================================
namespace
{
    constexpr float twoPi = juce::MathConstants<float>::twoPi;

    struct ToneDef
    {
        const char* name;
        bool additive;
        float ratios[6], amps[6], decays[6];     // additive partials (decay 0 = held by the envelope)
        int wave;                                // subtractive: 0 saw 1 square 2 pulse 3 triangle
        float pulseWidth, wave2Mix, wave2Detune; // second oscillator (semitones)
        float cutoff, keyTrack, envAmt, envDecay, reso;
        bool formant;
        float a, d, s, r;
        float vibDepth, vibRate, vibDelay, tremolo;
        float breath, octave, level;
        int digitalOrgan = 0;                    // 1, 2: an early-80s digital organ (stepped wavetable + analogue filter)
    };

    // name, additive, ratios, amps, decays, wave, pw, mix2, det2, cutoff, keytrack, envAmt, envDec, reso, formant, A D S R, vib depth rate delay, trem, breath, octave, level
    const ToneDef tones[] = {
        { "Piano",        false, {}, {}, {}, 0, 0.5f, 0.6f, 0.02f, 1800, 0.6f, 3200, 0.35f, 0.1f, false, 0.002f, 1.8f, 0.0f, 0.3f, 0, 0, 0, 0, 0, 0, 0.9f },
        { "Electric Piano", true, { 1, 2, 3, 7.9f, 0, 0 }, { 1, 0.12f, 0.05f, 0.25f, 0, 0 }, { 2.4f, 1.2f, 0.8f, 0.05f, 0, 0 },
                          0, 0.5f, 0, 0, 0, 0, 0, 0, 0, false, 0.002f, 2.4f, 0.0f, 0.35f, 0, 0, 0, 0.12f, 0, 0, 0.8f },
        { "Harpsichord",  false, {}, {}, {}, 2, 0.14f, 0.4f, 12.0f, 5200, 0.3f, 4000, 0.25f, 0.2f, false, 0.001f, 1.1f, 0.0f, 0.08f, 0, 0, 0, 0, 0, 0, 0.7f },
        { "Jazz Organ",   true, { 1, 2, 3, 4, 6, 8 }, { 1, 0.75f, 0.5f, 0.3f, 0.15f, 0.25f }, {}, 0, 0.5f, 0, 0, 0, 0, 0, 0, 0, false, 0.004f, 0.1f, 1.0f, 0.04f, 0.04f, 6.2f, 0, 0, 0, 0, 0.55f },
        { "Pipe Organ",   true, { 0.5f, 1, 2, 3, 4, 8 }, { 0.6f, 1, 0.55f, 0.2f, 0.45f, 0.3f }, {}, 0, 0.5f, 0, 0, 0, 0, 0, 0, 0, false, 0.06f, 0.1f, 1.0f, 0.25f, 0, 0, 0, 0, 0, 0, 0.5f },
        { "Accordion",    false, {}, {}, {}, 1, 0.5f, 0.9f, 0.14f, 3200, 0.3f, 0, 0.2f, 0.25f, false, 0.035f, 0.2f, 0.9f, 0.1f, 0, 0, 0, 0.05f, 0, 0, 0.5f },
        { "Flute",        true, { 1, 2, 3, 0, 0, 0 }, { 1, 0.12f, 0.04f, 0, 0, 0 }, {}, 0, 0.5f, 0, 0, 0, 0, 0, 0, 0, false, 0.07f, 0.2f, 0.9f, 0.12f, 0.12f, 5.0f, 0.3f, 0, 0.09f, 12, 0.8f },
        { "Clarinet",     false, {}, {}, {}, 1, 0.5f, 0.0f, 0, 2000, 0.5f, 400, 0.1f, 0.15f, false, 0.03f, 0.2f, 0.85f, 0.09f, 0.08f, 5.0f, 0.35f, 0, 0.02f, 0, 0.6f },
        { "Oboe",         false, {}, {}, {}, 2, 0.12f, 0.0f, 0, 2800, 0.4f, 400, 0.1f, 0.5f, false, 0.04f, 0.2f, 0.8f, 0.09f, 0.1f, 5.5f, 0.3f, 0, 0.01f, 0, 0.5f },
        { "Trumpet",      false, {}, {}, {}, 0, 0.5f, 0.0f, 0, 1100, 0.5f, 2600, 0.14f, 0.2f, false, 0.03f, 0.3f, 0.85f, 0.09f, 0.1f, 5.4f, 0.35f, 0, 0.02f, 0, 0.6f },
        { "Violin",       false, {}, {}, {}, 0, 0.5f, 0.0f, 0, 3600, 0.4f, 0, 0.2f, 0.15f, false, 0.12f, 0.3f, 0.9f, 0.2f, 0.18f, 5.6f, 0.2f, 0, 0, 0, 0.55f },
        { "Strings",      false, {}, {}, {}, 0, 0.5f, 0.85f, 0.16f, 3200, 0.3f, 0, 0.2f, 0.1f, false, 0.25f, 0.5f, 0.9f, 0.5f, 0.06f, 5.0f, 0.4f, 0, 0, 0, 0.45f },
        { "Vibraphone",   true, { 1, 4, 10, 0, 0, 0 }, { 1, 0.3f, 0.06f, 0, 0, 0 }, { 3.2f, 0.9f, 0.2f, 0, 0, 0 }, 0, 0.5f, 0, 0, 0, 0, 0, 0, 0, false, 0.001f, 3.2f, 0.0f, 0.5f, 0, 0, 0, 0.3f, 0, 0, 0.8f },
        { "Glockenspiel", true, { 1, 2.76f, 5.4f, 8.93f, 0, 0 }, { 1, 0.5f, 0.25f, 0.1f, 0, 0 }, { 1.4f, 0.5f, 0.3f, 0.15f, 0, 0 }, 0, 0.5f, 0, 0, 0, 0, 0, 0, 0, false, 0.001f, 1.5f, 0.0f, 0.6f, 0, 0, 0, 0, 0, 24, 0.6f },
        { "Music Box",    true, { 1, 3.0f, 4.1f, 6.0f, 0, 0 }, { 1, 0.3f, 0.25f, 0.1f, 0, 0 }, { 1.8f, 0.6f, 0.4f, 0.2f, 0, 0 }, 0, 0.5f, 0, 0, 0, 0, 0, 0, 0, false, 0.001f, 1.9f, 0.0f, 0.7f, 0, 0, 0, 0, 0, 12, 0.7f },
        { "Human Voice",  false, {}, {}, {}, 0, 0.5f, 0.5f, 0.1f, 0, 0, 0, 0, 0, true, 0.15f, 0.3f, 0.9f, 0.35f, 0.15f, 5.2f, 0.25f, 0, 0.02f, 0, 0.75f },
        // internal: accompaniment bass
        { "Synth Bass",   false, {}, {}, {}, 0, 0.5f, 0.5f, -12.0f, 700, 0.3f, 1400, 0.12f, 0.3f, false, 0.002f, 0.5f, 0.55f, 0.07f, 0, 0, 0, 0, 0, 0, 0.9f },
        // 3.6: the cheap early-80s portable organ sound (Beach House's thrift-store keyboard): square-wave footages
        // read from a coarse digital wavetable, then a warm analogue low-pass. Organ 2 has the percussive "bite".
        { "Dream Organ 1", false, {}, {}, {}, 0, 0.5f, 0, 0, 3400, 0.25f, 0, 0.1f, 0.15f, false, 0.006f, 0.2f, 1.0f, 0.06f, 0, 0, 0, 0, 0, 0, 0.62f, 1 },
        { "Dream Organ 2", false, {}, {}, {}, 0, 0.5f, 0, 0, 3000, 0.25f, 2600, 0.09f, 0.2f, false, 0.003f, 0.35f, 0.82f, 0.06f, 0, 0, 0, 0, 0, 0, 0.62f, 2 },
    };
    constexpr int numPublicTones = 18;
    constexpr int bassTone = 16;

    /** Public tone number (the TONE buttons) -> index into tones[] (the accompaniment bass sits in the middle). */
    int toneIndex (int publicTone)
    {
        publicTone = juce::jlimit (0, numPublicTones - 1, publicTone);
        return publicTone < bassTone ? publicTone : publicTone + 1;
    }

    /** The digital organ waveforms. The chip stores one cycle in 64 steps of 32 levels and plays it through a DAC;
        that stepped wave (with its zero-order-hold images) is what reaches the filter. We rebuild exactly that
        spectrum, band-limited per octave, so high notes keep the hardware's harmonics without aliasing hash. */
    struct OrganTables
    {
        static constexpr int steps = 64, size = 2048, levels = 11;   // level L holds harmonics up to 2^L
        std::vector<float> table[3][levels];
        OrganTables()
        {
            // footage mixes of square waves: 8' + 4' + 2' (hollow, reedy) and 8' + 4' + 2 2/3' + 2' + 1' (brighter, nasal)
            const float mixes[3][5] = { {}, { 1.0f, 0.55f, 0.0f, 0.24f, 0.0f }, { 1.0f, 0.45f, 0.38f, 0.2f, 0.08f } };
            const float ratios[5] = { 1.0f, 2.0f, 3.0f, 4.0f, 8.0f };
            const double pi = juce::MathConstants<double>::pi;
            for (int t = 1; t <= 2; ++t)
            {
                float stepped[steps] {};
                float peak = 0.0f;
                for (int i = 0; i < steps; ++i)
                {
                    float v = 0.0f;
                    for (int k = 0; k < 5; ++k)
                    {
                        float ph = (float) i / (float) steps * ratios[k];
                        ph -= std::floor (ph);
                        v += mixes[t][k] * (ph < 0.5f ? 1.0f : -1.0f);
                    }
                    stepped[i] = v;
                    peak = juce::jmax (peak, std::abs (v));
                }
                for (auto& v : stepped) v = std::round (v / peak * 15.5f) / 15.5f;   // 5-bit levels
                // Fourier series of the held staircase: harmonic h = DFT bin (h mod 64) x sinc, with the hold's half-step delay
                const int maxH = 1 << (levels - 1);
                std::vector<double> re ((size_t) maxH + 1), im ((size_t) maxH + 1);
                for (int h = 1; h <= maxH; ++h)
                {
                    double cr = 0, ci = 0;
                    for (int i = 0; i < steps; ++i) { const double w = -2.0 * pi * h * i / steps; cr += stepped[i] * std::cos (w); ci += stepped[i] * std::sin (w); }
                    const double x = pi * h / steps, sinc = std::sin (x) / x;
                    const double dr = std::cos (-x), di = std::sin (-x);   // e^{-i pi h / 64}
                    re[(size_t) h] = (cr * dr - ci * di) * sinc * 2.0 / steps;
                    im[(size_t) h] = (cr * di + ci * dr) * sinc * 2.0 / steps;
                }
                for (int L = 0; L < levels; ++L)
                {
                    auto& tab = table[t][L];
                    tab.assign ((size_t) size + 1, 0.0f);
                    const int hMax = 1 << L;
                    for (int h = 1; h <= hMax; ++h)
                    {
                        if (std::abs (re[(size_t) h]) + std::abs (im[(size_t) h]) < 1.0e-6) continue;
                        for (int i = 0; i < size; ++i)
                        {
                            const double w = 2.0 * pi * h * i / size;
                            tab[(size_t) i] += (float) (re[(size_t) h] * std::cos (w) - im[(size_t) h] * std::sin (w));
                        }
                    }
                    tab[(size_t) size] = tab[0];
                }
            }
        }
        /** One sample at phase 0..1 for a note at freq (harmonics kept below ~0.45 * sr). */
        float read (int t, float phase, float freq, float sr) const
        {
            const float hAllowed = sr * 0.45f / juce::jmax (1.0f, freq);
            int L = juce::jlimit (0, levels - 1, (int) std::floor (std::log2 (juce::jmax (1.0f, hAllowed))));
            const auto& tab = table[t][L];
            const float x = phase * (float) size;
            const int i = juce::jlimit (0, size - 1, (int) x);
            const float fr = x - (float) i;
            return tab[(size_t) i] + (tab[(size_t) i + 1] - tab[(size_t) i]) * fr;
        }
    };
    const OrganTables& organTables() { static const OrganTables t; return t; }

    /** The keyboard's tone generator tunes each note by a small, fixed amount (cents). */
    float dividerDetune (int note)
    {
        juce::uint32 h = (juce::uint32) note * 2654435761u;
        h ^= h >> 15;
        return ((float) (h & 1023) / 1023.0f - 0.5f) * 5.0f;
    }

    struct Svf
    {
        float ic1 = 0, ic2 = 0, g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0;
        void set (float f, float q, float sr)
        {
            g = std::tan (juce::MathConstants<float>::pi * juce::jlimit (20.0f, sr * 0.45f, f) / sr);
            k = 1.0f / juce::jmax (0.05f, q);
            a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
        }
        float lp (float x) { const float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3; ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2; return v2; }
        float bp (float x) { const float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3; ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2; return v1; }
        void reset() { ic1 = ic2 = 0; }
    };

    float polyBlep (float t, float dt)
    {
        if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
        if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
        return 0.0f;
    }

    struct Voice
    {
        bool active = false, released = false;
        int note = 60, tone = 0, group = 0;   // group: 0 melody, 1 chord, 2 bass
        float vel = 1.0f, t = 0.0f, relT = 0.0f, envAtRelease = 0.0f, lastEnv = 0.0f;
        float phase[6] {}, phase2 = 0.0f;
        Svf filter, formant1, formant2;
        juce::uint32 seed = 1;
        float releaseScale = 1.0f;

        void start (int n, float v, int toneIndex, int grp)
        {
            *this = Voice();
            active = true; note = n; vel = v; tone = toneIndex; group = grp;
            seed = (juce::uint32) (n * 7919 + 13);
        }
        void release (float scale) { if (! released) { released = true; relT = 0.0f; envAtRelease = lastEnv; releaseScale = scale; } }

        float render (float sr, bool vibratoOn, float brightness)
        {
            const auto& d = tones[tone];
            const float dt = 1.0f / sr;
            // envelope
            float env;
            if (t < d.a) env = t / d.a;
            else env = d.s + (1.0f - d.s) * std::exp (-(t - d.a) / juce::jmax (0.01f, d.d));
            if (released)
            {
                env = envAtRelease * std::exp (-relT / juce::jmax (0.01f, d.r * releaseScale));
                relT += dt;
                if (env < 1.0e-4f) { active = false; return 0.0f; }
            }
            lastEnv = env;

            // pitch with vibrato (delayed onset, like the real things)
            float vibDepth = d.vibDepth;
            if (vibratoOn && vibDepth < 0.1f) vibDepth = 0.14f;
            float semis = (float) note + d.octave;
            if (vibDepth > 0.0f && t > d.vibDelay)
                semis += vibDepth * juce::jmin (1.0f, (t - d.vibDelay) * 3.0f) * std::sin (twoPi * (d.vibRate > 0 ? d.vibRate : 5.5f) * t);
            const float freq = 440.0f * std::pow (2.0f, (semis - 69.0f) / 12.0f);

            float out = 0.0f;
            if (d.digitalOrgan > 0)
            {
                const float f = freq * std::pow (2.0f, dividerDetune (note) / 1200.0f);
                phase[0] += f * dt;
                phase[0] -= std::floor (phase[0]);
                out = organTables().read (d.digitalOrgan, phase[0], f, sr);
                if (d.digitalOrgan == 2)
                {
                    // the "bite": a 2 2/3' and 2' flash at the start of each note
                    auto square = [] (float& ph, float inc)
                    {
                        ph += inc; ph -= std::floor (ph);
                        float half = ph + 0.5f; half -= std::floor (half);
                        return (ph < 0.5f ? 1.0f : -1.0f) + polyBlep (ph, inc) - polyBlep (half, inc);
                    };
                    const float perc = std::exp (-t / 0.11f);
                    if (perc > 1.0e-4f)
                        out += perc * (0.45f * square (phase[1], juce::jmin (0.45f, f * 3.0f * dt)) + 0.25f * square (phase[2], juce::jmin (0.45f, f * 4.0f * dt)));
                }
                // the analogue filter after the chip (a little brighter when the key is hit hard)
                float cutoff = d.cutoff * std::pow (2.0f, d.keyTrack * (semis - 60.0f) / 12.0f) + d.envAmt * std::exp (-t / juce::jmax (0.01f, d.envDecay));
                cutoff *= std::pow (2.0f, brightness) * (0.75f + 0.25f * vel);
                filter.set (cutoff, 0.6f + d.reso * 3.0f, sr);
                out = filter.lp (out) * 0.5f;
                // key click
                if (t < 0.004f)
                {
                    seed = seed * 1664525u + 1013904223u;
                    out += (float) (int) seed * (1.0f / 2147483648.0f) * 0.05f * (1.0f - t / 0.004f);
                }
            }
            else if (d.additive)
            {
                for (int i = 0; i < 6; ++i)
                {
                    if (d.amps[i] <= 0.0f || d.ratios[i] <= 0.0f) continue;
                    const float f = freq * d.ratios[i];
                    if (f > sr * 0.45f) continue;
                    phase[i] += f * dt;
                    phase[i] -= std::floor (phase[i]);
                    float a = d.amps[i];
                    if (d.decays[i] > 0.0f) a *= std::exp (-t / d.decays[i]);
                    out += std::sin (twoPi * phase[i]) * a;
                }
                out *= 0.45f;
            }
            else
            {
                auto oscillator = [&] (float& ph, float f)
                {
                    const float inc = f * dt;
                    ph += inc;
                    ph -= std::floor (ph);
                    switch (d.wave)
                    {
                        case 0:  return 2.0f * ph - 1.0f - polyBlep (ph, inc);
                        case 3:  return 4.0f * std::abs (ph - 0.5f) - 1.0f;
                        default:
                        {
                            const float pw = d.wave == 1 ? 0.5f : d.pulseWidth;
                            float v = ph < pw ? 1.0f : -1.0f;
                            v += polyBlep (ph, inc);
                            float p2 = ph + 1.0f - pw; p2 -= std::floor (p2);
                            v -= polyBlep (p2, inc);
                            return v;
                        }
                    }
                };
                out = oscillator (phase[0], freq);
                if (d.wave2Mix > 0.0f) out = out * (1.0f - d.wave2Mix * 0.5f) + d.wave2Mix * 0.5f * oscillator (phase2, freq * std::pow (2.0f, d.wave2Detune / 12.0f));
                if (d.formant)
                {
                    // "aah": two vocal-tract resonances
                    formant1.set (800.0f, 6.0f, sr);
                    formant2.set (1150.0f, 7.0f, sr);
                    out = 0.6f * formant1.bp (out) + 0.4f * formant2.bp (out);
                    out *= 2.2f;
                }
                else
                {
                    float cutoff = d.cutoff * std::pow (2.0f, d.keyTrack * (semis - 60.0f) / 12.0f) + d.envAmt * std::exp (-t / juce::jmax (0.01f, d.envDecay));
                    cutoff *= std::pow (2.0f, brightness) * (0.6f + 0.4f * vel);
                    filter.set (cutoff, 0.6f + d.reso * 3.0f, sr);
                    out = filter.lp (out);
                }
                out *= 0.5f;
            }
            if (d.breath > 0.0f)
            {
                seed = seed * 1664525u + 1013904223u;
                const float n = (float) (int) seed * (1.0f / 2147483648.0f);
                out += n * d.breath * (0.5f + 0.5f * env);
            }
            if (d.tremolo > 0.0f) out *= 1.0f - d.tremolo * (0.5f + 0.5f * std::sin (twoPi * 5.5f * t));
            t += dt;
            return out * env * vel * d.level;
        }
    };

    /** Small stereo modulated delay used for the ensemble and the tape wow. */
    struct ModDelay
    {
        std::vector<float> buf;
        int pos = 0;
        void prepare (int size) { buf.assign ((size_t) size, 0.0f); pos = 0; }
        void write (float x) { buf[(size_t) pos] = x; if (++pos >= (int) buf.size()) pos = 0; }
        float read (float delaySamples) const
        {
            const int size = (int) buf.size();
            float rp = (float) pos - delaySamples;
            while (rp < 0) rp += (float) size;
            const int i0 = (int) rp % size, i1 = (i0 + 1) % size;
            const float f = rp - std::floor (rp);
            return buf[(size_t) i0] * (1.0f - f) + buf[(size_t) i1] * f;
        }
    };
}

// =====================================================================================================
struct HomeKeys::Impl
{
    double sr = 48000.0;
    std::array<Voice, 24> voices;
    DrumSynth drums;
    ModDelay ensL, ensR, wowL, wowR, wobL, wobR;
    float wobblePhase = 0.0f;
    Svf driveToneL, driveToneR;
    juce::Reverb reverb;
    std::vector<float> revL, revR;
    float lfo = 0.0f, wowPhase = 0.0f, flutterPhase = 0.0f;
    Svf ageL, ageR;
    juce::uint32 hiss = 12345;

    // left hand / chords
    std::bitset<128> held;
    int root = -1, type = 0;           // remembered chord (like the real thing, it keeps playing after you let go)
    std::vector<int> chordNotes;
    int bassNote = -1;

    // rhythm
    double internalPpq = 0.0;
    juce::int64 lastGlobalStep = -1;
    int fillBar = -1;
    bool fillPlayed = false;

    Voice* freeVoice()
    {
        for (auto& v : voices) if (! v.active) return &v;
        Voice* oldest = &voices[0];
        for (auto& v : voices) if (v.released && (! oldest->released || v.relT > oldest->relT)) oldest = &v;
        if (! oldest->released) for (auto& v : voices) if (v.t > oldest->t) oldest = &v;
        return oldest;
    }

    void noteOn (int note, float vel, int tone, int group)
    {
        // retrigger the same note in the same group
        for (auto& v : voices) if (v.active && v.note == note && v.group == group && ! v.released) v.release (0.2f);
        freeVoice()->start (note, vel, tone, group);
    }
    void noteOff (int note, int group, float releaseScale)
    {
        for (auto& v : voices) if (v.active && v.note == note && v.group == group) v.release (releaseScale);
    }
    void releaseGroup (int group, float scale) { for (auto& v : voices) if (v.active && v.group == group) v.release (scale); }

    void detectChord (int mode, int split)
    {
        std::vector<int> notes;
        for (int n = 0; n < split; ++n) if (held[(size_t) n]) notes.push_back (n);
        if (notes.empty()) return;   // keep the last chord
        auto isBlack = [] (int n) { const int pc = n % 12; return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; };
        if (mode == 1)
        {
            // single finger: the highest key is the root; a black key below = minor, a white key below = seventh
            const int r = notes.back();
            bool black = false, white = false;
            for (size_t i = 0; i + 1 < notes.size(); ++i) (isBlack (notes[i]) ? black : white) = true;
            root = r % 12;
            type = black && white ? 3 : black ? 1 : white ? 2 : 0;
            return;
        }
        // fingered: find a root whose intervals make a triad (7ths optional)
        std::set<int> pcs;
        for (int n : notes) pcs.insert (n % 12);
        for (int pass = 0; pass < 2; ++pass)
            for (int r : pcs)
            {
                auto has = [&] (int iv) { return pcs.count ((r + iv) % 12) > 0; };
                const bool fifth = has (7) || pass == 1;
                if (has (4) && fifth) { root = r; type = has (10) ? 2 : 0; return; }
                if (has (3) && fifth) { root = r; type = has (10) ? 3 : 1; return; }
            }
        root = notes.front() % 12;   // one or two notes: major on the lowest
        type = 0;
    }

    std::vector<int> voiceChord() const
    {
        if (root < 0) return {};
        std::vector<int> iv = { 0, (type == 1 || type == 3) ? 3 : 4, 7 };
        if (type >= 2) iv.push_back (10);
        // close voicing around F3..F4
        std::vector<int> out;
        for (int i : iv)
        {
            int n = 53 + ((root + i - 53) % 12 + 12) % 12;
            out.push_back (n);
        }
        std::sort (out.begin(), out.end());
        return out;
    }
};

// =====================================================================================================
juce::StringArray HomeKeys::toneNames()
{
    juce::StringArray s;
    for (int i = 0; i < numPublicTones; ++i) s.add (tones[toneIndex (i)].name);
    return s;
}

juce::StringArray HomeKeys::rhythmNames()
{
    juce::StringArray s;
    for (auto& r : vintageRhythms()) s.add (r.name);
    return s;
}

juce::StringArray HomeKeys::kitNames()
{
    auto k = DrumSynth::kitNames();
    k.add ("Portable '81");
    return k;
}

juce::String HomeKeys::currentRhythmName() const
{
    const int r = (int) param ("rhythm");
    if (param ("bank") > 0.5f)
        return portableRhythmNames()[juce::jlimit (0, 7, r)] + (param ("variation") > 0.5f ? " II" : " I");
    return rhythmNames()[juce::jlimit (0, rhythmNames().size() - 1, r)];
}

juce::String HomeKeys::chordName (int r, int t)
{
    if (r < 0) return "-";
    static const char* names[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    static const char* suffix[] = { "", "m", "7", "m7" };
    return juce::String (names[r % 12]) + suffix[juce::jlimit (0, 3, t)];
}

static prm::Layout homeKeysLayout()
{
    prm::Layout l;
    prm::addChoice (l, "tone", "Voice", HomeKeys::toneNames(), 3);
    prm::addDb (l, "volume", "Volume", -30.0f, 6.0f, -4.0f);
    prm::addBool (l, "ensemble", "Ensemble", true);
    prm::addBool (l, "vibrato", "Vibrato", false);
    prm::addBool (l, "sustain", "Sustain", false);
    prm::addPercent (l, "vintage", "Vintage", 0.35f);
    prm::addFloat (l, "bright", "Brightness", -1.0f, 1.0f, 0.0f, "", 0.0f, 2);
    prm::addChoice (l, "rhythm", "Rhythm", HomeKeys::rhythmNames(), 0);
    prm::addBool (l, "rhythmOn", "Rhythm with song", true);
    prm::addBool (l, "syncStart", "Sync Start", false);
    prm::addFloat (l, "tempo", "Tempo", 40.0f, 220.0f, 96.0f, "bpm", 0.0f, 0);
    prm::addDb (l, "rhythmVol", "Rhythm Volume", -30.0f, 6.0f, -6.0f);
    prm::addChoice (l, "kit", "Rhythm Sound", HomeKeys::kitNames(), 0);
    prm::addChoice (l, "abc", "Auto Accompaniment", { "Off", "Single Finger", "Fingered" }, 0);
    prm::addFloat (l, "split", "Split", 36.0f, 72.0f, 54.0f, "", 0.0f, 0);
    prm::addChoice (l, "chordTone", "Chord Voice", HomeKeys::toneNames(), 3);
    prm::addDb (l, "chordVol", "Chord Volume", -30.0f, 6.0f, -10.0f);
    prm::addDb (l, "bassVol", "Bass Volume", -30.0f, 6.0f, -6.0f);
    // 3.6: the effects it was always played through
    prm::addPercent (l, "drive", "Amp Drive", 0.0f);
    prm::addPercent (l, "wobble", "Wobble", 0.0f);
    prm::addFloat (l, "wobbleRate", "Wobble Speed", 0.3f, 9.0f, 5.0f, "Hz", 2.5f, 1);
    prm::addPercent (l, "reverb", "Reverb", 0.0f);
    prm::addPercent (l, "reverbSize", "Reverb Size", 0.8f);
    // 3.6.1: the early-80s portable's own rhythm section (8 rhythms with variation I / II, a fill every 8 bars)
    prm::addChoice (l, "bank", "Rhythm Bank", { "HomeKeys 20", "Portable '81" }, 0);
    prm::addBool (l, "variation", "Variation II", false);
    prm::addBool (l, "autoFill", "8-Bar Variation", false);
    return l;
}

HomeKeys::HomeKeys() : BuiltinProcessor ("homekeys", "HomeKeys 20", true, homeKeysLayout()), impl (std::make_unique<Impl>()) {}
HomeKeys::~HomeKeys() = default;

juce::StringArray HomeKeys::presetNames()
{
    return { "Teen Dream Organ", "Gila Organ (Devotion)", "Thrift Store Organ + Slow Rock ('06)", "Dream Organ, Dry",
             "Dream Organ + '81 Rock Beat", "Dream Organ + '81 Waltz", "Dream Organ + '81 Rhumba",
             "Dream Pop Organ (Slow Rock)", "Bedroom Waltz", "Tropical Bossa", "Haunted Music Box", "Cassette Strings",
             "Disco Brass", "Choir in the Attic", "Vibes Lounge" };
}

juce::StringArray HomeKeys::getProgramNames() { return presetNames(); }

void HomeKeys::loadProgram (int index)
{
    struct P { int tone, rhythm; float tempo, vintage; bool ens, vib, sus; int abc, chordTone, kit;
               bool rhythmOn = true; float drive = 0, wobble = 0, wobbleRate = 5.0f, reverb = 0, reverbSize = 0.8f, bright = 0;
               int bank = 0; bool variation = false, autoFill = false; float rhythmVol = -6.0f; };
    const int ps = DrumSynth::numKits;   // the "Portable '81" kit choice
    static const P presets[] = {
        // Beach House-style organ: the digital organ, warbling, through a small amp, drenched in reverb (no drums)
        // (START plays the portable's Rock I)
        { 16, 3, 92, 0.3f, true, false, false, 0, 16, ps, false, 0.3f, 0.32f, 5.4f, 0.55f, 0.88f, -0.25f, 1, false, true, -9.0f },
        // Devotion: the biting organ, slower and deeper wobble, longer notes, a bit more worn
        { 17, 3, 84, 0.45f, true, false, true, 0, 17, ps, false, 0.4f, 0.48f, 4.4f, 0.45f, 0.8f, -0.35f, 1, true, true, -9.0f },
        // the first album: the organ with the keyboard's own slow rock rhythm and one-finger chords
        // (slow rock comes from the old organs' rhythm units: the '81 portable has no slow rock)
        { 16, 0, 68, 0.55f, true, false, false, 1, 16, 1, true, 0.35f, 0.26f, 5.2f, 0.4f, 0.75f, -0.3f, 0, false, false, -8.0f },
        // the bare organ: no wobble, no reverb (add your own)
        { 16, 3, 96, 0.15f, false, false, false, 0, 16, ps, false, 0.0f, 0.0f, 5.0f, 0.0f, 0.8f, 0.0f, 1, false, false, -6.0f },
        // the organ on top of the portable's own rhythm section, one-finger chords in the left hand
        { 16, 3, 96, 0.35f, true, false, false, 1, 16, ps, true, 0.3f, 0.3f, 5.2f, 0.45f, 0.82f, -0.25f, 1, false, true, -8.0f },
        { 17, 2, 108, 0.4f, true, false, true, 1, 16, ps, true, 0.35f, 0.4f, 4.6f, 0.5f, 0.85f, -0.3f, 1, true, true, -8.0f },
        { 16, 6, 100, 0.4f, true, false, false, 1, 16, ps, true, 0.3f, 0.3f, 5.0f, 0.45f, 0.8f, -0.25f, 1, false, true, -8.0f },

        { 3, 0, 64, 0.45f, true, true, false, 1, 3, 0 },      // organ + slow rock
        { 1, 1, 132, 0.4f, true, false, true, 1, 1, 1 },      // EP + waltz
        { 6, 2, 128, 0.3f, true, true, false, 2, 1, 1 },      // flute + bossa
        { 14, 0, 70, 0.6f, false, false, true, 0, 14, 3 },    // music box
        { 11, 8, 88, 0.55f, true, false, true, 1, 11, 0 },    // strings + soft rock
        { 9, 5, 120, 0.2f, true, false, false, 2, 3, 2 },     // trumpet + disco
        { 15, 9, 72, 0.5f, true, true, true, 1, 15, 0 },      // human voice + ballad
        { 12, 3, 108, 0.25f, true, false, true, 2, 12, 1 },   // vibes + rhumba
    };
    const auto& p = presets[juce::jlimit (0, (int) std::size (presets) - 1, index)];
    setParam ("tone", (float) p.tone);
    setParam ("rhythm", (float) p.rhythm);
    setParam ("tempo", p.tempo);
    setParam ("vintage", p.vintage);
    setParam ("ensemble", p.ens ? 1.0f : 0.0f);
    setParam ("vibrato", p.vib ? 1.0f : 0.0f);
    setParam ("sustain", p.sus ? 1.0f : 0.0f);
    setParam ("abc", (float) p.abc);
    setParam ("chordTone", (float) p.chordTone);
    setParam ("kit", (float) p.kit);
    setParam ("rhythmOn", p.rhythmOn ? 1.0f : 0.0f);
    setParam ("drive", p.drive);
    setParam ("wobble", p.wobble);
    setParam ("wobbleRate", p.wobbleRate);
    setParam ("reverb", p.reverb);
    setParam ("reverbSize", p.reverbSize);
    setParam ("bright", p.bright);
    setParam ("bank", (float) p.bank);
    setParam ("variation", p.variation ? 1.0f : 0.0f);
    setParam ("autoFill", p.autoFill ? 1.0f : 0.0f);
    setParam ("rhythmVol", p.rhythmVol);
}

void HomeKeys::prepareToPlay (double sr, int block)
{
    auto& m = *impl;
    m.sr = sr;
    for (auto& v : m.voices) v.active = false;
    m.drums.prepare (sr);
    const int maxDelay = (int) (sr * 0.05) + 4;
    m.ensL.prepare (maxDelay); m.ensR.prepare (maxDelay);
    m.wowL.prepare (maxDelay); m.wowR.prepare (maxDelay);
    m.wobL.prepare (maxDelay); m.wobR.prepare (maxDelay);
    m.driveToneL.reset(); m.driveToneR.reset();
    m.reverb.setSampleRate (sr);
    m.reverb.reset();
    m.ageL.reset(); m.ageR.reset();
    m.lastGlobalStep = -1;
    m.revL.assign ((size_t) juce::jmax (16, block) * 2, 0.0f);
    m.revR.assign ((size_t) juce::jmax (16, block) * 2, 0.0f);
}

void HomeKeys::startStop()
{
    if (internalRunning.load()) stopRequested = true;
    else startRequested = true;
}

void HomeKeys::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals nd;
    auto& m = *impl;
    const int n = buffer.getNumSamples();
    buffer.clear();
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (buffer.getNumChannels() > 1 ? 1 : 0);
    const float sr = (float) m.sr;

    const int tone = toneIndex ((int) param ("tone"));
    const int chordTone = toneIndex ((int) param ("chordTone"));
    const int abc = (int) param ("abc");
    const int split = (int) param ("split");
    const bool sustain = param ("sustain") > 0.5f;
    const bool vibrato = param ("vibrato") > 0.5f;
    const float bright = param ("bright");
    const float releaseScale = sustain ? 5.0f : 1.0f;
    const int kitChoice = (int) param ("kit");
    m.drums.setKit (kitChoice == DrumSynth::numKits ? (int) DrumSynth::portable81 : kitChoice);
    const bool portable = param ("bank") > 0.5f;
    const auto& rhythms = portable ? portableRhythms() : vintageRhythms();
    const int rhythmIndex = portable ? juce::jlimit (0, 7, (int) param ("rhythm")) * 2 + (param ("variation") > 0.5f ? 1 : 0)
                                     : juce::jlimit (0, (int) rhythms.size() - 1, (int) param ("rhythm"));
    const auto& rhythm = rhythms[(size_t) rhythmIndex];
    const bool autoFill = param ("autoFill") > 0.5f;

    // ---- time: the song's transport when it's rolling, otherwise our own clock ----
    double ppq = m.internalPpq, bpm = param ("tempo");
    bool running = false;
    bool songRolling = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (pos->getIsPlaying())
            {
                songRolling = true;
                if (auto p = pos->getPpqPosition()) ppq = *p;
                if (auto b = pos->getBpm()) bpm = *b;
            }
    if (songRolling != hostPlaying.load()) { hostPlaying = songRolling; m.lastGlobalStep = -1; m.releaseGroup (1, 1.0f); m.releaseGroup (2, 1.0f); }
    if (songRolling)
    {
        running = param ("rhythmOn") > 0.5f;
        internalRunning = false;
    }
    else
    {
        if (startRequested.exchange (false)) { internalRunning = true; m.internalPpq = 0.0; ppq = 0.0; m.lastGlobalStep = -1; }
        if (stopRequested.exchange (false))
        {
            internalRunning = false;
            m.releaseGroup (1, 1.0f); m.releaseGroup (2, 1.0f);
        }
        running = internalRunning.load();
    }
    const double beatsPerSample = bpm / 60.0 / m.sr;
    const double stepBeats = (double) rhythm.beats / rhythm.steps;
    const bool minor = m.type == 1 || m.type == 3;

    auto playStep = [&] (juce::int64 globalStep)
    {
        const int stepInBar = (int) (globalStep % rhythm.steps);
        const int bar = (int) (globalStep / rhythm.steps);
        currentStep = stepInBar;
        currentBar = bar;
        if (stepInBar == 0)
        {
            if (m.fillPlayed) { m.drums.trigger (49, 0.85f); m.fillPlayed = false; }   // crash after a fill
            if (m.fillBar >= 0 && m.fillBar < bar) m.fillBar = -1;
        }
        if (fillRequested.exchange (false)) m.fillBar = bar;
        if (autoFill && stepInBar == 0 && bar % 8 == 7) m.fillBar = bar;   // the variation fill every 8th bar
        const bool fill = m.fillBar == bar;
        if (fill) m.fillPlayed = true;

        const float rhythmGain = juce::Decibels::decibelsToGain (param ("rhythmVol"));
        const auto lanes = fill ? vintageFill (rhythm) : rhythm.drums;
        for (auto& [note, steps] : lanes)
            if (stepInBar < steps.length())
                if (const float v = stepVelocity (steps[stepInBar]); v > 0.0f)
                    m.drums.trigger (note, v * rhythmGain);

        if (abc == 0 || m.root < 0) return;
        // bass
        const auto b = stepInBar < rhythm.bass.length() ? rhythm.bass[stepInBar] : '.';
        if (b == '.') { if (m.bassNote >= 0) m.noteOff (m.bassNote, 2, 1.0f); m.bassNote = -1; }
        else if (b != '-')
        {
            if (m.bassNote >= 0) m.noteOff (m.bassNote, 2, 0.3f);
            int bassRoot = 36 + m.root;
            if (bassRoot > 43) bassRoot -= 12;
            m.bassNote = bassRoot + bassInterval (b, minor);
            m.noteOn (m.bassNote, (stepInBar == 0 ? 0.95f : 0.8f) * juce::Decibels::decibelsToGain (param ("bassVol")), bassTone, 2);   // internal index
        }
        // chord
        const auto c = stepInBar < rhythm.chord.length() ? rhythm.chord[stepInBar] : '.';
        if (c == '.') { m.releaseGroup (1, 1.0f); }
        else if (c == 'X' || c == 'x')
        {
            m.releaseGroup (1, 0.25f);
            const float v = (c == 'X' ? 0.8f : 0.55f) * juce::Decibels::decibelsToGain (param ("chordVol"));
            for (int note : m.voiceChord()) m.noteOn (note, v, chordTone, 1);
        }
    };

    // ---- run sample by sample between MIDI events, steps and the end of the block ----
    auto it = midi.cbegin();
    int pos = 0;
    double ppqAt = ppq;
    const bool vintageOn = param ("vintage") > 0.001f;
    juce::ignoreUnused (vintageOn);

    while (pos < n)
    {
        // next event time
        int nextEvent = n;
        if (it != midi.cend()) nextEvent = juce::jlimit (pos, n, (*it).samplePosition);
        // next step boundary
        int nextStep = n;
        if (running)
        {
            const juce::int64 currentGlobal = (juce::int64) std::floor (ppqAt / stepBeats + 1.0e-9);
            if (currentGlobal != m.lastGlobalStep && ppqAt >= 0.0)
            {
                m.lastGlobalStep = currentGlobal;
                playStep (currentGlobal);
            }
            const double nextBoundary = (double) (currentGlobal + 1) * stepBeats;
            nextStep = juce::jlimit (pos + 1, n, pos + (int) std::ceil ((nextBoundary - ppqAt) / beatsPerSample - 1.0e-9));
        }
        const int until = juce::jmin (nextEvent, nextStep);

        // render [pos, until)
        for (int i = pos; i < until; ++i)
        {
            float melody = 0.0f, accomp = 0.0f;
            for (auto& v : m.voices)
                if (v.active)
                {
                    const float s = v.render (sr, vibrato, bright);
                    if (v.group == 0) melody += s; else accomp += s;
                }
            L[i] += melody + accomp;
            R[i] += melody + accomp;
        }
        if (until > pos)
        {
            m.drums.render (L + pos, R + pos, until - pos);
            ppqAt += (until - pos) * beatsPerSample;
        }
        pos = until;

        // MIDI at this position
        while (it != midi.cend() && (*it).samplePosition <= pos)
        {
            const auto msg = (*it).getMessage();
            ++it;
            if (msg.isNoteOn())
            {
                const int note = msg.getNoteNumber();
                const float vel = msg.getFloatVelocity();
                if (abc > 0 && note < split)
                {
                    m.held.set ((size_t) note);
                    m.detectChord (abc, split);
                    chordRoot = m.root; chordType = m.type;
                    if (! running && param ("syncStart") > 0.5f && ! songRolling)
                    {
                        internalRunning = true; running = true;
                        m.internalPpq = 0.0; ppqAt = 0.0; m.lastGlobalStep = -1;
                    }
                    if (! running)   // no rhythm: the left hand plays the chord voice directly
                        m.noteOn (note, vel * juce::Decibels::decibelsToGain (param ("chordVol")) * 1.6f, chordTone, 1);
                }
                else m.noteOn (note, vel, tone, 0);
            }
            else if (msg.isNoteOff())
            {
                const int note = msg.getNoteNumber();
                if (abc > 0 && note < split) { m.held.reset ((size_t) note); if (! running) m.noteOff (note, 1, releaseScale); }
                else m.noteOff (note, 0, releaseScale);
            }
            else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            {
                m.releaseGroup (0, 1.0f); m.releaseGroup (1, 1.0f); m.releaseGroup (2, 1.0f);
                m.held.reset();
            }
        }
    }
    if (! songRolling && running) m.internalPpq = ppqAt;
    if (! running) currentStep = -1;

    // ---- ensemble, vintage tape, output ----
    const bool ensemble = param ("ensemble") > 0.5f;
    const float vintage = param ("vintage");
    const float vol = juce::Decibels::decibelsToGain (param ("volume"));
    m.ageL.set (16000.0f * std::pow (0.22f, vintage), 0.6f, sr);
    m.ageR.set (16000.0f * std::pow (0.22f, vintage), 0.6f, sr);
    const float hissLevel = vintage * vintage * 0.012f;
    const float bits = std::pow (2.0f, 15.0f - vintage * 7.0f);
    const float drive = param ("drive"), wobble = param ("wobble"), wobbleRate = param ("wobbleRate");
    const float driveGain = 1.0f + drive * 9.0f, bias = 0.08f * drive, biasOut = std::tanh (bias * driveGain);
    m.driveToneL.set (6500.0f - 3500.0f * drive, 0.7f, sr);   // a small amp's speaker
    m.driveToneR.set (6500.0f - 3500.0f * drive, 0.7f, sr);
    for (int i = 0; i < n; ++i)
    {
        float l = L[i], r = R[i];
        if (drive > 0.001f)
        {
            // played through a small valve amp: soft, slightly asymmetric clipping and a rolled-off top
            // (unity gain for quiet playing, so turning it up adds grit rather than volume)
            auto amp = [&] (float x) { return (std::tanh ((x + bias) * driveGain) - biasOut) / driveGain * (1.0f + drive); };
            l = m.driveToneL.lp (l * (1.0f - drive) + amp (l) * drive);
            r = m.driveToneR.lp (r * (1.0f - drive) + amp (r) * drive);
        }
        if (ensemble)
        {
            m.lfo += 0.6f / sr; if (m.lfo >= 1.0f) m.lfo -= 1.0f;
            const float mono = 0.5f * (l + r);
            m.ensL.write (mono); m.ensR.write (mono);
            const float dl = (0.007f + 0.0025f * std::sin (twoPi * m.lfo)) * sr;
            const float dr = (0.007f + 0.0025f * std::sin (twoPi * (m.lfo + 0.33f))) * sr;
            l = 0.65f * l + 0.55f * m.ensL.read (dl);
            r = 0.65f * r + 0.55f * m.ensR.read (dr);
        }
        if (wobble > 0.001f)
        {
            // a vibrato pedal: pure pitch wobble (all wet), with a slower drift on top so it never sounds mechanical
            m.wobblePhase += wobbleRate / sr; if (m.wobblePhase >= 1.0f) m.wobblePhase -= 1.0f;
            const float lfo = std::sin (twoPi * m.wobblePhase) + 0.25f * std::sin (twoPi * m.wobblePhase * 0.37f + 1.0f);
            const float base = 0.0045f * sr, depth = wobble * 0.0032f * sr * 5.0f / juce::jmax (1.5f, wobbleRate);
            m.wobL.write (l); m.wobR.write (r);
            l = m.wobL.read (base + depth * lfo);
            r = m.wobR.read (base + depth * (0.9f * lfo + 0.1f * std::sin (twoPi * m.wobblePhase + 0.8f)));
        }
        if (vintage > 0.001f)
        {
            // wow & flutter, a worn tone, a little hiss and fewer bits
            m.wowPhase += 0.55f / sr; if (m.wowPhase >= 1.0f) m.wowPhase -= 1.0f;
            m.flutterPhase += 7.0f / sr; if (m.flutterPhase >= 1.0f) m.flutterPhase -= 1.0f;
            const float wobble = (0.003f + vintage * (0.0018f * std::sin (twoPi * m.wowPhase) + 0.0003f * std::sin (twoPi * m.flutterPhase))) * sr;
            m.wowL.write (l); m.wowR.write (r);
            l = m.wowL.read (wobble); r = m.wowR.read (wobble);
            l = m.ageL.lp (l); r = m.ageR.lp (r);
            m.hiss = m.hiss * 1664525u + 1013904223u;
            const float h = (float) (int) m.hiss * (1.0f / 2147483648.0f) * hissLevel;
            l = std::round ((l + h) * bits) / bits;
            r = std::round ((r + h) * bits) / bits;
        }
        L[i] = l;
        if (R != L) R[i] = r;
    }

    // a big, soft reverb: the sound is half room
    const float rev = param ("reverb");
    if (rev > 0.001f && (int) m.revL.size() >= n)
    {
        juce::Reverb::Parameters rp;
        rp.roomSize = 0.55f + 0.44f * param ("reverbSize");
        rp.damping = 0.55f;
        rp.width = 1.0f;
        rp.wetLevel = 1.0f;
        rp.dryLevel = 0.0f;
        m.reverb.setParameters (rp);
        std::copy (L, L + n, m.revL.begin());
        std::copy (R, R + n, m.revR.begin());
        m.reverb.processStereo (m.revL.data(), m.revR.data(), n);
        const float dry = std::cos (rev * juce::MathConstants<float>::halfPi * 0.6f), wet = std::sin (rev * juce::MathConstants<float>::halfPi) * 0.42f;
        for (int i = 0; i < n; ++i)
        {
            L[i] = L[i] * dry + m.revL[(size_t) i] * wet;
            if (R != L) R[i] = R[i] * dry + m.revR[(size_t) i] * wet;
        }
    }
    for (int i = 0; i < n; ++i)
    {
        L[i] = std::tanh (L[i] * vol);
        if (R != L) R[i] = std::tanh (R[i] * vol);
    }
}

} // namespace wis::daw
