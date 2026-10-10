#include "Whistler.h"
#include <cmath>

namespace wis::daw
{

using namespace prm;

static Layout whistleLayout()
{
    Layout l;
    addDb      (l, "volume", "Volume", -24.0f, 12.0f, -3.0f);
    addChoice  (l, "octave", "Octave", { "-1", "0", "+1", "+2" }, 2);
    addMs      (l, "glide", "Glide Between Notes", 0.0f, 1000.0f, 140.0f);
    addMs      (l, "glideGap", "Glide Over Gaps", 0.0f, 2000.0f, 600.0f);
    addFloat   (l, "scoop", "Scoop Up", 0.0f, 7.0f, 1.5f, "st", 0.0f, 1);
    addMs      (l, "scoopTime", "Scoop Time", 10.0f, 500.0f, 90.0f);
    addFloat   (l, "fall", "Fall-Off", 0.0f, 7.0f, 1.0f, "st", 0.0f, 1);
    addFloat   (l, "vibrato", "Vibrato", 0.0f, 150.0f, 18.0f, "ct", 0.0f, 0);
    addFloat   (l, "vibRate", "Vibrato Speed", 2.0f, 16.0f, 5.5f, "Hz", 0.0f, 1);
    addMs      (l, "vibDelay", "Vibrato Delay", 0.0f, 1500.0f, 300.0f);
    addFloat   (l, "wobble", "Human Wobble", 0.0f, 60.0f, 8.0f, "ct", 0.0f, 0);
    addPercent (l, "breath", "Breath", 0.15f);
    addPercent (l, "air", "Air", 0.08f);
    addPercent (l, "tone", "Shrill", 0.1f);
    addMs      (l, "attack", "Attack", 5.0f, 400.0f, 35.0f);
    addMs      (l, "release", "Release", 10.0f, 1000.0f, 90.0f);
    addPercent (l, "velsens", "Touch", 0.5f);
    addPercent (l, "echo", "Echo", 0.0f);
    addMs      (l, "echoTime", "Echo Time", 60.0f, 900.0f, 320.0f);
    addPercent (l, "reverb", "Reverb", 0.2f);
    addPercent (l, "roomSize", "Reverb Size", 0.6f);
    return l;
}

namespace
{
    struct WhistlePreset { const char* name; std::vector<std::pair<const char*, float>> values; };

    const std::vector<WhistlePreset>& whistlePresets()
    {
        static const std::vector<WhistlePreset> p {
            { "Lonesome Whistle", { { "glide", 180.0f }, { "scoop", 2.0f }, { "scoopTime", 120.0f }, { "vibrato", 22.0f }, { "vibRate", 5.2f }, { "vibDelay", 350.0f },
                                    { "breath", 0.15f }, { "echo", 0.25f }, { "echoTime", 420.0f }, { "reverb", 0.35f }, { "roomSize", 0.8f } } },
            { "Happy-Go-Lucky", { { "glide", 60.0f }, { "scoop", 1.5f }, { "scoopTime", 60.0f }, { "vibrato", 12.0f }, { "vibRate", 6.2f }, { "vibDelay", 200.0f },
                                  { "tone", 0.2f }, { "reverb", 0.15f }, { "roomSize", 0.4f }, { "fall", 0.6f } } },
            { "Close & Breathy", { { "breath", 0.45f }, { "air", 0.3f }, { "vibrato", 10.0f }, { "reverb", 0.1f }, { "roomSize", 0.3f }, { "tone", 0.05f } } },
            { "Two-Finger Whistle", { { "octave", 3.0f }, { "tone", 0.45f }, { "scoop", 4.0f }, { "scoopTime", 70.0f }, { "fall", 3.0f }, { "vibrato", 0.0f },
                                      { "glide", 90.0f }, { "breath", 0.1f }, { "air", 0.15f }, { "reverb", 0.2f }, { "volume", -4.0f } } },
            { "Old Man on the Porch", { { "wobble", 25.0f }, { "vibrato", 35.0f }, { "vibRate", 4.5f }, { "vibDelay", 150.0f }, { "breath", 0.3f }, { "air", 0.15f },
                                        { "tone", 0.0f }, { "glide", 220.0f }, { "scoop", 2.5f }, { "fall", 2.0f }, { "reverb", 0.15f } } },
            { "Ghost Whistle", { { "glide", 280.0f }, { "scoop", 3.0f }, { "scoopTime", 250.0f }, { "vibrato", 30.0f }, { "vibRate", 3.8f }, { "breath", 0.25f },
                                 { "attack", 180.0f }, { "release", 600.0f }, { "echo", 0.3f }, { "echoTime", 520.0f }, { "reverb", 0.6f }, { "roomSize", 0.95f } } },
            { "Bird Trills", { { "octave", 3.0f }, { "vibrato", 90.0f }, { "vibRate", 13.0f }, { "vibDelay", 0.0f }, { "glide", 25.0f }, { "scoop", 3.0f },
                               { "scoopTime", 30.0f }, { "tone", 0.2f }, { "reverb", 0.25f }, { "volume", -5.0f } } },
            { "Slide Whistle", { { "glide", 450.0f }, { "glideGap", 1500.0f }, { "scoop", 0.0f }, { "vibrato", 0.0f }, { "wobble", 0.0f }, { "breath", 0.3f },
                                 { "tone", 0.15f }, { "fall", 0.0f }, { "reverb", 0.15f } } },
            { "Dry (for mixing)", { { "reverb", 0.0f }, { "echo", 0.0f } } },
        };
        return p;
    }

    inline float mtof (float n) noexcept { return 440.0f * std::pow (2.0f, (n - 69.0f) / 12.0f); }
    inline float smooth (float t) noexcept { t = juce::jlimit (0.0f, 1.0f, t); return t * t * (3.0f - 2.0f * t); }
}

juce::StringArray Whistler::presetNames()
{
    juce::StringArray n;
    for (auto& p : whistlePresets()) n.add (p.name);
    return n;
}

void Whistler::loadProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) whistlePresets().size())) return;
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            rp->setValueNotifyingHost (rp->getDefaultValue());
    for (auto& [id, v] : whistlePresets()[(size_t) index].values)
        setParam (id, v);
}

Whistler::Whistler() : BuiltinProcessor ("whistle", "Whistler", true, whistleLayout())
{
    loadProgram (0);
}

void Whistler::prepareToPlay (double sr, int)
{
    sampleRate = sr;
    held.clear();
    targetNote = -1;
    sounding = false;
    env = 0.0f;
    releasedFor = 1 << 30;
    for (auto& e : echo) e.assign ((size_t) (sr * 1.0) + 8, 0.0f);
    echoPos = 0;
    reverb.setSampleRate (sr);
    reverb.reset();
    breathBp = {};
    airHp = toneLp = 0.0f;
    currentHz = 0.0f;
}

void Whistler::noteOn (int note, float vel)
{
    held.erase (std::remove (held.begin(), held.end(), note), held.end());
    held.push_back (note);
    const float octave = (float) (((int) param ("octave") - 1) * 12);
    const float target = (float) note + octave;
    const float sens = param ("velsens");
    velocity = (1.0f - sens) + sens * vel;

    const float sr = (float) sampleRate;
    // still whistling, or only just stopped: glide on from the last note
    const bool recently = sounding || (targetNote >= 0 && releasedFor < (int) (param ("glideGap") * 0.001f * sr));
    if (recently && param ("glide") > 0.5f)
    {
        // slide from where the whistle is (or just was) to the new note
        glideFrom = pitch + (sounding ? 0.0f : fall);
        glideTo = target;
        glideAge = 0;
        glideLen = juce::jmax (1, (int) (param ("glide") * 0.001f * sr));
        if (! sounding) { scoopFrom = 0.0f; puff = juce::jmax (puff, 0.4f); }
    }
    else
    {
        // a new phrase: scoop up into it
        glideFrom = glideTo = target;
        pitch = target;
        glideAge = glideLen = 1;
        scoopFrom = param ("scoop");
        scoopAge = 0;
        scoopLen = juce::jmax (1, (int) (param ("scoopTime") * 0.001f * sr));
        if (env < 0.05f) { noteAge = 0; vibPhase = 0.0f; }
        puff = 1.0f;
    }
    targetNote = note;
    sounding = true;
    releasedFor = 0;
}

void Whistler::noteOff (int note)
{
    held.erase (std::remove (held.begin(), held.end(), note), held.end());
    if (note != targetNote) return;
    if (! held.empty())
    {
        // back to the key that is still down (trills)
        const float octave = (float) (((int) param ("octave") - 1) * 12);
        glideFrom = pitch;
        glideTo = (float) held.back() + octave;
        glideAge = 0;
        glideLen = juce::jmax (1, (int) (param ("glide") * 0.001f * (float) sampleRate));
        targetNote = held.back();
        return;
    }
    sounding = false;
    releasedFor = 0;
}

void Whistler::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals nd;
    const int total = buffer.getNumSamples();
    buffer.clear();
    if (buffer.getNumChannels() < 2) return;
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);

    const float sr = (float) sampleRate;
    const float attackK = 1.0f - std::exp (-1.0f / (param ("attack") * 0.001f * sr));
    const float releaseK = 1.0f - std::exp (-1.0f / (param ("release") * 0.001f * sr));
    const float fallSt = param ("fall"), vibrato = param ("vibrato"), vibRate = param ("vibRate");
    const int vibDelay = (int) (param ("vibDelay") * 0.001f * sr);
    const float wobble = param ("wobble"), breath = param ("breath"), air = param ("air"), shrill = param ("tone");
    const float relSamples = param ("release") * 0.001f * sr;
    const float puffK = std::exp (-1.0f / (0.03f * sr));

    auto renderTo = [&] (int from, int to)
    {
        for (int i = from; i < to; ++i)
        {
            // ---- pitch ----
            if (glideAge < glideLen)
            {
                ++glideAge;
                pitch = glideFrom + (glideTo - glideFrom) * smooth ((float) glideAge / (float) glideLen);
            }
            else pitch = glideTo;
            float scoop = 0.0f;
            if (scoopFrom > 0.0f && scoopAge < scoopLen)
            {
                ++scoopAge;
                const float t = (float) scoopAge / (float) scoopLen;
                scoop = -scoopFrom * (1.0f - t) * (1.0f - t);
            }
            if (! sounding)
            {
                if (releasedFor < (1 << 30)) ++releasedFor;
                fall = -fallSt * smooth ((float) releasedFor / juce::jmax (1.0f, relSamples * 1.5f));
            }
            else fall = 0.0f;

            // wobble: a slow random walk, as a real mouth never holds still
            if ((i & 15) == 0)
            {
                driftVel += 0.02f * ((rnd.nextFloat() * 2.0f - 1.0f) - driftVel);
                drift = juce::jlimit (-1.0f, 1.0f, drift + 0.05f * driftVel);
            }
            vibPhase += vibRate / sr;
            if (vibPhase >= 1.0f) vibPhase -= 1.0f;
            ++noteAge;
            const float vibAmt = (vibrato + modWheel * 80.0f) * smooth ((float) (noteAge - vibDelay) / (0.4f * sr));
            const float cents = vibAmt * std::sin (juce::MathConstants<float>::twoPi * vibPhase) + wobble * drift;
            const float note = pitch + scoop + fall + bend + cents / 100.0f;
            const float hz = juce::jmin (mtof (note), sr * 0.2f);

            // ---- amplitude ----
            env += ((sounding ? velocity : 0.0f) - env) * (sounding ? attackK : releaseK);
            phase += hz / sr;
            if (phase >= 1.0) phase -= 1.0;
            const float ph = (float) phase * juce::MathConstants<float>::twoPi;
            float tone = std::sin (ph) + shrill * (0.22f * std::sin (2.0f * ph) + 0.12f * std::sin (3.0f * ph));
            // the higher you whistle, the louder it gets (a little)
            tone *= 0.8f + 0.2f * juce::jlimit (0.0f, 1.0f, (hz - 600.0f) / 2000.0f);

            // ---- breath: noise tuned to the note, and air ----
            const float white = rnd.nextFloat() * 2.0f - 1.0f;
            breathBp.g = std::tan (juce::MathConstants<float>::pi * juce::jmin (hz, sr * 0.45f) / sr);
            breathBp.k = 0.12f;
            const float v3 = white - breathBp.ic2;
            const float v1 = (breathBp.g * v3 + breathBp.ic1) / (1.0f + breathBp.g * (breathBp.g + breathBp.k));
            const float v2 = breathBp.ic2 + breathBp.g * v1;
            breathBp.ic1 = 2.0f * v1 - breathBp.ic1;
            breathBp.ic2 = 2.0f * v2 - breathBp.ic2;
            const float tuned = v1 * breathBp.k * 3.0f;
            airHp += 0.25f * (white - airHp);
            const float airy = white - airHp;
            puff *= puffK;

            float y = env * (tone * (1.0f - 0.5f * breath) + breath * tuned * 0.6f) + (env * air + puff * 0.25f) * airy * 0.35f;
            toneLp += 0.6f * (y - toneLp);
            y = toneLp * 0.28f;
            L[i] = y;
            R[i] = y;
            if ((i & 63) == 0) currentHz = env > 0.05f ? hz : 0.0f;
        }
    };

    int pos = 0;
    for (const auto m : midi)
    {
        const auto msg = m.getMessage();
        const int at = juce::jlimit (0, total, m.samplePosition);
        renderTo (pos, at);
        pos = at;
        if (msg.isNoteOn()) noteOn (msg.getNoteNumber(), msg.getFloatVelocity());
        else if (msg.isNoteOff()) noteOff (msg.getNoteNumber());
        else if (msg.isPitchWheel()) bend = (float) (msg.getPitchWheelValue() - 8192) / 8192.0f * 2.0f;
        else if (msg.isController() && msg.getControllerNumber() == 1) modWheel = (float) msg.getControllerValue() / 127.0f;
        else if (msg.isAllNotesOff() || msg.isAllSoundOff()) { held.clear(); sounding = false; }
    }
    renderTo (pos, total);

    // ---- slapback echo ----
    const float echoAmt = param ("echo");
    if (echoAmt > 0.001f)
    {
        const int size = (int) echo[0].size();
        const int dL = juce::jlimit (1, size - 1, (int) (param ("echoTime") * 0.001f * sr));
        const int dR = juce::jlimit (1, size - 1, (int) (dL * 1.25f));
        for (int i = 0; i < total; ++i)
        {
            const float inL = L[i], inR = R[i];
            const float eL = echo[0][(size_t) ((echoPos - dL + size) % size)];
            const float eR = echo[1][(size_t) ((echoPos - dR + size) % size)];
            echo[0][(size_t) echoPos] = inL + eR * 0.38f;
            echo[1][(size_t) echoPos] = inR * 0.3f + eL * 0.38f;
            echoPos = (echoPos + 1) % size;
            L[i] = inL + eL * echoAmt;
            R[i] = inR + eR * echoAmt;
        }
    }

    // ---- reverb ----
    const float rv = param ("reverb");
    if (rv > 0.001f)
    {
        juce::Reverb::Parameters p;
        p.roomSize = 0.3f + 0.68f * param ("roomSize");
        p.damping = 0.45f;
        p.wetLevel = rv * 0.5f;
        p.dryLevel = 1.0f - rv * 0.35f;
        p.width = 1.0f;
        reverb.setParameters (p);
        reverb.processStereo (L, R, total);
    }

    const float vol = juce::Decibels::decibelsToGain (param ("volume"));
    for (int ch = 0; ch < 2; ++ch)
    {
        float* x = buffer.getWritePointer (ch);
        for (int i = 0; i < total; ++i) { const float y = x[i] * vol; x[i] = std::abs (y) < 0.9f ? y : std::tanh (y); }
    }
}

} // namespace wis::daw
