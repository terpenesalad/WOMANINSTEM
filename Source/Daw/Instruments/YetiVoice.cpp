#include "YetiVoice.h"

namespace wis::daw
{

using namespace prm;

namespace
{
    // Formant tables (frequency Hz, level dB, bandwidth Hz) for U O A E I, after the classic Csound tables.
    struct Formant { float f, db, bw; };
    using VowelSet = std::array<std::array<Formant, 5>, 5>;   // [vowel U,O,A,E,I][formant]

    const VowelSet& table (int voiceType)
    {
        static const VowelSet bass { {
            { { { 350, 0, 40 }, { 600, -20, 80 }, { 2400, -32, 100 }, { 2675, -28, 120 }, { 2950, -36, 120 } } },   // U
            { { { 400, 0, 40 }, { 750, -11, 80 }, { 2400, -21, 100 }, { 2600, -20, 120 }, { 2900, -40, 120 } } },   // O
            { { { 600, 0, 60 }, { 1040, -7, 70 }, { 2250, -9, 110 }, { 2450, -9, 120 }, { 2750, -20, 130 } } },     // A
            { { { 400, 0, 40 }, { 1620, -12, 80 }, { 2400, -9, 100 }, { 2800, -12, 120 }, { 3100, -18, 120 } } },   // E
            { { { 250, 0, 60 }, { 1750, -14, 90 }, { 2600, -16, 100 }, { 3050, -22, 120 }, { 3340, -28, 120 } } },  // I
        } };
        static const VowelSet tenor { {
            { { { 350, 0, 40 }, { 600, -20, 60 }, { 2700, -17, 100 }, { 2900, -14, 120 }, { 3300, -26, 120 } } },
            { { { 400, 0, 40 }, { 800, -10, 80 }, { 2600, -12, 100 }, { 2800, -12, 120 }, { 3000, -26, 120 } } },
            { { { 650, 0, 80 }, { 1080, -6, 90 }, { 2650, -7, 120 }, { 2900, -8, 130 }, { 3250, -22, 140 } } },
            { { { 400, 0, 70 }, { 1700, -14, 80 }, { 2600, -12, 100 }, { 3200, -14, 120 }, { 3580, -20, 120 } } },
            { { { 290, 0, 40 }, { 1870, -15, 90 }, { 2800, -18, 100 }, { 3250, -20, 120 }, { 3540, -30, 120 } } },
        } };
        static const VowelSet alto { {
            { { { 325, 0, 50 }, { 700, -12, 60 }, { 2530, -30, 170 }, { 3500, -40, 180 }, { 4950, -64, 200 } } },
            { { { 450, 0, 70 }, { 800, -9, 80 }, { 2830, -16, 100 }, { 3500, -28, 130 }, { 4950, -55, 135 } } },
            { { { 800, 0, 80 }, { 1150, -4, 90 }, { 2800, -20, 120 }, { 3500, -36, 130 }, { 4950, -60, 140 } } },
            { { { 400, 0, 60 }, { 1600, -24, 80 }, { 2700, -30, 120 }, { 3300, -35, 150 }, { 4950, -60, 200 } } },
            { { { 350, 0, 50 }, { 1700, -20, 100 }, { 2700, -30, 120 }, { 3700, -36, 150 }, { 4950, -60, 200 } } },
        } };
        static const VowelSet soprano { {
            { { { 325, 0, 50 }, { 700, -16, 60 }, { 2700, -35, 170 }, { 3800, -40, 180 }, { 4950, -60, 200 } } },
            { { { 450, 0, 40 }, { 800, -11, 80 }, { 2830, -22, 100 }, { 3800, -22, 120 }, { 4950, -50, 120 } } },
            { { { 800, 0, 80 }, { 1150, -6, 90 }, { 2900, -32, 120 }, { 3900, -20, 130 }, { 4950, -50, 140 } } },
            { { { 350, 0, 60 }, { 2000, -20, 100 }, { 2800, -15, 120 }, { 3600, -40, 150 }, { 4950, -56, 200 } } },
            { { { 270, 0, 60 }, { 2140, -12, 90 }, { 2950, -26, 100 }, { 3900, -26, 120 }, { 4950, -44, 120 } } },
        } };
        switch (voiceType) { case 2: return tenor; case 3: return alto; case 4: return soprano; default: return bass; }
    }

    inline float polyBlep (double t, double dt) noexcept
    {
        if (t < dt) { const double x = t / dt; return (float) (x + x - x * x - 1.0); }
        if (t > 1.0 - dt) { const double x = (t - 1.0) / dt; return (float) (x * x + x + x + 1.0); }
        return 0.0f;
    }

    struct Svf
    {
        float ic1 = 0, ic2 = 0;
        float bp (float x, float a1, float a2, float a3) noexcept
        {
            const float v3 = x - ic2;
            const float v1 = a1 * ic1 + a2 * v3;
            const float v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            return v1;
        }
    };

    struct SvfCoeffs
    {
        float a1 = 0, a2 = 0, a3 = 0, k = 1, gain = 0;
        void set (float f, float bw, float g, double sr) noexcept
        {
            f = juce::jlimit (30.0f, (float) sr * 0.45f, f);
            const float q = juce::jmax (0.5f, f / juce::jmax (10.0f, bw));
            const float gg = std::tan (juce::MathConstants<float>::pi * f / (float) sr);
            k = 1.0f / q;
            a1 = 1.0f / (1.0f + gg * (gg + k));
            a2 = gg * a1;
            a3 = gg * a2;
            gain = g;
        }
    };
}

// =====================================================================================================
//  Parameters & presets
// =====================================================================================================
juce::StringArray YetiVoice::voiceNames()     { return { "Yeti (deepest)", "Bass", "Tenor", "Alto", "Soprano" }; }
juce::StringArray YetiVoice::vowelModeNames() { return { "Pad / mod wheel", "New vowel every note", "Random vowels", "Vowel LFO", "O-M chant" }; }

static Layout yetiLayout()
{
    Layout l;
    addChoice  (l, "voice", "Voice", YetiVoice::voiceNames(), 0);
    addFloat   (l, "size", "Head Size", 0.6f, 1.6f, 1.0f, "x", 1.0f, 2);
    addPercent (l, "vowel", "Vowel", 0.5f);
    addChoice  (l, "vowelMode", "Vowels", YetiVoice::vowelModeNames(), 0);
    addFloat   (l, "vowelRate", "Vowel LFO Rate", 0.05f, 8.0f, 0.6f, "Hz", 1.0f, 2);
    addPercent (l, "vowelDepth", "Vowel LFO Depth", 0.5f);
    addPercent (l, "mmm", "Mmm (closed mouth)", 0.35f);
    addPercent (l, "tension", "Tension", 0.45f);
    addPercent (l, "breath", "Breath", 0.12f);
    addPercent (l, "throat", "Throat Singing", 0.0f);
    addFloat   (l, "overtone", "Overtone", 400.0f, 3200.0f, 1300.0f, "Hz", 1200.0f, 0);
    addPercent (l, "growl", "Growl (undertone)", 0.0f);
    addBool    (l, "mono", "Legato (one voice)", true);
    addMs      (l, "glide", "Glide", 0.0f, 800.0f, 90.0f);
    addFloat   (l, "vibRate", "Vibrato Rate", 2.0f, 9.0f, 5.2f, "Hz", 0.0f, 1);
    addFloat   (l, "vibDepth", "Vibrato", 0.0f, 120.0f, 35.0f, "ct", 0.0f, 0);
    addFloat   (l, "vibDelay", "Vibrato Delay", 0.0f, 2.0f, 0.4f, "s", 0.0f, 2);
    addChoice  (l, "choir", "Singers", { "Solo", "Duo", "Trio", "Quartet", "Choir of 5" }, 0);
    addFloat   (l, "choirDetune", "Choir Spread", 0.0f, 40.0f, 12.0f, "ct", 0.0f, 0);
    addMs      (l, "attack", "Attack", 2.0f, 1500.0f, 60.0f);
    addMs      (l, "release", "Release", 20.0f, 3000.0f, 300.0f);
    addMs      (l, "delayTime", "Delay Time", 30.0f, 1500.0f, 500.0f);
    addPercent (l, "delayFeedback", "Delay Feedback", 0.35f);
    addPercent (l, "delayMix", "Delay Mix", 0.35f);
    addHz      (l, "delayTone", "Delay Tone", 800.0f, 16000.0f, 5000.0f);
    addBool    (l, "pingpong", "Ping-Pong", true);
    addPercent (l, "reverb", "Mountain Reverb", 0.18f);
    addDb      (l, "volume", "Volume", -24.0f, 12.0f, 0.0f);
    return l;
}

namespace
{
    struct YetiPreset { const char* name; std::vector<std::pair<const char*, float>> values; };
    const std::vector<YetiPreset>& yetiPresets()
    {
        static const std::vector<YetiPreset> p {
            { "Yodel Yeti", { } },
            { "Himalayan Chant", { { "vowelMode", 4 }, { "mmm", 0.7f }, { "choir", 3 }, { "delayMix", 0.3f }, { "reverb", 0.4f }, { "vibDepth", 15 }, { "glide", 200 } } },
            { "Throat Singer", { { "throat", 0.8f }, { "overtone", 1500 }, { "vowel", 0.25f }, { "tension", 0.7f }, { "vibDepth", 5 }, { "delayMix", 0.2f } } },
            { "Kargyraa Growl", { { "growl", 0.8f }, { "throat", 0.4f }, { "vowel", 0.3f }, { "tension", 0.8f }, { "vibDepth", 8 }, { "reverb", 0.3f } } },
            { "Monster Choir", { { "choir", 4 }, { "choirDetune", 18 }, { "mono", 0 }, { "vowelMode", 0 }, { "reverb", 0.45f }, { "attack", 250 }, { "release", 900 } } },
            { "Vowel Wobble", { { "vowelMode", 3 }, { "vowelRate", 2.0f }, { "vowelDepth", 0.9f }, { "delayMix", 0.4f } } },
            { "Chatterbox", { { "vowelMode", 2 }, { "glide", 40 }, { "vibDepth", 20 }, { "attack", 15 }, { "release", 120 }, { "mmm", 0.2f } } },
            { "Tiny Yeti", { { "voice", 4 }, { "size", 1.4f }, { "vibRate", 6.5f }, { "vibDepth", 45 } } },
            { "Opera Yeti", { { "voice", 2 }, { "vibDepth", 70 }, { "vibDelay", 0.25f }, { "tension", 0.6f }, { "reverb", 0.5f }, { "delayMix", 0.15f } } },
            { "Echo Canyon", { { "delayTime", 640 }, { "delayFeedback", 0.6f }, { "delayMix", 0.55f }, { "reverb", 0.35f } } },
            { "Dry Yeti", { { "delayMix", 0.0f }, { "reverb", 0.0f } } },
        };
        return p;
    }
}

juce::StringArray YetiVoice::getProgramNames()
{
    juce::StringArray n;
    for (auto& p : yetiPresets()) n.add (p.name);
    return n;
}

void YetiVoice::loadProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) yetiPresets().size())) return;
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            rp->setValueNotifyingHost (rp->getDefaultValue());
    for (auto& [id, v] : yetiPresets()[(size_t) index].values)
        setParam (id, v);
}

std::function<juce::AudioProcessorEditor* (YetiVoice&)> YetiVoice::editorFactory;

// =====================================================================================================
//  Voice
// =====================================================================================================
struct YetiVoice::Voice
{
    static constexpr int maxSingers = 5;
    bool active = false, keyDown = false;
    int note = 60;
    float velocity = 0.8f;
    double phase[maxSingers] {}, subPhase[maxSingers] {};
    float logFreq = 0.0f, logTarget = 0.0f;        // log2 of frequency
    float env = 0.0f, mouth = 0.0f, vowel = 0.5f, vowelTarget = 0.5f;
    float held = 0.0f;
    float tilt[maxSingers] {};
    float singerCents[maxSingers] {}, singerPan[maxSingers] {}, jitter = 0.0f, jitterTarget = 0.0f;
    Svf formL[5], formR[5], humL, humR, throatL, throatR;
    SvfCoeffs coeffs[5], hum, throat;
    int counter = 0;
};

YetiVoice::YetiVoice() : BuiltinProcessor ("yeti", "Yodel Yeti", true, yetiLayout())
{
    for (int i = 0; i < maxVoices; ++i) voices.push_back (std::make_unique<Voice>());
}

YetiVoice::~YetiVoice() = default;

void YetiVoice::prepareToPlay (double sr, int block)
{
    sampleRate = sr;
    maxBlock = juce::jmax (16, block);
    for (auto& v : voices) *v = Voice();
    heldNotes.clear();
    for (auto& d : delayBuf) d.assign ((size_t) (1.6 * sr) + 8, 0.0f);
    delayPos = 0;
    reverb.setSampleRate (sr);
    reverb.reset();
    revBuf.setSize (2, maxBlock);
}

void YetiVoice::startNote (int note, float vel)
{
    const bool mono = param ("mono") > 0.5f;
    const int mode = (int) param ("vowelMode");
    face.notes = face.notes.load() + 1;

    float vowelTarget = param ("vowel");
    if (mode == 1) { static const float seq[] = { 0.5f, 0.75f, 1.0f, 0.25f, 0.0f, 0.25f }; vowelTarget = seq[vowelStep++ % 6]; }
    else if (mode == 2) vowelTarget = random.nextFloat();
    else if (mode == 4) vowelTarget = 0.25f;   // "O" then closing to M

    const float target = std::log2 (440.0f * std::pow (2.0f, (float) (note - 69) / 12.0f));
    if (mono)
    {
        auto& v = *voices[0];
        heldNotes.erase (std::remove (heldNotes.begin(), heldNotes.end(), note), heldNotes.end());
        heldNotes.push_back (note);
        const bool legato = v.active && v.keyDown;
        if (! v.active || v.env < 0.001f) { v = Voice(); v.logFreq = target; v.vowel = vowelTarget; }
        v.active = true;
        v.keyDown = true;
        v.note = note;
        v.velocity = vel;
        v.logTarget = target;
        v.vowelTarget = vowelTarget;
        if (! legato) v.held = 0.0f;
        return;
    }

    Voice* free = nullptr;
    for (auto& v : voices) if (! v->active) { free = v.get(); break; }
    if (free == nullptr)
    {
        float lowest = 1.0e9f;
        for (auto& v : voices) if (v->env < lowest) { lowest = v->env; free = v.get(); }
    }
    *free = Voice();
    free->active = free->keyDown = true;
    free->note = note;
    free->velocity = vel;
    free->logFreq = free->logTarget = target;
    free->vowel = free->vowelTarget = vowelTarget;
}

void YetiVoice::stopNote (int note)
{
    if (param ("mono") > 0.5f)
    {
        heldNotes.erase (std::remove (heldNotes.begin(), heldNotes.end(), note), heldNotes.end());
        auto& v = *voices[0];
        if (! heldNotes.empty())
        {
            // back to the previous key still held (glides there)
            v.note = heldNotes.back();
            v.logTarget = std::log2 (440.0f * std::pow (2.0f, (float) (v.note - 69) / 12.0f));
        }
        else if (v.note == note || note < 0)
        {
            v.keyDown = false;
        }
        return;
    }
    for (auto& v : voices)
        if (v->active && v->keyDown && v->note == note) v->keyDown = false;
}

void YetiVoice::render (float* L, float* R, int n)
{
    const int voiceType = (int) param ("voice");
    const auto& vt = table (voiceType == 0 ? 1 : voiceType);
    const float size = param ("size") * (voiceType == 0 ? 0.86f : 1.0f);
    const float tension = param ("tension"), breath = param ("breath"), throatAmt = param ("throat"), growl = param ("growl");
    const float mmm = param ("mmm");
    const int mode = (int) param ("vowelMode");
    const int singers = (int) param ("choir") + 1;
    const float spread = param ("choirDetune");
    const float glideMs = param ("glide");
    const float glideCoef = glideMs < 1.0f ? 1.0f : (float) (1.0 - std::exp (-1.0 / (glideMs * 0.001 * sampleRate / 3.0)));
    const float vibRate = param ("vibRate"), vibDepth = param ("vibDepth") / 1200.0f, vibDelay = param ("vibDelay");
    const float attackCoef = (float) (1.0 - std::exp (-1.0 / (param ("attack") * 0.001 * sampleRate / 3.0)));
    const float releaseCoef = (float) (1.0 - std::exp (-1.0 / (param ("release") * 0.001 * sampleRate / 3.0)));
    const float mouthOpen = (float) (1.0 - std::exp (-1.0 / ((0.02 + 0.25 * mmm) * sampleRate / 3.0)));
    const float vowelCoef = (float) (1.0 - std::exp (-1.0 / (0.06 * sampleRate)));
    const float manualVowel = juce::jlimit (0.0f, 1.0f, param ("vowel") + modWheel * (1.0f - param ("vowel")));
    const double lfoInc = param ("vowelRate") / sampleRate;
    const float lfoDepth = param ("vowelDepth");
    const float bendLog = bend * 2.0f / 12.0f;
    const float overtone = param ("overtone");

    float level = 0.0f, faceVowel = face.vowel.load(), faceMouth = 0.0f, facePitch = face.pitch.load(), faceVib = 0.0f, faceHeld = 0.0f;
    int sounding = 0;

    for (auto& vp : voices)
    {
        auto& v = *vp;
        if (! v.active) continue;
        ++sounding;
        for (int s = 0; s < singers; ++s)
        {
            if (v.singerCents[s] == 0.0f && s > 0)
            {
                v.singerCents[s] = spread * (s % 2 == 0 ? 1.0f : -1.0f) * (0.5f + 0.5f * (float) ((s + 1) / 2) / 2.0f) * (0.7f + 0.6f * random.nextFloat());
                v.singerPan[s] = (s % 2 == 0 ? 1.0f : -1.0f) * juce::jmin (1.0f, 0.35f * (float) ((s + 1) / 2));
            }
        }

        for (int i = 0; i < n; ++i)
        {
            const bool on = v.keyDown;
            v.env += (on ? attackCoef : releaseCoef) * ((on ? 1.0f : 0.0f) - v.env);
            // the mouth: closed ("mmm") at the start and end of a note, open while singing
            const float mouthTarget = on ? 1.0f : (mmm > 0.01f ? 0.0f : 1.0f);
            v.mouth += (mmm > 0.01f ? mouthOpen : 1.0f) * (mouthTarget - v.mouth);
            v.logFreq += glideCoef * (v.logTarget - v.logFreq);
            v.held += on ? (float) (1.0 / sampleRate) : 0.0f;

            // vowel
            float vowelGoal = v.vowelTarget;
            if (mode == 0) vowelGoal = padDown.load() ? padX.load() : manualVowel;
            else if (mode == 3) vowelGoal = juce::jlimit (0.0f, 1.0f, manualVowel + lfoDepth * 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * lfoPhase));
            else if (mode == 4) vowelGoal = v.held < 0.35f ? 0.25f : juce::jmax (0.0f, 0.25f - (v.held - 0.35f) * 0.2f);   // O... sliding toward U, then M on release
            v.vowel += vowelCoef * (vowelGoal - v.vowel);

            // vibrato swells in after the vibrato delay, with a little natural jitter
            const float swell = vibDelay < 0.01f ? 1.0f : juce::jlimit (0.0f, 1.0f, (v.held - vibDelay * 0.5f) / juce::jmax (0.01f, vibDelay));
            const float vib = (float) std::sin (juce::MathConstants<double>::twoPi * vibRate * v.held) * swell;
            if ((v.counter & 255) == 0) v.jitterTarget = (random.nextFloat() - 0.5f) * 0.004f;
            v.jitter += 0.001f * (v.jitterTarget - v.jitter);

            // formant coefficients every 16 samples
            if ((v.counter++ & 15) == 0)
            {
                const float pos = v.vowel * 4.0f;
                const int a = juce::jlimit (0, 3, (int) pos);
                const float t = pos - (float) a;
                for (int f = 0; f < 5; ++f)
                {
                    const auto& fa = vt[(size_t) a][(size_t) f];
                    const auto& fb = vt[(size_t) a + 1][(size_t) f];
                    const float freq = (fa.f + t * (fb.f - fa.f)) * size;
                    const float db = fa.db + t * (fb.db - fa.db);
                    const float bw = (fa.bw + t * (fb.bw - fa.bw)) * (1.0f + 0.3f * (1.0f - tension));
                    v.coeffs[f].set (freq, bw, juce::Decibels::decibelsToGain (db), sampleRate);
                }
                v.hum.set (260.0f * size, 90.0f, 1.0f, sampleRate);
                v.throat.set (overtone, overtone / 45.0f, 1.0f, sampleRate);
            }

            const float logF = v.logFreq + bendLog + vibDepth * vib + v.jitter;
            const double f0 = std::pow (2.0, (double) logF);
            const float tiltA = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * f0 * (1.2 + tension * 7.0) / sampleRate));

            float srcL = 0.0f, srcR = 0.0f;
            for (int s = 0; s < singers; ++s)
            {
                const double f = f0 * std::pow (2.0, v.singerCents[s] / 1200.0);
                const double dt = f / sampleRate;
                double& ph = v.phase[s];
                float saw = (float) (2.0 * ph - 1.0) - polyBlep (ph, dt);
                ph += dt;
                if (ph >= 1.0) ph -= 1.0;
                if (growl > 0.001f)
                {
                    // period doubling: every other glottal pulse is weaker (Tuvan kargyraa)
                    double& sp = v.subPhase[s];
                    const double sdt = dt * 0.5;
                    const float sub = (float) (2.0 * sp - 1.0) - polyBlep (sp, sdt);
                    sp += sdt;
                    if (sp >= 1.0) sp -= 1.0;
                    saw = saw * (1.0f - 0.5f * growl) + sub * growl;
                }
                v.tilt[s] += tiltA * (-saw - v.tilt[s]);   // glottal pulse: a saw rolled off at -12 dB/oct above the fundamental
                const float pan = v.singerPan[s];
                srcL += v.tilt[s] * (1.0f - 0.5f * pan);
                srcR += v.tilt[s] * (1.0f + 0.5f * pan);
            }
            const float norm = 1.0f / std::sqrt ((float) singers);
            const float noise = (random.nextFloat() * 2.0f - 1.0f) * breath * (0.4f + 0.6f * v.env);
            srcL = srcL * norm * 3.0f + noise;
            srcR = srcR * norm * 3.0f + noise;

            float outL = 0.0f, outR = 0.0f;
            for (int f = 0; f < 5; ++f)
            {
                const auto& c = v.coeffs[f];
                outL += v.formL[f].bp (srcL, c.a1, c.a2, c.a3) * c.k * c.gain;
                outR += v.formR[f].bp (srcR, c.a1, c.a2, c.a3) * c.k * c.gain;
            }
            if (throatAmt > 0.001f)
            {
                outL += v.throatL.bp (outL + srcL * 0.3f, v.throat.a1, v.throat.a2, v.throat.a3) * v.throat.k * throatAmt * 14.0f;
                outR += v.throatR.bp (outR + srcR * 0.3f, v.throat.a1, v.throat.a2, v.throat.a3) * v.throat.k * throatAmt * 14.0f;
            }
            const float humL = v.humL.bp (srcL, v.hum.a1, v.hum.a2, v.hum.a3) * v.hum.k * 0.8f;
            const float humR = v.humR.bp (srcR, v.hum.a1, v.hum.a2, v.hum.a3) * v.hum.k * 0.8f;
            const float m = v.mouth;
            const float g = v.env * (0.35f + 0.65f * v.velocity) * 0.5f;
            L[i] += (outL * m + humL * (1.0f - m)) * g;
            R[i] += (outR * m + humR * (1.0f - m)) * g;

            level = juce::jmax (level, v.env * (0.4f + 0.6f * v.velocity));
            faceVowel = v.vowel;
            faceMouth = juce::jmax (faceMouth, m * v.env);
            facePitch = juce::jlimit (0.0f, 1.0f, (logF - std::log2 (55.0f)) / 4.0f);
            faceVib = vib;
            faceHeld = on ? v.held : 0.0f;
        }
        if (! v.keyDown && v.env < 1.0e-4f) v.active = false;
    }
    lfoPhase += lfoInc * n;
    if (lfoPhase > 1000.0) lfoPhase -= 1000.0;

    face.level = level;
    face.vowel = faceVowel;
    face.mouth = faceMouth;
    face.pitch = facePitch;
    face.vibrato = faceVib;
    face.heldSeconds = faceHeld;
    face.sounding = sounding;
}

void YetiVoice::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals nd;
    const int total = buffer.getNumSamples();
    buffer.clear();
    if (buffer.getNumChannels() < 2 || total == 0) return;

    // singing with the mouse on the pad
    const bool pad = padDown.load();
    if (pad != padWasDown)
    {
        padWasDown = pad;
        if (pad) { if (heldNotes.empty()) startNote (-1, 0.8f); }
        else stopNote (-1);
    }
    if (pad && voices[0]->note == -1)
        voices[0]->logTarget = std::log2 (55.0f) + padY.load() * 2.5f;   // A1 up two and a half octaves

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);
    int pos = 0;
    auto renderTo = [&] (int end)
    {
        for (int at = pos; at < end; at += maxBlock) render (L + at, R + at, juce::jmin (maxBlock, end - at));
        pos = end;
    };
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        renderTo (juce::jlimit (pos, total, meta.samplePosition));
        if (m.isNoteOn()) startNote (m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isNoteOff()) stopNote (m.getNoteNumber());
        else if (m.isController() && m.getControllerNumber() == 1) modWheel = (float) m.getControllerValue() / 127.0f;
        else if (m.isPitchWheel()) bend = (float) (m.getPitchWheelValue() - 8192) / 8192.0f;
        else if (m.isAllNotesOff() || m.isAllSoundOff()) { heldNotes.clear(); for (auto& v : voices) v->keyDown = false; }
    }
    renderTo (total);

    // ---- delay (synced to nothing: the yeti keeps his own time) ----
    const float mix = param ("delayMix");
    if (mix > 0.001f && ! delayBuf[0].empty())
    {
        const int len = (int) delayBuf[0].size();
        const int d = juce::jlimit (1, len - 1, (int) (param ("delayTime") * 0.001 * sampleRate));
        const float fb = param ("delayFeedback") * 0.95f;
        const bool pp = param ("pingpong") > 0.5f;
        const float lpA = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * param ("delayTone") / sampleRate));
        float echoPeak = 0.0f;
        for (int i = 0; i < total; ++i)
        {
            const int rp = (delayPos - d + len) % len;
            float yl = delayBuf[0][(size_t) rp], yr = delayBuf[1][(size_t) rp];
            delayLp[0] += lpA * (yl - delayLp[0]);
            delayLp[1] += lpA * (yr - delayLp[1]);
            yl = delayLp[0]; yr = delayLp[1];
            const float inL = L[i], inR = R[i];
            if (pp)
            {
                delayBuf[0][(size_t) delayPos] = 0.5f * (inL + inR) + fb * yr;
                delayBuf[1][(size_t) delayPos] = fb * yl;
            }
            else
            {
                delayBuf[0][(size_t) delayPos] = inL + fb * yl;
                delayBuf[1][(size_t) delayPos] = inR + fb * yr;
            }
            delayPos = (delayPos + 1) % len;
            L[i] = inL + mix * yl;
            R[i] = inR + mix * yr;
            echoPeak = juce::jmax (echoPeak, std::abs (yl), std::abs (yr));
        }
        echoEnv = juce::jmax (echoPeak * mix, echoEnv * 0.9f);
    }
    else echoEnv *= 0.9f;
    face.echo = juce::jmin (1.0f, echoEnv * 4.0f);

    // ---- mountain reverb ----
    const float rv = param ("reverb");
    if (rv > 0.001f && total <= revBuf.getNumSamples())
    {
        juce::Reverb::Parameters p;
        p.roomSize = 0.88f; p.damping = 0.45f; p.width = 1.0f; p.wetLevel = 1.0f; p.dryLevel = 0.0f;
        reverb.setParameters (p);
        revBuf.copyFrom (0, 0, buffer, 0, 0, total);
        revBuf.copyFrom (1, 0, buffer, 1, 0, total);
        reverb.processStereo (revBuf.getWritePointer (0), revBuf.getWritePointer (1), total);
        buffer.addFrom (0, 0, revBuf, 0, 0, total, rv * 0.5f);
        buffer.addFrom (1, 0, revBuf, 1, 0, total, rv * 0.5f);
    }

    const float vol = juce::Decibels::decibelsToGain (param ("volume"));
    for (int c = 0; c < 2; ++c)
    {
        float* d = buffer.getWritePointer (c);
        for (int i = 0; i < total; ++i)
        {
            const float x = d[i] * vol;
            d[i] = std::isfinite (x) ? juce::jlimit (-4.0f, 4.0f, x) : 0.0f;
        }
    }
    for (auto& v : voices)
        for (auto& f : v->formL) if (! std::isfinite (f.ic1) || ! std::isfinite (f.ic2)) { *v = Voice(); break; }
}

} // namespace wis::daw
