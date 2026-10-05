#include "StudioSynth.h"
#include "InstrumentRefs.h"

namespace wis::daw
{

using namespace prm;

static const juce::StringArray waveNames { "Saw", "Square", "Triangle", "Sine", "Pulse 25%" };

static Layout synthLayout()
{
    Layout l;
    addChoice  (l, "wave1", "Osc 1", waveNames, 0);
    addChoice  (l, "wave2", "Osc 2", waveNames, 0);
    addFloat   (l, "semi2", "Osc 2 Pitch", -24.0f, 24.0f, 0.0f, "st", 0.0f, 0);
    addFloat   (l, "detune2", "Osc 2 Detune", -50.0f, 50.0f, 7.0f, "ct", 0.0f, 0);
    addPercent (l, "mix", "Osc Mix", 0.5f);
    addPercent (l, "sub", "Sub Osc", 0.0f);
    addPercent (l, "noise", "Noise", 0.0f);
    addChoice  (l, "ftype", "Filter", { "Low-pass 24", "Low-pass 12", "High-pass", "Band-pass" }, 0);
    addHz      (l, "cutoff", "Cutoff", 30.0f, 18000.0f, 4000.0f);
    addPercent (l, "reso", "Resonance", 0.2f);
    addFloat   (l, "envamt", "Filter Env", -1.0f, 1.0f, 0.3f, "", 0.0f, 2);
    addPercent (l, "keytrack", "Key Track", 0.5f);
    addPercent (l, "veltofilter", "Vel > Filter", 0.3f);
    addFloat   (l, "fA", "Filt Attack", 0.001f, 5.0f, 0.005f, "s", 0.3f, 3);
    addFloat   (l, "fD", "Filt Decay", 0.005f, 5.0f, 0.4f, "s", 0.5f, 3);
    addPercent (l, "fS", "Filt Sustain", 0.3f);
    addFloat   (l, "fR", "Filt Release", 0.005f, 8.0f, 0.4f, "s", 0.8f, 3);
    addFloat   (l, "aA", "Amp Attack", 0.001f, 5.0f, 0.003f, "s", 0.3f, 3);
    addFloat   (l, "aD", "Amp Decay", 0.005f, 5.0f, 0.3f, "s", 0.5f, 3);
    addPercent (l, "aS", "Amp Sustain", 0.8f);
    addFloat   (l, "aR", "Amp Release", 0.005f, 8.0f, 0.3f, "s", 0.8f, 3);
    addFloat   (l, "lforate", "LFO Rate", 0.05f, 20.0f, 5.0f, "Hz", 3.0f, 2);
    addFloat   (l, "lfopitch", "LFO > Pitch", 0.0f, 100.0f, 0.0f, "ct", 0.0f, 0);
    addPercent (l, "lfofilter", "LFO > Filter", 0.0f);
    addMs      (l, "glide", "Glide", 0.0f, 1000.0f, 0.0f);
    addBool    (l, "mono", "Mono / Legato", false);
    addDb      (l, "volume", "Volume", -24.0f, 6.0f, -6.0f);
    return l;
}

// ---- band-limited oscillator ---------------------------------------------------------------------------
static inline double polyBlep (double t, double dt)
{
    if (t < dt)        { t /= dt; return t + t - t * t - 1.0; }
    if (t > 1.0 - dt)  { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
    return 0.0;
}

struct Osc
{
    double phase = 0.0, tri = 0.0;
    float next (int wave, double inc)
    {
        double v;
        switch (wave)
        {
            case 0: v = 2.0 * phase - 1.0 - polyBlep (phase, inc); break;                              // saw
            case 1: case 4:                                                                             // square / pulse
            {
                const double pw = wave == 4 ? 0.25 : 0.5;
                v = (phase < pw ? 1.0 : -1.0) + polyBlep (phase, inc) - polyBlep (std::fmod (phase + 1.0 - pw, 1.0), inc);
                break;
            }
            case 2:                                                                                    // triangle = leaky-integrated square
            {
                double sq = (phase < 0.5 ? 1.0 : -1.0) + polyBlep (phase, inc) - polyBlep (std::fmod (phase + 0.5, 1.0), inc);
                tri = inc * 4.0 * sq + (1.0 - 0.0005) * tri;
                v = tri;
                break;
            }
            default: v = std::sin (juce::MathConstants<double>::twoPi * phase); break;
        }
        phase += inc;
        if (phase >= 1.0) phase -= 1.0;
        return (float) v;
    }
};

// ---- TPT state-variable filter (Zavalishin / Simper) ---------------------------------------------------
struct Svf
{
    float ic1 = 0, ic2 = 0, g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0;
    void set (float cutoff, float q, double sr)
    {
        g = (float) std::tan (juce::MathConstants<double>::pi * juce::jlimit (20.0, sr * 0.45, (double) cutoff) / sr);
        k = 1.0f / q;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    void reset() { ic1 = ic2 = 0; }
    // returns low, band, high via refs
    inline void tick (float x, float& lp, float& bp, float& hp)
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        lp = v2; bp = v1; hp = x - k * v1 - v2;
    }
};

struct SynthSound : public juce::SynthesiserSound
{
    bool appliesToNote (int) override { return true; }
    bool appliesToChannel (int) override { return true; }
};

class SynthVoice : public juce::SynthesiserVoice
{
public:
    explicit SynthVoice (StudioSynth& o) : owner (o) {}

    bool canPlaySound (juce::SynthesiserSound*) override { return true; }

    void startNote (int note, float velocity, juce::SynthesiserSound*, int pitchWheel) override
    {
        const auto& p = owner.getParams();
        const bool legato = p.mono && stolenWhileActive && ampEnv.isActive();
        stolenWhileActive = false;
        targetFreq = juce::MidiMessage::getMidiNoteInHertz (note);
        const double from = owner.lastNoteFrequency > 0.0 ? owner.lastNoteFrequency : targetFreq;
        currentFreq = p.glide > 0.5f ? from : targetFreq;
        // per 16-sample control tick
        glideCoeff = p.glide > 0.5f ? std::exp (-16.0 / (p.glide * 0.001 * getSampleRate())) : 0.0;
        owner.lastNoteFrequency = targetFreq;
        vel = velocity;
        noteNumber = note;
        bend = (pitchWheel - 8192) / 8192.0f * 2.0f;

        if (! legato)
        {
            ampEnv.setSampleRate (getSampleRate());
            filtEnv.setSampleRate (getSampleRate());
            ampEnv.setParameters ({ p.aA, p.aD, p.aS, p.aR });
            filtEnv.setParameters ({ p.fA, p.fD, p.fS, p.fR });
            ampEnv.noteOn();
            filtEnv.noteOn();
            f1.reset(); f2.reset();
            o1.phase = 0.0; o2.phase = juce::Random::getSystemRandom().nextDouble(); sub.phase = 0.0;
        }
    }

    void stopNote (float, bool allowTailOff) override
    {
        if (allowTailOff) { ampEnv.noteOff(); filtEnv.noteOff(); return; }

        // Hard stop. In mono mode this is usually the Synthesiser stealing the voice for the next
        // note: keep the envelopes running so startNote() can glide legato. If no note follows,
        // renderNextBlock() sees there's no current note and silences the voice.
        stolenWhileActive = owner.getParams().mono && ampEnv.isActive();
        if (! stolenWhileActive) { ampEnv.reset(); filtEnv.reset(); }
        clearCurrentNote();
    }

    void pitchWheelMoved (int v) override { bend = (v - 8192) / 8192.0f * 2.0f; }
    void controllerMoved (int, int) override {}

    void renderNextBlock (juce::AudioBuffer<float>& out, int start, int num) override
    {
        if (getCurrentlyPlayingNote() < 0) { ampEnv.reset(); filtEnv.reset(); stolenWhileActive = false; return; }
        if (! ampEnv.isActive()) { clearCurrentNote(); return; }

        const auto& p = owner.getParams();
        const double sr = getSampleRate();
        ampEnv.setParameters ({ p.aA, p.aD, p.aS, p.aR });
        filtEnv.setParameters ({ p.fA, p.fD, p.fS, p.fR });

        const float q = 0.6f + p.reso * 9.0f;
        const float gain = juce::Decibels::decibelsToGain (p.volume) * (0.35f + 0.65f * vel);
        const double lfoInc = p.lfoRate / sr;
        const float keyTrackRatio = std::pow (2.0f, (noteNumber - 60) / 12.0f * p.keyTrack);
        const double detuneRatio = std::pow (2.0, (p.semi2 + p.detune2 / 100.0) / 12.0);

        auto* L = out.getWritePointer (0);
        auto* R = out.getNumChannels() > 1 ? out.getWritePointer (1) : nullptr;

        for (int i = 0; i < num; ++i)
        {
            if ((i & 15) == 0)
            {
                // control-rate updates
                if (glideCoeff > 0.0) currentFreq = targetFreq + (currentFreq - targetFreq) * glideCoeff;
                else currentFreq = targetFreq;
                lfoPhase += lfoInc * 16.0;
                if (lfoPhase >= 1.0) lfoPhase -= 1.0;
                const float lfo = (float) std::sin (juce::MathConstants<double>::twoPi * lfoPhase);
                const float fe = filtEnv.getNextSample();
                for (int s = 1; s < 16; ++s) filtEnv.getNextSample();
                const float octaves = p.envAmt * fe * 6.0f + p.lfoFilter * lfo * 2.0f + p.velToFilter * (vel - 0.5f) * 2.0f;
                const float cutoff = p.cutoff * keyTrackRatio * std::pow (2.0f, octaves);
                f1.set (cutoff, q, sr);
                f2.set (cutoff, 0.54f + p.reso * 0.4f, sr);
                pitchMul = std::pow (2.0, (bend + p.lfoPitch / 100.0 * lfo) / 12.0);
            }

            const double f = currentFreq * pitchMul;
            float s = o1.next (p.wave1, f / sr) * (1.0f - p.mix)
                    + o2.next (p.wave2, f * detuneRatio / sr) * p.mix;
            if (p.sub > 0.0f)   s += sub.next (1, f * 0.5 / sr) * p.sub * 0.6f;
            if (p.noise > 0.0f) s += (rng.nextFloat() * 2.0f - 1.0f) * p.noise * 0.5f;

            float lp, bp, hp, y;
            f1.tick (s, lp, bp, hp);
            switch (p.filterType)
            {
                case 1:  y = lp; break;
                case 2:  y = hp; break;
                case 3:  y = bp; break;
                default: { float lp2, bp2, hp2; f2.tick (lp, lp2, bp2, hp2); y = lp2; break; }
            }

            const float a = ampEnv.getNextSample();
            const float v = std::tanh (y * 0.8f) * a * gain;
            L[start + i] += v;
            if (R != nullptr) R[start + i] += v;
        }

        if (! ampEnv.isActive())
            clearCurrentNote();
    }

private:
    StudioSynth& owner;
    Osc o1, o2, sub;
    Svf f1, f2;
    juce::ADSR ampEnv, filtEnv;
    juce::Random rng;
    double targetFreq = 440, currentFreq = 440, glideCoeff = 0, lfoPhase = 0, pitchMul = 1;
    float vel = 1, bend = 0;
    int noteNumber = 60;
    bool stolenWhileActive = false;
};

// ---- processor ----------------------------------------------------------------------------------------------
StudioSynth::StudioSynth() : BuiltinProcessor ("synth", "Studio Synth", true, synthLayout())
{
    synth.addSound (new SynthSound());
    for (int i = 0; i < voiceCount; ++i)
        synth.addVoice (new SynthVoice (*this));
    synth.setNoteStealingEnabled (true);
    monoSynth.addSound (new SynthSound());
    monoSynth.addVoice (new SynthVoice (*this));
    monoSynth.setNoteStealingEnabled (true);
    loadProgram (0);
}

void StudioSynth::prepareToPlay (double sr, int)
{
    synth.setCurrentPlaybackSampleRate (sr);
    monoSynth.setCurrentPlaybackSampleRate (sr);
    updateSnapshot();
}

void StudioSynth::updateSnapshot()
{
    auto& p = snapshot;
    p.wave1 = (int) param ("wave1");  p.wave2 = (int) param ("wave2");
    p.semi2 = param ("semi2");        p.detune2 = param ("detune2");
    p.mix = param ("mix");            p.sub = param ("sub");          p.noise = param ("noise");
    p.filterType = (int) param ("ftype");
    p.cutoff = param ("cutoff");      p.reso = param ("reso");        p.envAmt = param ("envamt");
    p.keyTrack = param ("keytrack");  p.velToFilter = param ("veltofilter");
    p.fA = param ("fA"); p.fD = param ("fD"); p.fS = param ("fS"); p.fR = param ("fR");
    p.aA = param ("aA"); p.aD = param ("aD"); p.aS = param ("aS"); p.aR = param ("aR");
    p.lfoRate = param ("lforate");    p.lfoPitch = param ("lfopitch"); p.lfoFilter = param ("lfofilter");
    p.glide = param ("glide");        p.mono = param ("mono") > 0.5f;  p.volume = param ("volume");
}

void StudioSynth::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals nd;
    updateSnapshot();

    if (snapshot.mono != lastMono)
    {
        lastMono = snapshot.mono;
        synth.allNotesOff (0, false);
        monoSynth.allNotesOff (0, false);
    }

    buffer.clear();
    (snapshot.mono ? monoSynth : synth).renderNextBlock (buffer, midi, 0, buffer.getNumSamples());
}

juce::StringArray StudioSynth::getProgramNames()
{
    return { "Init Saw", "Warm Analog Pad", "Fat Bass", "Sub Bass", "Classic Lead", "Soft Pluck", "Brass Stab",
             "String Machine", "Bell Keys", "Wobble Bass", "Dreamy Sweep", "Square Chip Lead", "Acid Line", "Organ-ish",
             "Glide Lead", "Noise Riser" };
}

void StudioSynth::loadProgram (int index)
{
    struct P
    {
        int w1, w2; float semi, det, mix, sub, noise; int ft; float cut, res, env, kt, vtf;
        float fA, fD, fS, fR, aA, aD, aS, aR, lr, lp, lf, glide; bool mono; float vol;
    };
    static const P presets[] = {
        //  w1 w2 semi det  mix  sub  noi ft  cut     res   env   kt   vtf   fA     fD    fS    fR    aA     aD    aS   aR    lr   lp  lf    gl  mono vol
        {  0, 0,  0,   7, 0.5f, 0.0f, 0, 0, 4000, 0.2f, 0.3f, 0.5f, 0.3f, 0.005f, 0.4f, 0.3f, 0.4f, 0.003f, 0.3f, 0.8f, 0.3f, 5,   0,  0,    0,  false, -6 },
        {  0, 0,  0,  12, 0.5f, 0.0f, 0.03f,0,1200,0.15f,0.25f,0.3f, 0.1f, 1.2f,   2.0f, 0.6f, 2.5f, 1.2f,  1.5f, 0.9f, 2.8f, 0.3f,4,  0.15f,0,  false, -9 },
        {  0, 1,  -12, 5, 0.4f, 0.5f, 0, 0, 500,  0.3f, 0.45f,0.3f, 0.3f, 0.002f, 0.25f,0.2f, 0.15f,0.002f, 0.3f, 0.9f, 0.12f,5,   0,  0,    0,  true,  -6 },
        {  3, 2,  -12, 0, 0.3f, 0.6f, 0, 1, 400,  0.0f, 0.1f, 0.2f, 0.1f, 0.002f, 0.2f, 0.5f, 0.1f, 0.003f, 0.2f, 1.0f, 0.08f,5,   0,  0,    0,  true,  -4 },
        {  0, 1,  0,   9, 0.45f,0.2f, 0, 0, 2500, 0.35f,0.35f,0.6f, 0.4f, 0.003f, 0.35f,0.4f, 0.3f, 0.003f, 0.2f, 0.9f, 0.25f,5.5f,12, 0,    40, true,  -8 },
        {  0, 4,  12,  3, 0.3f, 0.0f, 0, 0, 900,  0.25f,0.6f, 0.6f, 0.5f, 0.001f, 0.18f,0.0f, 0.2f, 0.001f, 0.45f,0.0f, 0.35f,5,   0,  0,    0,  false, -6 },
        {  0, 0,  0,  10, 0.5f, 0.0f, 0, 0, 900,  0.15f,0.55f,0.4f, 0.4f, 0.04f,  0.3f, 0.35f,0.25f,0.02f,  0.25f,0.8f, 0.2f, 5,   0,  0,    0,  false, -8 },
        {  0, 0,  12, 14, 0.5f, 0.0f, 0, 1, 3000, 0.05f,0.0f, 0.4f, 0.0f, 0.4f,   1.0f, 1.0f, 1.0f, 0.4f,   0.8f, 0.9f, 1.2f, 5.5f,6,  0,    0,  false, -9 },
        {  3, 3,  19,  0, 0.35f,0.0f, 0, 0, 6000, 0.0f, 0.2f, 0.5f, 0.2f, 0.001f, 0.6f, 0.2f, 0.8f, 0.001f, 1.6f, 0.0f, 1.4f, 5,   0,  0,    0,  false, -5 },
        {  0, 1,  -12, 4, 0.5f, 0.4f, 0, 0, 300,  0.55f,0.0f, 0.2f, 0.0f, 0.005f, 0.3f, 0.5f, 0.2f, 0.003f, 0.3f, 1.0f, 0.15f,3,   0,  0.9f, 0,  true,  -8 },
        {  4, 0,  7,  12, 0.5f, 0.0f, 0.05f,0,600,0.5f,  0.2f, 0.4f, 0.0f, 2.5f,   3.0f, 0.5f, 3.0f, 1.5f,   2.0f, 0.8f, 3.5f, 0.1f,8,  0.6f, 0,  false, -10 },
        {  1, 4,  12,  0, 0.4f, 0.0f, 0, 1, 9000, 0.0f, 0.0f, 0.5f, 0.0f, 0.001f, 0.2f, 1.0f, 0.1f, 0.001f, 0.1f, 0.8f, 0.1f, 6,   15, 0,    0,  true,  -9 },
        {  0, 0,  0,   0, 0.0f, 0.0f, 0, 0, 350,  0.8f, 0.7f, 0.3f, 0.6f, 0.001f, 0.2f, 0.0f, 0.15f,0.001f, 0.25f,0.8f, 0.08f,5,   0,  0,    60, true,  -9 },
        {  3, 3,  12,  0, 0.5f, 0.3f, 0, 1, 7000, 0.0f, 0.0f, 0.5f, 0.0f, 0.001f, 0.1f, 1.0f, 0.1f, 0.002f, 0.1f, 1.0f, 0.06f,6.5f,8,  0,    0,  false, -8 },
        {  0, 1,  0,  12, 0.5f, 0.0f, 0, 0, 3500, 0.3f, 0.2f, 0.5f, 0.3f, 0.01f,  0.5f, 0.6f, 0.4f, 0.005f, 0.2f, 0.9f, 0.3f, 5.5f,10, 0,    180,true,  -8 },
        {  0, 0,  0,   0, 0.0f, 0.0f, 1.0f,2,200, 0.6f,  1.0f, 0.0f, 0.0f, 4.0f,   1.0f, 1.0f, 2.0f, 2.0f,   1.0f, 1.0f, 2.0f, 0.2f,0,  0.3f, 0,  false, -14 },
    };
    if (! juce::isPositiveAndBelow (index, (int) std::size (presets))) return;
    const auto& p = presets[index];
    currentProgram = index;
    setParam ("wave1", (float) p.w1); setParam ("wave2", (float) p.w2); setParam ("semi2", p.semi); setParam ("detune2", p.det);
    setParam ("mix", p.mix); setParam ("sub", p.sub); setParam ("noise", p.noise); setParam ("ftype", (float) p.ft);
    setParam ("cutoff", p.cut); setParam ("reso", p.res); setParam ("envamt", p.env); setParam ("keytrack", p.kt);
    setParam ("veltofilter", p.vtf); setParam ("fA", p.fA); setParam ("fD", p.fD); setParam ("fS", p.fS); setParam ("fR", p.fR);
    setParam ("aA", p.aA); setParam ("aD", p.aD); setParam ("aS", p.aS); setParam ("aR", p.aR);
    setParam ("lforate", p.lr); setParam ("lfopitch", p.lp); setParam ("lfofilter", p.lf); setParam ("glide", p.glide);
    setParam ("mono", p.mono ? 1.0f : 0.0f); setParam ("volume", p.vol);
}

PluginRef synthRef (int presetIndex)
{
    StudioSynth tmp;
    tmp.setCurrentProgram (presetIndex);
    PluginRef r = builtinRef ("synth");
    r.name = tmp.getProgramName (presetIndex);
    r.state = encodeState (tmp);
    return r;
}

} // namespace wis::daw
