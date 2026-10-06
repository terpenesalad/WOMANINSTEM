#include "Sampler.h"
#include "DrumSynth.h"
#include "Separation/AudioFileLoader.h"

namespace wis::daw
{

// =====================================================================================================
//  Sample data
// =====================================================================================================
std::unique_ptr<SampleData> SampleData::fromFile (const juce::File& f, double maxSeconds, juce::String& error)
{
    std::unique_ptr<juce::AudioFormatReader> reader (sharedFormatManager().createReaderFor (f));
    if (reader == nullptr || reader->lengthInSamples <= 0)
    {
        error = "Couldn't read " + f.getFileName();
        return {};
    }
    const int len = (int) juce::jmin (reader->lengthInSamples, (juce::int64) (reader->sampleRate * maxSeconds));
    juce::AudioBuffer<float> b (2, len);
    if (reader->numChannels == 1)
    {
        reader->read (&b, 0, len, 0, true, false);
        b.copyFrom (1, 0, b, 0, 0, len);
    }
    else reader->read (&b, 0, len, 0, true, true);
    auto d = fromBuffer (std::move (b), reader->sampleRate, f.getFileNameWithoutExtension());
    return d;
}

std::unique_ptr<SampleData> SampleData::fromBuffer (juce::AudioBuffer<float> b, double sr, const juce::String& name)
{
    auto d = std::make_unique<SampleData>();
    if (b.getNumChannels() == 1)
    {
        juce::AudioBuffer<float> st (2, b.getNumSamples());
        st.copyFrom (0, 0, b, 0, 0, b.getNumSamples());
        st.copyFrom (1, 0, b, 0, 0, b.getNumSamples());
        d->buffer = std::move (st);
    }
    else d->buffer = std::move (b);
    d->sampleRate = sr;
    d->name = name;
    d->analyse();
    return d;
}

void SampleData::analyse()
{
    const int n = buffer.getNumSamples();
    peaks.assign ((size_t) (n / 512 + 1), 0.0f);
    for (int i = 0; i < n; ++i)
    {
        const float v = juce::jmax (std::abs (buffer.getSample (0, i)), std::abs (buffer.getSample (1, i)));
        auto& p = peaks[(size_t) (i / 512)];
        p = juce::jmax (p, v);
    }

    // onsets: rises in short-term energy (good for drums and plucked parts)
    onsets.clear();
    const int hop = juce::jmax (64, (int) (sampleRate * 0.005)), win = hop * 2;
    std::vector<float> energy;
    for (int start = 0; start + win < n; start += hop)
    {
        float e = 0.0f;
        for (int i = start; i < start + win; ++i)
        {
            const float s = 0.5f * (buffer.getSample (0, i) + buffer.getSample (1, i));
            e += s * s;
        }
        energy.push_back (std::log1p (e * 1000.0f));
    }
    std::vector<float> flux (energy.size(), 0.0f);
    float maxFlux = 1.0e-6f;
    for (size_t i = 1; i < energy.size(); ++i)
    {
        flux[i] = juce::jmax (0.0f, energy[i] - energy[i - 1]);
        maxFlux = juce::jmax (maxFlux, flux[i]);
    }
    const int minGap = (int) (0.06 * sampleRate / hop);   // 60 ms between onsets
    int lastOnset = -minGap;
    onsets.push_back ({ 0, 1.0f });
    for (size_t i = 1; i + 1 < flux.size(); ++i)
    {
        if (flux[i] < flux[i - 1] || flux[i] < flux[i + 1]) continue;
        const float strength = flux[i] / maxFlux;
        if (strength < 0.05f || (int) i - lastOnset < minGap) continue;
        const int pos = (int) i * hop;
        if (pos < hop * 4) continue;
        onsets.push_back ({ pos, strength });
        lastOnset = (int) i;
    }
}

void SampleSlot::set (std::unique_ptr<SampleData> d)
{
    auto* raw = d.get();
    history.push_back (std::move (d));
    current = raw;
    while (history.size() > 3) history.erase (history.begin());
}

// ---- shared pieces ---------------------------------------------------------------------------------------
namespace
{
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
    };

    /** 4-point Hermite interpolation. */
    inline float interp (const float* d, int len, double pos)
    {
        const int i = (int) std::floor (pos);
        const float f = (float) (pos - i);
        auto at = [d, len] (int k) { return k >= 0 && k < len ? d[k] : 0.0f; };
        const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
        const float c = (x1 - xm1) * 0.5f;
        const float v = x0 - x1;
        const float w = c + v;
        const float a = w + v + (x2 - x0) * 0.5f;
        const float b = w + a;
        return ((a * f - b) * f + c) * f + x0;
    }
}

// =====================================================================================================
//  Sampler
// =====================================================================================================
std::function<juce::AudioProcessorEditor* (Sampler&)> Sampler::editorFactory;

struct Sampler::Voice
{
    bool active = false, released = false;
    int note = 60;
    float vel = 1.0f;
    double pos = 0.0, inc = 1.0, targetInc = 1.0;
    int from = 0, to = 0, loopStart = 0, loopEnd = 0;
    bool loop = false, reverse = false, gateless = false;
    juce::ADSR amp;
    Svf filter;
    float filterEnv = 1.0f;
    float fade = 1.0f;   // quick fade for voice stealing
    bool stealing = false;
};

static prm::Layout samplerLayout()
{
    prm::Layout l;
    prm::addChoice (l, "mode", "Mode", { "Classic", "One Shot", "Slice" }, 0);
    prm::addFloat (l, "root", "Root Key", 24.0f, 96.0f, 60.0f, "", 0.0f, 0);
    prm::addFloat (l, "tune", "Tune", -24.0f, 24.0f, 0.0f, "st", 0.0f, 0);
    prm::addFloat (l, "fine", "Fine", -100.0f, 100.0f, 0.0f, "ct", 0.0f, 0);
    prm::addDb (l, "gain", "Gain", -30.0f, 12.0f, 0.0f);
    prm::addMs (l, "attack", "Attack", 0.0f, 3000.0f, 2.0f);
    prm::addMs (l, "decay", "Decay", 1.0f, 5000.0f, 400.0f);
    prm::addPercent (l, "sustain", "Sustain", 1.0f);
    prm::addMs (l, "release", "Release", 1.0f, 8000.0f, 200.0f);
    prm::addHz (l, "cutoff", "Cutoff", 40.0f, 20000.0f, 20000.0f);
    prm::addFloat (l, "reso", "Resonance", 0.0f, 1.0f, 0.1f, "", 0.0f, 2);
    prm::addPercent (l, "fenv", "Filter Env", 0.0f);
    prm::addPercent (l, "velsens", "Velocity", 0.7f);
    prm::addBool (l, "loop", "Loop", false);
    prm::addPercent (l, "loopStart", "Loop Start", 0.25f);
    prm::addPercent (l, "loopEnd", "Loop End", 0.9f);
    prm::addPercent (l, "xfade", "Loop Crossfade", 0.1f);
    prm::addPercent (l, "start", "Start", 0.0f);
    prm::addPercent (l, "end", "End", 1.0f);
    prm::addBool (l, "reverse", "Reverse", false);
    prm::addChoice (l, "voicing", "Voices", { "Poly", "Mono", "Legato" }, 0);
    prm::addMs (l, "glide", "Glide", 0.0f, 1000.0f, 0.0f);
    prm::addChoice (l, "slices", "Slices", { "Transients", "4", "8", "16", "32" }, 0);
    prm::addPercent (l, "sens", "Slice Sensitivity", 0.5f);
    return l;
}

Sampler::Sampler() : BuiltinProcessor ("sampler", "Sampler", true, samplerLayout()), voices (std::make_unique<std::array<Voice, 32>>()) {}
Sampler::~Sampler() = default;

juce::String Sampler::loadFile (const juce::File& f)
{
    juce::String error;
    auto d = SampleData::fromFile (f, 600.0, error);
    if (d == nullptr) return error;
    d->ref = makeFileRef (f);
    suspendProcessing (true);
    sample.set (std::move (d));
    suspendProcessing (false);
    return {};
}

void Sampler::loadBuffer (juce::AudioBuffer<float> b, double rate, const juce::String& name)
{
    sample.set (SampleData::fromBuffer (std::move (b), rate, name));
}

void Sampler::saveExtraState (juce::ValueTree& v)
{
    if (auto* s = sample.get()) { v.setProperty ("file", s->ref, nullptr); v.setProperty ("name", s->name, nullptr); }
}

void Sampler::loadExtraState (const juce::ValueTree& v)
{
    const auto ref = v["file"].toString();
    if (ref.isEmpty()) return;
    juce::String error;
    if (auto d = SampleData::fromFile (resolveFileRef (ref), 600.0, error))
    {
        d->ref = ref;
        sample.set (std::move (d));
    }
}

std::vector<int> Sampler::slicesFor (const SampleData& s) const
{
    std::vector<int> out;
    const int choice = (int) param ("slices");
    const int len = s.length();
    const int start = (int) (param ("start") * len), end = juce::jmax (start + 1, (int) (param ("end") * len));
    if (choice == 0)
    {
        const float threshold = 0.6f * (1.0f - param ("sens")) + 0.03f;
        out.push_back (start);
        for (auto& [pos, strength] : s.onsets)
            if (pos > start + 64 && pos < end && strength >= threshold && (int) out.size() < 64) out.push_back (pos);
    }
    else
    {
        const int count = choice == 1 ? 4 : choice == 2 ? 8 : choice == 3 ? 16 : 32;
        for (int i = 0; i < count; ++i) out.push_back (start + (int) ((juce::int64) (end - start) * i / count));
    }
    out.push_back (end);
    return out;
}

std::vector<int> Sampler::currentSlices() const
{
    if (auto* s = sample.get()) return slicesFor (*s);
    return {};
}

void Sampler::prepareToPlay (double rate, int)
{
    sr = rate;
    for (auto& v : *voices) v.active = false;
}

void Sampler::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals nd;
    buffer.clear();
    auto* s = sample.get();
    if (s == nullptr || s->length() < 4) return;

    const int mode = (int) param ("mode");
    const int voicing = (int) param ("voicing");
    const int len = s->length();
    const float rootNote = param ("root");
    const double tuneSemis = param ("tune") + param ("fine") / 100.0;
    const double rateRatio = s->sampleRate / sr;
    const bool reverse = param ("reverse") > 0.5f;
    const float velSens = param ("velsens");
    const float fenv = param ("fenv");
    const float cutoff = param ("cutoff");
    const float q = 0.5f + param ("reso") * 8.0f;
    const float gain = juce::Decibels::decibelsToGain (param ("gain"));
    const double glideSec = param ("glide") * 0.001;
    juce::ADSR::Parameters ap { param ("attack") * 0.001f, param ("decay") * 0.001f, param ("sustain"), param ("release") * 0.001f };
    std::vector<int> slices;   // worked out on the first slice note of the block
    auto startVoice = [&] (int note, float velocity)
    {
        Voice* v = nullptr;
        const bool mono = voicing != 0;
        if (mono)
        {
            for (auto& x : *voices) if (x.active && ! x.released) { v = &x; break; }
            if (v != nullptr && voicing == 2)
            {
                // legato: glide to the new pitch without retriggering
                v->note = note;
                v->targetInc = rateRatio * std::pow (2.0, (note - rootNote + tuneSemis) / 12.0);
                if (glideSec <= 0.0) v->inc = v->targetInc;
                return;
            }
            for (auto& x : *voices) if (x.active) { x.stealing = true; }
        }
        for (auto& x : *voices) if (! x.active) { v = &x; break; }
        if (v == nullptr)
        {
            v = &(*voices)[0];
            for (auto& x : *voices) if (x.released) { v = &x; break; }
        }
        *v = Voice();
        v->active = true;
        v->note = note;
        v->vel = 1.0f - velSens + velSens * velocity;
        v->reverse = reverse;
        v->amp.setSampleRate (sr);
        v->amp.setParameters (ap);
        v->amp.noteOn();
        v->filterEnv = 1.0f;

        const int start = (int) (param ("start") * len), end = juce::jmax (start + 1, (int) (param ("end") * len));
        if (mode == slice)
        {
            if (slices.empty()) slices = slicesFor (*s);
            const int idx = note - firstSliceNote;
            if (idx < 0 || idx + 1 >= (int) slices.size()) { v->active = false; return; }
            v->from = slices[(size_t) idx];
            v->to = slices[(size_t) idx + 1];
            v->targetInc = v->inc = rateRatio * std::pow (2.0, tuneSemis / 12.0);
            v->gateless = true;
        }
        else
        {
            v->from = start; v->to = end;
            v->targetInc = rateRatio * std::pow (2.0, (note - rootNote + tuneSemis) / 12.0);
            v->inc = (voicing != 0 && glideSec > 0.0 && glideFrom > 0.0f) ? (double) glideFrom : v->targetInc;
            v->gateless = mode == oneShot;
            v->loop = mode == classic && param ("loop") > 0.5f;
            v->loopStart = juce::jlimit (v->from, v->to - 2, (int) (param ("loopStart") * len));
            v->loopEnd = juce::jlimit (v->loopStart + 2, v->to, (int) (param ("loopEnd") * len));
        }
        v->pos = v->reverse ? (double) v->to - 1.0 : (double) v->from;
        glideFrom = (float) v->targetInc;
    };

    const float* srcL = s->buffer.getReadPointer (0);
    const float* srcR = s->buffer.getReadPointer (1);
    const int n = buffer.getNumSamples();
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (buffer.getNumChannels() > 1 ? 1 : 0);
    const double glideCoef = glideSec > 0.0 ? 1.0 - std::exp (-1.0 / (glideSec * sr)) : 1.0;
    const float xfadeFrac = param ("xfade");

    auto renderUntil = [&] (int from, int until)
    {
        for (auto& v : *voices)
        {
            if (! v.active) continue;
            const int loopLen = v.loopEnd - v.loopStart;
            const int xf = (int) (loopLen * xfadeFrac * 0.5f);
            for (int i = from; i < until && v.active; ++i)
            {
                v.inc += (v.targetInc - v.inc) * glideCoef;
                double p = v.pos;
                float l = interp (srcL, len, p), r = interp (srcR, len, p);
                // loop crossfade: blend in audio from before the loop start as we approach the loop end
                if (v.loop && ! v.released && ! v.reverse && xf > 0 && p > v.loopEnd - xf)
                {
                    const float t = (float) ((p - (v.loopEnd - xf)) / xf);
                    const double q2 = p - loopLen;
                    l = l * (1.0f - t) + interp (srcL, len, q2) * t;
                    r = r * (1.0f - t) + interp (srcR, len, q2) * t;
                }
                float env = v.amp.getNextSample();
                if (v.stealing) { v.fade -= 1.0f / (0.004f * (float) sr); if (v.fade <= 0.0f) { v.active = false; break; } env *= v.fade; }
                float cut = cutoff;
                if (fenv > 0.0f) { cut = cutoff * (1.0f + fenv * 8.0f * v.filterEnv); v.filterEnv *= 0.9995f; }
                float gl = l * env * v.vel * gain, gr = r * env * v.vel * gain;
                if (cut < 19000.0f)
                {
                    v.filter.set (cut, q, (float) sr);
                    const float m = v.filter.lp (0.5f * (gl + gr));
                    const float side = 0.5f * (gl - gr);
                    gl = m + side * 0.5f; gr = m - side * 0.5f;
                }
                L[i] += gl; R[i] += gr;

                v.pos += v.reverse ? -v.inc : v.inc;
                if (v.loop && ! v.released && ! v.reverse && v.pos >= v.loopEnd) v.pos -= loopLen;
                if (v.loop && ! v.released && v.reverse && v.pos < v.loopStart) v.pos += loopLen;
                if (v.pos >= v.to || v.pos < v.from) { v.active = false; break; }
                if (! v.amp.isActive()) { v.active = false; break; }
            }
        }
    };

    int pos = 0;
    for (const auto meta : midi)
    {
        const int at = juce::jlimit (0, n, meta.samplePosition);
        if (at > pos) { renderUntil (pos, at); pos = at; }
        const auto m = meta.getMessage();
        if (m.isNoteOn()) startVoice (m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isNoteOff())
        {
            for (auto& v : *voices)
                if (v.active && v.note == m.getNoteNumber() && ! v.gateless && ! v.released) { v.released = true; v.amp.noteOff(); }
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            for (auto& v : *voices) if (v.active) { v.released = true; v.amp.noteOff(); }
    }
    if (pos < n) renderUntil (pos, n);

    float shown = -1.0f;
    for (auto& v : *voices) if (v.active) shown = (float) (v.pos / len);
    playPosition = shown;
}

// =====================================================================================================
//  Drum Pads
// =====================================================================================================
std::function<juce::AudioProcessorEditor* (DrumPads&)> DrumPads::editorFactory;

struct DrumPads::Voice
{
    bool active = false, released = false;
    int pad = 0;
    double pos = 0.0, inc = 1.0;
    float vel = 1.0f, env = 1.0f, decayCoef = 1.0f, fadeOut = 0.0f;
    float gl = 1.0f, gr = 1.0f;
    bool reverse = false;
    Svf filter;
};

static prm::Layout drumPadsLayout()
{
    prm::Layout l;
    prm::addDb (l, "volume", "Volume", -30.0f, 6.0f, -2.0f);
    for (int p = 0; p < DrumPads::numPads; ++p)
    {
        const auto name = "Pad " + juce::String (p + 1) + " ";
        prm::addFloat (l, DrumPads::padParam (p, "tune"), name + "Tune", -24.0f, 24.0f, 0.0f, "st", 0.0f, 1);
        prm::addDb (l, DrumPads::padParam (p, "gain"), name + "Gain", -30.0f, 12.0f, 0.0f);
        prm::addFloat (l, DrumPads::padParam (p, "pan"), name + "Pan", -1.0f, 1.0f, 0.0f, "", 0.0f, 2);
        prm::addFloat (l, DrumPads::padParam (p, "decay"), name + "Decay", 0.02f, 5.0f, 5.0f, "s", 0.5f, 2);
        prm::addHz (l, DrumPads::padParam (p, "cutoff"), name + "Cutoff", 60.0f, 20000.0f, 20000.0f);
        prm::addBool (l, DrumPads::padParam (p, "reverse"), name + "Reverse", false);
        prm::addChoice (l, DrumPads::padParam (p, "choke"), name + "Choke Group", { "None", "1", "2", "3", "4" }, (p == 6 || p == 8 || p == 10) ? 1 : 0);
    }
    return l;
}

DrumPads::DrumPads() : BuiltinProcessor ("drumpads", "Drum Pads", true, drumPadsLayout()), voices (std::make_unique<std::array<Voice, 32>>())
{
    loadSynthKit (DrumSynth::eightOhEight);
}

DrumPads::~DrumPads() = default;

juce::StringArray DrumPads::getProgramNames()
{
    auto names = DrumSynth::kitNames();
    for (auto& n : names) n = n + " (synthesized)";
    return names;
}

void DrumPads::loadProgram (int index) { loadSynthKit (index); }

static DrumSynth::Voice synthVoiceForPad (int pad)
{
    // pads are the General MIDI drum notes 36-51
    static const DrumSynth::Voice v[] = { DrumSynth::kick, DrumSynth::rim, DrumSynth::snare, DrumSynth::clap, DrumSynth::snare,
                                          DrumSynth::lowTom, DrumSynth::closedHat, DrumSynth::lowTom, DrumSynth::pedalHat, DrumSynth::midTom,
                                          DrumSynth::openHat, DrumSynth::midTom, DrumSynth::highTom, DrumSynth::crash, DrumSynth::highTom, DrumSynth::ride };
    return v[pad];
}

void DrumPads::loadSynthKit (int kit)
{
    synthKit = juce::jlimit (0, (int) DrumSynth::numKits - 1, kit);
    const double rate = sr > 0 ? sr : 48000.0;
    for (int p = 0; p < numPads; ++p)
    {
        auto v = synthVoiceForPad (p);
        // the doubled GM notes get a different kit for variety (e.g. a second snare)
        const int k = (p == 4 || p == 7 || p == 11 || p == 14) ? (synthKit + 1) % DrumSynth::numKits : synthKit;
        auto d = SampleData::fromBuffer (DrumSynth::renderHit (v, k, rate), rate, DrumSynth::voiceName (v));
        pads[(size_t) p].set (std::move (d));
        padSources[(size_t) p] = "synth:" + juce::String ((int) v) + ":" + juce::String (k);
    }
}

juce::String DrumPads::loadPadFile (int pad, const juce::File& f)
{
    if (! juce::isPositiveAndBelow (pad, numPads)) return "No such pad";
    juce::String error;
    auto d = SampleData::fromFile (f, 30.0, error);
    if (d == nullptr) return error;
    d->ref = makeFileRef (f);
    padSources[(size_t) pad] = d->ref;
    pads[(size_t) pad].set (std::move (d));
    return {};
}

juce::String DrumPads::padName (int pad) const
{
    if (auto* s = pads[(size_t) pad].get()) return s->name;
    return "Empty";
}

void DrumPads::saveExtraState (juce::ValueTree& v)
{
    v.setProperty ("kit", synthKit, nullptr);
    for (int p = 0; p < numPads; ++p)
        v.setProperty ("pad" + juce::String (p), padSources[(size_t) p], nullptr);
}

void DrumPads::loadExtraState (const juce::ValueTree& v)
{
    loadSynthKit ((int) v.getProperty ("kit", 2));
    for (int p = 0; p < numPads; ++p)
    {
        const auto src = v["pad" + juce::String (p)].toString();
        if (src.isEmpty() || src.startsWith ("synth:"))
        {
            if (src.startsWith ("synth:"))
            {
                auto parts = juce::StringArray::fromTokens (src, ":", {});
                const auto voice = (DrumSynth::Voice) juce::jlimit (0, (int) DrumSynth::numVoices - 1, parts[1].getIntValue());
                const double rate = sr > 0 ? sr : 48000.0;
                pads[(size_t) p].set (SampleData::fromBuffer (DrumSynth::renderHit (voice, parts[2].getIntValue(), rate), rate, DrumSynth::voiceName (voice)));
                padSources[(size_t) p] = src;
            }
            continue;
        }
        juce::String error;
        if (auto d = SampleData::fromFile (resolveFileRef (src), 30.0, error))
        {
            d->ref = src;
            padSources[(size_t) p] = src;
            pads[(size_t) p].set (std::move (d));
        }
    }
}

void DrumPads::prepareToPlay (double rate, int)
{
    sr = rate;   // pads keep their own sample rate; playback speed accounts for it
    for (auto& v : *voices) v.active = false;
}

void DrumPads::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals nd;
    buffer.clear();
    const int n = buffer.getNumSamples();
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (buffer.getNumChannels() > 1 ? 1 : 0);

    auto trigger = [&] (int pad, float velocity)
    {
        auto* s = pads[(size_t) pad].get();
        if (s == nullptr) return;
        lastHit = pad;
        const int choke = (int) param (padParam (pad, "choke").toRawUTF8());
        for (auto& v : *voices)
            if (v.active && (v.pad == pad || (choke > 0 && (int) param (padParam (v.pad, "choke").toRawUTF8()) == choke)))
                v.fadeOut = 1.0f;   // choke / retrigger
        Voice* v = nullptr;
        for (auto& x : *voices) if (! x.active) { v = &x; break; }
        if (v == nullptr) v = &(*voices)[0];
        *v = Voice();
        v->active = true;
        v->pad = pad;
        v->vel = velocity;
        v->reverse = param (padParam (pad, "reverse").toRawUTF8()) > 0.5f;
        v->inc = s->sampleRate / sr * std::pow (2.0, param (padParam (pad, "tune").toRawUTF8()) / 12.0);
        v->pos = v->reverse ? s->length() - 1.0 : 0.0;
        const float decay = param (padParam (pad, "decay").toRawUTF8());
        v->decayCoef = decay >= 4.99f ? 1.0f : std::exp (-1.0f / (decay * (float) sr));
        const float pan = param (padParam (pad, "pan").toRawUTF8());
        const float angle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        const float g = juce::Decibels::decibelsToGain (param (padParam (pad, "gain").toRawUTF8()));
        v->gl = std::cos (angle) * juce::MathConstants<float>::sqrt2 * g;
        v->gr = std::sin (angle) * juce::MathConstants<float>::sqrt2 * g;
    };

    auto renderUntil = [&] (int from, int until)
    {
        for (auto& v : *voices)
        {
            if (! v.active) continue;
            auto* s = pads[(size_t) v.pad].get();
            if (s == nullptr) { v.active = false; continue; }
            const float* sl = s->buffer.getReadPointer (0);
            const float* sr2 = s->buffer.getReadPointer (1);
            const int len = s->length();
            const float cut = param (padParam (v.pad, "cutoff").toRawUTF8());
            if (cut < 19000.0f) v.filter.set (cut, 0.7f, (float) sr);
            for (int i = from; i < until; ++i)
            {
                float l = interp (sl, len, v.pos) * v.env * v.vel, r = interp (sr2, len, v.pos) * v.env * v.vel;
                if (v.fadeOut > 0.0f)
                {
                    v.fadeOut -= 1.0f / (0.003f * (float) sr);
                    if (v.fadeOut <= 0.0f) { v.active = false; break; }
                    l *= v.fadeOut; r *= v.fadeOut;
                }
                if (cut < 19000.0f) { const float m = v.filter.lp (0.5f * (l + r)); l = r = m; }
                L[i] += l * v.gl; R[i] += r * v.gr;
                v.env *= v.decayCoef;
                v.pos += v.reverse ? -v.inc : v.inc;
                if (v.pos >= len || v.pos < 0 || v.env < 1.0e-4f) { v.active = false; break; }
            }
        }
    };

    if (const int pr = previewRequest.exchange (-1); pr >= 0) trigger (pr, 0.85f);
    int pos = 0;
    for (const auto meta : midi)
    {
        const int at = juce::jlimit (0, n, meta.samplePosition);
        if (at > pos) { renderUntil (pos, at); pos = at; }
        const auto m = meta.getMessage();
        if (m.isNoteOn())
        {
            const int pad = m.getNoteNumber() - firstNote;
            if (juce::isPositiveAndBelow (pad, numPads)) trigger (pad, m.getFloatVelocity());
        }
        else if (m.isAllSoundOff())
            for (auto& v : *voices) v.active = false;
    }
    if (pos < n) renderUntil (pos, n);

    const float vol = juce::Decibels::decibelsToGain (param ("volume"));
    buffer.applyGain (vol);
}

} // namespace wis::daw
