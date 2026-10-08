#include "BeatLab.h"

namespace wis::daw
{

std::function<juce::AudioProcessorEditor* (BeatLab&)> BeatLab::editorFactory;
std::function<void (BeatLab&, const juce::MidiMessageSequence&, double)> BeatLab::onPatternToSong;

namespace
{
    constexpr double twoPi = juce::MathConstants<double>::twoPi;

    inline juce::uint32 mix32 (juce::uint32 x)
    {
        x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
        return x;
    }
    /** Deterministic 0..1 hash (so a step decides the same way every time it's looked at). */
    inline float hash01 (juce::int64 a, int b, int c)
    {
        return (float) (mix32 ((juce::uint32) a * 2654435761U ^ mix32 ((juce::uint32) (b * 97 + c * 7919 + 13))) & 0xffffff) / 16777216.0f;
    }

    struct Svf
    {
        float g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0, ic1 = 0, ic2 = 0;
        void set (double sr, float hz, float q)
        {
            g = (float) std::tan (juce::MathConstants<double>::pi * juce::jlimit (20.0, sr * 0.45, (double) hz) / sr);
            k = 1.0f / juce::jmax (0.05f, q);
            a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
        }
        inline void tick (float x, float& lp, float& hp)
        {
            const float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2;
            lp = v2; hp = x - k * v1 - v2;
        }
        void reset() { ic1 = ic2 = 0; }
    };

    /** "DJ filter": below 0 sweeps a low-pass down, above 0 a high-pass up. */
    struct DjFilter
    {
        Svf f[2];
        float lastSetting = 99.0f, lastReso = -1.0f;
        void update (double sr, float setting, float reso)
        {
            if (std::abs (setting - lastSetting) < 0.002f && std::abs (reso - lastReso) < 0.002f) return;
            lastSetting = setting; lastReso = reso;
            const float q = 0.7f + reso * reso * 9.0f;
            const float hz = setting < 0.0f ? 20000.0f * std::pow (60.0f / 20000.0f, -setting) : 20.0f * std::pow (8000.0f / 20.0f, setting);
            for (auto& s : f) s.set (sr, hz, q);
        }
        inline void process (float& l, float& r, float setting)
        {
            if (std::abs (setting) < 0.02f) return;
            float lp, hp;
            f[0].tick (l, lp, hp); l = setting < 0 ? lp : hp;
            f[1].tick (r, lp, hp); r = setting < 0 ? lp : hp;
        }
    };

    // ---- glitch sounds (the IDM kit) ----
    juce::AudioBuffer<float> renderGlitch (int voice, double sr)
    {
        juce::Random rng (777 + voice);
        auto make = [&] (double seconds) { juce::AudioBuffer<float> b (1, juce::jmax (16, (int) (seconds * sr))); b.clear(); return b; };
        auto env = [] (double t, double tau) { return std::exp (-t / tau); };
        juce::AudioBuffer<float> b;
        auto* d = (float*) nullptr;
        auto sweep = [&] (double secs, double f0, double f1, double tauPitch, double tauAmp, double drive)
        {
            b = make (secs); d = b.getWritePointer (0);
            double ph = 0;
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const double t = i / sr;
                const double f = f1 + (f0 - f1) * env (t, tauPitch);
                ph += f / sr;
                d[i] = (float) (std::tanh (std::sin (twoPi * ph) * drive) / std::tanh (drive) * env (t, tauAmp));
            }
        };
        switch (voice)
        {
            case 0: sweep (1.4, 170, 46, 0.03, 0.5, 2.5); break;                                     // Sub Boom
            case 1:                                                                                    // Bit Snare
                b = make (0.25); d = b.getWritePointer (0);
                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    const double t = i / sr;
                    const float nz = std::round ((rng.nextFloat() * 2 - 1) * 4.0f) / 4.0f;
                    d[i] = (float) ((nz * 0.8 + 0.5 * std::sin (twoPi * 210 * t)) * env (t, 0.07));
                }
                break;
            case 2:                                                                                    // Click
                b = make (0.01); d = b.getWritePointer (0);
                for (int i = 0; i < b.getNumSamples(); ++i) d[i] = (float) ((i % 3 == 0 ? 1.0 : -0.6) * env (i / sr, 0.0008));
                break;
            case 3:                                                                                    // Shred: stuttered noise bursts
                b = make (0.18); d = b.getWritePointer (0);
                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    const double t = i / sr;
                    const bool gate = std::fmod (t, 0.022) < 0.008;
                    d[i] = gate ? (float) ((rng.nextFloat() * 2 - 1) * env (t, 0.08)) : 0.0f;
                }
                break;
            case 4: case 5:                                                                            // Tick / Digital Hat
            {
                b = make (voice == 4 ? 0.02 : 0.05); d = b.getWritePointer (0);
                float prev = 0;
                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    const double t = i / sr;
                    float x = voice == 4 ? rng.nextFloat() * 2 - 1
                                         : (float) ((std::sin (twoPi * 7100 * t) > 0 ? 1 : -1) * (std::sin (twoPi * 5300 * t) > 0 ? 1 : -1));
                    const float hp = x - prev; prev = x;
                    d[i] = (float) (hp * 0.6 * env (t, voice == 4 ? 0.004 : 0.012));
                }
                break;
            }
            case 6:                                                                                    // Fizz: combed noise
            {
                b = make (0.35); d = b.getWritePointer (0);
                const int delay = (int) (sr / 3100.0);
                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    const float nz = rng.nextFloat() * 2 - 1;
                    d[i] = (float) ((nz + (i >= delay ? d[i - delay] * 0.7f : 0.0f)) * 0.5 * env (i / sr, 0.09));
                }
                break;
            }
            case 7: case 12:                                                                           // Glass (FM bell) / FM Blip
            {
                const bool bell = voice == 7;
                b = make (bell ? 1.6 : 0.12); d = b.getWritePointer (0);
                const double fc = bell ? 1240 : 610, ratio = bell ? 2.76 : 3.5;
                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    const double t = i / sr;
                    const double idx = (bell ? 3.0 : 5.0) * env (t, bell ? 0.3 : 0.03);
                    d[i] = (float) (std::sin (twoPi * fc * t + idx * std::sin (twoPi * fc * ratio * t)) * env (t, bell ? 0.45 : 0.035) * 0.7);
                }
                break;
            }
            case 8: sweep (0.5, 2000, 2000, 1, 0.12, 1.0); break;                                    // Sine Ping
            case 9: sweep (0.2, 800, 60, 0.02, 0.07, 1.5); break;                                    // Zap Low
            case 10: sweep (0.12, 2000, 200, 0.012, 0.04, 1.5); break;                               // Zap
            case 11: sweep (0.06, 5000, 800, 0.006, 0.02, 1.2); break;                               // Laser
            case 13:                                                                                   // Bleep: square
                b = make (0.04); d = b.getWritePointer (0);
                for (int i = 0; i < b.getNumSamples(); ++i) d[i] = (float) ((std::fmod (i * 1200.0 / sr, 1.0) < 0.5 ? 0.5 : -0.5) * env (i / sr, 0.012));
                break;
            case 14:                                                                                   // Crackle
                b = make (0.25); d = b.getWritePointer (0);
                for (int i = 0; i < b.getNumSamples(); ++i) d[i] = rng.nextFloat() < 0.004f ? (rng.nextFloat() * 2 - 1) * 0.9f : 0.0f;
                break;
            case 15:                                                                                   // Metal Tick: ring mod
                b = make (0.08); d = b.getWritePointer (0);
                for (int i = 0; i < b.getNumSamples(); ++i) { const double t = i / sr; d[i] = (float) (std::sin (twoPi * 3000 * t) * std::sin (twoPi * 4370 * t) * env (t, 0.018)); }
                break;
            case 16:                                                                                   // Wood Blip: resonant ping
            {
                b = make (0.15); d = b.getWritePointer (0);
                Svf f; f.set (sr, 620, 25);
                for (int i = 0; i < b.getNumSamples(); ++i) { float lp, hp; f.tick (i == 0 ? 1.0f : 0.0f, lp, hp); d[i] = (lp - hp * 0.0f) * 3.0f; }
                break;
            }
            case 17: sweep (0.18, 300, 100, 0.05, 0.08, 1.2); break;                                 // Drop
            case 18: sweep (0.05, 1000, 4000, 0.015, 0.02, 1.0); break;                              // Chirp (rises)
            default: sweep (0.3, 110, 110, 1, 0.12, 1.8); break;                                     // Bass Blip (C2-ish, play it in tune)
        }
        // normalise
        const float pk = b.getMagnitude (0, 0, b.getNumSamples());
        if (pk > 0.0f) b.applyGain (0.85f / pk);
        return b;
    }
}

// =====================================================================================================
//  Steps
// =====================================================================================================
juce::uint32 BeatLab::Step::pack() const
{
    juce::uint32 v = on ? 1u : 0u;
    v |= (juce::uint32) juce::jlimit (1, 127, velocity) << 1;
    v |= (juce::uint32) juce::jlimit (0, 100, probability) << 8;
    v |= (juce::uint32) (juce::jlimit (1, 16, ratchet) - 1) << 15;
    v |= (juce::uint32) (juce::jlimit (-24, 24, pitch) + 32) << 19;
    v |= (juce::uint32) (juce::jlimit (-8, 7, nudge) + 8) << 25;
    v |= (juce::uint32) (juce::jlimit (0, 3, ratchetShape)) << 29;
    v |= reverse ? (1u << 31) : 0u;
    return v;
}

BeatLab::Step BeatLab::Step::unpack (juce::uint32 v)
{
    Step s;
    s.on = (v & 1u) != 0;
    s.velocity = (int) ((v >> 1) & 127u);
    s.probability = (int) ((v >> 8) & 127u);
    s.ratchet = (int) ((v >> 15) & 15u) + 1;
    s.pitch = (int) ((v >> 19) & 63u) - 32;
    s.nudge = (int) ((v >> 25) & 15u) - 8;
    s.ratchetShape = (int) ((v >> 29) & 3u);
    s.reverse = (v >> 31) != 0;
    if (s.velocity == 0) s.velocity = 100;
    return s;
}

static size_t stepIndex (int p, int l, int s) { return (size_t) ((p * BeatLab::numLanes + l) * BeatLab::maxSteps + s); }

BeatLab::Step BeatLab::getStep (int p, int l, int s) const
{
    if (! juce::isPositiveAndBelow (p, numPatterns) || ! juce::isPositiveAndBelow (l, numLanes) || ! juce::isPositiveAndBelow (s, maxSteps)) return {};
    return Step::unpack (steps[stepIndex (p, l, s)].load (std::memory_order_relaxed));
}

void BeatLab::setStep (int p, int l, int s, const Step& st)
{
    if (! juce::isPositiveAndBelow (p, numPatterns) || ! juce::isPositiveAndBelow (l, numLanes) || ! juce::isPositiveAndBelow (s, maxSteps)) return;
    steps[stepIndex (p, l, s)].store (st.pack(), std::memory_order_relaxed);
}

void BeatLab::clearPattern (int p, int lane)
{
    const juce::uint32 empty = Step().pack();
    for (int l = 0; l < numLanes; ++l)
        if (lane < 0 || l == lane)
            for (int s = 0; s < maxSteps; ++s) steps[stepIndex (p, l, s)].store (empty);
}

void BeatLab::copyPattern (int from, int to)
{
    if (from == to) return;
    for (int l = 0; l < numLanes; ++l)
        for (int s = 0; s < maxSteps; ++s) steps[stepIndex (to, l, s)].store (steps[stepIndex (from, l, s)].load());
}

void BeatLab::euclid (int p, int lane, int hits, int rotate)
{
    const int len = juce::jlimit (1, maxSteps, (int) param (laneParam (lane, "steps").toRawUTF8()));
    hits = juce::jlimit (0, len, hits);
    for (int s = 0; s < maxSteps; ++s)
    {
        Step st = getStep (p, lane, s);
        const int i = ((s - rotate) % len + len) % len;
        st.on = s < len && hits > 0 && ((i * hits) % len) < hits;
        if (st.on && st.velocity < 20) st.velocity = 100;
        setStep (p, lane, s, st);
    }
}

void BeatLab::randomise (int p, int lane, float density, float wild)
{
    const int len = juce::jlimit (1, maxSteps, (int) param (laneParam (lane, "steps").toRawUTF8()));
    for (int s = 0; s < maxSteps; ++s)
    {
        Step st;
        // favour the beat a little, so it still grooves
        const float accent = (s % 4 == 0 ? 0.25f : s % 2 == 0 ? 0.08f : 0.0f) * (1.0f - wild);
        st.on = s < len && rng.nextFloat() < density + accent;
        st.velocity = 60 + rng.nextInt (68);
        if (rng.nextFloat() < wild * 0.35f) { static const int r[] = { 2, 3, 4, 6, 8, 12, 16 }; st.ratchet = r[rng.nextInt (7)]; st.ratchetShape = rng.nextInt (4); }
        if (rng.nextFloat() < wild * 0.3f) st.probability = 30 + rng.nextInt (60);
        if (rng.nextFloat() < wild * 0.2f) { static const int pj[] = { -12, -7, -5, 5, 7, 12 }; st.pitch = pj[rng.nextInt (6)]; }
        if (rng.nextFloat() < wild * 0.15f) st.reverse = true;
        if (rng.nextFloat() < wild * 0.25f) st.nudge = rng.nextInt (9) - 4;
        setStep (p, lane, s, st);
    }
}

// =====================================================================================================
//  Parameters
// =====================================================================================================
juce::StringArray BeatLab::kitNames()
{
    auto k = DrumSynth::kitNames();
    k.add ("Glitch Lab");
    return k;
}

juce::StringArray BeatLab::voiceNamesForKit (int kit)
{
    if (kit == glitchKit)
        return { "Sub Boom", "Bit Snare", "Click", "Shred", "Tick", "Digital Hat", "Fizz", "Glass Bell", "Sine Ping", "Zap Low",
                 "Zap", "Laser", "FM Blip", "Bleep", "Crackle", "Metal Tick", "Wood Blip", "Drop", "Chirp", "Bass Blip" };
    juce::StringArray s;
    for (int v = 0; v < DrumSynth::numVoices; ++v) s.add (DrumSynth::voiceName ((DrumSynth::Voice) v));
    return s;
}

juce::StringArray BeatLab::rateNames() { return { "1/32", "1/16 T", "1/16", "1/8 T", "1/8", "1/4" }; }
double BeatLab::rateBeats (int i) { static const double b[] = { 0.125, 1.0 / 6.0, 0.25, 1.0 / 3.0, 0.5, 1.0 }; return b[juce::jlimit (0, 5, i)]; }
int BeatLab::stepsForRate (int i) { static const int s[] = { 32, 24, 16, 12, 8, 4 }; return s[juce::jlimit (0, 5, i)]; }

static prm::Layout beatLabLayout()
{
    prm::Layout l;
    prm::addDb (l, "volume", "Volume", -30.0f, 6.0f, -3.0f);
    prm::addChoice (l, "pattern", "Pattern", { "A", "B", "C", "D", "E", "F", "G", "H" }, 0);
    prm::addChoice (l, "chain", "Chain Patterns", { "Off", "A-B", "A-D", "A-H" }, 0);
    prm::addPercent (l, "swing", "Swing", 0.0f);
    prm::addBool (l, "sync", "Play with the Song", true);
    prm::addFloat (l, "tempo", "Tempo", 60.0f, 200.0f, 120.0f, "bpm", 0.0f, 0);
    prm::addPercent (l, "mutate", "Mutate", 0.0f);
    prm::addPercent (l, "chaos", "Chaos", 0.0f);
    prm::addPercent (l, "shuffle", "Break Shuffle", 0.0f);
    prm::addPercent (l, "revSize", "Reverb Size", 0.6f);
    prm::addChoice (l, "dlyDiv", "Delay Time", { "1/16", "1/8 T", "1/8", "1/8 D", "1/4" }, 3);
    prm::addPercent (l, "dlyFb", "Delay Feedback", 0.35f);
    prm::addFloat (l, "perfFilter", "Filter Sweep", -1.0f, 1.0f, 0.0f, "", 0.0f, 2);
    prm::addPercent (l, "perfReso", "Sweep Resonance", 0.3f);
    prm::addChoice (l, "keysLane", "Keyboard Plays", { "Lane 1", "Lane 2", "Lane 3", "Lane 4", "Lane 5", "Lane 6", "Lane 7", "Lane 8" }, 0);

    static const int defVoices[] = { DrumSynth::kick, DrumSynth::snare, DrumSynth::closedHat, DrumSynth::openHat,
                                     DrumSynth::clap, DrumSynth::rim, DrumSynth::lowTom, DrumSynth::cowbell };
    for (int i = 0; i < BeatLab::numLanes; ++i)
    {
        auto id = [i] (const char* n) { return BeatLab::laneParam (i, n); };
        const auto name = "Lane " + juce::String (i + 1) + " ";
        prm::addChoice (l, id ("kit"), name + "Kit", BeatLab::kitNames(), DrumSynth::eightOhEight);
        prm::addChoice (l, id ("voice"), name + "Sound", BeatLab::voiceNamesForKit (0), defVoices[i]);
        prm::addChoice (l, id ("mode"), name + "Play Mode", { "One Shot", "Loop: Slices", "Loop: Repitch" }, 0);
        prm::addDb (l, id ("vol"), name + "Volume", -40.0f, 6.0f, -4.0f);
        prm::addFloat (l, id ("pan"), name + "Pan", -1.0f, 1.0f, 0.0f, "", 0.0f, 2);
        prm::addFloat (l, id ("tune"), name + "Tune", -24.0f, 24.0f, 0.0f, "st", 0.0f, 1);
        prm::addFloat (l, id ("decay"), name + "Decay", 0.02f, 4.0f, 4.0f, "s", 0.4f, 2);
        prm::addFloat (l, id ("filter"), name + "Filter", -1.0f, 1.0f, 0.0f, "", 0.0f, 2);
        prm::addPercent (l, id ("reso"), name + "Resonance", 0.2f);
        prm::addPercent (l, id ("drive"), name + "Drive", 0.0f);
        prm::addPercent (l, id ("crush"), name + "Crush", 0.0f);
        prm::addPercent (l, id ("rev"), name + "Reverb", 0.0f);
        prm::addPercent (l, id ("dly"), name + "Delay", 0.0f);
        prm::addFloat (l, id ("steps"), name + "Steps", 1.0f, (float) BeatLab::maxSteps, 16.0f, "", 0.0f, 0);
        prm::addChoice (l, id ("rate"), name + "Rate", BeatLab::rateNames(), 2);
        prm::addBool (l, id ("reverse"), name + "Reverse", false);
        prm::addBool (l, id ("mute"), name + "Mute", false);
        prm::addBool (l, id ("solo"), name + "Solo", false);
        prm::addChoice (l, id ("choke"), name + "Choke Group", { "None", "1", "2", "3" }, i == 2 || i == 3 ? 1 : 0);
    }
    return l;
}

// =====================================================================================================
//  Voices, lane FX, master
// =====================================================================================================
struct BeatLab::Voice
{
    bool active = false;
    int lane = 0, delay = 0;
    SampleData* data = nullptr;
    double pos = 0, inc = 1, start = 0, end = 0;
    float gain = 1, env = 1, envMul = 1, fade = 1, fadeStep = 0, edgeFade = 64;
    juce::uint32 order = 0;
};

struct BeatLab::LaneFx
{
    DjFilter filter;
    float held[2] {};
    float crushCount = 0;
    float lastL = 0, lastR = 0;
};

struct BeatLab::Master
{
    juce::Reverb reverb;
    std::vector<float> dl[2];
    int dw = 0;
    float dlyLp[2] {}, fbL = 0, fbR = 0;
    // performance: everything that played recently, for stutter / tape stop / reverse
    juce::AudioBuffer<float> ring;
    int rw = 0;
    int perf = 0, stutterLen = 1, elapsed = 0;
    int captureStart = 0;
    double readPos = 0, speed = 1;
    float perfMix = 0;
    DjFilter sweep;
};

BeatLab::BeatLab()
    : BuiltinProcessor ("beatlab", "Beat Lab", true, beatLabLayout()),
      voices (std::make_unique<std::array<Voice, 48>>()), laneFx (std::make_unique<std::array<LaneFx, numLanes>>()), master (std::make_unique<Master>())
{
    for (auto& s : steps) s.store (Step().pack());
    for (auto& a : laneStep) a = -1;
    for (auto& f : laneFlash) f = 0.0f;
    for (auto& d : laneDirty) d = true;
    for (int i = 0; i < numLanes; ++i)
        for (auto* n : { "kit", "voice" }) state.addParameterListener (laneParam (i, n), this);
    loadProgram (0);
}

BeatLab::~BeatLab()
{
    cancelPendingUpdate();
    for (int i = 0; i < numLanes; ++i)
        for (auto* n : { "kit", "voice" }) state.removeParameterListener (laneParam (i, n), this);
}

void BeatLab::parameterChanged (const juce::String& id, float)
{
    const int lane = id.substring (1).getIntValue();
    if (juce::isPositiveAndBelow (lane, numLanes) && ! laneHasUserSample (lane))
    {
        laneDirty[(size_t) lane] = true;
        triggerAsyncUpdate();
    }
}

void BeatLab::buildLaneSound (int lane)
{
    laneDirty[(size_t) lane] = false;
    if (laneHasUserSample (lane)) return;
    const int kit = (int) param (laneParam (lane, "kit").toRawUTF8());
    const int voice = (int) param (laneParam (lane, "voice").toRawUTF8());
    auto b = kit == glitchKit ? renderGlitch (voice, sr) : DrumSynth::renderHit ((DrumSynth::Voice) juce::jlimit (0, DrumSynth::numVoices - 1, voice), kit, sr);
    laneSamples[(size_t) lane].set (SampleData::fromBuffer (std::move (b), sr, voiceNamesForKit (kit)[voice]));
}

void BeatLab::rebuildSoundsNow()
{
    for (int l = 0; l < numLanes; ++l)
        if (laneDirty[(size_t) l].load() || laneSamples[(size_t) l].get() == nullptr) buildLaneSound (l);
}

juce::String BeatLab::loadLaneFile (int lane, const juce::File& f)
{
    juce::String error;
    auto d = SampleData::fromFile (f, 60.0, error);
    if (d == nullptr) return error;
    d->ref = makeFileRef (f);
    laneRefs[(size_t) lane] = d->ref;
    laneUserBuffer[(size_t) lane] = false;
    const double secs = d->length() / d->sampleRate;
    laneSamples[(size_t) lane].set (std::move (d));
    // longer sounds are loops: chop them across the lane, so they stay in time at any tempo
    if (secs > 1.2) setParam (laneParam (lane, "mode"), (float) loopSlice);
    else if (secs <= 1.2 && (int) param (laneParam (lane, "mode").toRawUTF8()) != oneShot) setParam (laneParam (lane, "mode"), (float) oneShot);
    setParam (laneParam (lane, "decay"), 4.0f);
    return {};
}

void BeatLab::loadLaneBuffer (int lane, juce::AudioBuffer<float> b, double rate, const juce::String& name)
{
    const double secs = b.getNumSamples() / rate;
    laneRefs[(size_t) lane] = {};
    laneUserBuffer[(size_t) lane] = true;
    laneSamples[(size_t) lane].set (SampleData::fromBuffer (std::move (b), rate, name));
    setParam (laneParam (lane, "mode"), secs > 1.2 ? (float) loopSlice : (float) oneShot);
    setParam (laneParam (lane, "decay"), 4.0f);
}

void BeatLab::useSynthSound (int lane)
{
    laneRefs[(size_t) lane] = {};
    laneUserBuffer[(size_t) lane] = false;
    setParam (laneParam (lane, "mode"), (float) oneShot);
    buildLaneSound (lane);
}

juce::String BeatLab::laneName (int lane) const
{
    if (auto* s = laneSamples[(size_t) lane].get(); s != nullptr && laneHasUserSample (lane)) return s->name;
    const int kit = (int) param (laneParam (lane, "kit").toRawUTF8());
    return voiceNamesForKit (kit)[(int) param (laneParam (lane, "voice").toRawUTF8())];
}

void BeatLab::saveExtraState (juce::ValueTree& v)
{
    juce::MemoryBlock mb (steps.size() * sizeof (juce::uint32));
    auto* d = static_cast<juce::uint32*> (mb.getData());
    for (size_t i = 0; i < steps.size(); ++i) d[i] = juce::ByteOrder::swapIfBigEndian (steps[i].load());
    v.setProperty ("steps", mb.toBase64Encoding(), nullptr);
    for (int l = 0; l < numLanes; ++l)
        v.setProperty ("sample" + juce::String (l), laneRefs[(size_t) l], nullptr);
}

void BeatLab::loadExtraState (const juce::ValueTree& v)
{
    juce::MemoryBlock mb;
    if (mb.fromBase64Encoding (v["steps"].toString()) && mb.getSize() == steps.size() * sizeof (juce::uint32))
    {
        auto* d = static_cast<const juce::uint32*> (mb.getData());
        for (size_t i = 0; i < steps.size(); ++i) steps[i].store (juce::ByteOrder::swapIfBigEndian (d[i]));
    }
    for (int l = 0; l < numLanes; ++l)
    {
        const auto ref = v["sample" + juce::String (l)].toString();
        laneRefs[(size_t) l] = {};
        laneUserBuffer[(size_t) l] = false;
        if (ref.isNotEmpty())
        {
            juce::String error;
            if (auto d = SampleData::fromFile (resolveFileRef (ref), 60.0, error))
            {
                d->ref = ref;
                laneRefs[(size_t) l] = ref;
                laneSamples[(size_t) l].set (std::move (d));
                continue;
            }
        }
        laneDirty[(size_t) l] = true;
    }
    rebuildSoundsNow();
}

// =====================================================================================================
//  Processing
// =====================================================================================================
void BeatLab::prepareToPlay (double rate, int block)
{
    const bool rateChanged = std::abs (rate - sr) > 1.0;
    sr = rate;
    blockSize = block;
    laneBuf.setSize (numLanes * 2, block, false, false, true);
    revBus.setSize (2, block, false, false, true);
    dlyBus.setSize (2, block, false, false, true);
    for (auto& v : *voices) v.active = false;
    master->reverb.setSampleRate (rate);
    master->reverb.reset();
    for (auto& d : master->dl) d.assign ((size_t) (rate * 2.5), 0.0f);
    master->dw = 0;
    master->ring.setSize (2, (int) (rate * 4.0));
    master->ring.clear();
    master->rw = 0;
    master->perf = 0;
    master->perfMix = 0;
    if (rateChanged)
        for (auto& d : laneDirty) d = true;
    for (int l = 0; l < numLanes; ++l)
        if (laneSamples[(size_t) l].get() == nullptr || (rateChanged && ! laneHasUserSample (l))) laneDirty[(size_t) l] = true;
    rebuildSoundsNow();
    lastBar = -1;
}

void BeatLab::startVoice (int lane, int offset, float vel, double pitch, bool reverse, int slice, int count, bool repitch)
{
    auto* data = laneSamples[(size_t) lane].get();
    if (data == nullptr || data->length() < 2) return;
    auto& vs = *voices;
    const int mode = (int) param (laneParam (lane, "mode").toRawUTF8());
    const int choke = (int) param (laneParam (lane, "choke").toRawUTF8());
    static juce::uint32 counter = 0;

    // loops are monophonic per lane; choke groups cut each other (open / closed hats)
    for (auto& v : vs)
    {
        if (! v.active) continue;
        const bool sameLane = v.lane == lane && (mode != oneShot);
        const bool choked = choke > 0 && v.lane != lane && (int) param (laneParam (v.lane, "choke").toRawUTF8()) == choke;
        if (sameLane || choked)
        {
            if (v.delay > offset) v.active = false;
            else { v.fadeStep = 1.0f / (0.004f * (float) sr); }
        }
    }

    Voice* slot = nullptr;
    int perLane = 0;
    for (auto& v : vs) if (v.active && v.lane == lane) ++perLane;
    for (auto& v : vs) if (! v.active) { slot = &v; break; }
    if (slot == nullptr || perLane >= 6)
    {
        // steal the oldest voice
        for (auto& v : vs) if (slot == nullptr || v.order < slot->order) slot = &v;
    }
    auto& v = *slot;
    const double len = data->length();
    v = Voice();
    v.active = true;
    v.lane = lane;
    v.data = data;
    v.delay = offset;
    v.order = ++counter;
    double speed = 1.0;
    if (mode == oneShot)          { v.start = 0; v.end = len; }
    else if (mode == loopSlice)   { v.start = len * slice / count; v.end = len * (slice + 1) / count; }
    else
    {
        v.start = len * slice / count; v.end = len;
        if (repitch)
        {
            // play the whole loop across the lane's length (speeds up / slows down with the tempo, like a record)
            const double laneBeats = count * rateBeats ((int) param (laneParam (lane, "rate").toRawUTF8()));
            const double bpm = juce::jmax (20.0, currentBpm);
            speed = (len / data->sampleRate) / (laneBeats * 60.0 / bpm);
        }
    }
    v.inc = data->sampleRate / sr * std::pow (2.0, pitch / 12.0) * speed;
    if (reverse) { v.pos = v.end - 1.0; v.inc = -v.inc; }
    else v.pos = v.start;
    v.gain = std::pow (juce::jlimit (0.0f, 1.0f, vel), 1.3f);
    const float decay = param (laneParam (lane, "decay").toRawUTF8());
    v.envMul = decay >= 3.95f ? 1.0f : std::exp (-1.0f / (decay * (float) sr));
    v.edgeFade = mode == loopSlice ? (float) juce::jmin (96.0, (v.end - v.start) * 0.1) : 16.0f;
    laneFlash[(size_t) lane] = 1.0f;
}

void BeatLab::runSequencer (int n, double ppq0, double bps, double bpb)
{
    const double ppq1 = ppq0 + n * bps;
    const int chain = (int) param ("chain");
    const auto bar = (juce::int64) std::floor (ppq0 / bpb + 1.0e-9);
    if (bar != lastBar)
    {
        const int count = chain == 1 ? 2 : chain == 2 ? 4 : chain == 3 ? 8 : 1;
        patternPlaying = chain == 0 ? getPatternEditing() : (int) (((bar % count) + count) % count);
        lastBar = bar;
    }
    const int pat = patternPlaying.load();
    const float swing = param ("swing") * 0.75f, mutate = param ("mutate"), shuffle = param ("shuffle");
    float chaos = param ("chaos");
    const bool fill = performance.load() == perfFill;
    if (fill) chaos = juce::jmax (chaos, 0.6f);

    bool anySolo = false;
    for (int l = 0; l < numLanes; ++l) anySolo = anySolo || param (laneParam (l, "solo").toRawUTF8()) > 0.5f;

    for (int l = 0; l < numLanes; ++l)
    {
        const int len = juce::jlimit (1, maxSteps, (int) std::round (param (laneParam (l, "steps").toRawUTF8())));
        const double s = rateBeats ((int) param (laneParam (l, "rate").toRawUTF8()));
        const auto kNow = (juce::int64) std::floor (ppq0 / s);
        laneStep[(size_t) l] = (int) (((kNow % len) + len) % len);
        const bool muted = param (laneParam (l, "mute").toRawUTF8()) > 0.5f || (anySolo && param (laneParam (l, "solo").toRawUTF8()) < 0.5f);
        if (muted) continue;
        const int mode = (int) param (laneParam (l, "mode").toRawUTF8());
        const double tune = param (laneParam (l, "tune").toRawUTF8());
        const bool laneRev = param (laneParam (l, "reverse").toRawUTF8()) > 0.5f;

        for (juce::int64 k = kNow - 1; k <= (juce::int64) std::floor (ppq1 / s) + 1; ++k)
        {
            const int pos = (int) (((k % len) + len) % len);
            Step st = Step::unpack (steps[stepIndex (pat, l, pos)].load (std::memory_order_relaxed));
            const auto stepBar = (juce::int64) std::floor (k * s / bpb + 1.0e-9);

            // mutate: each bar gets its own small variation (ghost notes appear, hits drop out)
            if (mutate > 0.0f && hash01 (stepBar, l, pos) < mutate * 0.3f)
            {
                if (st.on) st.on = hash01 (stepBar, l, pos + 500) < 0.4f;
                else { st.on = true; st.velocity = 35 + (int) (hash01 (stepBar, l, pos + 900) * 40); }
            }
            if (! st.on) continue;

            // chaos: rolls, pitch jumps, reversals, the drill'n'bass toolkit
            int ratchet = st.ratchet, shape = st.ratchetShape;
            double pitch = st.pitch;
            bool rev = st.reverse != laneRev;
            if (chaos > 0.0f)
            {
                if (hash01 (k, l, 11) < chaos * 0.35f)
                {
                    static const int r[] = { 2, 3, 4, 6, 8, 12, 16 };
                    ratchet = juce::jmax (ratchet, r[(int) (hash01 (k, l, 12) * 6.99f)]);
                    shape = (int) (hash01 (k, l, 13) * 3.99f);
                }
                if (hash01 (k, l, 14) < chaos * 0.15f) rev = ! rev;
                if (hash01 (k, l, 15) < chaos * 0.2f) { static const int pj[] = { -12, -5, 7, 12 }; pitch += pj[(int) (hash01 (k, l, 16) * 3.99f)]; }
            }

            int slice = pos;
            if (mode != oneShot && shuffle > 0.0f && hash01 (k, l, 21) < shuffle)
                slice = (int) (hash01 (k, l, 22) * (float) len * 0.999f);   // break shuffle: play a different chop here

            double t = k * s + st.nudge / 16.0 * s;
            if ((k & 1) != 0) t += swing * s * 0.5;
            for (int j = 0; j < ratchet; ++j)
            {
                const double tj = t + j * s / ratchet;
                if (tj < ppq0 || tj >= ppq1) continue;
                if (st.probability < 100 && hash01 (k, l, 31 + j) * 100.0f >= (float) st.probability) continue;
                float vel = st.velocity / 127.0f;
                double pj = pitch;
                const double frac = ratchet > 1 ? (double) j / (ratchet - 1) : 0.0;
                if (shape == 1) pj += 12.0 * frac;
                if (shape == 2) pj -= 12.0 * frac;
                if (shape == 3) vel *= (float) (1.0 - 0.8 * frac);
                const int offset = juce::jlimit (0, n - 1, (int) ((tj - ppq0) / bps));
                startVoice (l, offset, vel, pj + tune, rev, slice, len, mode == loopRepitch);
            }
        }
    }
}

void BeatLab::renderVoices (int n)
{
    laneBuf.clear (0, n);
    for (auto& v : *voices)
    {
        if (! v.active) continue;
        // the lane's sound was swapped: let go of the old one
        if (v.data != laneSamples[(size_t) v.lane].get()) { v.active = false; continue; }
        const auto& buf = v.data->buffer;
        const float* src0 = buf.getReadPointer (0);
        const float* src1 = buf.getReadPointer (buf.getNumChannels() > 1 ? 1 : 0);
        float* outL = laneBuf.getWritePointer (v.lane * 2);
        float* outR = laneBuf.getWritePointer (v.lane * 2 + 1);
        const int last = v.data->length() - 1;
        int i = 0;
        if (v.delay > 0) { i = juce::jmin (n, v.delay); v.delay -= i; }
        for (; i < n; ++i)
        {
            if (v.pos < v.start || v.pos >= v.end) { v.active = false; break; }
            const int a = (int) v.pos;
            const float fr = (float) (v.pos - a);
            const int b = juce::jmin (last, a + 1);
            // short fades at the edges of slices so chops don't click
            const float edge = (float) juce::jmin (v.pos - v.start, v.end - v.pos) / v.edgeFade;
            const float g = v.gain * v.env * v.fade * juce::jmin (1.0f, edge);
            outL[i] += (src0[a] + (src0[b] - src0[a]) * fr) * g;
            outR[i] += (src1[a] + (src1[b] - src1[a]) * fr) * g;
            v.pos += v.inc;
            v.env *= v.envMul;
            if (v.fadeStep > 0.0f) { v.fade -= v.fadeStep; if (v.fade <= 0.0f) { v.active = false; break; } }
            if (v.env < 0.0005f) { v.active = false; break; }
        }
    }
}

void BeatLab::renderMaster (juce::AudioBuffer<float>& out, int n, double bpm)
{
    auto& m = *master;
    out.clear (0, n);
    revBus.clear (0, n);
    dlyBus.clear (0, n);
    float* L = out.getWritePointer (0);
    float* R = out.getWritePointer (out.getNumChannels() > 1 ? 1 : 0);

    bool anySolo = false;
    for (int l = 0; l < numLanes; ++l) anySolo = anySolo || param (laneParam (l, "solo").toRawUTF8()) > 0.5f;

    for (int l = 0; l < numLanes; ++l)
    {
        auto& fx = (*laneFx)[(size_t) l];
        const bool muted = param (laneParam (l, "mute").toRawUTF8()) > 0.5f || (anySolo && param (laneParam (l, "solo").toRawUTF8()) < 0.5f);
        const float vol = muted ? 0.0f : juce::Decibels::decibelsToGain (param (laneParam (l, "vol").toRawUTF8()), -40.0f);
        const float pan = param (laneParam (l, "pan").toRawUTF8());
        const float gl = vol * std::cos ((pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f) * 1.414f;
        const float gr = vol * std::sin ((pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f) * 1.414f;
        const float filt = param (laneParam (l, "filter").toRawUTF8());
        fx.filter.update (sr, filt, param (laneParam (l, "reso").toRawUTF8()));
        const float drive = param (laneParam (l, "drive").toRawUTF8());
        const float dg = 1.0f + drive * 12.0f, dnorm = 1.0f / std::sqrt (dg);
        const float crush = param (laneParam (l, "crush").toRawUTF8());
        const float levels = std::pow (2.0f, 15.0f - crush * 12.0f), downs = 1.0f + crush * crush * 20.0f;
        const float rev = param (laneParam (l, "rev").toRawUTF8()), dly = param (laneParam (l, "dly").toRawUTF8());
        const float* inL = laneBuf.getReadPointer (l * 2);
        const float* inR = laneBuf.getReadPointer (l * 2 + 1);
        for (int i = 0; i < n; ++i)
        {
            float a = inL[i], b = inR[i];
            if (drive > 0.001f) { a = std::tanh (a * dg) * dnorm; b = std::tanh (b * dg) * dnorm; }
            if (crush > 0.001f)
            {
                fx.crushCount += 1.0f;
                if (fx.crushCount >= downs) { fx.crushCount -= downs; fx.held[0] = std::round (a * levels) / levels; fx.held[1] = std::round (b * levels) / levels; }
                a = fx.held[0]; b = fx.held[1];
            }
            fx.filter.process (a, b, filt);
            a *= gl; b *= gr;
            L[i] += a; R[i] += b;
            revBus.getWritePointer (0)[i] += a * rev; revBus.getWritePointer (1)[i] += b * rev;
            dlyBus.getWritePointer (0)[i] += a * dly; dlyBus.getWritePointer (1)[i] += b * dly;
        }
    }

    // reverb return
    juce::Reverb::Parameters rp;
    rp.roomSize = 0.3f + param ("revSize") * 0.68f; rp.damping = 0.45f; rp.wetLevel = 1.0f; rp.dryLevel = 0.0f; rp.width = 1.0f;
    m.reverb.setParameters (rp);
    m.reverb.processStereo (revBus.getWritePointer (0), revBus.getWritePointer (1), n);

    // ping-pong delay return
    static const double divs[] = { 0.25, 1.0 / 3.0, 0.5, 0.75, 1.0 };
    const int dlen = (int) m.dl[0].size();
    const int dsamp = juce::jlimit (16, dlen - 2, (int) (divs[juce::jlimit (0, 4, (int) param ("dlyDiv"))] * 60.0 / bpm * sr));
    const float fb = param ("dlyFb") * 0.9f;
    const float lpA = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 5000.0f / (float) sr);
    for (int i = 0; i < n; ++i)
    {
        const int rd = (m.dw - dsamp + dlen) % dlen;
        const float oL = m.dl[0][(size_t) rd], oR = m.dl[1][(size_t) rd];
        m.dlyLp[0] += (oR - m.dlyLp[0]) * lpA;
        m.dlyLp[1] += (oL - m.dlyLp[1]) * lpA;
        m.dl[0][(size_t) m.dw] = dlyBus.getSample (0, i) + dlyBus.getSample (1, i) * 0.0f + m.dlyLp[0] * fb;
        m.dl[1][(size_t) m.dw] = m.dlyLp[1] * fb;
        m.dw = (m.dw + 1) % dlen;
        L[i] += revBus.getSample (0, i) * 0.8f + oL * 0.8f;
        R[i] += revBus.getSample (1, i) * 0.8f + oR * 0.8f;
    }

    // performance effects: stutter / tape stop / reverse from what just played
    const int perf = performance.load();
    const int ringLen = m.ring.getNumSamples();
    if (perf != m.perf)
    {
        m.perf = perf;
        m.elapsed = 0;
        m.captureStart = m.rw;
        m.readPos = (double) ((m.rw - 1 + ringLen) % ringLen);
        m.speed = 1.0;
        static const double stutterBeats[] = { 0, 1.0, 0.5, 0.25, 0.125, 0.0625 };
        if (perf >= perfStutter4 && perf <= perfStutter64)
            m.stutterLen = juce::jmax (32, (int) (stutterBeats[perf] * 60.0 / bpm * sr));
    }
    float* r0 = m.ring.getWritePointer (0);
    float* r1 = m.ring.getWritePointer (1);
    const float mixStep = 1.0f / (0.004f * (float) sr);
    for (int i = 0; i < n; ++i)
    {
        r0[m.rw] = L[i]; r1[m.rw] = R[i];
        float pl = L[i], pr = R[i];
        bool active = false;
        if (m.perf >= perfStutter4 && m.perf <= perfStutter64)
        {
            if (m.elapsed >= m.stutterLen)
            {
                const int idx = (m.captureStart + m.elapsed % m.stutterLen) % ringLen;
                const int within = m.elapsed % m.stutterLen;
                const float edge = juce::jmin (1.0f, juce::jmin ((float) within, (float) (m.stutterLen - within)) / 48.0f);
                pl = r0[idx] * edge; pr = r1[idx] * edge;
                active = true;
            }
            ++m.elapsed;
        }
        else if (m.perf == perfTapeStop || m.perf == perfReverse)
        {
            if (m.perf == perfTapeStop) m.speed = juce::jmax (0.0, m.speed - 1.0 / (0.6 * sr));
            const double rp2 = m.readPos;
            const int a = (int) rp2;
            const float fr = (float) (rp2 - a);
            const int b = (a + 1) % ringLen;
            const float s = m.perf == perfTapeStop ? (float) juce::jmin (1.0, m.speed * 3.0) : 1.0f;
            pl = (r0[a] + (r1[0] * 0.0f) + (r0[b] - r0[a]) * fr) * s;
            pr = (r1[a] + (r1[b] - r1[a]) * fr) * s;
            m.readPos += m.perf == perfTapeStop ? m.speed : -1.0;
            if (m.readPos < 0) m.readPos += ringLen;
            if (m.readPos >= ringLen) m.readPos -= ringLen;
            active = true;
        }
        m.rw = (m.rw + 1) % ringLen;
        m.perfMix = active ? juce::jmin (1.0f, m.perfMix + mixStep) : juce::jmax (0.0f, m.perfMix - mixStep);
        L[i] += (pl - L[i]) * m.perfMix;
        R[i] += (pr - R[i]) * m.perfMix;
    }

    // filter sweep, volume, soft limit
    const float sweep = param ("perfFilter");
    m.sweep.update (sr, sweep, param ("perfReso"));
    const float vol = juce::Decibels::decibelsToGain (param ("volume"));
    for (int i = 0; i < n; ++i)
    {
        float a = L[i], b = R[i];
        m.sweep.process (a, b, sweep);
        a *= vol; b *= vol;
        auto soft = [] (float x) { const float ax = std::abs (x); return ax < 0.9f ? x : std::copysign (0.9f + 0.1f * std::tanh ((ax - 0.9f) * 10.0f), x); };
        L[i] = soft (a);
        if (R != L) R[i] = soft (b);
    }
}

void BeatLab::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals nd;
    const int total = buffer.getNumSamples();
    if (laneBuf.getNumSamples() < total)
        laneBuf.setSize (numLanes * 2, total, false, false, true), revBus.setSize (2, total, false, false, true), dlyBus.setSize (2, total, false, false, true);

    bool hostPlaying = false;
    double hostPpq = 0.0, hostBpm = 0.0, bpb = 4.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            hostPlaying = pos->getIsPlaying();
            if (auto p = pos->getPpqPosition()) hostPpq = *p;
            if (auto b = pos->getBpm()) hostBpm = *b;
            if (auto ts = pos->getTimeSignature()) bpb = ts->numerator * 4.0 / juce::jmax (1, ts->denominator);
        }
    const bool sync = param ("sync") > 0.5f;
    const double bpm = sync && hostBpm > 0.0 ? hostBpm : (double) param ("tempo");
    const double bps = bpm / 60.0 / sr;
    currentBpm = bpm;

    // live playing: lane pads on C2..G2, the chosen lane chromatically from C3 up
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (! m.isNoteOn()) continue;
        const int note = m.getNoteNumber();
        const int off = juce::jlimit (0, total - 1, meta.samplePosition);
        if (note >= firstLaneNote && note < firstLaneNote + numLanes)
        {
            const int l = note - firstLaneNote;
            const int mode = (int) param (laneParam (l, "mode").toRawUTF8());
            int slice = 0, count = 1;
            if (mode != oneShot && hostPlaying)
            {
                // a chopped loop played from a clip: play the slice that belongs at this point of the bar, like the sequencer does
                count = juce::jlimit (1, maxSteps, (int) std::round (param (laneParam (l, "steps").toRawUTF8())));
                const double s = rateBeats ((int) param (laneParam (l, "rate").toRawUTF8()));
                const auto k = (juce::int64) std::floor ((hostPpq + off * bps) / s + 0.01);
                slice = (int) (((k % count) + count) % count);
            }
            startVoice (l, off, m.getFloatVelocity(), param (laneParam (l, "tune").toRawUTF8()), param (laneParam (l, "reverse").toRawUTF8()) > 0.5f,
                        slice, count, mode == loopRepitch);
        }
        else if (note >= 48)
        {
            const int l = (int) param ("keysLane");
            startVoice (l, off, m.getFloatVelocity(), note - 60 + param (laneParam (l, "tune").toRawUTF8()), false, 0, 1, false);
        }
    }
    if (const int a = auditionRequest.exchange (-1); juce::isPositiveAndBelow (a, numLanes))
        startVoice (a, 0, auditionVelocity.load(), param (laneParam (a, "tune").toRawUTF8()), false, 0, 1, false);

    hostRolling = sync && hostPlaying;
    bool rolling = false;
    double ppq0 = 0.0;
    if (sync && hostPlaying)
    {
        rolling = true;
        ppq0 = hostPpq;
        internalRunning = false;
    }
    else if (internalRunning.load())
    {
        if (restartRequested.exchange (false)) { internalPpq = 0.0; lastBar = -1; }
        rolling = true;
        ppq0 = internalPpq;
        internalPpq += total * bps;
    }

    if (rolling) runSequencer (total, ppq0, bps, bpb);
    else
    {
        for (auto& a : laneStep) a = -1;
        lastBar = -1;
        patternPlaying = getPatternEditing();
    }
    wasRolling = rolling;

    renderVoices (total);
    renderMaster (buffer, total, bpm);
    for (int ch = 2; ch < buffer.getNumChannels(); ++ch) buffer.clear (ch, 0, total);
}

// =====================================================================================================
//  Pattern -> song
// =====================================================================================================
juce::MidiMessageSequence BeatLab::patternToMidi (int pat, double& lengthBeats) const
{
    juce::MidiMessageSequence seq;
    lengthBeats = 0.0;
    for (int l = 0; l < numLanes; ++l)
    {
        const int len = juce::jlimit (1, maxSteps, (int) std::round (param (laneParam (l, "steps").toRawUTF8())));
        lengthBeats = juce::jmax (lengthBeats, len * rateBeats ((int) param (laneParam (l, "rate").toRawUTF8())));
    }
    lengthBeats = juce::jmax (1.0, lengthBeats);
    const float swing = param ("swing") * 0.75f;
    for (int l = 0; l < numLanes; ++l)
    {
        if (param (laneParam (l, "mute").toRawUTF8()) > 0.5f) continue;
        const int len = juce::jlimit (1, maxSteps, (int) std::round (param (laneParam (l, "steps").toRawUTF8())));
        const double s = rateBeats ((int) param (laneParam (l, "rate").toRawUTF8()));
        for (juce::int64 k = 0; k * s < lengthBeats - 1.0e-9; ++k)
        {
            const auto st = getStep (pat, l, (int) (k % len));
            if (! st.on) continue;
            double t = k * s + st.nudge / 16.0 * s;
            if ((k & 1) != 0) t += swing * s * 0.5;
            for (int j = 0; j < st.ratchet; ++j)
            {
                if (st.probability < 100 && hash01 (k, l, 31 + j) * 100.0f >= (float) st.probability) continue;
                const double tj = juce::jmax (0.0, t + j * s / st.ratchet);
                const double frac = st.ratchet > 1 ? (double) j / (st.ratchet - 1) : 0.0;
                const float vel = st.velocity / 127.0f * (st.ratchetShape == 3 ? (float) (1.0 - 0.8 * frac) : 1.0f);
                seq.addEvent (juce::MidiMessage::noteOn (10, firstLaneNote + l, juce::jlimit (0.05f, 1.0f, vel)), tj);
                seq.addEvent (juce::MidiMessage::noteOff (10, firstLaneNote + l), tj + juce::jmin (0.1, s / st.ratchet * 0.9));
            }
        }
    }
    seq.updateMatchedPairs();
    return seq;
}

// =====================================================================================================
//  Presets
// =====================================================================================================
juce::StringArray BeatLab::presetNames()
{
    return { "Boom Bap", "Lo-Fi Hip Hop", "House", "Techno", "Trap", "Breakbeat", "Drum & Bass", "Dream Pop Machine", "Reggaeton",
             "Afrobeat", "IDM: Drill Machine", "IDM: Polymeter Maze", "IDM: Ambient Glitch", "IDM: Braindance", "Empty Kit" };
}

void BeatLab::loadProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, getProgramNames().size())) return;

    // reset everything to defaults
    for (auto* p : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p)) r->setValueNotifyingHost (r->getDefaultValue());
    for (int p = 0; p < numPatterns; ++p) clearPattern (p);
    for (int l = 0; l < numLanes; ++l) { laneRefs[(size_t) l] = {}; laneUserBuffer[(size_t) l] = false; laneDirty[(size_t) l] = true; }

    auto set = [this] (const juce::String& id, float v) { setParam (id, v); };
    auto lane = [&] (int l, int kit, int voice, const char* pattern, float vol = -4.0f)
    {
        set (laneParam (l, "kit"), (float) kit);
        set (laneParam (l, "voice"), (float) voice);
        set (laneParam (l, "vol"), vol);
        const int len = (int) std::strlen (pattern);
        set (laneParam (l, "steps"), (float) juce::jlimit (1, maxSteps, len));
        for (int s = 0; s < len && s < maxSteps; ++s)
        {
            Step st;
            switch (pattern[s])
            {
                case 'x': st.on = true; break;
                case 'X': st.on = true; st.velocity = 127; break;
                case 'o': st.on = true; st.velocity = 55; break;
                case 'p': st.on = true; st.velocity = 90; st.probability = 50; break;
                case 'r': st.on = true; st.velocity = 90; st.ratchet = 2; break;
                case 'R': st.on = true; st.velocity = 95; st.ratchet = 3; break;
                case 'D': st.on = true; st.velocity = 100; st.ratchet = 8; st.ratchetShape = 1; break;
                case 'd': st.on = true; st.velocity = 100; st.ratchet = 6; st.ratchetShape = 2; break;
                case '!': st.on = true; st.velocity = 105; st.ratchet = 16; st.ratchetShape = 1; break;
                case '~': st.on = true; st.velocity = 110; st.ratchet = 4; st.ratchetShape = 3; break;
                case '<': st.on = true; st.reverse = true; break;
                default: break;
            }
            setStep (0, l, s, st);
        }
    };
    auto pitches = [&] (int l, std::initializer_list<int> ps)
    {
        int s = 0;
        for (int p : ps) { auto st = getStep (0, l, s); st.pitch = p; setStep (0, l, s, st); ++s; }
    };
    auto fx = [&] (int l, const char* id, float v) { set (laneParam (l, id), v); };
    // pattern B: the same groove with a fill in its last quarter (rolls on the snare lane)
    auto makeFill = [&] (int snareLane)
    {
        copyPattern (0, 1);
        const int len = (int) std::round (param (laneParam (snareLane, "steps").toRawUTF8()));
        for (int s = len * 3 / 4; s < len; ++s)
        {
            Step st = getStep (1, snareLane, s);
            st.on = true; st.velocity = 70 + (s - len * 3 / 4) * 10;
            if (s >= len - 2) { st.ratchet = 4; st.ratchetShape = 1; }
            setStep (1, snareLane, s, st);
        }
    };

    const int home = DrumSynth::homeKeyboard, unit = DrumSynth::rhythmUnit, e8 = DrumSynth::eightOhEight, toy = DrumSynth::toyBox, gl = glitchKit;
    using V = DrumSynth::Voice;
    switch (index)
    {
        case 0: // Boom Bap
            set ("tempo", 90); set ("swing", 0.55f);
            lane (0, e8, V::kick,      "x.........x.x...", -2);  fx (0, "decay", 0.5f); fx (0, "drive", 0.3f);
            lane (1, unit, V::snare,   "....x.......x...");     fx (1, "crush", 0.25f); fx (1, "rev", 0.15f);
            lane (2, e8, V::closedHat, "x.x.x.x.x.x.x.xo", -9);
            lane (3, e8, V::openHat,   "..............x.", -12);
            lane (4, unit, V::rim,     "...o......o....o", -12);
            lane (5, e8, V::clap,      "", -8); lane (6, e8, V::lowTom, ""); lane (7, e8, V::cowbell, "");
            makeFill (1);
            break;
        case 1: // Lo-Fi Hip Hop
            set ("tempo", 78); set ("swing", 0.6f); set ("volume", 1.0f);
            lane (0, toy, V::kick,     "x......x..x.....", -2);  fx (0, "crush", 0.35f);
            lane (1, toy, V::snare,    "....x.......x...", -3);  fx (1, "crush", 0.4f); fx (1, "rev", 0.25f); fx (1, "filter", -0.25f);
            lane (2, home, V::closedHat, "x.xox.xox.xox.xo", -11); fx (2, "filter", -0.3f);
            lane (3, home, V::rim,     "..o.....o.....o.", -12);
            lane (4, toy, V::tambourine, "........x.......", -14); fx (4, "rev", 0.3f);
            lane (5, e8, V::clap, ""); lane (6, e8, V::lowTom, ""); lane (7, e8, V::cowbell, "");
            set ("revSize", 0.45f);
            makeFill (1);
            break;
        case 2: // House
            set ("tempo", 124);
            lane (0, e8, V::kick,      "x...x...x...x...", -1);  fx (0, "decay", 0.35f);
            lane (1, e8, V::clap,      "....x.......x...", -4);  fx (1, "rev", 0.2f);
            lane (2, e8, V::closedHat, "x.x.x.x.x.x.x.x.", -14);
            lane (3, e8, V::openHat,   "..x...x...x...x.", -8);
            lane (4, e8, V::rim,       "......x..x......", -10);
            lane (5, e8, V::cowbell,   "", -10); lane (6, e8, V::highConga, "...x.....x...x..", -12); lane (7, e8, V::maracas, "xxxxxxxxxxxxxxxx", -18);
            makeFill (1);
            break;
        case 3: // Techno
            set ("tempo", 132);
            lane (0, e8, V::kick,      "x...x...x...x...", -1);  fx (0, "drive", 0.4f); fx (0, "decay", 0.4f);
            lane (1, e8, V::clap,      "....x.......x...", -6);  fx (1, "rev", 0.35f);
            lane (2, e8, V::closedHat, "xoxoxoxoxoxoxoxo", -13);
            lane (3, e8, V::openHat,   "..x...x...x...x.", -10); fx (3, "dly", 0.2f);
            lane (4, e8, V::rim,       "...x..x....x..x.", -9);  fx (4, "dly", 0.35f);
            lane (5, e8, V::lowTom,    "x..x..x..x..", -10);     fx (5, "filter", -0.35f); fx (5, "reso", 0.6f);
            lane (6, gl, 4,            "x.x.x.x.x.x.x.x.", -16); lane (7, e8, V::cowbell, "");
            makeFill (1);
            break;
        case 4: // Trap
            set ("tempo", 140); set ("volume", -5.0f);
            lane (0, e8, V::kick,      "x......x..x.....", 0);   fx (0, "decay", 2.0f); fx (0, "tune", -5); fx (0, "drive", 0.35f);
            lane (1, e8, V::clap,      "........x.......", -3);  fx (1, "rev", 0.2f);
            lane (2, e8, V::closedHat, "x.x.x.xRx.x.x!x.", -10);
            lane (3, e8, V::openHat,   "..............x.", -12);
            lane (4, e8, V::snare,     "........x.......", -6);
            lane (5, e8, V::rim,       ".......o.....o..", -12);
            lane (6, e8, V::lowTom, ""); lane (7, e8, V::cowbell, "");
            makeFill (2);
            break;
        case 5: // Breakbeat
            set ("tempo", 105); set ("swing", 0.15f);
            lane (0, unit, V::kick,    "x.x.......xx....", -1);
            lane (1, unit, V::snare,   "....x..o.o..x..o", -3);
            lane (2, unit, V::closedHat, "x.x.x.x.x.x.x.x.", -10);
            lane (3, unit, V::openHat, ".......x........", -10);
            lane (4, e8, V::ride,      "x...x...x...x...", -16);
            lane (5, e8, V::clap, ""); lane (6, e8, V::lowTom, ""); lane (7, e8, V::cowbell, "");
            makeFill (1);
            break;
        case 6: // Drum & Bass
            set ("tempo", 172); set ("volume", -1.0f);
            lane (0, e8, V::kick,      "x.........x.....", -1);  fx (0, "decay", 0.4f);
            lane (1, unit, V::snare,   "....x.......x...", -2);  fx (1, "rev", 0.12f);
            lane (2, e8, V::closedHat, "x.xox.xox.xox.xo", -11);
            lane (3, unit, V::snare,   ".......o.o....o.", -12);
            lane (4, e8, V::ride,      "x...x...x...x...", -16);
            lane (5, e8, V::clap, ""); lane (6, e8, V::lowTom, ""); lane (7, e8, V::cowbell, "");
            makeFill (1);
            break;
        case 7: // Dream Pop Machine: a cheap rhythm box, slow, drenched in reverb
            set ("tempo", 72); set ("revSize", 0.85f);
            for (int l = 0; l < 4; ++l) set (laneParam (l, "rate"), 3.0f);   // 1/8 triplets: 12 per bar
            lane (0, unit, V::kick,    "x.....x.x...", -2);     fx (0, "rev", 0.2f);
            lane (1, unit, V::snare,   "...x.....x..", -4);     fx (1, "rev", 0.55f);
            lane (2, home, V::closedHat, "xxxxxxxxxxxx", -14);  fx (2, "rev", 0.3f);
            lane (3, home, V::maracas, "x..x..x..x..", -15);
            lane (4, home, V::clave,   "", -12); lane (5, e8, V::clap, ""); lane (6, e8, V::lowTom, ""); lane (7, e8, V::cowbell, "");
            for (int l = 0; l < 4; ++l) set (laneParam (l, "rate"), 3.0f);
            makeFill (1);
            break;
        case 8: // Reggaeton (dembow)
            set ("tempo", 95);
            lane (0, e8, V::kick,      "x...x...x...x...", -1);
            lane (1, e8, V::snare,     "...x..x....x..x.", -4);
            lane (2, e8, V::closedHat, "x.x.x.x.x.x.x.x.", -14);
            lane (3, e8, V::rim,       "...x..x....x..x.", -12);
            lane (4, e8, V::highConga, "..x....x..x....x", -12);
            lane (5, e8, V::clap, ""); lane (6, e8, V::lowTom, ""); lane (7, e8, V::cowbell, "");
            makeFill (1);
            break;
        case 9: // Afrobeat
            set ("tempo", 108); set ("swing", 0.2f);
            lane (0, unit, V::kick,    "x.....x...x.....", -1);
            lane (1, unit, V::snare,   "....x..o....x...", -4);
            lane (2, unit, V::closedHat, "xxxxxxxxxxxxxxxx", -14);
            lane (3, e8, V::cowbell,   "x.x.xx.x.x.xx.x.", -10);
            lane (4, unit, V::highConga, "..x..x..x.x..x..", -10);
            lane (5, unit, V::lowConga, "x....x.....x....", -10);
            lane (6, e8, V::maracas,   "x.xxx.xxx.xxx.xx", -16);
            lane (7, e8, V::clave,     "x..x...x..x.....", -12);
            makeFill (1);
            break;
        case 10: // IDM: Drill Machine - fast, broken, rolling
            set ("tempo", 165); set ("chaos", 0.3f); set ("mutate", 0.25f); set ("volume", -5.0f);
            lane (0, gl, 0,            "x..x......x.x...", -2);  fx (0, "drive", 0.3f);
            lane (1, gl, 1,            "....x..D....x.!.", -4);  fx (1, "crush", 0.2f);
            lane (2, gl, 4,            "xRxxRxDxxRxx!xRx", -12); fx (2, "pan", 0.3f);
            lane (3, gl, 2,            "..x.r..x.x..r..x", -9);  fx (3, "pan", -0.4f);
            lane (4, gl, 3,            ".......~.......d", -8);  fx (4, "dly", 0.25f);
            lane (5, gl, 12,           "x...<...x..<....", -10); fx (5, "rev", 0.2f);
            lane (6, gl, 11,           "...........D....", -12);
            lane (7, gl, 15,           "..x...x...x...x.", -14);
            makeFill (1);
            break;
        case 11: // IDM: Polymeter Maze - every lane a different length, so the pattern never quite repeats
            set ("tempo", 120); set ("mutate", 0.3f);
            lane (0, gl, 0,            "x.......x.....x.", -2);
            lane (1, gl, 1,            "....x......x..x", -5);
            lane (2, gl, 4,            "x.x.xx.x.xx.x", -12);
            lane (3, gl, 12,           "x..x.x..x.x", -10);    fx (3, "dly", 0.3f);
            lane (4, gl, 16,           "x.x..x.x.", -10);      fx (4, "tune", 7);
            lane (5, gl, 13,           "x..x..x", -12);        fx (5, "rev", 0.3f);
            lane (6, gl, 15,           "x.x.x", -14);
            lane (7, gl, 7,            "x..", -16);           fx (7, "rev", 0.4f);
            break;
        case 12: // IDM: Ambient Glitch - sparse, chance-based, washed out
            set ("tempo", 88); set ("revSize", 0.9f); set ("chaos", 0.15f); set ("dlyFb", 0.55f); set ("volume", 0.0f);
            lane (0, gl, 0,            "x.......p.......", -4);  fx (0, "decay", 1.2f);
            lane (1, gl, 14,           "p.p.p.p.p.p.p.p.", -10); fx (1, "rev", 0.5f);
            lane (2, gl, 2,            "..p...p..p...p..", -10); fx (2, "dly", 0.5f);
            lane (3, gl, 7,            "p...............", -12); fx (3, "rev", 0.7f);
            lane (4, gl, 8,            "....p.......p...", -14); fx (4, "dly", 0.6f); fx (4, "tune", 5);
            lane (5, gl, 6,            "..........<.....", -14); fx (5, "rev", 0.6f);
            lane (6, gl, 16,           "x..p..x..p..x..p", -14); fx (6, "pan", -0.5f);
            lane (7, gl, 15,           ".p...p...p...p..", -16); fx (7, "pan", 0.5f);
            break;
        case 13: // IDM: Braindance - a pitched blip bass line over chopped beats
            set ("tempo", 140); set ("chaos", 0.18f); set ("swing", 0.2f); set ("volume", -7.0f);
            lane (0, gl, 0,            "x..x..x...x..x..", -2);
            lane (1, unit, V::snare,   "....x..r....x.D.", -4);  fx (1, "crush", 0.3f);
            lane (2, gl, 4,            "xxRxxxRxxxRxxx!x", -13);
            lane (3, gl, 19,           "x.xx.x.xx.x.x.xx", -5);  fx (3, "filter", -0.2f); fx (3, "reso", 0.7f); fx (3, "drive", 0.4f);
            pitches (3, { 0, 0, 12, 0, 0, 7, 0, 10, 0, 0, 12, 0, 3, 0, 5, 7 });
            lane (4, gl, 12,           "..x.....x.....x.", -10); fx (4, "dly", 0.3f);
            lane (5, gl, 3,            "...............~", -8);
            lane (6, gl, 9,            "x...............", -10); lane (7, gl, 15, "", -14);
            makeFill (1);
            break;
        default: // Empty Kit
            lane (0, e8, V::kick, ""); lane (1, e8, V::snare, ""); lane (2, e8, V::closedHat, ""); lane (3, e8, V::openHat, "");
            lane (4, e8, V::clap, ""); lane (5, e8, V::rim, ""); lane (6, e8, V::lowTom, ""); lane (7, e8, V::cowbell, "");
            for (int l = 0; l < numLanes; ++l) set (laneParam (l, "steps"), 16.0f);
            break;
    }
    // lanes with no pattern still get a full bar of steps to draw in
    for (int l = 0; l < numLanes; ++l)
        if ((int) param (laneParam (l, "steps").toRawUTF8()) <= 1) set (laneParam (l, "steps"), (float) stepsForRate ((int) param (laneParam (l, "rate").toRawUTF8())));
    set ("pattern", 0.0f);
    rebuildSoundsNow();
}

} // namespace wis::daw
