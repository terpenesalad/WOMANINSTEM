#include "RoomIr.h"
#include <cmath>

namespace wis::daw::roomir
{

namespace
{
    struct Echo { float ms, gain; };

    struct Spec
    {
        const char* name;
        const char* description;
        float rtLow, rtMid, rtHigh;      // seconds to -60 dB per band (< 250 Hz, middle, > 3.5 kHz)
        int erCount;                     // early reflections
        float erFirstMs, erSpreadMs;     // first reflection and how long they keep coming
        float erLevel, lateLevel;        // relative energy of the two parts
        float erCutHz;                   // early reflections are this bright
        float buildMs;                   // how slowly the diffuse tail swells in
        float decorrelate;               // 0 = same tail left and right .. 1 = fully independent
        std::vector<Echo> echoes;        // discrete echoes (canyon walls, ground bounce)
        float flutterMs = 0.0f;          // regular slap pattern between parallel walls
        bool spring = false;
    };

    const std::vector<Spec>& specs()
    {
        static const std::vector<Spec> s {
            { "Dry", "No space at all: the piano straight into the console", 0, 0, 0, 0, 0, 0, 0, 0, 20000, 0, 0, {} },
            { "Vocal Booth", "A small dead booth: just a hint of air around the notes", 0.10f, 0.12f, 0.08f, 6, 1.5f, 8, 1.0f, 0.15f, 9000, 2, 0.8f, {} },
            { "Living Room", "A sofa, a rug and curtains: close, warm and homely", 0.35f, 0.45f, 0.28f, 14, 3.0f, 30, 0.9f, 0.4f, 7000, 6, 0.85f, {} },
            { "Wooden Studio", "A wood-panelled tracking room: warm, tight and flattering", 0.55f, 0.6f, 0.45f, 18, 4.0f, 40, 0.8f, 0.5f, 9000, 8, 0.9f, {} },
            { "Big Live Room", "A large studio live room with a high ceiling: big but still controlled", 1.1f, 1.2f, 0.85f, 24, 9.0f, 70, 0.7f, 0.65f, 11000, 18, 0.95f, {} },
            { "Bar / Club", "A late-night bar: low ceiling, bodies, wood and glass", 0.7f, 0.8f, 0.45f, 20, 3.5f, 45, 0.8f, 0.45f, 6000, 10, 0.9f, {} },
            { "Bathroom", "Tiles everywhere: short, bright and ringing", 0.9f, 1.1f, 1.0f, 26, 1.8f, 25, 1.0f, 0.8f, 14000, 4, 0.8f, {} },
            { "Concert Hall", "A shoebox concert hall: smooth, wide and lush", 2.0f, 2.2f, 1.5f, 28, 14.0f, 90, 0.55f, 0.9f, 9000, 30, 1.0f, {} },
            { "Church", "Stone and wooden pews: long, soft and reverent", 3.2f, 3.4f, 2.0f, 26, 18.0f, 110, 0.5f, 1.0f, 7000, 45, 1.0f, {} },
            { "Cathedral", "Endless stone: the notes hang in the air for seconds", 6.0f, 6.0f, 3.5f, 30, 28.0f, 160, 0.4f, 1.0f, 6000, 70, 1.0f, {} },
            { "Forest Clearing", "Outdoors among the trees: scattered reflections from trunks, the ground bounce, no ceiling", 0.35f, 0.75f, 0.5f, 90, 9.0f, 280, 0.9f, 0.3f, 12000, 25, 1.0f, { { 4.5f, 0.55f } } },
            { "Canyon", "Rock walls far away: distinct echoes that come back again and again", 1.2f, 2.4f, 1.4f, 8, 25.0f, 80, 0.4f, 0.22f, 5000, 40, 1.0f,
              { { 190.0f, 0.62f }, { 430.0f, 0.45f }, { 720.0f, 0.32f }, { 1060.0f, 0.22f }, { 1480.0f, 0.15f }, { 1950.0f, 0.1f } } },
            { "Car Park", "An underground car park: hard concrete, a metallic flutter and a long grey tail", 2.4f, 2.6f, 1.8f, 30, 6.0f, 120, 0.7f, 0.9f, 12000, 15, 0.95f, {}, 23.0f },
            { "Plate", "A studio plate reverb: dense, bright and smooth, no room at all", 2.0f, 2.3f, 2.1f, 0, 0.5f, 0, 0.0f, 1.0f, 16000, 3, 1.0f, {} },
            { "Spring Tank", "A guitar-amp spring reverb: boingy, drippy and very 60s", 1.6f, 2.0f, 1.2f, 0, 0.5f, 0, 0.0f, 0.5f, 6000, 2, 0.7f, {}, 0.0f, true },
            // 3.7: spaces for junk percussion and game sound design
            { "Concrete Storeroom", "A bare concrete storeroom with a hard floor: close, boxy and ringing (where junkyard percussion lives)", 0.9f, 1.0f, 0.75f, 22, 2.2f, 28, 1.0f, 0.7f, 12000, 5, 0.85f, {}, 9.0f },
            { "Tin Shed", "A corrugated-iron shed: tinny, metallic and bright, with a flutter between the walls", 0.6f, 0.8f, 0.9f, 20, 2.5f, 30, 1.0f, 0.6f, 15000, 4, 0.8f, {}, 13.0f },
            { "Cave", "A deep limestone cave: dark, enormous and dripping, with far walls answering", 3.5f, 4.2f, 1.6f, 26, 12.0f, 160, 0.6f, 0.95f, 5000, 60, 1.0f, { { 260.0f, 0.3f }, { 610.0f, 0.18f } } },
            { "Stairwell", "A tall concrete stairwell: a long, bright flutter climbing up the floors", 2.2f, 2.8f, 2.0f, 24, 3.0f, 90, 0.8f, 0.85f, 12000, 12, 0.9f, {}, 31.0f },
        };
        return s;
    }

    /** One-pole low-pass, used to split the noise into bands. */
    struct OnePole
    {
        float a = 0.0f, z = 0.0f;
        void set (double fc, double sr) { a = (float) std::exp (-juce::MathConstants<double>::twoPi * fc / sr); }
        float lp (float x) noexcept { z = x + a * (z - x); return z; }
    };
}

juce::StringArray spaceNames()
{
    juce::StringArray n;
    for (auto& s : specs()) n.add (s.name);
    return n;
}

juce::String spaceDescription (int space)
{
    return juce::isPositiveAndBelow (space, (int) specs().size()) ? juce::String (specs()[(size_t) space].description) : juce::String();
}

float decaySeconds (int space)
{
    return juce::isPositiveAndBelow (space, (int) specs().size()) ? specs()[(size_t) space].rtMid : 0.0f;
}

juce::AudioBuffer<float> design (int space, double sr, float size, float tone, float predelayMs, juce::uint32 seed)
{
    space = juce::jlimit (0, numSpaces - 1, space);
    const auto& sp = specs()[(size_t) space];
    size = juce::jlimit (0.3f, 3.0f, size);
    tone = juce::jlimit (0.0f, 1.0f, tone);

    if (space == dry)
    {
        juce::AudioBuffer<float> b (2, 1);
        b.setSample (0, 0, 1.0f); b.setSample (1, 0, 1.0f);
        return b;
    }

    // tone: the high band's decay (and the reflections' brightness) up to 2x shorter / longer
    const float toneScale = std::pow (2.0f, (tone - 0.5f) * 2.0f);
    const float rtL = sp.rtLow * size, rtM = sp.rtMid * size, rtH = juce::jmin (sp.rtHigh * size * toneScale, rtM * 1.3f);
    const float timeScale = std::sqrt (size);     // bigger room: reflections spread out
    float lastEcho = 0.0f;
    for (auto& e : sp.echoes) lastEcho = juce::jmax (lastEcho, e.ms * size);
    const float pre = juce::jmax (0.0f, predelayMs) / 1000.0f;
    const float seconds = juce::jlimit (0.15f, 9.0f, pre + juce::jmax (rtL, rtM, rtH) * 1.15f + lastEcho / 1000.0f + 0.05f);
    const int len = (int) (seconds * sr);
    const int preN = (int) (pre * sr);

    juce::AudioBuffer<float> ir (2, len);
    ir.clear();
    juce::Random common (seed * 7919u + (juce::uint32) space);

    for (int ch = 0; ch < 2; ++ch)
    {
        juce::Random rnd (seed * 104729u + (juce::uint32) (space * 31 + ch * 977));
        float* d = ir.getWritePointer (ch);

        // ---- diffuse tail: noise in three bands, each with its own decay, swelling in ----
        if (sp.lateLevel > 0.0f)
        {
            OnePole lo, hiSplit;
            lo.set (250.0, sr);
            hiSplit.set (3500.0, sr);
            const float kL = rtL > 0.0f ? 6.907755f / (rtL * (float) sr) : 1.0f;
            const float kM = rtM > 0.0f ? 6.907755f / (rtM * (float) sr) : 1.0f;
            const float kH = rtH > 0.0f ? 6.907755f / (rtH * (float) sr) : 1.0f;
            const float build = juce::jmax (1.0f, sp.buildMs * timeScale * 0.001f * (float) sr);
            const int start = preN + (int) (sp.erFirstMs * timeScale * 0.001f * (float) sr);
            for (int i = start; i < len; ++i)
            {
                // mix of a shared and an independent noise: decorrelate controls stereo width
                const float own = rnd.nextFloat() + rnd.nextFloat() - 1.0f;
                const float shared = common.nextFloat() + common.nextFloat() - 1.0f;
                const float n = own * sp.decorrelate + shared * (1.0f - sp.decorrelate);
                const float bl = lo.lp (n);
                const float bh = n - hiSplit.lp (n);
                const float bm = n - bl - bh;
                const float t = (float) (i - start);
                const float env = 1.0f - std::exp (-t / build);
                d[i] += sp.lateLevel * env * (bl * std::exp (-kL * t) + bm * std::exp (-kM * t) + bh * std::exp (-kH * t));
            }
        }

        // ---- early reflections: sparse taps, quieter and duller the later they arrive ----
        auto blip = [&] (float timeSec, float gain, float cutHz)
        {
            const int at = preN + (int) (timeSec * (float) sr);
            OnePole f;
            f.set (juce::jlimit (500.0, sr * 0.45, (double) cutHz), sr);
            for (int k = 0; k < 48 && at + k < len; ++k)
                d[at + k] += gain * f.lp (k == 0 ? 1.0f : 0.0f) * (1.0f - std::exp (-(float) (cutHz) / 2000.0f));
        };
        for (int k = 0; k < sp.erCount; ++k)
        {
            const float frac = std::pow (((float) k + rnd.nextFloat()) / (float) sp.erCount, 0.75f);
            const float tMs = (sp.erFirstMs + sp.erSpreadMs * frac) * timeScale;
            const float g = sp.erLevel * juce::jmin (1.0f, sp.erFirstMs / juce::jmax (1.0f, tMs)) * (0.4f + 0.6f * rnd.nextFloat()) * (rnd.nextBool() ? 1.0f : -1.0f);
            blip (tMs * 0.001f, g * 3.0f, sp.erCutHz * toneScale * (1.0f - 0.5f * frac));
        }
        if (sp.flutterMs > 0.0f)
            for (int k = 1; k < 14; ++k)
                blip (sp.flutterMs * timeScale * (float) k * 0.001f * (1.0f + 0.01f * (float) ch), 1.6f * std::pow (0.78f, (float) k), 9000.0f);

        // ---- discrete echoes: little smeared bursts, each one duller than the last ----
        for (size_t e = 0; e < sp.echoes.size(); ++e)
        {
            const auto& ec = sp.echoes[e];
            const float tSec = ec.ms * (ec.ms > 20.0f ? size : 1.0f) * 0.001f * (1.0f + 0.015f * (float) ch * (float) (e + 1));
            OnePole f;
            f.set (juce::jmax (800.0f, 9000.0f * toneScale / (1.0f + (float) e)), sr);
            const int at = preN + (int) (tSec * (float) sr);
            const int smear = (int) (0.012 * sr * (1.0 + (double) e));
            for (int k = 0; k < smear && at + k < len; ++k)
                d[at + k] += ec.gain * 10.0f * f.lp ((rnd.nextFloat() * 2.0f - 1.0f) * std::exp (-6.0f * (float) k / (float) smear)) / std::sqrt ((float) smear / 100.0f);
        }

        // ---- spring tank: a decaying train of round trips, smeared into chirps by an all-pass chain ----
        if (sp.spring)
        {
            std::vector<float> train ((size_t) len, 0.0f);
            const float trip = (0.031f + 0.004f * (float) ch) * size;
            const float k = 6.907755f / (rtM * (float) sr);
            for (float t = 0.0f; t < seconds - pre; t += trip)
            {
                const int at = preN + (int) (t * (float) sr);
                if (at < len) train[(size_t) at] += std::exp (-k * t * (float) sr) * (1.0f + 0.3f * rnd.nextFloat());
            }
            const float c = 0.62f;
            for (int stage = 0; stage < 60; ++stage)
            {
                float x1 = 0.0f, y1 = 0.0f;
                for (auto& v : train)
                {
                    const float y = -c * v + x1 + c * y1;
                    x1 = v; y1 = y;
                    v = y;
                }
            }
            OnePole f;
            f.set (4500.0 * toneScale, sr);
            for (int i = 0; i < len; ++i) d[i] += 2.5f * f.lp (train[(size_t) i]);
        }
    }

    // unit energy per channel
    for (int ch = 0; ch < 2; ++ch)
    {
        double e = 0.0;
        const float* d = ir.getReadPointer (ch);
        for (int i = 0; i < len; ++i) e += (double) d[i] * d[i];
        if (e > 1.0e-12) ir.applyGain (ch, 0, len, (float) (1.0 / std::sqrt (e)));
    }
    return ir;
}

} // namespace wis::daw::roomir
