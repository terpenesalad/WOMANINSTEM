#include "BuiltinEffects.h"

namespace wis::daw
{

using namespace prm;

static inline float dbToGain (float db) { return juce::Decibels::decibelsToGain (db, -100.0f); }

// =====================================================================================================
//  Channel EQ: low cut, low shelf, 2 bells, high shelf, high cut
// =====================================================================================================
static Layout eqLayout()
{
    Layout l;
    addBool  (l, "lc_on", "Low Cut", false);
    addHz    (l, "lc_freq", "Low Cut Freq", 20.0f, 1000.0f, 80.0f);
    addHz    (l, "ls_freq", "Low Freq", 30.0f, 600.0f, 100.0f);
    addDb    (l, "ls_gain", "Low Gain", -18.0f, 18.0f, 0.0f);
    addHz    (l, "m1_freq", "Mid 1 Freq", 80.0f, 8000.0f, 400.0f);
    addDb    (l, "m1_gain", "Mid 1 Gain", -18.0f, 18.0f, 0.0f);
    addFloat (l, "m1_q", "Mid 1 Q", 0.2f, 8.0f, 1.0f, "", 1.0f, 2);
    addHz    (l, "m2_freq", "Mid 2 Freq", 300.0f, 16000.0f, 2500.0f);
    addDb    (l, "m2_gain", "Mid 2 Gain", -18.0f, 18.0f, 0.0f);
    addFloat (l, "m2_q", "Mid 2 Q", 0.2f, 8.0f, 1.0f, "", 1.0f, 2);
    addHz    (l, "hs_freq", "High Freq", 1500.0f, 18000.0f, 8000.0f);
    addDb    (l, "hs_gain", "High Gain", -18.0f, 18.0f, 0.0f);
    addBool  (l, "hc_on", "High Cut", false);
    addHz    (l, "hc_freq", "High Cut Freq", 1000.0f, 20000.0f, 16000.0f);
    addDb    (l, "output", "Output", -24.0f, 24.0f, 0.0f);
    return l;
}

ChannelEq::ChannelEq() : BuiltinProcessor ("eq", "Channel EQ", false, eqLayout())
{
    for (auto& c : coeffs) c = new juce::dsp::IIR::Coefficients<float> (1, 0, 1, 0);
}

void ChannelEq::prepareToPlay (double sr, int)
{
    sampleRate = sr;
    cache.fill (-12345.0f);
    for (auto& ch : filters)
        for (auto& f : ch) { f.coefficients = new juce::dsp::IIR::Coefficients<float> (1, 0, 1, 0); f.coefficients->coefficients.ensureStorageAllocated (8); f.reset(); }
    outGain.reset (sr, 0.02);
    update();
}

void ChannelEq::update()
{
    const char* idsList[] = { "lc_on", "lc_freq", "ls_freq", "ls_gain", "m1_freq", "m1_gain", "m1_q", "m2_freq", "m2_gain",
                              "m2_q", "hs_freq", "hs_gain", "hc_on", "hc_freq" };
    bool changed = false;
    for (int i = 0; i < 14; ++i)
    {
        const float v = param (idsList[i]);
        if (v != cache[(size_t) i]) { cache[(size_t) i] = v; changed = true; }
    }
    if (! changed) return;

    std::array<std::array<float, 6>, numBands> c {};
    designBands (sampleRate, c, enabled);

    for (auto& ch : filters)
        for (int b = 0; b < numBands; ++b)
            *ch[(size_t) b].coefficients = c[(size_t) b];

}

void ChannelEq::designBands (double sr, std::array<std::array<float, 6>, numBands>& c, std::array<bool, numBands>& on) const
{
    const float nyq = (float) sr * 0.45f;
    auto lim = [nyq] (float f) { return juce::jlimit (10.0f, nyq, f); };
    c[0] = AC::makeHighPass (sr, lim (param ("lc_freq")), 0.707f);
    c[1] = AC::makeLowShelf (sr, lim (param ("ls_freq")), 0.7f, dbToGain (param ("ls_gain")));
    c[2] = AC::makePeakFilter (sr, lim (param ("m1_freq")), param ("m1_q"), dbToGain (param ("m1_gain")));
    c[3] = AC::makePeakFilter (sr, lim (param ("m2_freq")), param ("m2_q"), dbToGain (param ("m2_gain")));
    c[4] = AC::makeHighShelf (sr, lim (param ("hs_freq")), 0.7f, dbToGain (param ("hs_gain")));
    c[5] = AC::makeLowPass (sr, lim (param ("hc_freq")), 0.707f);
    on = { param ("lc_on") > 0.5f, std::abs (param ("ls_gain")) > 0.01f, std::abs (param ("m1_gain")) > 0.01f,
           std::abs (param ("m2_gain")) > 0.01f, std::abs (param ("hs_gain")) > 0.01f, param ("hc_on") > 0.5f };
}

void ChannelEq::refreshDisplay() const
{
    // message thread only: designs its own copy of the filters from the current parameters
    std::array<std::array<float, 6>, numBands> c {};
    designBands (sampleRate, c, displayEnabled);
    for (int b = 0; b < numBands; ++b)
        *coeffs[(size_t) b] = c[(size_t) b];
}

float ChannelEq::responseDb (double hz) const
{
    double mag = 1.0;
    for (int b = 0; b < numBands; ++b)
        if (displayEnabled[(size_t) b])
            mag *= coeffs[(size_t) b]->getMagnitudeForFrequency (hz, sampleRate);
    return (float) juce::Decibels::gainToDecibels (mag, -60.0) + param ("output");
}

void ChannelEq::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nd;
    update();
    const int n = buffer.getNumSamples();
    for (int ch = 0; ch < juce::jmin (2, buffer.getNumChannels()); ++ch)
    {
        auto* d = buffer.getWritePointer (ch);
        for (int b = 0; b < numBands; ++b)
            if (enabled[(size_t) b])
            {
                auto& f = filters[(size_t) ch][(size_t) b];
                for (int i = 0; i < n; ++i) d[i] = f.processSample (d[i]);
            }
    }
    outGain.setTargetValue (dbToGain (param ("output")));
    if (outGain.isSmoothing() || std::abs (outGain.getTargetValue() - 1.0f) > 1.0e-4f)
        for (int i = 0; i < n; ++i)
        {
            const float g = outGain.getNextValue();
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch) buffer.getWritePointer (ch)[i] *= g;
        }
}

juce::StringArray ChannelEq::getProgramNames()
{
    return { "Flat", "Vocal Presence", "Kick Punch", "Bass Tighten", "Acoustic Sparkle", "Bright Mix", "Warm & Round", "Telephone" };
}

void ChannelEq::loadProgram (int i)
{
    struct P { bool lc; float lcf, lsf, lsg, m1f, m1g, m1q, m2f, m2g, m2q, hsf, hsg; bool hc; float hcf; };
    static const P presets[] = {
        { false, 80,  100,  0,   400,  0, 1.0f, 2500,  0, 1.0f, 8000,  0, false, 16000 },
        { true, 100,  200, -2,   350, -3, 1.2f, 3500,  3, 0.9f, 10000, 3, false, 18000 },
        { true,  30,   60,  4,   350, -5, 1.4f, 3200,  4, 1.5f, 9000,  0, false, 16000 },
        { true,  35,   90,  2,   250, -3, 1.2f,  900,  2, 1.0f, 5000, -2, true,   8000 },
        { true,  90,  150, -2,   250, -3, 1.0f, 5000,  2, 0.8f, 12000, 4, false, 18000 },
        { true,  30,  100,  1,   400, -1, 0.8f, 3000,  1, 0.8f, 12000, 3, false, 18000 },
        { false, 30,  150,  3,   500,  1, 0.7f, 3000, -2, 1.0f, 8000, -3, true,  12000 },
        { true, 400,  600,  0,  1500,  8, 0.7f, 2500,  4, 1.0f, 8000,  0, true,   3500 },
    };
    if (! juce::isPositiveAndBelow (i, (int) std::size (presets))) return;
    auto& p = presets[i];
    setParam ("lc_on", p.lc ? 1.0f : 0.0f); setParam ("lc_freq", p.lcf); setParam ("ls_freq", p.lsf); setParam ("ls_gain", p.lsg);
    setParam ("m1_freq", p.m1f); setParam ("m1_gain", p.m1g); setParam ("m1_q", p.m1q);
    setParam ("m2_freq", p.m2f); setParam ("m2_gain", p.m2g); setParam ("m2_q", p.m2q);
    setParam ("hs_freq", p.hsf); setParam ("hs_gain", p.hsg); setParam ("hc_on", p.hc ? 1.0f : 0.0f); setParam ("hc_freq", p.hcf);
    setParam ("output", 0.0f);
}

// =====================================================================================================
//  Compressor: stereo-linked, soft knee, parallel mix
// =====================================================================================================
static Layout compLayout()
{
    Layout l;
    addDb    (l, "thresh", "Threshold", -60.0f, 0.0f, -18.0f);
    addFloat (l, "ratio", "Ratio", 1.0f, 20.0f, 4.0f, ":1", 4.0f, 1);
    addMs    (l, "attack", "Attack", 0.1f, 100.0f, 10.0f);
    addMs    (l, "release", "Release", 10.0f, 1000.0f, 120.0f);
    addDb    (l, "knee", "Knee", 0.0f, 18.0f, 6.0f);
    addDb    (l, "makeup", "Make-up", 0.0f, 24.0f, 0.0f);
    addPercent (l, "mix", "Mix", 1.0f);
    return l;
}

Compressor::Compressor() : BuiltinProcessor ("compressor", "Compressor", false, compLayout()) {}

void Compressor::prepareToPlay (double sr, int) { sampleRate = sr; envDb = 0.0f; }

void Compressor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nd;
    const float thresh = param ("thresh"), ratio = param ("ratio"), knee = param ("knee");
    const float makeup = param ("makeup"), mix = param ("mix");
    const float att = std::exp (-1.0f / (float) (param ("attack") * 0.001 * sampleRate));
    const float rel = std::exp (-1.0f / (float) (param ("release") * 0.001 * sampleRate));
    const float slope = 1.0f / ratio - 1.0f;
    const int n = buffer.getNumSamples(), nch = juce::jmin (2, buffer.getNumChannels());
    float maxGr = 0.0f;

    // side-chain: detect on the key track (e.g. kick ducking a bass), apply to this one
    const auto* key = sidechainInput != nullptr && sidechainInput->getNumSamples() >= n ? sidechainInput : nullptr;
    const auto& detect = key != nullptr ? *key : buffer;
    const int dch = juce::jmin (2, detect.getNumChannels());

    for (int i = 0; i < n; ++i)
    {
        float peak = 0.0f;
        for (int ch = 0; ch < dch; ++ch) peak = juce::jmax (peak, std::abs (detect.getSample (ch, i)));
        const float lvl = juce::Decibels::gainToDecibels (peak, -120.0f);
        const float over = lvl - thresh;
        float gr;
        if (2.0f * over < -knee)              gr = 0.0f;
        else if (2.0f * std::abs (over) <= knee && knee > 0.0f)
        {
            const float x = over + knee * 0.5f;
            gr = slope * x * x / (2.0f * knee);
        }
        else                                  gr = slope * over;

        const float coeff = gr < envDb ? att : rel;   // gr is negative: falling = attacking
        envDb = coeff * envDb + (1.0f - coeff) * gr;
        maxGr = juce::jmin (maxGr, envDb);

        const float g = dbToGain (envDb + makeup);
        const float wetMix = mix, dryMix = 1.0f - mix;
        for (int ch = 0; ch < nch; ++ch)
        {
            auto* d = buffer.getWritePointer (ch);
            d[i] = d[i] * (g * wetMix + dryMix);
        }
    }
    gainReductionDb = maxGr;
}

juce::StringArray Compressor::getProgramNames()
{
    return { "Gentle Glue", "Vocal Leveler", "Punchy Drums", "Bass Squash", "Parallel Smash", "Acoustic Smooth" };
}

void Compressor::loadProgram (int i)
{
    struct P { float t, r, a, rel, k, m, mix; };
    static const P presets[] = {
        { -14, 2.0f, 30, 200, 9, 2, 1.0f }, { -22, 3.5f, 5, 120, 6, 5, 1.0f }, { -18, 4.0f, 20, 80, 3, 4, 1.0f },
        { -24, 6.0f, 8, 150, 6, 6, 1.0f },  { -35, 10.0f, 1, 60, 0, 12, 0.4f }, { -20, 2.5f, 15, 250, 9, 3, 1.0f } };
    if (! juce::isPositiveAndBelow (i, (int) std::size (presets))) return;
    auto& p = presets[i];
    setParam ("thresh", p.t); setParam ("ratio", p.r); setParam ("attack", p.a); setParam ("release", p.rel);
    setParam ("knee", p.k); setParam ("makeup", p.m); setParam ("mix", p.mix);
}

// =====================================================================================================
//  Reverb
// =====================================================================================================
static Layout reverbLayout()
{
    Layout l;
    addPercent (l, "size", "Size", 0.6f);
    addPercent (l, "damp", "Damping", 0.45f);
    addPercent (l, "width", "Width", 1.0f);
    addMs      (l, "predelay", "Pre-Delay", 0.0f, 200.0f, 20.0f);
    addHz      (l, "lowcut", "Low Cut", 20.0f, 1000.0f, 150.0f);
    addHz      (l, "highcut", "High Cut", 1000.0f, 20000.0f, 9000.0f);
    addPercent (l, "mix", "Mix", 0.3f);
    return l;
}

ReverbFx::ReverbFx() : BuiltinProcessor ("reverb", "Reverb", false, reverbLayout()) {}

void ReverbFx::prepareToPlay (double sr, int block)
{
    sampleRate = sr;
    reverb.prepare ({ sr, (juce::uint32) block, 2 });
    preDelay.prepare (sr);
    wet.setSize (2, block * 2);
    for (auto* arr : { &lowCut, &highCut })
        for (auto& f : *arr) { f.coefficients->coefficients.ensureStorageAllocated (8); f.reset(); }
    lastLowCut = lastHighCut = -1.0f;
}

void ReverbFx::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nd;
    const int n = buffer.getNumSamples();
    if (wet.getNumSamples() < n) return;

    if (param ("lowcut") != lastLowCut)
    {
        lastLowCut = param ("lowcut");
        for (auto& f : lowCut) *f.coefficients = AC::makeHighPass (sampleRate, lastLowCut, 0.707f);
    }
    if (param ("highcut") != lastHighCut)
    {
        lastHighCut = juce::jmin (param ("highcut"), (float) sampleRate * 0.45f);
        for (auto& f : highCut) *f.coefficients = AC::makeLowPass (sampleRate, lastHighCut, 0.707f);
    }

    juce::dsp::Reverb::Parameters p;
    p.roomSize = 0.25f + 0.74f * param ("size");
    p.damping = param ("damp");
    p.width = param ("width");
    p.wetLevel = 1.0f;
    p.dryLevel = 0.0f;
    reverb.setParameters (p);

    const int nch = juce::jmin (2, buffer.getNumChannels());
    for (int ch = 0; ch < 2; ++ch)
        wet.copyFrom (ch, 0, buffer, juce::jmin (ch, nch - 1), 0, n);

    preDelay.setDelayMs (param ("predelay"));
    preDelay.process (wet.getWritePointer (0), wet.getWritePointer (1), n);
    for (int ch = 0; ch < 2; ++ch)
    {
        auto* d = wet.getWritePointer (ch);
        for (int i = 0; i < n; ++i) d[i] = highCut[(size_t) ch].processSample (lowCut[(size_t) ch].processSample (d[i]));
    }

    float* chans[] = { wet.getWritePointer (0), wet.getWritePointer (1) };
    juce::dsp::AudioBlock<float> block (chans, 2, (size_t) n);
    reverb.process (juce::dsp::ProcessContextReplacing<float> (block));

    const float mix = param ("mix");
    const float dryG = std::cos (mix * juce::MathConstants<float>::halfPi * 0.6f);
    const float wetG = mix * 0.7f;
    for (int ch = 0; ch < nch; ++ch)
    {
        auto* d = buffer.getWritePointer (ch);
        auto* w = wet.getReadPointer (ch);
        for (int i = 0; i < n; ++i) d[i] = d[i] * dryG + w[i] * wetG;
    }
}

juce::StringArray ReverbFx::getProgramNames() { return { "Medium Room", "Large Hall", "Vocal Plate", "Small Room", "Ambient Wash", "Drum Room" }; }

void ReverbFx::loadProgram (int i)
{
    struct P { float size, damp, pre, lc, hc, mix; };
    static const P presets[] = { { 0.55f, 0.45f, 15, 150, 9000, 0.25f }, { 0.9f, 0.35f, 35, 120, 8000, 0.3f },
                                 { 0.7f, 0.2f, 25, 250, 12000, 0.22f }, { 0.3f, 0.6f, 5, 150, 8000, 0.2f },
                                 { 0.98f, 0.5f, 60, 300, 6000, 0.5f }, { 0.4f, 0.55f, 0, 100, 10000, 0.18f } };
    if (! juce::isPositiveAndBelow (i, (int) std::size (presets))) return;
    auto& p = presets[i];
    setParam ("size", p.size); setParam ("damp", p.damp); setParam ("predelay", p.pre);
    setParam ("lowcut", p.lc); setParam ("highcut", p.hc); setParam ("mix", p.mix);
}

// =====================================================================================================
//  Delay (tempo-synced)
// =====================================================================================================
juce::StringArray DelayFx::divisionNames() { return { "1/2", "1/4", "1/4 dotted", "1/4 triplet", "1/8", "1/8 dotted", "1/8 triplet", "1/16" }; }

double DelayFx::divisionBeats (int i)
{
    static const double b[] = { 2.0, 1.0, 1.5, 2.0 / 3.0, 0.5, 0.75, 1.0 / 3.0, 0.25 };
    return b[juce::jlimit (0, 7, i)];
}

static Layout delayLayout()
{
    Layout l;
    addBool    (l, "sync", "Sync to Tempo", true);
    addChoice  (l, "division", "Note", DelayFx::divisionNames(), 5);
    addMs      (l, "time", "Time", 20.0f, 2000.0f, 350.0f);
    addPercent (l, "feedback", "Feedback", 0.35f);
    addHz      (l, "tone", "Tone", 800.0f, 16000.0f, 5000.0f);
    addPercent (l, "mix", "Mix", 0.25f);
    addBool    (l, "pingpong", "Ping-Pong", true);
    return l;
}

DelayFx::DelayFx() : BuiltinProcessor ("delay", "Delay", false, delayLayout()) {}

void DelayFx::prepareToPlay (double sr, int block) { delay.prepare (sr, block); }

void DelayFx::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nd;
    double ms = param ("time");
    if (param ("sync") > 0.5f)
        ms = divisionBeats ((int) param ("division")) * 60000.0 / hostTempo();
    delay.setParameters ((float) juce::jlimit (20.0, 2000.0, ms), param ("feedback") * 0.95f, param ("tone"), param ("mix"), param ("pingpong") > 0.5f);
    if (buffer.getNumChannels() >= 2)
        delay.process (buffer.getWritePointer (0), buffer.getWritePointer (1), buffer.getNumSamples());
}

juce::StringArray DelayFx::getProgramNames() { return { "Dotted Eighth", "Quarter Echo", "Slapback", "Ping-Pong Eighths", "Dub Wash" }; }

void DelayFx::loadProgram (int i)
{
    struct P { bool sync; int div; float time, fb, tone, mix; bool pp; };
    static const P presets[] = { { true, 5, 350, 0.35f, 5000, 0.25f, true }, { true, 1, 500, 0.3f, 4500, 0.2f, false },
                                 { false, 4, 110, 0.05f, 6000, 0.25f, false }, { true, 4, 250, 0.4f, 6000, 0.25f, true },
                                 { true, 2, 600, 0.65f, 2500, 0.35f, true } };
    if (! juce::isPositiveAndBelow (i, (int) std::size (presets))) return;
    auto& p = presets[i];
    setParam ("sync", p.sync ? 1.0f : 0.0f); setParam ("division", (float) p.div); setParam ("time", p.time);
    setParam ("feedback", p.fb); setParam ("tone", p.tone); setParam ("mix", p.mix); setParam ("pingpong", p.pp ? 1.0f : 0.0f);
}

// =====================================================================================================
//  Chorus / Phaser / Tremolo
// =====================================================================================================
static Layout chorusLayout()
{
    Layout l;
    addFloat   (l, "rate", "Rate", 0.05f, 5.0f, 0.8f, "Hz", 1.0f, 2);
    addPercent (l, "depth", "Depth", 0.35f);
    addMs      (l, "centre", "Delay", 2.0f, 25.0f, 8.0f);
    addPercent (l, "feedback", "Feedback", 0.0f);
    addPercent (l, "mix", "Mix", 0.5f);
    return l;
}

ChorusFx::ChorusFx() : BuiltinProcessor ("chorus", "Chorus", false, chorusLayout()) {}
void ChorusFx::prepareToPlay (double sr, int block) { chorus.prepare ({ sr, (juce::uint32) block, 2 }); chorus.reset(); }

void ChorusFx::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    chorus.setRate (param ("rate"));
    chorus.setDepth (param ("depth"));
    chorus.setCentreDelay (param ("centre"));
    chorus.setFeedback (param ("feedback") * 0.9f);
    chorus.setMix (param ("mix"));
    juce::dsp::AudioBlock<float> block (buffer);
    chorus.process (juce::dsp::ProcessContextReplacing<float> (block));
}

static Layout phaserLayout()
{
    Layout l;
    addFloat   (l, "rate", "Rate", 0.02f, 5.0f, 0.4f, "Hz", 0.8f, 2);
    addPercent (l, "depth", "Depth", 0.7f);
    addHz      (l, "centre", "Centre", 200.0f, 5000.0f, 1000.0f);
    addPercent (l, "feedback", "Feedback", 0.5f);
    addPercent (l, "mix", "Mix", 0.5f);
    return l;
}

PhaserFx::PhaserFx() : BuiltinProcessor ("phaser", "Phaser", false, phaserLayout()) {}
void PhaserFx::prepareToPlay (double sr, int block) { phaser.prepare ({ sr, (juce::uint32) block, 2 }); phaser.reset(); }

void PhaserFx::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    phaser.setRate (param ("rate"));
    phaser.setDepth (param ("depth"));
    phaser.setCentreFrequency (param ("centre"));
    phaser.setFeedback (param ("feedback") * 0.95f);
    phaser.setMix (param ("mix"));
    juce::dsp::AudioBlock<float> block (buffer);
    phaser.process (juce::dsp::ProcessContextReplacing<float> (block));
}

static Layout tremoloLayout()
{
    Layout l;
    addBool    (l, "sync", "Sync to Tempo", true);
    addChoice  (l, "division", "Note", { "1/2", "1/4", "1/8", "1/8 triplet", "1/16" }, 2);
    addFloat   (l, "rate", "Rate", 0.1f, 20.0f, 5.0f, "Hz", 4.0f, 2);
    addPercent (l, "depth", "Depth", 0.6f);
    addChoice  (l, "shape", "Shape", { "Sine", "Triangle", "Square" }, 0);
    addPercent (l, "stereo", "Auto-Pan", 0.0f);
    return l;
}

TremoloFx::TremoloFx() : BuiltinProcessor ("tremolo", "Tremolo / Auto-Pan", false, tremoloLayout()) {}
void TremoloFx::prepareToPlay (double sr, int) { sampleRate = sr; phase = 0.0; }

void TremoloFx::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    static const double divBeats[] = { 2.0, 1.0, 0.5, 1.0 / 3.0, 0.25 };
    double hz = param ("rate");
    if (param ("sync") > 0.5f)
        hz = hostTempo() / 60.0 / divBeats[juce::jlimit (0, 4, (int) param ("division"))];

    const double inc = hz / sampleRate;
    const float depth = param ("depth"), stereo = param ("stereo");
    const int shape = (int) param ("shape");
    auto lfo = [shape] (double ph)   // 0..1
    {
        ph -= std::floor (ph);
        switch (shape)
        {
            case 1:  return (float) (ph < 0.5 ? ph * 2.0 : 2.0 - ph * 2.0);
            case 2:  return ph < 0.5 ? 1.0f : 0.0f;
            default: return (float) (0.5 + 0.5 * std::sin (juce::MathConstants<double>::twoPi * ph));
        }
    };

    const int n = buffer.getNumSamples(), nch = buffer.getNumChannels();
    for (int i = 0; i < n; ++i)
    {
        const float a = lfo (phase), b = lfo (phase + 0.5 * stereo);
        const float gl = 1.0f - depth * (1.0f - a);
        const float gr = 1.0f - depth * (1.0f - b);
        if (nch > 0) buffer.getWritePointer (0)[i] *= gl;
        if (nch > 1) buffer.getWritePointer (1)[i] *= gr;
        phase += inc;
        if (phase > 1.0e6) phase -= std::floor (phase);
    }
}

// =====================================================================================================
//  Saturator
// =====================================================================================================
static Layout satLayout()
{
    Layout l;
    addChoice  (l, "type", "Type", { "Tape", "Tube", "Hard Clip", "Fuzz", "Lo-Fi" }, 0);
    addDb      (l, "drive", "Drive", 0.0f, 36.0f, 8.0f);
    addHz      (l, "tone", "Tone", 1000.0f, 20000.0f, 12000.0f);
    addPercent (l, "mix", "Mix", 1.0f);
    addDb      (l, "output", "Output", -24.0f, 12.0f, 0.0f);
    return l;
}

Saturator::Saturator() : BuiltinProcessor ("saturator", "Saturator", false, satLayout()) {}

void Saturator::prepareToPlay (double sr, int block)
{
    sampleRate = sr;
    oversampling.initProcessing ((size_t) block);
    oversampling.reset();
    dry.setSize (2, block * 2);
    for (auto& f : tone) { f.coefficients->coefficients.ensureStorageAllocated (8); f.reset(); }
    lastTone = -1.0f;
}

void Saturator::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nd;
    const int n = buffer.getNumSamples();
    const int nch = juce::jmin (2, buffer.getNumChannels());
    if (dry.getNumSamples() < n || nch == 0) return;

    for (int ch = 0; ch < nch; ++ch) dry.copyFrom (ch, 0, buffer, ch, 0, n);

    const int type = (int) param ("type");
    const float drive = dbToGain (param ("drive"));
    // keep perceived loudness roughly constant as drive goes up
    const float comp = 1.0f / std::sqrt (drive);

    if (type == 4)   // Lo-Fi: bit + rate reduction (no oversampling needed)
    {
        const float d01 = param ("drive") / 36.0f;
        const float levels = std::pow (2.0f, 16.0f - 12.0f * d01);
        const int hold = 1 + (int) (d01 * 12.0f);
        for (int i = 0; i < n; ++i)
        {
            if (holdCounter <= 0)
            {
                for (int ch = 0; ch < nch; ++ch)
                    holdValue[(size_t) ch] = std::round (buffer.getSample (ch, i) * levels) / levels;
                holdCounter = hold;
            }
            --holdCounter;
            for (int ch = 0; ch < nch; ++ch) buffer.setSample (ch, i, holdValue[(size_t) ch]);
        }
    }
    else
    {
        float* chans[2] = { buffer.getWritePointer (0), buffer.getWritePointer (juce::jmin (1, nch - 1)) };
        juce::dsp::AudioBlock<float> block (chans, (size_t) nch, (size_t) n);
        auto up = oversampling.processSamplesUp (block);
        for (size_t ch = 0; ch < up.getNumChannels(); ++ch)
        {
            auto* d = up.getChannelPointer (ch);
            for (size_t i = 0; i < up.getNumSamples(); ++i)
            {
                const float x = d[i] * drive;
                float y;
                switch (type)
                {
                    case 1:  y = std::tanh (x + 0.25f) - std::tanh (0.25f); break;                    // tube: asymmetric
                    case 2:  y = juce::jlimit (-1.0f, 1.0f, x); break;                                  // hard clip
                    case 3:  y = std::tanh (x * 3.0f) * 0.8f; break;                                    // fuzz
                    default: y = x / (1.0f + std::abs (x)) * 1.2f; break;                              // tape: soft
                }
                d[i] = y * (type == 2 ? 0.9f : 1.0f) * juce::jmax (comp, 0.25f) * 1.6f;
            }
        }
        oversampling.processSamplesDown (block);
    }

    if (param ("tone") != lastTone)
    {
        lastTone = juce::jmin (param ("tone"), (float) sampleRate * 0.45f);
        for (auto& f : tone) *f.coefficients = AC::makeLowPass (sampleRate, lastTone, 0.707f);
    }

    const float mix = param ("mix"), out = dbToGain (param ("output"));
    for (int ch = 0; ch < nch; ++ch)
    {
        auto* d = buffer.getWritePointer (ch);
        auto* dr = dry.getReadPointer (ch);
        for (int i = 0; i < n; ++i)
        {
            const float wet = tone[(size_t) ch].processSample (d[i]);
            d[i] = (wet * mix + dr[i] * (1.0f - mix)) * out;
        }
    }
}

// =====================================================================================================
//  Limiter / Gate
// =====================================================================================================
static Layout limiterLayout()
{
    Layout l;
    addDb (l, "gain", "Gain", 0.0f, 24.0f, 0.0f);
    addDb (l, "ceiling", "Ceiling", -12.0f, 0.0f, -0.3f);
    addMs (l, "release", "Release", 1.0f, 500.0f, 60.0f);
    return l;
}

LimiterFx::LimiterFx() : BuiltinProcessor ("limiter", "Limiter", false, limiterLayout()) {}
void LimiterFx::prepareToPlay (double sr, int block) { limiter.prepare ({ sr, (juce::uint32) block, 2 }); limiter.reset(); }

void LimiterFx::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    buffer.applyGain (dbToGain (param ("gain")));
    limiter.setThreshold (param ("ceiling"));
    limiter.setRelease (param ("release"));
    juce::dsp::AudioBlock<float> block (buffer);
    limiter.process (juce::dsp::ProcessContextReplacing<float> (block));
}

static Layout gateLayout()
{
    Layout l;
    addDb (l, "thresh", "Threshold", -80.0f, 0.0f, -45.0f);
    addMs (l, "attack", "Attack", 0.1f, 50.0f, 1.0f);
    addMs (l, "hold", "Hold", 0.0f, 500.0f, 40.0f);
    addMs (l, "release", "Release", 5.0f, 1000.0f, 120.0f);
    addDb (l, "range", "Range", -80.0f, 0.0f, -80.0f);
    return l;
}

GateFx::GateFx() : BuiltinProcessor ("gate", "Noise Gate", false, gateLayout()) {}
void GateFx::prepareToPlay (double sr, int) { sampleRate = sr; env = gain = 0.0f; holdCounter = 0; isOpen = false; }

void GateFx::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const float thOpen = dbToGain (param ("thresh")), thClose = thOpen * 0.5f;
    const float att = std::exp (-1.0f / (float) (param ("attack") * 0.001 * sampleRate));
    const float rel = std::exp (-1.0f / (float) (param ("release") * 0.001 * sampleRate));
    const float envRel = std::exp (-1.0f / (float) (0.02 * sampleRate));
    const int holdSamples = (int) (param ("hold") * 0.001 * sampleRate);
    const float floorGain = dbToGain (param ("range"));
    const int n = buffer.getNumSamples(), nch = buffer.getNumChannels();

    const auto* key = sidechainInput != nullptr && sidechainInput->getNumSamples() >= n ? sidechainInput : nullptr;
    const auto& detect = key != nullptr ? *key : buffer;
    const int dch = juce::jmin (2, detect.getNumChannels());

    for (int i = 0; i < n; ++i)
    {
        float a = 0.0f;
        for (int ch = 0; ch < dch; ++ch) a = juce::jmax (a, std::abs (detect.getSample (ch, i)));
        env = a > env ? a : env * envRel + a * (1.0f - envRel);

        if (env > thOpen)       { isOpen = true; holdCounter = holdSamples; }
        else if (env < thClose) { if (holdCounter > 0) --holdCounter; else isOpen = false; }

        const float target = isOpen ? 1.0f : 0.0f;
        const float c = target > gain ? att : rel;
        gain = c * gain + (1.0f - c) * target;
        const float g = floorGain + (1.0f - floorGain) * gain;
        for (int ch = 0; ch < nch; ++ch) buffer.getWritePointer (ch)[i] *= g;
    }
}

// =====================================================================================================
//  Amp & Pedals
// =====================================================================================================
std::function<juce::AudioProcessorEditor* (AmpRigFx&)> AmpRigFx::editorFactory;

AmpRigFx::AmpRigFx() : BuiltinProcessor ("amprig", "Amp & Pedals", false, Layout())
{
    rig.loadFactoryPreset (2);
}

void AmpRigFx::prepareToPlay (double sr, int block)
{
    maxBlock = block;
    mono.assign ((size_t) block * 2 + 16, 0.0f);
    rig.setRateAndBufferSizeDetails (sr, block);
    rig.prepareToPlay (sr, block);
}

void AmpRigFx::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const int n = buffer.getNumSamples();
    if (n > (int) mono.size() || buffer.getNumChannels() < 2) return;
    auto* l = buffer.getWritePointer (0);
    auto* r = buffer.getWritePointer (1);
    for (int i = 0; i < n; ++i) mono[(size_t) i] = 0.5f * (l[i] + r[i]);
    rig.processMonoToStereo (mono.data(), l, r, n);
}

void AmpRigFx::getStateInformation (juce::MemoryBlock& dest) { rig.getStateInformation (dest); }
void AmpRigFx::setStateInformation (const void* d, int s) { rig.setStateInformation (d, s); }

} // namespace wis::daw
