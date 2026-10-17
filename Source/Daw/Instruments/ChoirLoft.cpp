#include "ChoirLoft.h"
#include "RoomIr.h"
#include <cmath>

namespace wis::daw
{

std::function<juce::AudioProcessorEditor* (ChoirLoft&)> ChoirLoft::editorFactory;

using namespace prm;

namespace
{
    constexpr float twoPi = juce::MathConstants<float>::twoPi;
    constexpr int controlEvery = 16;          // samples between control updates (pitch, vowel, formants)

    inline float mtof (float n) noexcept { return 440.0f * std::exp2 ((n - 69.0f) / 12.0f); }
    inline float smoothstep (float a, float b, float x) noexcept
    {
        const float t = juce::jlimit (0.0f, 1.0f, (x - a) / (b - a));
        return t * t * (3.0f - 2.0f * t);
    }
    inline float lerp (float a, float b, float t) noexcept { return a + (b - a) * t; }
    inline float gauss (juce::Random& r) noexcept   // roughly normal, unit variance
    {
        return (r.nextFloat() + r.nextFloat() + r.nextFloat() + r.nextFloat() - 2.0f) * 1.732f;
    }

    // ---- styles -------------------------------------------------------------------------------------------
    struct StyleDef
    {
        const char* name;
        const char* description;
        float vibRate, vibDepth, vibDelay;   // Hz, semitones (peak), seconds before it comes in
        float rd;                            // added to the folds' Rd: - pressed, + breathy
        float breath, ring, f1Track, belt;   // aspiration x, singer's-formant cluster, F1 follows f0, F1 on 2 x f0
        float scoop, scoopTime;              // semitones below, seconds
        float rasp, nasal, bright, loose, attack;
        bool overtone;
    };

    const std::array<StyleDef, 10>& styles()
    {
        static const std::array<StyleDef, 10> s { {
            { "Classical", "Operatic: a ringing core, wide even vibrato, F1 tuned to high notes", 5.6f, 0.55f, 0.22f, -0.15f, 0.6f, 1.0f, 1.0f, 0.0f, 0.25f, 0.06f, 0.0f, 0.0f, 0.1f, 0.6f, 1.0f, false },
            { "Choral", "Blended church / chamber choir: soft vibrato that arrives late, round vowels", 5.3f, 0.18f, 0.45f, 0.05f, 0.9f, 0.35f, 0.8f, 0.0f, 0.1f, 0.05f, 0.0f, 0.0f, 0.0f, 0.8f, 1.2f, false },
            { "Early Music", "Straight, pure tone with almost no vibrato (renaissance consorts, folk drones)", 5.0f, 0.04f, 0.8f, 0.0f, 0.8f, 0.25f, 0.6f, 0.0f, 0.0f, 0.04f, 0.0f, 0.0f, 0.05f, 0.5f, 1.0f, false },
            { "Gospel", "Full-throated and pressed, big scoops into the notes, a little grit", 5.8f, 0.42f, 0.3f, -0.3f, 0.9f, 0.3f, 0.6f, 0.4f, 1.4f, 0.09f, 0.12f, 0.02f, 0.2f, 1.2f, 0.8f, false },
            { "Pop", "Close and breathy, a late narrow vibrato, gentle scoops", 5.5f, 0.22f, 0.5f, 0.35f, 1.6f, 0.0f, 0.3f, 0.2f, 0.5f, 0.07f, 0.03f, 0.02f, 0.0f, 1.0f, 0.8f, false },
            { "Folk", "Plain, straight and a touch nasal: kitchen-table harmonies", 5.2f, 0.08f, 0.6f, 0.1f, 1.1f, 0.0f, 0.2f, 0.2f, 0.3f, 0.05f, 0.05f, 0.08f, 0.05f, 1.0f, 0.8f, false },
            { "Belt", "Musical-theatre belt: pressed, bright, the first formant riding the 2nd harmonic", 5.9f, 0.35f, 0.55f, -0.4f, 0.6f, 0.5f, 0.0f, 1.0f, 0.4f, 0.06f, 0.02f, 0.02f, 0.3f, 0.8f, 0.7f, false },
            { "Blues & Raspy", "Gravel in the voice, long scoops and bends", 5.0f, 0.3f, 0.4f, -0.15f, 1.2f, 0.0f, 0.2f, 0.5f, 2.0f, 0.14f, 0.45f, 0.02f, 0.1f, 1.2f, 0.8f, false },
            { "Lullaby", "Hushed and airy, half-whispered", 4.8f, 0.12f, 0.6f, 0.9f, 2.2f, 0.0f, 0.3f, 0.0f, 0.2f, 0.08f, 0.0f, 0.02f, -0.3f, 0.8f, 1.6f, false },
            { "Overtone", "Throat / overtone singing: two formants squeezed onto one harmonic so it whistles above the drone (move the vowel left-right to play the overtones)", 5.0f, 0.0f, 1.0f, -0.2f, 0.3f, 0.0f, 0.0f, 0.0f, 0.0f, 0.05f, 0.05f, 0.05f, 0.4f, 0.5f, 1.0f, true },
        } };
        return s;
    }

    // ---- vowels: male reference formants on the chart (rows close..open, columns front..back) ----------
    const float vowelF[4][3][3] = {
        { { 290, 2250, 2950 }, { 330, 1600, 2400 }, { 320,  820, 2300 } },   // i  ɨ  u
        { { 420, 2050, 2650 }, { 500, 1450, 2450 }, { 450,  880, 2400 } },   // e  ə  o
        { { 580, 1800, 2550 }, { 620, 1220, 2500 }, { 590,  940, 2450 } },   // ɛ  ʌ  ɔ
        { { 700, 1700, 2500 }, { 760, 1330, 2500 }, { 720, 1080, 2450 } },   // æ  a  ɑ
    };
    const float refF4 = 3450.0f, refF5 = 4350.0f;
    const float refBw[5] = { 70.0f, 90.0f, 130.0f, 180.0f, 230.0f };
    const float murmurF[5] = { 270.0f, 1150.0f, 2300.0f, 3250.0f, 4250.0f };
    const float murmurBw[5] = { 90.0f, 260.0f, 300.0f, 320.0f, 340.0f };

    std::array<float, 3> chartFormants (float x, float y)
    {
        x = juce::jlimit (0.0f, 1.0f, x) * 2.0f;
        y = juce::jlimit (0.0f, 1.0f, y) * 3.0f;
        const int c0 = juce::jmin (1, (int) x), r0 = juce::jmin (2, (int) y);
        const float fx = x - (float) c0, fy = y - (float) r0;
        std::array<float, 3> f {};
        for (int k = 0; k < 3; ++k)
        {
            const float top = lerp (vowelF[r0][c0][k], vowelF[r0][c0 + 1][k], fx);
            const float bot = lerp (vowelF[r0 + 1][c0][k], vowelF[r0 + 1][c0 + 1][k], fx);
            f[(size_t) k] = lerp (top, bot, fy);
        }
        return f;
    }

    // ---- LF glottal pulse tables (flow derivative, Ee = 1), Rd 0.3 .. 2.7 -----------------------------------
    constexpr int lfTables = 49, lfLen = 1024;
    constexpr float rdMin = 0.3f, rdMax = 2.7f;

    struct LfBank
    {
        std::vector<float> data;     // lfTables * (lfLen + 1)
        std::array<float, lfTables> te {};

        LfBank() : data ((size_t) lfTables * (lfLen + 1))
        {
            for (int t = 0; t < lfTables; ++t)
            {
                const double rd = rdMin + (rdMax - rdMin) * t / (lfTables - 1);
                const double ra = (-1.0 + 4.8 * rd) / 100.0;
                const double rk = (22.4 + 11.8 * rd) / 100.0;
                const double rg = rk / (4.0 * ((0.11 * rd) / (0.5 + 1.2 * rk) - ra));
                const double tp = 1.0 / (2.0 * rg);
                const double teV = tp * (1.0 + rk);
                const double epsilon = 1.0 / ra;
                const double shift = std::exp (-epsilon * (1.0 - teV));
                const double delta = 1.0 - shift;
                double rhs = (1.0 / epsilon) * (shift - 1.0) + (1.0 - teV) * shift;
                rhs /= delta;
                const double lower = -(teV - tp) / 2.0 + rhs;
                const double upper = -lower;
                const double omega = juce::MathConstants<double>::pi / tp;
                const double s = std::sin (omega * teV);
                const double y = -juce::MathConstants<double>::pi * s * upper / (tp * 2.0);
                const double alpha = std::log (juce::jmax (1.0e-9, y)) / (tp / 2.0 - teV);
                const double e0 = -1.0 / (s * std::exp (alpha * teV));
                float* out = data.data() + (size_t) t * (lfLen + 1);
                double mean = 0.0;
                for (int i = 0; i <= lfLen; ++i)
                {
                    const double ph = (double) (i % lfLen) / lfLen;
                    const double v = ph > teV ? (-std::exp (-epsilon * (ph - teV)) + shift) / delta
                                              : e0 * std::exp (alpha * ph) * std::sin (omega * ph);
                    out[i] = (float) v;
                    if (i < lfLen) mean += v;
                }
                mean /= lfLen;
                for (int i = 0; i <= lfLen; ++i) out[i] -= (float) mean;   // no net DC per cycle
                te[(size_t) t] = (float) teV;
            }
        }
        const float* table (int t) const { return data.data() + (size_t) t * (lfLen + 1); }
    };

    const LfBank& lfBank()
    {
        static const LfBank bank;
        return bank;
    }

    // ---- Klatt resonators ------------------------------------------------------------------------------------
    struct Res
    {
        float a = 1, b = 0, c = 0, y1 = 0, y2 = 0;
        void set (float f, float bw, float sr) noexcept
        {
            const float r = std::exp (-juce::MathConstants<float>::pi * bw / sr);
            c = -r * r;
            b = 2.0f * r * std::cos (twoPi * f / sr);
            a = 1.0f - b - c;
        }
        float process (float x) noexcept
        {
            const float y = a * x + b * y1 + c * y2;
            y2 = y1; y1 = y;
            return y;
        }
        void reset() noexcept { y1 = y2 = 0.0f; }
    };

    struct AntiRes
    {
        float a = 1, b = 0, c = 0, x1 = 0, x2 = 0;
        void set (float f, float bw, float sr) noexcept
        {
            const float r = std::exp (-juce::MathConstants<float>::pi * bw / sr);
            const float cc = -r * r, bb = 2.0f * r * std::cos (twoPi * f / sr), aa = 1.0f - bb - cc;
            a = 1.0f / aa; b = -bb / aa; c = -cc / aa;
        }
        float process (float x) noexcept
        {
            const float y = a * x + b * x1 + c * x2;
            x2 = x1; x1 = x;
            return y;
        }
        void reset() noexcept { x1 = x2 = 0.0f; }
    };

    const char* const womenNames[] = { "Ada", "Marta", "Nia", "Ingrid", "Rosa", "Yuki", "Amara", "Freya", "Lucia", "Priya", "Maeve", "Hana",
                                       "Zola", "Elif", "Agnes", "Mei", "Ines", "Saoirse", "Leila", "Olga", "Tamsin", "Ama", "Clara", "Noor",
                                       "Signe", "Beatriz", "Wren", "Esi", "Mira", "June", "Kalani", "Dagny" };
    const char* const menNames[] = { "Tomas", "Kwame", "Henrik", "Luis", "Kenji", "Oisin", "Rafael", "Arjun", "Emil", "Tariq", "Bram", "Joon",
                                     "Mateo", "Idris", "Silas", "Ravi", "Anton", "Kofi", "Felix", "Tane", "Hugo", "Ilya", "Desmond", "Bao",
                                     "Ezra", "Magnus", "Duc", "Owen", "Nikolai", "Jomo", "Pablo", "Cormac" };
    const char* const kidNames[] = { "Pip", "Milo", "Ava", "Theo", "Lila", "Kai", "Iris", "Finn", "Zara", "Otto", "Nell", "Rio" };
}

// ---- the voice -----------------------------------------------------------------------------------------------
struct ChoirLoft::Voice
{
    // stage: the singer's dry voice goes into a short line read by each microphone at its own distance
    std::vector<float> line;
    int mask = 0, wpos = 0;
    float delayL = 1, delayR = 1, tDelayL = 1, tDelayR = 1, gainL = 0, gainR = 0, roomL = 0, roomR = 0, air = 1, airLp = 0;
    int tail = 0;

    bool gate = false, active = false;
    juce::uint32 order = 0;          // which held note
    juce::uint32 started = 0;        // for stealing the oldest
    int key = -1, channel = 1;
    float sung = 60.0f;              // target pitch (MIDI)
    float vel = 0.8f;
    int wait = 0;                    // samples until a delayed (loose) onset starts
    bool pendingStart = false;

    float pitch = 60.0f, glideFrom = 60.0f;
    int glideAge = 0, glideLen = 0;
    float scoop = 0.0f, scoopCoef = 0.99f, error = 0.0f;
    float drift = 0.0f, driftV = 0.0f, detune = 0.0f;
    float vibPhase = 0.0f, vibRate = 5.5f, sinceOnset = 0.0f, sinceRelease = 10.0f, sungFor = 0.0f;
    float env = 0.0f, dip = 0.0f;
    int onset = 0;

    double phase = 0.0;
    double inc = 0.0;
    int cycles = 0;
    int tA = 0; float tFrac = 0.0f, te = 0.5f, cycAmp = 1.0f, cycMul = 1.0f;
    float rdNow = 1.0f;

    // control-rate values (ramped per sample where it matters)
    float voicing = 1.0f, voicingTarget = 1.0f, aspAmt = 0.05f, raspAmt = 0.0f, burst = 0.0f;
    float level = 0.0f, levelTarget = 0.0f;
    float tiltA = 0.5f, bright = 1.0f, tiltLp = 0.0f, aspHp = 0.0f, aspPrev = 0.0f;
    float presA = 0.3f, presence = 0.3f, presLp1 = 0.0f, presLp2 = 0.0f;   // the air above the formants
    float fx = 0.5f, fy = 0.8f, hum = 0.0f;   // smoothed vowel
    float motionOffset = 0.0f, randX = 0.5f, randY = 0.5f, driftX = 0.0f, driftY = 0.0f;
    float F[5] { 500, 1500, 2500, 3450, 4350 }, B[5] { 70, 90, 130, 180, 230 };
    float nasalCoupling = 0.1f;
    Res formant[5], nasalPole;
    AntiRes nasalZero;
    float breathEnv = 0.0f, breathClock = 0.0f;
    int breathState = 0;             // 0 none, 1 waiting to breathe in, 2 breathing in
    float amp = 0.0f, ampStep = 0.0f;
    juce::Random rnd;

    void prepare (double sr)
    {
        int len = 1;
        while (len < (int) (sr * 0.09) + 8) len <<= 1;
        line.assign ((size_t) len, 0.0f);
        mask = len - 1;
        wpos = 0;
        reset();
    }

    void reset()
    {
        std::fill (line.begin(), line.end(), 0.0f);
        gate = active = pendingStart = false;
        env = level = levelTarget = 0.0f;
        tail = 0;
        for (auto& r : formant) r.reset();
        nasalPole.reset(); nasalZero.reset();
        tiltLp = aspHp = aspPrev = airLp = presLp1 = presLp2 = 0.0f;
        breathState = 0; breathEnv = 0.0f;
        amp = ampStep = 0.0f;
        key = -1;
    }
};

// ---- parameters -------------------------------------------------------------------------------------------
static Layout choirLayout()
{
    Layout l;
    addChoice (l, "mode", "Mode", { "Sections (whole choir shares the chord)", "Octaves (each in their own range)", "Round Robin (next singer per note)", "Solo (the selected singer)" }, 0);
    l.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "singers", 1 }, "Singers", 1, ChoirLoft::maxSingers, 12));
    addPercent (l, "women", "Women", 0.5f);
    addFloat   (l, "age", "Age", 6.0f, 85.0f, 32.0f, "y", 0.0f, 0);
    addFloat   (l, "ageSpread", "Age Spread", 0.0f, 30.0f, 8.0f, "y", 0.0f, 0);
    addPercent (l, "diversity", "Diversity", 0.5f);
    juce::StringArray sn;
    for (auto& s : styles()) sn.add (s.name);
    addChoice  (l, "style", "Style", sn, 1);
    l.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "castSeed", 1 }, "Cast Number", 1, 9999, 1));
    l.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "perNote", 1 }, "Singers per Note", 1, 16, 2));
    l.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "singer", 1 }, "Selected Singer", 1, ChoirLoft::maxSingers, 1));
    addMs      (l, "glide", "Glide (solo)", 0.0f, 1000.0f, 120.0f);

    addPercent (l, "vowelX", "Vowel: Front - Back", 0.5f);
    addPercent (l, "vowelY", "Vowel: Close - Open", 1.0f);
    addPercent (l, "vowel2X", "Vowel B: Front - Back", 1.0f);
    addPercent (l, "vowel2Y", "Vowel B: Close - Open", 0.0f);
    addFloat   (l, "round", "Lip Rounding", -1.0f, 1.0f, 0.0f, {}, 0.0f, 2);
    addPercent (l, "hum", "Hum (mouth closed)", 0.0f);
    addChoice  (l, "motion", "Vowel Motion", ChoirLoft::motionNames(), 0);
    addHz      (l, "motionRate", "Motion Rate", 0.05f, 12.0f, 0.5f);
    addPercent (l, "motionDepth", "Motion Depth", 1.0f);
    addBool    (l, "motionPoly", "Each Singer Moves Alone", true);
    addChoice  (l, "onset", "Onset", ChoirLoft::onsetNames(), 0);

    addPercent (l, "dynamics", "Dynamics (touch)", 0.7f);
    addPercent (l, "breath", "Breath", 0.5f);
    addPercent (l, "vibrato", "Vibrato", 0.5f);
    addPercent (l, "looseness", "Ensemble Looseness", 0.5f);
    addPercent (l, "rasp", "Rasp", 0.0f);
    addPercent (l, "breaths", "Breaths", 0.25f);
    addMs      (l, "attack", "Attack", 5.0f, 1500.0f, 70.0f);
    addMs      (l, "release", "Release", 20.0f, 3000.0f, 320.0f);
    addFloat   (l, "tune", "Fine Tune", -50.0f, 50.0f, 0.0f, "ct", 0.0f, 0);

    addChoice  (l, "arrangement", "Arrangement", ChoirLoft::arrangementNames(), 0);
    addFloat   (l, "distance", "Mic Distance", 0.5f, 15.0f, 4.0f, "m", 3.0f, 1);
    addChoice  (l, "mics", "Microphones", ChoirLoft::micNames(), 1);
    addFloat   (l, "width", "Stereo Width", 0.0f, 2.0f, 1.0f, "x", 0.0f, 2);
    addChoice  (l, "space", "Room", roomir::spaceNames(), (int) roomir::church);
    addFloat   (l, "roomSize", "Room Size", 0.5f, 2.0f, 1.0f, "x", 1.0f, 2);
    addDb      (l, "roomLevel", "Room Level", -40.0f, 6.0f, -4.0f);
    addDb      (l, "volume", "Volume", -24.0f, 12.0f, 0.0f);
    return l;
}

juce::StringArray ChoirLoft::styleNames()
{
    juce::StringArray n;
    for (auto& s : styles()) n.add (s.name);
    return n;
}

juce::String ChoirLoft::styleDescription (int s) { return juce::isPositiveAndBelow (s, (int) styles().size()) ? styles()[(size_t) s].description : ""; }

juce::StringArray ChoirLoft::onsetNames()
{
    return { "Soft", "Hard (glottal)", "H (breathy)", "M (hummed)", "N", "L", "D (doo / dah)", "B (bah / boo)", "W (woo / wah)", "Y (yah)", "Random" };
}
juce::StringArray ChoirLoft::micNames()
{
    return { "Spaced Pair (omnis)", "Near-Coincident Pair", "X-Y Cardioids", "Crossed Figure-8s", "One Mic (mono)" };
}
juce::StringArray ChoirLoft::arrangementNames() { return { "Rows", "Arc", "Sections", "Line", "Cluster" }; }
juce::StringArray ChoirLoft::motionNames() { return { "Still", "Wave (A <-> B)", "Envelope (A -> B per note)", "Steps (A / B)", "Random Vowel per Note", "Drift" }; }

const std::array<ChoirLoft::VowelPoint, 12>& ChoirLoft::vowelChart()
{
    static const std::array<VowelPoint, 12> v { {
        { "i", "see", 0.0f, 0.0f }, { "\xc9\xa8", "roses (2nd)", 0.5f, 0.0f }, { "u", "food", 1.0f, 0.0f },
        { "e", "day", 0.0f, 1.0f / 3.0f }, { "\xc9\x99", "about", 0.5f, 1.0f / 3.0f }, { "o", "go", 1.0f, 1.0f / 3.0f },
        { "\xc9\x9b", "bed", 0.0f, 2.0f / 3.0f }, { "\xca\x8c", "cup", 0.5f, 2.0f / 3.0f }, { "\xc9\x94", "thought", 1.0f, 2.0f / 3.0f },
        { "\xc3\xa6", "cat", 0.0f, 1.0f }, { "a", "father (open)", 0.5f, 1.0f }, { "\xc9\x91", "palm", 1.0f, 1.0f },
    } };
    return v;
}

std::array<float, 3> ChoirLoft::referenceFormants (float x, float y) { return chartFormants (x, y); }

// ---- singers ------------------------------------------------------------------------------------------------
juce::String ChoirLoft::fieldName (int f)
{
    static const char* n[] = { "Woman - Man", "Age", "Head size", "Face length", "Jaw width", "Chin", "Cheekbones", "Nose", "Lip fullness",
                               "Mouth width", "Neck length", "Chest", "Pitch (low - high)", "Rasp", "Air", "Vibrato speed", "Vibrato width",
                               "Brightness", "Pitch accuracy" };
    return juce::isPositiveAndBelow (f, (int) numFields) ? n[f] : "";
}

juce::String ChoirLoft::fieldTip (int f)
{
    static const char* t[] = {
        "Body and larynx size: shorter or longer vocal tract and folds",
        "Children have short tracts and high voices; voices settle after puberty; older singers wobble more, darken and get breathier",
        "Bigger head: fuller, more resonant (narrower formants)",
        "Longer face: longer vocal tract, every formant lower (darker)",
        "Wider jaw opens the mouth more: first formant up (brighter, more open vowels)",
        "Chin size: a little more or less jaw opening",
        "Wider cheekbones: a wider mouth cavity, second formant up",
        "Bigger nose: more nasal resonance",
        "Fuller lips: rounder, longer lip tube, darker",
        "Wider mouth: spread lips, brighter",
        "Longer neck: longer throat (pharynx), lower and darker",
        "Bigger chest: warmer low end and a slightly lower range",
        "Where their range sits for their kind of voice (an alto or a soprano, a bass or a tenor)",
        "Rough, gravelly folds",
        "Breathy (more air through the folds) or clean and pressed",
        "How fast their vibrato is",
        "How wide their vibrato is",
        "Dark and covered or bright and forward",
        "How well they hit the note: low means they arrive off pitch and settle late",
    };
    return juce::isPositiveAndBelow (f, (int) numFields) ? t[f] : "";
}

float& ChoirLoft::field (Singer& s, int f)
{
    switch (f)
    {
        case fSex: return s.sex;            case fAge: return s.age;          case fHead: return s.head;
        case fFaceLength: return s.faceLength; case fJaw: return s.jaw;       case fChin: return s.chin;
        case fCheeks: return s.cheeks;      case fNose: return s.nose;        case fLips: return s.lips;
        case fMouth: return s.mouth;        case fNeck: return s.neck;        case fChest: return s.chest;
        case fPitch: return s.pitch;        case fRasp: return s.rasp;        case fAir: return s.air;
        case fVibSpeed: return s.vibSpeed;  case fVibWidth: return s.vibWidth; case fBright: return s.bright;
        default: return s.accuracy;
    }
}

ChoirLoft::VoiceInfo ChoirLoft::describe (const Singer& s)
{
    VoiceInfo v;
    const float age = juce::jlimit (4.0f, 95.0f, s.age);
    const float puberty = smoothstep (11.5f, 16.0f, age);
    const float male = juce::jlimit (0.0f, 1.0f, s.sex) * puberty;
    const float adultL = 14.4f + 2.8f * male;
    float L = lerp (10.6f, adultL, smoothstep (4.0f, 17.0f, age));
    if (age > 50.0f) L *= 1.0f + 0.0015f * (age - 50.0f);
    L *= 1.0f + 0.10f * (s.faceLength - 0.5f) + 0.12f * (s.neck - 0.5f) + 0.04f * (s.chin - 0.5f) + 0.04f * (s.head - 0.5f);
    v.tractCm = L;
    v.formantScale = 17.0f / L;
    v.f1Mul = 1.0f + 0.16f * (s.jaw - 0.5f) + 0.06f * (s.chin - 0.5f) - 0.05f * (s.neck - 0.5f);
    v.f2Mul = 1.0f + 0.08f * (s.cheeks - 0.5f) + 0.06f * (s.mouth - 0.5f) - 0.07f * (s.lips - 0.5f) - 0.06f * (s.neck - 0.5f);
    v.f3Mul = 1.0f + 0.06f * (s.mouth - 0.5f) - 0.05f * (s.lips - 0.5f);
    v.bwMul = (1.0f - 0.3f * (s.head - 0.5f)) * (age > 60.0f ? 1.0f + 0.004f * (age - 60.0f) : 1.0f);
    v.nasal = 0.04f + 0.22f * std::pow (juce::jlimit (0.0f, 1.0f, s.nose), 1.5f);
    v.nasalHz = (280.0f - 70.0f * (s.nose - 0.5f)) * std::sqrt (v.formantScale);
    v.chestDb = 6.0f * (s.chest - 0.5f) + 1.5f * male;

    // where the voice sits: children ~A4, women ~D#4-E4, men ~D3; then the person, their chest and their tract
    float centre = lerp (70.0f, lerp (68.5f, 51.0f, male), puberty);
    if (male < 0.5f && age > 55.0f) centre -= juce::jmin (2.5f, 0.06f * (age - 55.0f));
    if (male >= 0.5f && age > 60.0f) centre += juce::jmin (1.2f, 0.03f * (age - 60.0f));
    centre += (s.pitch - 0.5f) * 9.0f - 1.5f * (s.chest - 0.5f) - 0.8f * (L - lerp (lerp (10.6f, adultL, smoothstep (4.0f, 17.0f, age)), adultL, 0.0f));
    v.centre = centre;
    const float halfRange = age < 13.0f ? 8.0f : 10.0f;
    v.low = centre - halfRange;
    v.high = centre + halfRange;
    // chest -> head: about E4-F4 for women and children, G4 for tenors, E4 for baritones
    v.breakNote = age < 13.0f ? centre - 5.0f : (male >= 0.5f ? centre + 11.0f : centre - 4.0f);

    if (age < 13.0f) v.kind = "Treble";
    else if (male >= 0.5f) v.kind = centre < 48.5f ? "Bass" : centre < 52.5f ? "Baritone" : centre < 56.5f ? "Tenor" : "Countertenor";
    else v.kind = centre < 66.5f ? "Alto" : centre < 70.0f ? "Mezzo" : "Soprano";

    v.rd = 0.95f + 0.25f * (1.0f - male) + (age < 13.0f ? 0.2f : 0.0f) + (age > 60.0f ? 0.006f * (age - 60.0f) : 0.0f) + (s.air - 0.5f) * 0.8f;
    v.breath = juce::jmax (0.0f, 0.06f + 0.08f * (1.0f - male) + (s.air - 0.5f) * 0.16f + (age > 60.0f ? 0.003f * (age - 60.0f) : 0.0f));
    v.jitter = 0.004f + 0.0002f * juce::jmax (0.0f, age - 45.0f);
    v.shimmer = 0.02f + 0.0012f * juce::jmax (0.0f, age - 45.0f);
    v.vibRate = 5.6f - 0.02f * juce::jmax (0.0f, age - 40.0f) + (s.vibSpeed - 0.5f) * 1.6f;
    v.vibScale = juce::jmax (0.0f, 1.0f + 0.012f * juce::jmax (0.0f, age - 50.0f) + (s.vibWidth - 0.5f) * 1.2f);
    v.wobble = 0.03f + 0.003f * juce::jmax (0.0f, age - 55.0f);
    v.bright = s.bright;
    v.rasp = s.rasp;
    v.errorCents = (1.0f - juce::jlimit (0.0f, 1.0f, s.accuracy)) * 30.0f + 3.0f;
    return v;
}

juce::String ChoirLoft::noteName (float midi)
{
    static const char* n[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int m = juce::roundToInt (midi);
    return juce::String (n[((m % 12) + 12) % 12]) + juce::String (m / 12 - 1);
}

juce::String ChoirLoft::personName (const Singer& s)
{
    if (s.age < 13.0f) return kidNames[(size_t) s.name % std::size (kidNames)];
    return s.sex >= 0.5f ? menNames[(size_t) s.name % std::size (menNames)] : womenNames[(size_t) s.name % std::size (womenNames)];
}

juce::String ChoirLoft::personLabel (const Singer& s)
{
    const juce::String who = s.age < 13.0f ? (s.sex >= 0.5f ? "Boy" : "Girl")
                           : s.age < 18.0f ? (s.sex >= 0.5f ? "Teenage boy" : "Teenage girl")
                           : (s.sex >= 0.65f ? "Man" : s.sex <= 0.35f ? "Woman" : "Person");
    return who + ", " + juce::String (juce::roundToInt (s.age));
}

bool ChoirLoft::CastKey::operator!= (const CastKey& o) const
{
    return seed != o.seed || count != o.count || arrangement != o.arrangement || women != o.women || age != o.age
        || spread != o.spread || diversity != o.diversity || distance != o.distance;
}

ChoirLoft::CastKey ChoirLoft::castKey() const
{
    CastKey k;
    k.seed = juce::roundToInt (param ("castSeed"));
    k.count = juce::jlimit (1, maxSingers, juce::roundToInt (param ("singers")));
    k.arrangement = (int) param ("arrangement");
    k.women = param ("women");
    k.age = param ("age");
    k.spread = param ("ageSpread");
    k.diversity = param ("diversity");
    k.distance = param ("distance");
    return k;
}

ChoirLoft::Singer ChoirLoft::generate (int index, const CastKey& k) const
{
    // who is a woman: rank everyone by a seeded number, the lowest "women x count" are women (so changing
    // the balance swaps people over one by one rather than reshuffling the whole choir)
    auto rankValue = [&] (int i) { juce::Random r ((juce::int64) k.seed * 7717 + i * 131 + 3); return r.nextFloat(); };
    const float mine = rankValue (index);
    int below = 0;
    for (int i = 0; i < k.count; ++i)
        if (i != index && (rankValue (i) < mine || (rankValue (i) == mine && i < index))) ++below;
    const bool woman = below < juce::roundToInt (k.women * (float) k.count);

    juce::Random r ((juce::int64) k.seed * 1000003 + index * 7919 + 12345);
    Singer s;
    const float div = k.diversity;
    s.sex = woman ? juce::jlimit (0.0f, 0.3f, std::abs (gauss (r)) * 0.05f * div) : juce::jlimit (0.7f, 1.0f, 1.0f - std::abs (gauss (r)) * 0.05f * div);
    s.age = juce::jlimit (6.0f, 88.0f, k.age + gauss (r) * k.spread * 0.6f);
    auto around = [&] (float spread) { return juce::jlimit (0.0f, 1.0f, 0.5f + (r.nextFloat() - 0.5f) * spread); };
    for (int f = fHead; f <= fChest; ++f) field (s, f) = around (div);
    s.pitch = around (0.35f + 0.65f * div);
    s.rasp = juce::jlimit (0.0f, 1.0f, std::pow (r.nextFloat(), 3.0f) * 0.3f * div);
    s.air = around (0.6f * div);
    s.vibSpeed = around (0.8f * div);
    s.vibWidth = around (0.8f * div);
    s.bright = around (0.8f * div);
    s.accuracy = juce::jlimit (0.0f, 1.0f, 0.75f - r.nextFloat() * 0.4f * div);
    s.skin = r.nextInt (6);
    s.hair = r.nextInt (6);
    const float grey = smoothstep (45.0f, 75.0f, s.age);
    s.hairColour = r.nextFloat() < grey ? 5 : r.nextInt (5);
    s.robe = r.nextInt (4);
    s.name = r.nextInt (64);
    s.beard = ! woman && s.age > 18.0f && r.nextFloat() < 0.3f;
    s.glasses = r.nextFloat() < 0.08f + 0.3f * smoothstep (40.0f, 75.0f, s.age);
    if (! woman && s.hair == 1 && r.nextFloat() < 0.6f) s.hair = 0;
    if (s.hair == 5 && (woman || s.age < 35.0f)) s.hair = woman ? 4 : 0;   // thinning hair: older men
    return s;
}

juce::Point<float> ChoirLoft::sectionsPosition (int rank, int n)
{
    const float sp = 0.62f, rowDepth = 0.85f;
    const int perCol = n <= 6 ? 1 : 2;
    const int col = rank / perCol, row = rank % perCol;
    const int cols = (n + perCol - 1) / perCol;
    return { ((float) col - (float) (cols - 1) * 0.5f) * sp, (float) row * rowDepth };
}

void ChoirLoft::getCast (std::vector<Singer>& singers, std::vector<juce::Point<float>>& positions) const
{
    const auto k = castKey();
    std::array<bool, maxSingers> hc {}, hp {};
    std::array<Singer, maxSingers> cu {};
    std::array<juce::Point<float>, maxSingers> cp {};
    {
        const juce::SpinLock::ScopedLockType sl (castLock);
        hc = hasCustom; hp = hasPos; cu = custom; cp = customPos;
    }
    std::array<Singer, maxSingers> cast {};
    for (int i = 0; i < k.count; ++i) cast[(size_t) i] = hc[(size_t) i] ? cu[(size_t) i] : generate (i, k);
    singers.assign (cast.begin(), cast.begin() + k.count);
    positions.resize ((size_t) k.count);
    if (k.arrangement == arrSections)
    {
        std::array<std::pair<float, int>, maxSingers> order {};
        for (int i = 0; i < k.count; ++i) order[(size_t) i] = { -describe (cast[(size_t) i]).centre, i };
        std::stable_sort (order.begin(), order.begin() + k.count);
        for (int r = 0; r < k.count; ++r) positions[(size_t) order[(size_t) r].second] = sectionsPosition (r, k.count);
    }
    else
        for (int i = 0; i < k.count; ++i) positions[(size_t) i] = arrangedPosition (i, k, cast);
    for (int i = 0; i < k.count; ++i)
        if (hp[(size_t) i]) positions[(size_t) i] = cp[(size_t) i];
}

juce::Point<float> ChoirLoft::arrangedPosition (int index, const CastKey& k, const std::array<Singer, maxSingers>& cast) const
{
    const int n = k.count;
    const float sp = 0.62f, rowDepth = 0.85f;
    const int rows = n <= 7 ? 1 : n <= 18 ? 2 : n <= 36 ? 3 : 4;
    auto inRows = [&] (int i, int nRows)
    {
        const int perRow = (n + nRows - 1) / nRows;
        const int row = i / perRow, col = i % perRow;
        const int inThisRow = juce::jmin (perRow, n - row * perRow);
        const float x = ((float) col - (float) (inThisRow - 1) * 0.5f) * sp + ((row & 1) ? sp * 0.5f : 0.0f);
        return juce::Point<float> (x, (float) row * rowDepth);
    };
    switch (k.arrangement)
    {
        case arrArc:
        {
            const auto p = inRows (index, rows);
            const float radius = juce::jmax (2.0f, (float) ((n + rows - 1) / rows) * sp / 2.0f);
            const float a = p.x / radius;
            return { std::sin (a) * (radius + p.y), p.y + (radius + p.y) * (1.0f - std::cos (a)) * -0.6f + radius * 0.25f };
        }
        case arrSections:
        {
            // high voices on the left, low voices on the right, two rows
            std::array<std::pair<float, int>, maxSingers> order {};
            for (int i = 0; i < n; ++i) order[(size_t) i] = { -describe (cast[(size_t) i]).centre, i };
            std::stable_sort (order.begin(), order.begin() + n);
            int rank = 0;
            for (int i = 0; i < n; ++i) if (order[(size_t) i].second == index) rank = i;
            return sectionsPosition (rank, n);
        }
        case arrLine: return { ((float) index - (float) (n - 1) * 0.5f) * sp, 0.0f };
        case arrCluster:
        {
            juce::Random r ((juce::int64) k.seed * 77 + index * 977 + 5);
            const float w = juce::jmax (1.2f, std::sqrt ((float) n) * 0.75f);
            return { (r.nextFloat() - 0.5f) * w * 2.0f, r.nextFloat() * w * 0.9f };
        }
        default: return inRows (index, rows);
    }
}

int ChoirLoft::numSingers() const { return juce::jlimit (1, maxSingers, juce::roundToInt (param ("singers"))); }

ChoirLoft::Singer ChoirLoft::getSinger (int index) const
{
    index = juce::jlimit (0, maxSingers - 1, index);
    {
        const juce::SpinLock::ScopedLockType sl (castLock);
        if (hasCustom[(size_t) index]) return custom[(size_t) index];
    }
    return generate (index, castKey());
}

void ChoirLoft::setSinger (int index, const Singer& s)
{
    if (! juce::isPositiveAndBelow (index, maxSingers)) return;
    {
        const juce::SpinLock::ScopedLockType sl (castLock);
        custom[(size_t) index] = s;
        custom[(size_t) index].locked = true;
        hasCustom[(size_t) index] = true;
    }
    ++castVersion;
}

void ChoirLoft::setLocked (int index, bool locked)
{
    if (! juce::isPositiveAndBelow (index, maxSingers)) return;
    auto s = getSinger (index);
    {
        const juce::SpinLock::ScopedLockType sl (castLock);
        custom[(size_t) index] = s;
        custom[(size_t) index].locked = locked;
        hasCustom[(size_t) index] = true;
    }
    ++castVersion;
}

void ChoirLoft::rerollSinger (int index)
{
    if (! juce::isPositiveAndBelow (index, maxSingers)) return;
    auto k = castKey();
    k.seed = juce::Random::getSystemRandom().nextInt (1 << 30) + 10000;
    auto s = generate (index, k);
    const auto old = getSinger (index);
    s.sex = old.sex < 0.5f ? juce::jmin (s.sex, 0.3f) : juce::jmax (s.sex, 0.7f);   // keep the part they sing
    if (old.sex < 0.5f != s.sex < 0.5f) s.sex = old.sex;
    setSinger (index, s);
}

void ChoirLoft::rerollCast()
{
    {
        const juce::SpinLock::ScopedLockType sl (castLock);
        for (int i = 0; i < maxSingers; ++i)
            if (hasCustom[(size_t) i] && ! custom[(size_t) i].locked) hasCustom[(size_t) i] = false;
    }
    setParam ("castSeed", (float) (juce::Random::getSystemRandom().nextInt (9998) + 1));
    ++castVersion;
}

juce::Point<float> ChoirLoft::getPosition (int index) const
{
    index = juce::jlimit (0, maxSingers - 1, index);
    {
        const juce::SpinLock::ScopedLockType sl (castLock);
        if (hasPos[(size_t) index]) return customPos[(size_t) index];
    }
    const auto k = castKey();
    std::array<Singer, maxSingers> cast {};
    if (k.arrangement == arrSections)
        for (int i = 0; i < k.count; ++i) cast[(size_t) i] = getSinger (i);
    return arrangedPosition (index, k, cast);
}

void ChoirLoft::setPosition (int index, juce::Point<float> p)
{
    if (! juce::isPositiveAndBelow (index, maxSingers)) return;
    {
        const juce::SpinLock::ScopedLockType sl (castLock);
        customPos[(size_t) index] = { juce::jlimit (-12.0f, 12.0f, p.x), juce::jlimit (0.0f, 12.0f, p.y) };
        hasPos[(size_t) index] = true;
    }
    ++castVersion;
}

void ChoirLoft::rearrange()
{
    {
        const juce::SpinLock::ScopedLockType sl (castLock);
        hasPos.fill (false);
    }
    setParam ("arrangement", (float) (((int) param ("arrangement") + 1) % arrangementNames().size()));
    ++castVersion;
}

ChoirLoft::Live ChoirLoft::getLive (int i) const
{
    Live l;
    if (! juce::isPositiveAndBelow (i, maxSingers)) return l;
    l.level = liveLevel[(size_t) i].load();
    l.vowelX = liveX[(size_t) i].load();
    l.vowelY = liveY[(size_t) i].load();
    l.open = liveOpen[(size_t) i].load();
    l.hum = liveHum[(size_t) i].load();
    l.note = liveNote[(size_t) i].load();
    return l;
}

// ---- presets ------------------------------------------------------------------------------------------------
namespace
{
    struct ChoirPreset { const char* name; const char* description; std::vector<std::pair<const char*, float>> values; };

    const std::vector<ChoirPreset>& choirPresets()
    {
        // vowel shortcuts: a (0.5, 1), o (1, .33), u (1, 0), i (0, 0), e (0, .33), ɔ (1, .67), ə (.5, .33)
        static const std::vector<ChoirPreset> p {
            { "Cathedral Choir (Aah)", "24 voices, men and women, singing 'aah' under a stone vault",
              { { "singers", 24 }, { "style", 1 }, { "vowelX", 0.55f }, { "vowelY", 0.95f }, { "space", (float) roomir::cathedral }, { "distance", 6.0f }, { "roomLevel", -2.0f },
                { "attack", 180.0f }, { "release", 900.0f }, { "castSeed", 11 } } },
            { "Chamber Choir (Ooh)", "12 voices on a soft 'ooh' in a wooden church",
              { { "volume", -1.0f }, { "singers", 12 }, { "style", 1 }, { "vowelX", 1.0f }, { "vowelY", 0.08f }, { "round", 0.3f }, { "space", (float) roomir::church }, { "distance", 3.5f },
                { "attack", 140.0f }, { "release", 600.0f }, { "castSeed", 23 } } },
            { "Mixed Choir (Oh)", "16 voices on an open 'oh'",
              { { "volume", -0.5f }, { "singers", 16 }, { "style", 1 }, { "vowelX", 1.0f }, { "vowelY", 0.4f }, { "space", (float) roomir::church }, { "distance", 4.0f }, { "castSeed", 5 } } },
            { "Boys' Choir (Treble)", "10 children, pure and straight",
              { { "volume", 3.0f }, { "singers", 10 }, { "women", 0.4f }, { "age", 10.0f }, { "ageSpread", 2.0f }, { "style", 2 }, { "vowelX", 0.9f }, { "vowelY", 0.25f },
                { "space", (float) roomir::cathedral }, { "distance", 5.0f }, { "vibrato", 0.2f }, { "breath", 0.35f }, { "castSeed", 41 } } },
            { "Women's Choir", "14 women, sopranos to altos",
              { { "volume", -1.5f }, { "singers", 14 }, { "women", 1.0f }, { "style", 1 }, { "vowelX", 0.5f }, { "vowelY", 0.9f }, { "space", (float) roomir::concertHall }, { "castSeed", 8 } } },
            { "Men's Chorus", "12 men, tenors to basses",
              { { "volume", 0.5f }, { "singers", 12 }, { "women", 0.0f }, { "age", 40.0f }, { "style", 1 }, { "vowelX", 0.8f }, { "vowelY", 0.6f }, { "space", (float) roomir::church }, { "castSeed", 17 } } },
            { "Monks (low drone)", "Eight low men's voices, straight tone, in a stone chapel",
              { { "volume", 2.0f }, { "singers", 8 }, { "women", 0.0f }, { "age", 50.0f }, { "ageSpread", 12.0f }, { "style", 2 }, { "mode", 1 }, { "vowelX", 0.9f }, { "vowelY", 0.45f },
                { "space", (float) roomir::cathedral }, { "distance", 7.0f }, { "vibrato", 0.15f }, { "attack", 250.0f }, { "release", 1200.0f }, { "castSeed", 3 } } },
            { "Hum Choir (Mmm)", "Closed-mouth humming, warm and close",
              { { "volume", -2.5f }, { "singers", 12 }, { "style", 1 }, { "hum", 1.0f }, { "vowelX", 0.5f }, { "vowelY", 0.4f }, { "space", (float) roomir::woodenStudio }, { "distance", 2.5f }, { "castSeed", 29 } } },
            { "Gospel Choir", "20 voices, pressed and bright, scooping into every chord",
              { { "volume", -3.5f }, { "singers", 20 }, { "style", 3 }, { "vowelX", 0.6f }, { "vowelY", 0.85f }, { "onset", (float) ChoirLoft::onsetH }, { "space", (float) roomir::bigLiveRoom },
                { "distance", 3.0f }, { "looseness", 0.6f }, { "attack", 40.0f }, { "castSeed", 61 } } },
            { "Folk Harmony Trio", "Three close voices round one microphone",
              { { "volume", -5.5f }, { "singers", 3 }, { "women", 0.67f }, { "style", 5 }, { "mode", 0 }, { "vowelX", 0.5f }, { "vowelY", 0.85f }, { "arrangement", (float) ChoirLoft::arrLine },
                { "mics", (float) ChoirLoft::micMono }, { "distance", 1.0f }, { "space", (float) roomir::livingRoom }, { "roomLevel", -10.0f }, { "castSeed", 7 } } },
            { "Barbershop Quartet", "Four men, one to a note, ringing chords",
              { { "volume", -3.0f }, { "singers", 4 }, { "women", 0.0f }, { "style", 6 }, { "mode", 2 }, { "perNote", 1 }, { "vowelX", 0.45f }, { "vowelY", 0.75f }, { "arrangement", (float) ChoirLoft::arrArc },
                { "distance", 2.0f }, { "space", (float) roomir::woodenStudio }, { "vibrato", 0.35f }, { "castSeed", 44 } } },
            { "Doo-Wop Backing (Doo)", "Four voices on 'doo', tight and short",
              { { "volume", -6.5f }, { "singers", 4 }, { "women", 0.25f }, { "style", 4 }, { "onset", (float) ChoirLoft::onsetD }, { "vowelX", 1.0f }, { "vowelY", 0.05f }, { "attack", 15.0f },
                { "release", 140.0f }, { "space", (float) roomir::plate }, { "distance", 1.5f }, { "roomLevel", -8.0f }, { "castSeed", 12 } } },
            { "Pop Backing (Ooh)", "Six breathy pop voices stacked wide",
              { { "volume", -8.5f }, { "singers", 6 }, { "style", 4 }, { "vowelX", 1.0f }, { "vowelY", 0.1f }, { "mics", (float) ChoirLoft::micSpaced }, { "width", 1.5f }, { "distance", 1.5f },
                { "space", (float) roomir::vocalBooth }, { "roomLevel", -12.0f }, { "castSeed", 90 } } },
            { "Wordless Film Choir", "40 voices swelling from 'ooh' to 'aah' on every note",
              { { "volume", 2.0f }, { "singers", 40 }, { "style", 1 }, { "motion", (float) ChoirLoft::envelope }, { "motionRate", 0.3f }, { "vowelX", 1.0f }, { "vowelY", 0.1f },
                { "vowel2X", 0.5f }, { "vowel2Y", 1.0f }, { "attack", 600.0f }, { "release", 1500.0f }, { "space", (float) roomir::concertHall }, { "distance", 8.0f }, { "castSeed", 101 } } },
            { "Vowel Sweep Pad", "The whole choir slowly morphs between 'ee' and 'oh'",
              { { "volume", -0.5f }, { "singers", 16 }, { "style", 1 }, { "motion", (float) ChoirLoft::wave }, { "motionRate", 0.12f }, { "vowelX", 0.0f }, { "vowelY", 0.0f }, { "vowel2X", 1.0f },
                { "vowel2Y", 0.5f }, { "motionPoly", 0.0f }, { "attack", 400.0f }, { "release", 1200.0f }, { "space", (float) roomir::church }, { "castSeed", 55 } } },
            { "Talking Choir (random vowels)", "Every note gets a different vowel",
              { { "volume", 1.5f }, { "singers", 10 }, { "style", 5 }, { "motion", (float) ChoirLoft::randomPerNote }, { "motionDepth", 1.0f }, { "motionPoly", 0.0f },
                { "space", (float) roomir::woodenStudio }, { "castSeed", 66 } } },
            { "Ghost Choir", "Breathy, far away, drifting vowels in a cave",
              { { "volume", -2.5f }, { "singers", 16 }, { "style", 8 }, { "motion", (float) ChoirLoft::drift }, { "motionRate", 0.3f }, { "motionDepth", 0.7f }, { "breath", 0.9f },
                { "space", (float) roomir::cave }, { "distance", 10.0f }, { "roomLevel", 2.0f }, { "attack", 700.0f }, { "release", 2500.0f }, { "castSeed", 13 } } },
            { "Village Choir (loose)", "A friendly amateur choir: everyone a little late and a little off",
              { { "volume", 1.0f }, { "singers", 18 }, { "age", 50.0f }, { "ageSpread", 20.0f }, { "diversity", 0.9f }, { "style", 5 }, { "looseness", 1.0f }, { "vowelX", 0.5f }, { "vowelY", 0.85f },
                { "space", (float) roomir::church }, { "arrangement", (float) ChoirLoft::arrCluster }, { "castSeed", 77 } } },
            { "Old Folks' Choir", "Seniors with wide vibratos and warm voices",
              { { "volume", 0.5f }, { "singers", 12 }, { "age", 74.0f }, { "ageSpread", 7.0f }, { "style", 1 }, { "vowelX", 0.8f }, { "vowelY", 0.5f }, { "space", (float) roomir::church }, { "castSeed", 84 } } },
            { "Classroom Singalong", "Twenty children in a school hall",
              { { "volume", 1.5f }, { "singers", 20 }, { "age", 8.0f }, { "ageSpread", 2.0f }, { "diversity", 0.8f }, { "style", 5 }, { "looseness", 0.9f }, { "vowelX", 0.5f }, { "vowelY", 0.85f },
                { "space", (float) roomir::bigLiveRoom }, { "castSeed", 30 } } },
            { "Stadium Crowd (64)", "Sixty-four voices: a terrace singing along",
              { { "volume", 1.5f }, { "singers", 64 }, { "age", 35.0f }, { "ageSpread", 14.0f }, { "women", 0.4f }, { "diversity", 1.0f }, { "style", 4 }, { "looseness", 1.0f }, { "vowelX", 0.6f },
                { "vowelY", 0.95f }, { "arrangement", (float) ChoirLoft::arrCluster }, { "space", (float) roomir::canyon }, { "distance", 12.0f }, { "castSeed", 9 } } },
            { "Early Music Consort", "Six straight voices, crystal tuning",
              { { "volume", 0.5f }, { "singers", 6 }, { "style", 2 }, { "looseness", 0.15f }, { "vowelX", 0.5f }, { "vowelY", 0.75f }, { "space", (float) roomir::church }, { "arrangement", (float) ChoirLoft::arrArc }, { "castSeed", 19 } } },
            { "Soprano Solo", "One classical soprano, legato",
              { { "volume", -2.0f }, { "singers", 1 }, { "women", 1.0f }, { "age", 30.0f }, { "ageSpread", 0.0f }, { "style", 0 }, { "mode", 3 }, { "vowelX", 0.5f }, { "vowelY", 1.0f },
                { "space", (float) roomir::concertHall }, { "distance", 4.0f }, { "glide", 90.0f }, { "castSeed", 2 } } },
            { "Tenor Solo", "One classical tenor",
              { { "volume", 2.0f }, { "singers", 1 }, { "women", 0.0f }, { "age", 35.0f }, { "ageSpread", 0.0f }, { "style", 0 }, { "mode", 3 }, { "vowelX", 0.75f }, { "vowelY", 0.7f },
                { "space", (float) roomir::concertHall }, { "distance", 4.0f }, { "castSeed", 4 } } },
            { "Lullaby Voice", "One hushed voice, close up",
              { { "volume", -4.0f }, { "singers", 1 }, { "women", 1.0f }, { "ageSpread", 0.0f }, { "style", 8 }, { "mode", 3 }, { "vowelX", 0.6f }, { "vowelY", 0.5f }, { "hum", 0.4f },
                { "mics", (float) ChoirLoft::micMono }, { "distance", 0.6f }, { "space", (float) roomir::livingRoom }, { "roomLevel", -14.0f }, { "castSeed", 21 } } },
            { "Blues Holler", "One gravelly voice with long bends",
              { { "volume", -7.5f }, { "singers", 1 }, { "women", 0.0f }, { "age", 55.0f }, { "ageSpread", 0.0f }, { "style", 7 }, { "mode", 3 }, { "glide", 220.0f }, { "vowelX", 0.6f }, { "vowelY", 0.9f },
                { "onset", (float) ChoirLoft::onsetW }, { "mics", (float) ChoirLoft::micMono }, { "distance", 1.0f }, { "space", (float) roomir::barClub }, { "castSeed", 14 } } },
            { "Bass Drone", "Two deep voices on an 'oh'",
              { { "volume", -2.0f }, { "singers", 2 }, { "women", 0.0f }, { "age", 45.0f }, { "style", 2 }, { "mode", 0 }, { "vowelX", 1.0f }, { "vowelY", 0.4f }, { "attack", 400.0f },
                { "release", 1500.0f }, { "space", (float) roomir::cathedral }, { "castSeed", 3 } } },
            { "Overtone Singer", "Throat singing: the whistle above the drone follows the vowel (try Vowel Motion: Wave)",
              { { "singers", 1 }, { "women", 0.0f }, { "age", 40.0f }, { "ageSpread", 0.0f }, { "style", 9 }, { "mode", 3 }, { "vowelX", 0.3f }, { "vowelY", 0.3f },
                { "motion", (float) ChoirLoft::wave }, { "motionRate", 0.25f }, { "vowel2X", 0.9f }, { "vowel2Y", 0.3f }, { "space", (float) roomir::cave }, { "castSeed", 6 } } },
            { "Belting Ensemble", "Eight bright belters",
              { { "volume", -4.0f }, { "singers", 8 }, { "style", 6 }, { "vowelX", 0.4f }, { "vowelY", 0.9f }, { "space", (float) roomir::bigLiveRoom }, { "distance", 2.5f }, { "castSeed", 36 } } },
            { "Dry Studio Choir (for mixing)", "12 voices, no room",
              { { "volume", -5.0f }, { "singers", 12 }, { "style", 1 }, { "space", (float) roomir::dry }, { "distance", 1.5f }, { "castSeed", 1 } } },
            { "Init (8 singers)", "A plain starting point", { { "volume", -1.0f }, { "singers", 8 } } },
        };
        return p;
    }
}

juce::StringArray ChoirLoft::presetNames()
{
    juce::StringArray n;
    for (auto& p : choirPresets()) n.add (p.name);
    return n;
}

juce::String ChoirLoft::presetDescription (int i)
{
    return juce::isPositiveAndBelow (i, (int) choirPresets().size()) ? choirPresets()[(size_t) i].description : "";
}

void ChoirLoft::loadProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) choirPresets().size())) return;
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            rp->setValueNotifyingHost (rp->getDefaultValue());
    for (auto& [id, v] : choirPresets()[(size_t) index].values)
        setParam (id, v);
    {
        const juce::SpinLock::ScopedLockType sl (castLock);
        hasCustom.fill (false);
        hasPos.fill (false);
    }
    ++castVersion;
}

void ChoirLoft::saveExtraState (juce::ValueTree& t)
{
    const juce::SpinLock::ScopedLockType sl (castLock);
    for (int i = 0; i < maxSingers; ++i)
    {
        if (! hasCustom[(size_t) i] && ! hasPos[(size_t) i]) continue;
        juce::ValueTree n ("SINGER");
        n.setProperty ("index", i, nullptr);
        if (hasCustom[(size_t) i])
        {
            auto s = custom[(size_t) i];
            juce::String v;
            for (int f = 0; f < numFields; ++f) v << juce::String (field (s, f), 4) << " ";
            n.setProperty ("fields", v.trim(), nullptr);
            n.setProperty ("look", juce::String (s.skin) + " " + juce::String (s.hair) + " " + juce::String (s.hairColour) + " " + juce::String (s.robe)
                                    + " " + juce::String (s.name) + " " + juce::String ((int) s.beard) + " " + juce::String ((int) s.glasses), nullptr);
            n.setProperty ("locked", s.locked, nullptr);
        }
        if (hasPos[(size_t) i])
        {
            n.setProperty ("x", customPos[(size_t) i].x, nullptr);
            n.setProperty ("z", customPos[(size_t) i].y, nullptr);
        }
        t.appendChild (n, nullptr);
    }
}

void ChoirLoft::loadExtraState (const juce::ValueTree& t)
{
    {
        const juce::SpinLock::ScopedLockType sl (castLock);
        hasCustom.fill (false);
        hasPos.fill (false);
        for (auto n : t)
        {
            if (! n.hasType ("SINGER")) continue;
            const int i = n["index"];
            if (! juce::isPositiveAndBelow (i, maxSingers)) continue;
            if (n.hasProperty ("fields"))
            {
                Singer s;
                auto v = juce::StringArray::fromTokens (n["fields"].toString(), " ", "");
                for (int f = 0; f < numFields && f < v.size(); ++f) field (s, f) = v[f].getFloatValue();
                auto look = juce::StringArray::fromTokens (n["look"].toString(), " ", "");
                if (look.size() >= 7)
                {
                    s.skin = look[0].getIntValue(); s.hair = look[1].getIntValue(); s.hairColour = look[2].getIntValue();
                    s.robe = look[3].getIntValue(); s.name = look[4].getIntValue(); s.beard = look[5].getIntValue() != 0; s.glasses = look[6].getIntValue() != 0;
                }
                s.locked = n["locked"];
                custom[(size_t) i] = s;
                hasCustom[(size_t) i] = true;
            }
            if (n.hasProperty ("x"))
            {
                customPos[(size_t) i] = { (float) n["x"], (float) n["z"] };
                hasPos[(size_t) i] = true;
            }
        }
    }
    ++castVersion;
}

// ---- lifecycle ----------------------------------------------------------------------------------------------
ChoirLoft::ChoirLoft() : BuiltinProcessor ("choir", "Choir Loft", true, choirLayout())
{
    for (int i = 0; i < maxSingers; ++i)
    {
        voices.push_back (std::make_unique<Voice>());
        voices.back()->rnd.setSeed ((juce::int64) i * 9973 + 1);
        liveNote[(size_t) i] = -1;
    }
    (void) lfBank();
    loadProgram (0);
    startTimerHz (5);
}

ChoirLoft::~ChoirLoft()
{
    stopTimer();
}

void ChoirLoft::prepareToPlay (double sr, int block)
{
    sampleRate = sr;
    maxBlock = juce::jmax (16, block);
    for (auto& v : voices) v->prepare (sr);
    held.clear();
    held.reserve (128);
    sustain = false;
    chanBend.fill (0.0f);
    chanPressure.fill (0.0f);
    roomBus.setSize (2, maxBlock);
    wet.setSize (2, maxBlock);
    convolution.prepare ({ sr, (juce::uint32) maxBlock, 2 });
    roomKey[0] = roomKey[1] = -1;
    rebuildRoomIfNeeded (true);
    dcL = dcR = dcXL = dcXR = 0.0f;
    refreshCast (true);
}

void ChoirLoft::timerCallback() { rebuildRoomIfNeeded (false); }

void ChoirLoft::rebuildRoomIfNeeded (bool force)
{
    const int pre = juce::roundToInt (juce::jmax (2.0f, 18.0f - param ("distance") * 1.5f));
    const int key[2] = { (int) param ("space") * 1000 + pre, juce::roundToInt (param ("roomSize") * 100.0f) };
    if (! force && key[0] == roomKey[0] && key[1] == roomKey[1]) return;
    roomKey[0] = key[0]; roomKey[1] = key[1];
    auto ir = roomir::design ((int) param ("space"), sampleRate, (float) key[1] / 100.0f, 0.5f, (float) pre);
    convolution.loadImpulseResponse (std::move (ir), sampleRate, juce::dsp::Convolution::Stereo::yes,
                                     juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
    roomReady = true;
}

void ChoirLoft::waitUntilReady()
{
    rebuildRoomIfNeeded (true);
    juce::Thread::sleep (250);
}

// ---- cast -> voices (audio thread) --------------------------------------------------------------------------
void ChoirLoft::refreshCast (bool force)
{
    const auto k = castKey();
    const int ver = castVersion.load();
    const int mics = (int) param ("mics");
    if (! force && ver == seenVersion && ! (k != seenKey) && mics == seenMics) return;

    std::array<bool, maxSingers> hc {}, hp {};
    std::array<Singer, maxSingers> cu {};
    std::array<juce::Point<float>, maxSingers> cp {};
    {
        const juce::SpinLock::ScopedTryLockType sl (castLock);
        if (! sl.isLocked()) return;   // try again next block
        hc = hasCustom; hp = hasPos; cu = custom; cp = customPos;
    }
    seenVersion = ver;
    seenKey = k;
    seenMics = mics;
    count = k.count;

    for (int i = 0; i < k.count; ++i)
    {
        aSinger[(size_t) i] = hc[(size_t) i] ? cu[(size_t) i] : generate (i, k);
        aInfo[(size_t) i] = describe (aSinger[(size_t) i]);
    }
    if (k.arrangement == arrSections)
    {
        std::array<std::pair<float, int>, maxSingers> order {};
        for (int i = 0; i < k.count; ++i) order[(size_t) i] = { -aInfo[(size_t) i].centre, i };
        std::stable_sort (order.begin(), order.begin() + k.count);
        for (int r = 0; r < k.count; ++r) aPos[(size_t) order[(size_t) r].second] = sectionsPosition (r, k.count);
    }
    else
        for (int i = 0; i < k.count; ++i) aPos[(size_t) i] = arrangedPosition (i, k, aSinger);
    for (int i = 0; i < k.count; ++i)
        if (hp[(size_t) i]) aPos[(size_t) i] = cp[(size_t) i];

    // microphones
    struct Mic { float x, axis, a; };   // position (m), axis (radians, + to the right), pattern: a + (1-a) cos
    Mic m[2];
    switch (mics)
    {
        case micSpaced:  m[0] = { -0.3f, 0.0f, 1.0f }; m[1] = { 0.3f, 0.0f, 1.0f }; break;
        case micXY:      m[0] = { 0.0f, -0.785f, 0.5f }; m[1] = { 0.0f, 0.785f, 0.5f }; break;
        case micFigure8: m[0] = { 0.0f, -0.785f, 0.0f }; m[1] = { 0.0f, 0.785f, 0.0f }; break;
        case micMono:    m[0] = { 0.0f, 0.0f, 0.5f }; m[1] = m[0]; break;
        default:         m[0] = { -0.085f, -0.96f, 0.5f }; m[1] = { 0.085f, 0.96f, 0.5f }; break;
    }
    const float dist = k.distance;
    float minD = 1.0e9f;
    std::array<float, maxSingers * 2> d {};
    for (int i = 0; i < k.count; ++i)
        for (int c = 0; c < 2; ++c)
        {
            const float dx = aPos[(size_t) i].x - m[c].x, dz = dist + aPos[(size_t) i].y;
            d[(size_t) (i * 2 + c)] = std::sqrt (dx * dx + dz * dz);
            minD = juce::jmin (minD, d[(size_t) (i * 2 + c)]);
        }
    const float maxDelay = (float) voices[0]->mask - 4.0f;
    for (int i = 0; i < k.count; ++i)
    {
        auto& v = *voices[(size_t) i];
        float g[2], rg[2];
        for (int c = 0; c < 2; ++c)
        {
            const float dx = aPos[(size_t) i].x - m[c].x, dz = dist + aPos[(size_t) i].y;
            const float theta = std::atan2 (dx, dz) - m[c].axis;
            const float pat = m[c].a + (1.0f - m[c].a) * std::cos (theta);
            const float di = d[(size_t) (i * 2 + c)];
            g[c] = pat * dist / juce::jmax (0.3f, di);
            rg[c] = std::abs (pat);
        }
        const float dl = juce::jlimit (1.0f, maxDelay, 1.0f + (d[(size_t) (i * 2)] - minD) / 343.0f * (float) sampleRate);
        const float dr = juce::jlimit (1.0f, maxDelay, 1.0f + (d[(size_t) (i * 2 + 1)] - minD) / 343.0f * (float) sampleRate);
        if (! v.active) { v.delayL = dl; v.delayR = dr; }
        v.tDelayL = dl; v.tDelayR = dr;
        v.gainL = g[0]; v.gainR = g[1];
        v.roomL = rg[0]; v.roomR = rg[1];
        const float avgD = 0.5f * (d[(size_t) (i * 2)] + d[(size_t) (i * 2 + 1)]);
        const float fc = juce::jmin (0.45f * (float) sampleRate, 18000.0f / (1.0f + avgD / 10.0f));
        v.air = 1.0f - std::exp (-twoPi * fc / (float) sampleRate);
    }
    for (int i = k.count; i < maxSingers; ++i)
    {
        auto& v = *voices[(size_t) i];
        v.gate = false;
        v.pendingStart = false;
    }
}

// ---- notes --------------------------------------------------------------------------------------------------
int ChoirLoft::sungNote (int s, int note) const
{
    if ((int) param ("mode") != octaves) return note;
    const float c = aInfo[(size_t) s].centre;
    return note + 12 * juce::roundToInt ((c - (float) note) / 12.0f);
}

float ChoirLoft::singerCost (int s, int note) const
{
    const auto& vi = aInfo[(size_t) s];
    const float n = (float) sungNote (s, note);
    float cost = std::abs (n - vi.centre);
    if (n < vi.low) cost += (vi.low - n) * 3.0f;
    if (n > vi.high) cost += (n - vi.high) * 3.0f;
    return cost;
}

void ChoirLoft::startSinger (int s, int note, int channel, float velocity, bool legato)
{
    auto& v = *voices[(size_t) s];
    const auto& vi = aInfo[(size_t) s];
    const auto& st = styles()[(size_t) juce::jlimit (0, (int) styles().size() - 1, (int) param ("style"))];
    const float loose = param ("looseness") * st.loose;
    const bool wasSounding = v.gate || v.env > 0.05f;
    v.key = note;
    v.channel = juce::jlimit (1, 16, channel);
    v.vel = velocity;
    v.sung = (float) sungNote (s, note);
    v.started = ++noteCounter;
    if (legato || (wasSounding && v.gate))
    {
        // glide over to the new note (solo legato, or a singer moved to another note)
        v.glideFrom = v.pitch;
        const float ms = legato ? param ("glide") : 60.0f;
        v.glideLen = juce::jmax (1, (int) (ms * 0.001f * (float) sampleRate / controlEvery));
        v.glideAge = 0;
        if (! legato) v.dip = 0.35f;
        v.gate = true;
        v.pendingStart = false;
        return;
    }
    v.gate = true;
    v.pendingStart = true;
    v.wait = (int) param ("mode") == solo ? 0 : (int) (v.rnd.nextFloat() * loose * 0.045f * (float) sampleRate);
    int onset = (int) param ("onset");
    if (onset == onsetRandom)
    {
        juce::Random r ((juce::int64) note * 31 + (juce::int64) (noteCounter / 3));
        onset = r.nextInt (onsetRandom);
    }
    v.onset = onset;
    v.scoop = -(st.scoop * (0.6f + 0.4f * v.rnd.nextFloat()) + (onset == onsetW ? 0.3f : 0.0f));
    v.scoopCoef = std::exp (-(float) controlEvery / (juce::jmax (0.01f, st.scoopTime) * (float) sampleRate));
    v.error = gauss (v.rnd) * vi.errorCents * 0.01f * (0.4f + loose);
    v.detune = gauss (v.rnd) * 0.12f * loose;
    v.vibRate = vi.vibRate * (st.vibRate / 5.5f) * (1.0f + gauss (v.rnd) * 0.04f);
    if (! wasSounding)
    {
        v.pitch = v.sung + v.scoop + v.error;
        v.glideLen = 0;
        v.phase = v.rnd.nextFloat();
        v.vibPhase = v.rnd.nextFloat() * twoPi;
    }
    else
    {
        v.glideFrom = v.pitch;
        v.glideLen = juce::jmax (1, (int) (0.05f * (float) sampleRate / controlEvery));
        v.glideAge = 0;
    }
    v.motionOffset = param ("motionPoly") > 0.5f ? v.rnd.nextFloat() : 0.0f;
    {
        // random vowel per note: shared by the note's singers unless they move alone
        juce::Random r (param ("motionPoly") > 0.5f ? v.rnd.nextInt() : (juce::int64) note * 977 + (juce::int64) noteCounter / 5);
        v.randX = r.nextFloat();
        v.randY = r.nextFloat();
    }
    v.breathState = 0;
    v.breathEnv = 0.0f;
}

void ChoirLoft::noteOn (int note, int channel, float velocity)
{
    // a repeated key: let go of the old one first
    for (size_t i = 0; i < held.size(); ++i)
        if (held[i].note == note && held[i].channel == channel)
        {
            const auto ord = held[i].order;
            for (int s = 0; s < maxSingers; ++s)
                if (voices[(size_t) s]->gate && voices[(size_t) s]->order == ord) voices[(size_t) s]->gate = false;
            held.erase (held.begin() + (long) i);
            break;
        }
    Held h;
    h.note = note; h.channel = channel; h.velocity = velocity; h.order = ++noteCounter; h.down = true; h.time = now;
    if (held.size() >= 120) held.erase (held.begin());
    held.push_back (h);

    const int mode = (int) param ("mode");
    auto begin = [&] (int s, bool legato)
    {
        startSinger (s, note, channel, velocity, legato);
        voices[(size_t) s]->order = h.order;
    };

    if (mode == solo)
    {
        const int s = juce::jlimit (0, count - 1, juce::roundToInt (param ("singer")) - 1);
        begin (s, voices[(size_t) s]->gate);
        return;
    }

    if (mode == roundRobin)
    {
        const int per = juce::jlimit (1, count, juce::roundToInt (param ("perNote")));
        for (int k = 0; k < per; ++k)
        {
            int pick = -1;
            for (int j = 0; j < count; ++j)
            {
                const int s = (rrNext + j) % count;
                if (! voices[(size_t) s]->gate) { pick = s; break; }
            }
            if (pick < 0)
            {
                juce::uint32 oldest = 0xffffffffu;
                for (int s = 0; s < count; ++s)
                    if (voices[(size_t) s]->order != h.order && voices[(size_t) s]->started < oldest) { oldest = voices[(size_t) s]->started; pick = s; }
            }
            if (pick < 0) break;
            begin (pick, false);
            rrNext = (pick + 1) % count;
        }
        return;
    }

    // sections / octaves: the choir shares the held notes, everyone takes the note that suits them
    int notes = 0;
    for (auto& x : held) if (x.down || sustain) ++notes;
    notes = juce::jmax (1, notes);
    const int target = juce::jmax (1, count / notes);
    std::array<std::pair<float, int>, maxSingers> free {};
    int nFree = 0;
    for (int s = 0; s < count; ++s)
        if (! voices[(size_t) s]->gate) free[(size_t) nFree++] = { singerCost (s, note), s };
    std::sort (free.begin(), free.begin() + nFree);
    int taken = 0;
    for (int i = 0; i < nFree && taken < target; ++i, ++taken) begin (free[(size_t) i].second, false);

    while (taken < target)
    {
        // take a singer from the note that has the most (more than its share); if every note is down to one
        // singer and the new note has nobody, the oldest note gives up its singer
        juce::uint32 richest = 0, oldestOrder = 0xffffffffu;
        int most = 0;
        for (auto& x : held)
        {
            if (x.order == h.order) continue;
            int c = 0;
            for (int s = 0; s < count; ++s) if (voices[(size_t) s]->gate && voices[(size_t) s]->order == x.order) ++c;
            if (c > most) { most = c; richest = x.order; }
            if (c > 0 && x.order < oldestOrder) oldestOrder = x.order;
        }
        juce::uint32 from = 0;
        int best = -1;
        if (most > target) from = richest;
        else if (taken == 0 && oldestOrder != 0xffffffffu)
        {
            // more notes than singers. Played one after another, the newest note wins (a melody keeps moving);
            // struck together as a chord, a singer only moves if the new note suits their voice better
            bool together = true;
            for (auto& x : held)
                if (x.order != h.order && (x.down || sustain) && now - x.time > (juce::int64) (0.04 * sampleRate)) together = false;
            if (! together) from = oldestOrder;
            else
            {
                float bestGain = 0.0f;
                for (int s = 0; s < count; ++s)
                {
                    auto& vs = *voices[(size_t) s];
                    if (! vs.gate) continue;
                    const float gain = singerCost (s, vs.key) - singerCost (s, note);
                    if (gain > bestGain) { bestGain = gain; best = s; }
                }
                if (best < 0) break;
            }
        }
        else break;
        if (best < 0)
        {
            float bestCost = 1.0e9f;
            for (int s = 0; s < count; ++s)
                if (voices[(size_t) s]->gate && voices[(size_t) s]->order == from)
                {
                    const float c = singerCost (s, note);
                    if (c < bestCost) { bestCost = c; best = s; }
                }
        }
        if (best < 0) break;
        begin (best, false);
        ++taken;
    }
}

void ChoirLoft::noteOff (int note, int channel)
{
    for (size_t i = 0; i < held.size(); ++i)
    {
        if (held[i].note != note || held[i].channel != channel || ! held[i].down) continue;
        if (sustain) { held[i].down = false; return; }
        const auto ord = held[i].order;
        held.erase (held.begin() + (long) i);
        if ((int) param ("mode") == solo)
        {
            const int s = juce::jlimit (0, count - 1, juce::roundToInt (param ("singer")) - 1);
            auto& v = *voices[(size_t) s];
            if (v.order != ord) return;
            // last-note priority: go back to the newest key still down
            for (auto it = held.rbegin(); it != held.rend(); ++it)
                if (it->down)
                {
                    startSinger (s, it->note, it->channel, it->velocity, true);
                    v.order = it->order;
                    return;
                }
            v.gate = false;
            return;
        }
        for (int s = 0; s < maxSingers; ++s)
            if (voices[(size_t) s]->gate && voices[(size_t) s]->order == ord) voices[(size_t) s]->gate = false;
        return;
    }
}

void ChoirLoft::releaseAll (bool hard)
{
    held.clear();
    for (auto& v : voices)
    {
        v->gate = false;
        v->pendingStart = false;
        if (hard) { v->env = v->level = v->amp = v->ampStep = 0.0f; v->breathState = 0; v->breathEnv = 0.0f; }
    }
}

void ChoirLoft::handleMidi (const juce::MidiMessage& m)
{
    if (m.isNoteOn()) noteOn (m.getNoteNumber(), m.getChannel(), m.getFloatVelocity());
    else if (m.isNoteOff()) noteOff (m.getNoteNumber(), m.getChannel());
    else if (m.isPitchWheel())
    {
        const float v = (float) (m.getPitchWheelValue() - 8192) / 8192.0f;
        const int ch = m.getChannel();
        chanBend[(size_t) ch] = ch == 1 ? v * 2.0f : v * 48.0f;   // MPE member channels bend +-48
    }
    else if (m.isChannelPressure()) chanPressure[(size_t) m.getChannel()] = (float) m.getChannelPressureValue() / 127.0f;
    else if (m.isController())
    {
        const int cc = m.getControllerNumber();
        const float v = (float) m.getControllerValue() / 127.0f;
        if (cc == 1) modWheel = v;
        else if (cc == 11) expression = v;
        else if (cc == 64)
        {
            sustain = v >= 0.5f;
            if (! sustain)
                for (size_t i = 0; i < held.size();)
                {
                    if (held[i].down) { ++i; continue; }
                    const auto ord = held[i].order;
                    for (auto& vc : voices) if (vc->gate && vc->order == ord) vc->gate = false;
                    held.erase (held.begin() + (long) i);
                }
        }
        else if (cc == 120) releaseAll (true);
        else if (cc == 123) releaseAll (false);
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff()) releaseAll (m.isAllSoundOff());
}

// ---- audio ------------------------------------------------------------------------------------------------------
void ChoirLoft::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals nd;
    const int total = buffer.getNumSamples();
    buffer.clear();
    refreshCast (false);
    if (roomBus.getNumSamples() < total) { roomBus.setSize (2, total, false, false, true); wet.setSize (2, total, false, false, true); }
    roomBus.clear();

    const float sr = (float) sampleRate;
    const auto& st = styles()[(size_t) juce::jlimit (0, (int) styles().size() - 1, (int) param ("style"))];
    const auto& lf = lfBank();
    const float pAttack = param ("attack") * 0.001f, pRelease = param ("release") * 0.001f;
    const float pBreath = param ("breath") * 2.0f, pVib = param ("vibrato") * 2.0f, pLoose = param ("looseness") * st.loose;
    const float pRasp = param ("rasp"), pDyn = param ("dynamics"), pBreaths = param ("breaths");
    const float vx0 = param ("vowelX"), vy0 = param ("vowelY"), vx1 = param ("vowel2X"), vy1 = param ("vowel2Y");
    const float pRound = param ("round"), pHum = param ("hum");
    const int motion = (int) param ("motion");
    const float mRate = param ("motionRate"), mDepth = param ("motionDepth");
    const float tune = param ("tune") / 100.0f;
    const float countGain = std::pow ((float) count, -0.35f);
    const float distance = param ("distance");
    const float directGain = juce::jlimit (0.2f, 1.6f, std::pow (2.5f / distance, 0.8f));
    static constexpr float norm = 0.5f, breathGain = 0.02f;
    const int ctrlSamples = controlEvery;
    const float ctrlDt = (float) ctrlSamples / sr;
    const float delaySmooth = 1.0f - std::exp (-1.0f / (0.05f * sr));

    float* outL = buffer.getWritePointer (0);
    float* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    float* rbL = roomBus.getWritePointer (0);
    float* rbR = roomBus.getWritePointer (1);

    // split the block at MIDI events so notes land on time
    int pos = 0;
    auto it = midi.begin();
    int active = 0;
    while (pos < total)
    {
        int next = total;
        while (it != midi.end())
        {
            const auto meta = *it;
            const int t = juce::jlimit (0, total - 1, meta.samplePosition);
            if (t > pos) { next = t; break; }
            now = sampleClock + t;
            handleMidi (meta.getMessage());
            ++it;
        }
        const int n = next - pos;

        for (int s = 0; s < maxSingers; ++s)
        {
            auto& v = *voices[(size_t) s];
            const bool inCast = s < count;
            if (! v.gate && ! v.pendingStart && v.level < 1.0e-6f && v.amp < 1.0e-6f && v.breathState == 0 && v.tail <= 0) { v.active = false; continue; }
            v.active = true;
            const auto& vi = aInfo[(size_t) juce::jmin (s, count - 1)];

            for (int i = 0; i < n; ++i)
            {
                // ---- control rate ----
                if (((sampleClock + pos + i) % ctrlSamples) == 0)
                {
                    if (v.pendingStart)
                    {
                        if (v.wait > 0) v.wait -= ctrlSamples;
                        else { v.pendingStart = false; v.sinceOnset = 0.0f; v.sungFor = 0.0f; }
                    }
                    const bool sounding = v.gate && ! v.pendingStart && inCast;
                    if (sounding) { v.sinceOnset += ctrlDt; v.sungFor += ctrlDt; v.sinceRelease = 0.0f; }
                    else
                    {
                        if (v.sinceRelease == 0.0f && v.sungFor > 0.8f && pBreaths > 0.001f && v.gate == false)
                        {
                            v.breathState = 1;                               // waits a moment, then breathes in
                            v.breathClock = -(0.2f + v.rnd.nextFloat() * 0.5f);
                        }
                        v.sinceRelease += ctrlDt;
                        v.sungFor = 0.0f;
                    }
                    if (v.breathState != 0)
                    {
                        v.breathClock += ctrlDt;
                        if (sounding) v.breathState = 0;
                        else if (v.breathClock >= 0.0f)
                        {
                            v.breathState = 2;
                            const float bt = v.breathClock / 0.42f;
                            v.breathEnv = bt < 1.0f ? std::sin (bt * juce::MathConstants<float>::pi) * pBreaths : 0.0f;
                            if (bt >= 1.0f) v.breathState = 0;
                        }
                    }
                    if (v.breathState != 2) v.breathEnv = 0.0f;

                    // pitch
                    if (v.glideLen > 0 && v.glideAge < v.glideLen)
                    {
                        ++v.glideAge;
                        const float t = smoothstep (0.0f, 1.0f, (float) v.glideAge / (float) v.glideLen);
                        v.pitch = lerp (v.glideFrom, v.sung, t);
                    }
                    else v.pitch = v.sung;
                    v.scoop *= v.scoopCoef;
                    v.error *= std::exp (-ctrlDt / 0.25f);
                    v.driftV += gauss (v.rnd) * 0.02f * ctrlDt * 60.0f * vi.wobble * (0.5f + pLoose);
                    v.driftV *= 0.995f;
                    v.drift = juce::jlimit (-0.4f, 0.4f, v.drift * 0.9995f + v.driftV * ctrlDt);
                    v.vibPhase += twoPi * v.vibRate * ctrlDt;
                    if (v.vibPhase > twoPi) v.vibPhase -= twoPi;
                    const float vibIn = smoothstep (st.vibDelay, st.vibDelay + 0.45f, v.sinceOnset);
                    const float vibDepth = (st.vibDepth * vi.vibScale * pVib * vibIn + modWheel * 0.6f) * (1.0f + 0.15f * std::sin (v.vibPhase * 0.13f));
                    const float bend = chanBend[1] + (v.channel > 1 ? chanBend[(size_t) v.channel] : 0.0f);
                    const float notePitch = v.pitch + v.scoop + v.error + v.drift + v.detune + bend + tune - v.dip
                                          + vibDepth * std::sin (v.vibPhase);
                    v.dip *= 0.9f;
                    const float f0 = mtof (notePitch);
                    v.inc = (double) f0 / sampleRate;

                    // dynamics -> loudness, pressed / breathy folds, brightness
                    const float press = chanPressure[(size_t) v.channel];
                    const float dyn = juce::jlimit (0.0f, 1.0f, ((1.0f - pDyn) * 0.75f + pDyn * v.vel) * (0.35f + 0.65f * expression) + press * 0.3f);
                    float rd = vi.rd + st.rd + (0.62f - dyn) * 0.9f;
                    const float head = smoothstep (vi.breakNote - 1.0f, vi.breakNote + 2.0f, v.sung);
                    rd += head * 0.3f;
                    float gainDb = -26.0f * std::pow (1.0f - dyn, 1.3f) - head * 1.5f;
                    if (v.sung < vi.low) { gainDb -= (vi.low - v.sung) * 1.2f; rd += (vi.low - v.sung) * 0.05f; }
                    if (v.sung > vi.high) { gainDb -= (v.sung - vi.high) * 1.5f; rd += (v.sung - vi.high) * 0.08f; }
                    gainDb += vi.chestDb * 0.3f;
                    gainDb -= 6.0f * std::log2 (juce::jmax (60.0f, f0) / 220.0f) * 0.25f;
                    v.rdNow = juce::jlimit (rdMin, rdMax, rd);
                    v.raspAmt = juce::jlimit (0.0f, 1.0f, vi.rasp + st.rasp + pRasp);
                    v.aspAmt = (vi.breath * pBreath * st.breath + (1.0f - dyn) * 0.03f) * (1.0f + head * 0.4f);
                    const float tiltHz = juce::jlimit (900.0f, 0.45f * sr, (1800.0f + 9000.0f * std::pow (dyn, 1.4f)) * (0.6f + vi.bright * 0.8f + st.bright) * std::sqrt (vi.formantScale));
                    v.tiltA = 1.0f - std::exp (-twoPi * tiltHz / sr);
                    // above the fifth formant a real voice still has air and the upper harmonics: a gentle parallel band
                    v.presA = 1.0f - std::exp (-twoPi * juce::jmin (0.4f * sr, 3600.0f * std::sqrt (vi.formantScale)) / sr);
                    v.presence = 0.32f * (0.5f + vi.bright + st.bright) * (0.4f + 0.6f * dyn) * (1.0f - 0.85f * juce::jmax (v.hum, pHum));

                    // the vowel: base A, motion towards B, onset consonant targets
                    float ax = vx0, ay = vy0;
                    const float t = v.sinceOnset;
                    const float ph = (float) (t * mRate) + v.motionOffset;
                    switch (motion)
                    {
                        case wave:     { const float w = 0.5f - 0.5f * std::cos (twoPi * ph); ax = lerp (vx0, vx1, w * mDepth); ay = lerp (vy0, vy1, w * mDepth); break; }
                        case envelope: { const float w = smoothstep (0.0f, 1.0f, t * mRate); ax = lerp (vx0, vx1, w * mDepth); ay = lerp (vy0, vy1, w * mDepth); break; }
                        case steps:    { const bool b = ((int) std::floor (ph * 2.0f)) & 1; ax = b ? lerp (vx0, vx1, mDepth) : vx0; ay = b ? lerp (vy0, vy1, mDepth) : vy0; break; }
                        case randomPerNote: ax = lerp (vx0, v.randX, mDepth); ay = lerp (vy0, v.randY, mDepth); break;
                        case drift:
                            v.driftX = juce::jlimit (-1.0f, 1.0f, v.driftX + gauss (v.rnd) * std::sqrt (ctrlDt * mRate) * 0.5f - v.driftX * ctrlDt * mRate * 0.3f);
                            v.driftY = juce::jlimit (-1.0f, 1.0f, v.driftY + gauss (v.rnd) * std::sqrt (ctrlDt * mRate) * 0.5f - v.driftY * ctrlDt * mRate * 0.3f);
                            ax = juce::jlimit (0.0f, 1.0f, vx0 + v.driftX * 0.5f * mDepth);
                            ay = juce::jlimit (0.0f, 1.0f, vy0 + v.driftY * 0.5f * mDepth);
                            break;
                        default: break;
                    }
                    const float vk = 1.0f - std::exp (-ctrlDt / 0.03f);
                    v.fx += (ax - v.fx) * vk;
                    v.fy += (ay - v.fy) * vk;
                    auto f3 = chartFormants (v.fx, v.fy);
                    float F[5] = { f3[0], f3[1], f3[2], refF4, refF5 };
                    float Bw[5] = { refBw[0], refBw[1], refBw[2], refBw[3], refBw[4] };
                    float humNow = pHum, voicingT = 1.0f, burst = 0.0f;
                    // onset consonants (first ~100 ms of a note)
                    const float ot = v.sinceOnset;
                    auto towards = [&] (std::initializer_list<float> target, float dur)
                    {
                        const float w = 1.0f - smoothstep (0.0f, dur, ot);
                        int k = 0;
                        for (float tf : target) { F[k] = lerp (F[k], tf, w); ++k; }
                        return w;
                    };
                    float onsetAtk = 1.0f;
                    switch (v.onset)
                    {
                        case onsetHard: onsetAtk = 0.15f; if (ot < 0.03f) v.rdNow = juce::jmax (rdMin, v.rdNow - 0.4f); break;
                        case onsetH:    { const float w = 1.0f - smoothstep (0.04f, 0.1f, ot); voicingT = 1.0f - w; v.aspAmt += w * 0.6f; onsetAtk = 0.3f; break; }
                        case onsetM:    humNow = juce::jmax (humNow, 1.0f - smoothstep (0.06f, 0.11f, ot)); onsetAtk = 0.4f; break;
                        case onsetN:    { const float w = 1.0f - smoothstep (0.06f, 0.11f, ot); humNow = juce::jmax (humNow, w); F[1] = lerp (F[1], 1650.0f, w); onsetAtk = 0.4f; break; }
                        case onsetL:    towards ({ 360.0f, 1050.0f, 2900.0f }, 0.09f); onsetAtk = 0.4f; break;
                        case onsetD:
                        {
                            const float closed = 1.0f - smoothstep (0.03f, 0.036f, ot);
                            voicingT = lerp (1.0f, 0.06f, closed);
                            if (ot >= 0.03f && ot < 0.04f) burst = 0.12f;
                            towards ({ 260.0f, 1750.0f, 2700.0f }, 0.08f);
                            onsetAtk = 0.08f;
                            break;
                        }
                        case onsetB:
                        {
                            const float closed = 1.0f - smoothstep (0.03f, 0.036f, ot);
                            voicingT = lerp (1.0f, 0.08f, closed);
                            if (ot >= 0.03f && ot < 0.037f) burst = 0.06f;
                            towards ({ 250.0f, 800.0f, 2200.0f }, 0.07f);
                            onsetAtk = 0.08f;
                            break;
                        }
                        case onsetW: { auto u = chartFormants (1.0f, 0.0f); towards ({ u[0] * 0.95f, u[1] * 0.9f, u[2] }, 0.12f); onsetAtk = 0.5f; break; }
                        case onsetY: { auto ii = chartFormants (0.0f, 0.0f); towards ({ ii[0] * 0.95f, ii[1] * 1.05f, ii[2] * 1.05f }, 0.12f); onsetAtk = 0.5f; break; }
                        default: break;
                    }
                    if (! sounding) voicingT = 1.0f;
                    v.voicingTarget = voicingT;
                    v.burst = burst;

                    // lips
                    const float backness = v.fx;
                    const float round = juce::jlimit (-1.0f, 1.0f, pRound + (backness - 0.5f) * 0.2f);
                    F[1] *= 1.0f - 0.12f * round;
                    F[2] *= 1.0f - 0.05f * round;
                    // the singer's body
                    F[0] *= vi.f1Mul; F[1] *= vi.f2Mul; F[2] *= vi.f3Mul;
                    for (int k = 0; k < 5; ++k) { F[k] *= vi.formantScale; Bw[k] *= vi.bwMul * std::sqrt (vi.formantScale); }
                    // hum: lips closed, the sound comes through the nose
                    humNow = juce::jlimit (0.0f, 1.0f, humNow);
                    for (int k = 0; k < 5; ++k)
                    {
                        F[k] = lerp (F[k], murmurF[k] * std::sqrt (vi.formantScale), humNow);
                        Bw[k] = lerp (Bw[k], murmurBw[k], humNow);
                    }
                    // singing technique
                    if (st.overtone)
                    {
                        const int h = juce::jlimit (4, 16, juce::roundToInt (lerp (6.0f, 13.0f, v.fx) - (v.fy - 0.3f) * 3.0f));
                        F[1] = f0 * (float) h; Bw[1] = 28.0f;
                        F[2] = F[1] * 1.07f;  Bw[2] = 40.0f;
                        F[3] = juce::jmax (F[3], F[2] * 1.3f);
                    }
                    else
                    {
                        if (F[0] < f0 * 1.08f) F[0] = lerp (F[0], f0 * 1.08f, juce::jmax (0.35f, st.f1Track));
                        if (st.belt > 0.0f && 2.0f * f0 > F[0] * 0.7f && 2.0f * f0 < F[0] * 1.45f) F[0] = lerp (F[0], 2.04f * f0, st.belt * 0.8f);
                        if (st.ring > 0.0f)
                        {
                            const float c = (vi.formantScale > 1.1f ? 3200.0f : 2850.0f);
                            F[2] = lerp (F[2], c * 0.9f, st.ring * 0.5f);
                            F[3] = lerp (F[3], c * 1.04f, st.ring * 0.7f);
                            F[4] = lerp (F[4], c * 1.22f, st.ring * 0.5f);
                            for (int k = 2; k < 5; ++k) Bw[k] *= 1.0f - 0.3f * st.ring;
                        }
                    }
                    for (int k = 1; k < 5; ++k) F[k] = juce::jmax (F[k], F[k - 1] + 120.0f);
                    const float fk = 1.0f - std::exp (-ctrlDt / (v.sinceOnset < 0.12f ? 0.012f : 0.02f));
                    for (int k = 0; k < 5; ++k)
                    {
                        const float tf = juce::jlimit (90.0f, 0.44f * sr, F[k]);
                        v.F[k] += (tf - v.F[k]) * fk;
                        v.B[k] += (Bw[k] - v.B[k]) * fk;
                        v.formant[k].set (v.F[k], juce::jmax (20.0f, v.B[k]), sr);
                    }
                    v.hum += (humNow - v.hum) * fk;
                    const float coupling = juce::jlimit (0.0f, 1.0f, vi.nasal + st.nasal + v.hum * 0.9f);
                    v.nasalCoupling = coupling;
                    v.nasalPole.set (vi.nasalHz, 100.0f, sr);
                    v.nasalZero.set (juce::jmin (0.44f * sr, vi.nasalHz * (1.0f + 1.3f * coupling)), 100.0f, sr);
                    if (! sounding && v.breathEnv > 0.0f)
                        for (int k = 0; k < 5; ++k) v.formant[k].set (v.F[k] * 1.05f, v.B[k] * 2.5f, sr);   // breathing in through an open mouth

                    // amplitude envelope target and speed
                    const float oralLoss = 1.0f - 0.55f * v.hum;
                    v.levelTarget = sounding ? juce::Decibels::decibelsToGain (gainDb) * oralLoss * (1.0f + st.rasp * 0.2f) : 0.0f;
                    const float atk = juce::jmax (0.004f, pAttack * st.attack * onsetAtk);
                    const float tc = sounding ? atk : pRelease;
                    v.level += (v.levelTarget - v.level) * (1.0f - std::exp (-ctrlDt / juce::jmax (0.003f, tc * 0.45f)));
                    if (v.level < 1.0e-6f && v.levelTarget == 0.0f) v.level = 0.0f;
                    v.env = v.level;
                    v.ampStep = (v.level - v.amp) / (float) ctrlSamples;

                    // what the editor shows
                    liveLevel[(size_t) s] = v.level;
                    liveX[(size_t) s] = v.fx;
                    liveY[(size_t) s] = v.fy;
                    liveOpen[(size_t) s] = sounding ? juce::jlimit (0.0f, 1.0f, (0.2f + 0.8f * v.fy) * (1.0f - v.hum) * juce::jmin (1.0f, v.level * 4.0f)) : 0.0f;
                    liveHum[(size_t) s] = v.hum;
                    liveNote[(size_t) s] = sounding ? juce::roundToInt (v.sung) : -1;
                }

                // ---- audio rate ----
                float y = 0.0f;
                v.amp += v.ampStep;
                if (v.amp > 1.0e-6f || v.level > 1.0e-6f || v.breathEnv > 0.0f || v.burst > 0.0f)
                {
                    v.phase += v.inc * v.cycMul;
                    if (v.phase >= 1.0)
                    {
                        v.phase -= 1.0;
                        ++v.cycles;
                        const float rdc = juce::jlimit (rdMin, rdMax, v.rdNow + gauss (v.rnd) * 0.03f + v.raspAmt * 0.15f * (float) (v.cycles & 1));
                        const float tf = (rdc - rdMin) / (rdMax - rdMin) * (float) (lfTables - 1);
                        v.tA = juce::jmin (lfTables - 2, (int) tf);
                        v.tFrac = tf - (float) v.tA;
                        v.te = lf.te[(size_t) v.tA];
                        v.cycMul = 1.0f + gauss (v.rnd) * vi.jitter * (1.0f + v.raspAmt * 4.0f);
                        v.cycAmp = 1.0f + gauss (v.rnd) * vi.shimmer * (1.0f + v.raspAmt * 3.0f);
                        if (v.cycles & 1) v.cycAmp *= 1.0f - v.raspAmt * 0.45f;
                    }
                    const float pf = (float) v.phase * (float) lfLen;
                    const int i0 = juce::jmin (lfLen - 1, (int) pf);
                    const float fr = pf - (float) i0;
                    const float* ta = lf.table (v.tA);
                    const float* tb = lf.table (v.tA + 1);
                    const float ga = ta[i0] + (ta[i0 + 1] - ta[i0]) * fr;
                    const float gb = tb[i0] + (tb[i0 + 1] - tb[i0]) * fr;
                    const float glot = (ga + (gb - ga) * v.tFrac) * v.cycAmp;

                    // aspiration: noise through the open glottis (and a rough burst at closure when raspy)
                    const float wn = v.rnd.nextFloat() * 2.0f - 1.0f;
                    v.aspHp = 0.9f * (v.aspHp + wn - v.aspPrev);
                    v.aspPrev = wn;
                    const float ph = (float) v.phase;
                    const float open = ph < v.te ? 1.0f : 0.3f;
                    const float rough = (ph >= v.te && ph < v.te + 0.1f) ? v.raspAmt * 0.6f : 0.0f;
                    v.voicing += (v.voicingTarget - v.voicing) * 0.01f;
                    float src = glot * v.voicing + v.aspHp * (v.aspAmt * open + rough) + v.aspHp * v.burst;
                    if (v.amp <= 1.0e-6f) src = v.aspHp;     // only breathing
                    // spectral tilt
                    v.tiltLp += v.tiltA * (src - v.tiltLp);
                    src = v.tiltLp;
                    // the air band (2-pole high-pass of the source), the nose, then the five formants
                    v.presLp1 += v.presA * (src - v.presLp1);
                    const float hp1 = src - v.presLp1;
                    v.presLp2 += v.presA * (hp1 - v.presLp2);
                    const float air = (hp1 - v.presLp2) * v.presence;
                    src = v.nasalPole.process (v.nasalZero.process (src));
                    for (int k = 4; k >= 0; --k) src = v.formant[k].process (src);
                    src += air;
                    y = src * (juce::jmax (0.0f, v.amp) + v.breathEnv * breathGain) * countGain * norm;
                    v.tail = v.mask + 1;
                }
                else if (v.tail > 0) --v.tail;

                // the stage: air absorption, then the distance to each microphone
                v.airLp += v.air * (y - v.airLp);
                v.line[(size_t) v.wpos] = v.airLp;
                v.delayL += (v.tDelayL - v.delayL) * delaySmooth;
                v.delayR += (v.tDelayR - v.delayR) * delaySmooth;
                auto tap = [&v] (float d)
                {
                    const float rp = (float) v.wpos - d;
                    const int i0 = (int) std::floor (rp);
                    const float f = rp - (float) i0;
                    const float a = v.line[(size_t) (i0 & v.mask)], b = v.line[(size_t) ((i0 + 1) & v.mask)];
                    return a + (b - a) * f;
                };
                const float l = tap (v.delayL), r = tap (v.delayR);
                v.wpos = (v.wpos + 1) & v.mask;
                const int o = pos + i;
                outL[o] += l * v.gainL;
                if (outR != nullptr) outR[o] += r * v.gainR;
                rbL[o] += l * v.roomL;
                rbR[o] += r * v.roomR;
            }
            if (! v.gate && ! v.pendingStart && v.level < 1.0e-6f && v.breathState == 0 && v.tail <= 0)
            {
                v.active = false;
                liveLevel[(size_t) s] = 0.0f;
                liveNote[(size_t) s] = -1;
            }
        }
        pos = next;
    }
    while (it != midi.end()) { handleMidi ((*it).getMessage()); ++it; }
    for (auto& v : voices) if (v->level > 1.0e-4f) ++active;
    activeSingers = active;
    sampleClock += total;

    // ---- stereo width, distance, the room, volume ----
    const float width = param ("width");
    if (outR != nullptr)
        for (int i = 0; i < total; ++i)
        {
            const float m = 0.5f * (outL[i] + outR[i]), sd = 0.5f * (outL[i] - outR[i]) * width;
            outL[i] = (m + sd) * directGain;
            outR[i] = (m - sd) * directGain;
        }
    else
        juce::FloatVectorOperations::multiply (outL, directGain, total);

    const float roomGain = juce::Decibels::decibelsToGain (param ("roomLevel"), -39.9f);
    if (roomReady.load() && roomGain > 0.0f && (int) param ("space") != (int) roomir::dry && outR != nullptr)
    {
        for (int ch = 0; ch < 2; ++ch) wet.copyFrom (ch, 0, roomBus, ch, 0, total);
        juce::dsp::AudioBlock<float> blk (wet.getArrayOfWritePointers(), 2, (size_t) total);
        convolution.process (juce::dsp::ProcessContextReplacing<float> (blk));
        buffer.addFrom (0, 0, wet, 0, 0, total, roomGain * 0.5f);
        buffer.addFrom (1, 0, wet, 1, 0, total, roomGain * 0.5f);
    }

    const float vol = juce::Decibels::decibelsToGain (param ("volume"));
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        float* x = buffer.getWritePointer (ch);
        float& dc = ch == 0 ? dcL : dcR;
        float& dx = ch == 0 ? dcXL : dcXR;
        for (int i = 0; i < total; ++i)
        {
            // DC blocker, then a soft ceiling
            const float in = x[i] * vol;
            const float yv = in - dx + 0.9995f * dc;
            dx = in; dc = yv;
            const float ay = std::abs (yv);   // continuous soft knee from 0.8 up to a ceiling of 1
            x[i] = ay < 0.8f ? yv : std::copysign (0.8f + 0.2f * std::tanh ((ay - 0.8f) * 5.0f), yv);
        }
    }
}

} // namespace wis::daw
