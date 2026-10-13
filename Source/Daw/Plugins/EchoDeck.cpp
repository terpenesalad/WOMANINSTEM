#include "EchoDeck.h"

namespace wis::daw
{

std::function<juce::AudioProcessorEditor* (EchoDeck&)> EchoDeck::editorFactory;

using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;
using namespace prm;

namespace
{
    constexpr double maxEchoMs = 6000.0;     // longest echo (2 bars at 80 BPM)
    constexpr double bufferSeconds = 8.8;    // room for the longest echo + groove + feel + wobble
    constexpr int chunk = 32;                // loop filters / style follow the echo time every 32 samples
    constexpr float twoPi = juce::MathConstants<float>::twoPi;

    /** One echo machine. Wow / flutter / drift are pitch deviations in percent (what you hear), turned into
        read-head movement for the delay time. Hold: sample-and-hold rate (0 = none, -1 = a bucket-brigade
        clock that slows with the echo time). */
    struct StyleDef
    {
        const char* name;
        const char* blurb;
        int sat;
        float drive, lpHz, lpQ, hpHz;
        float wowPct, wowHz, flutPct, flutHz, driftPct;
        float hiss;
        int bits;
        float holdHz;
        float diffusion;
        float glideSec;
    };

    const StyleDef styles[] = {
        { "Studio Tape", "Clean pro reel-to-reel: warm, round repeats with a little tape squash and the slightest wow",
          1, 1.3f, 11000.0f, 0.62f, 35.0f,   0.05f, 0.55f, 0.025f, 7.1f, 0.03f,  0.00025f, 0, 0.0f, 0.0f, 0.22f },
        { "Tape Echo Unit", "A looped-tape echo box with several heads: darker, saturating, flutter you can hear, a little spring in the tail",
          1, 2.0f, 5200.0f, 0.70f, 90.0f,    0.14f, 0.85f, 0.09f, 5.6f, 0.07f,   0.0009f, 0, 0.0f, 0.12f, 0.30f },
        { "Valve Tape", "Tape echo with a valve preamp: thick, chewy, asymmetric drive that blooms as it repeats",
          2, 1.9f, 7200.0f, 0.65f, 70.0f,    0.07f, 0.5f, 0.045f, 6.3f, 0.04f,   0.0006f, 0, 0.0f, 0.0f, 0.26f },
        { "Worn Tape", "An old machine with a stretched tape: dull, wobbly, hissy repeats that sound like a memory",
          1, 1.6f, 4100.0f, 0.68f, 120.0f,   0.32f, 0.45f, 0.16f, 6.8f, 0.18f,   0.0016f, 0, 0.0f, 0.05f, 0.34f },
        { "Cassette", "Lo-fi cassette deck: narrow and seasick, big slow wow, soft highs",
          1, 1.4f, 3400.0f, 0.70f, 150.0f,   0.48f, 0.33f, 0.22f, 9.0f, 0.25f,   0.0024f, 0, 0.0f, 0.0f, 0.36f },
        { "Bucket Brigade", "Analog chip delay: the longer the echo, the slower the clock and the darker and grittier each repeat",
          3, 1.5f, 9000.0f, 0.75f, 60.0f,    0.08f, 0.8f, 0.0f, 6.0f, 0.02f,     0.0012f, 0, -1.0f, 0.0f, 0.18f },
        { "Oil Can", "Electrostatic oil-can echo: smeared, swimming, quick warble, almost a reverb at high feedback",
          1, 1.3f, 3800.0f, 0.60f, 200.0f,   0.45f, 1.6f, 0.18f, 11.0f, 0.10f,   0.0010f, 0, 0.0f, 0.55f, 0.20f },
        { "Early Digital", "An 80s rack delay: 12-bit, slightly gritty, bright and steady",
          4, 1.1f, 9500.0f, 0.71f, 30.0f,    0.0f, 0.5f, 0.0f, 6.0f, 0.0f,       0.0f, 12, 32000.0f, 0.0f, 0.08f },
        { "Clean Digital", "Transparent: every repeat a clean copy (a gentle limiter only when feedback runs away)",
          0, 1.0f, 20000.0f, 0.71f, 15.0f,   0.0f, 0.5f, 0.0f, 6.0f, 0.0f,       0.0f, 0, 0.0f, 0.0f, 0.06f },
        { "Lo-Fi Radio", "A small speaker and a cheap converter: thin, crunchy, band-limited echoes",
          4, 2.4f, 3000.0f, 0.90f, 450.0f,   0.10f, 0.3f, 0.05f, 8.0f, 0.06f,    0.0020f, 9, 11025.0f, 0.0f, 0.15f },
    };
    constexpr int numStyles = (int) std::size (styles);

    float bound (float x)
    {
        // unity below 1, bounded at 2: nothing runs away even with feedback above 100% and no saturation
        const float a = std::abs (x);
        return a <= 1.0f ? x : std::copysign (1.0f + std::tanh (a - 1.0f), x);
    }
}

// ---------------------------------------------------------------------------------------------------------------
juce::StringArray EchoDeck::styleNames()
{
    juce::StringArray s;
    for (auto& st : styles) s.add (st.name);
    return s;
}

juce::StringArray EchoDeck::styleBlurbs()
{
    juce::StringArray s;
    for (auto& st : styles) s.add (st.blurb);
    return s;
}

juce::StringArray EchoDeck::noteNames() { return { "1/64", "1/32", "1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars" }; }

double EchoDeck::noteBeats (int noteIndex, int timeMode)
{
    static const double beats[] = { 1.0 / 16.0, 1.0 / 8.0, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0 };
    const double b = beats[juce::jlimit (0, 7, noteIndex)];
    return timeMode == dotted ? b * 1.5 : timeMode == triplet ? b * 2.0 / 3.0 : b;
}

static Layout echoLayout()
{
    Layout l;
    addPercent (l, "mix", "Mix", 0.25f);
    addChoice  (l, "timeMode", "Time Mode", { "Milliseconds", "Note", "Dotted", "Triplet" }, EchoDeck::dotted);
    addMs      (l, "timeMs", "Echo Time", 1.0f, (float) maxEchoMs, 375.0f);
    addChoice  (l, "note", "Echo Note", EchoDeck::noteNames(), 3);
    addFloat   (l, "feedback", "Feedback", 0.0f, 1.1f, 0.32f, "%");
    addHz      (l, "lowCut", "Low Cut", 20.0f, 2000.0f, 120.0f);
    addHz      (l, "highCut", "High Cut", 800.0f, 20000.0f, 9000.0f);
    addPercent (l, "saturation", "Saturation", 0.3f);
    addDb      (l, "inputDb", "Input", -24.0f, 24.0f, 0.0f);
    addDb      (l, "outputDb", "Output", -24.0f, 24.0f, 0.0f);
    addChoice  (l, "mode", "Mode", { "Single", "Dual", "Ping-Pong", "Rhythm" }, EchoDeck::single);
    addChoice  (l, "style", "Style", EchoDeck::styleNames(), 0);
    addFloat   (l, "groove", "Groove", -1.0f, 1.0f, 0.0f, "", 0.0f, 2);
    addFloat   (l, "feel", "Feel", -1.0f, 1.0f, 0.0f, "", 0.0f, 2);
    addChoice  (l, "tempoSrc", "Tempo From", { "Song", "Tap" }, 0);
    addFloat   (l, "tapBpm", "Tap Tempo", 40.0f, 240.0f, 120.0f, "BPM", 0.0f, 1);
    addFloat   (l, "ratio", "Dual Ratio", 0.25f, 2.0f, 0.75f, "x", 0.0f, 2);
    addFloat   (l, "wobble", "Wobble", 0.0f, 2.0f, 1.0f, "%");
    addPercent (l, "diffusion", "Diffusion", 0.0f);
    addPercent (l, "width", "Width", 1.0f);
    addPercent (l, "duck", "Duck", 0.0f);
    addBool    (l, "hold", "Hold", false);
    addChoice  (l, "rhythmLen", "Rhythm Steps", { "1", "2", "3", "4", "5", "6", "7", "8" }, 7);
    const float defTaps[] = { 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.65f, 0.0f, 0.85f };
    for (int i = 0; i < EchoDeck::numTaps; ++i)
        addPercent (l, "tap" + juce::String (i + 1), "Tap " + juce::String (i + 1), defTaps[i]);
    return l;
}

EchoDeck::EchoDeck() : BuiltinProcessor ("echodeck", "Echo Deck", false, echoLayout()) {}
EchoDeck::~EchoDeck() = default;

// ---- small DSP parts ------------------------------------------------------------------------------------------
void EchoDeck::Svf::set (double sr, float hz, float q)
{
    const double f = juce::jlimit (5.0, sr * 0.49, (double) hz);
    g = (float) std::tan (juce::MathConstants<double>::pi * f / sr);
    k = 1.0f / juce::jmax (0.1f, q);
    a1 = 1.0f / (1.0f + g * (g + k));
    a2 = g * a1;
    a3 = g * a2;
}

float EchoDeck::Svf::lp (float x)
{
    const float v3 = x - ic2;
    const float v1 = a1 * ic1 + a2 * v3;
    const float v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2.0f * v1 - ic1;
    ic2 = 2.0f * v2 - ic2;
    return v2;
}

float EchoDeck::Svf::hp (float x)
{
    const float v3 = x - ic2;
    const float v1 = a1 * ic1 + a2 * v3;
    const float v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2.0f * v1 - ic1;
    ic2 = 2.0f * v2 - ic2;
    return x - k * v1 - v2;
}

float EchoDeck::Allpass::process (float x, float gain)
{
    const float delayed = buf[(size_t) pos];
    const float w = x + gain * delayed;
    buf[(size_t) pos] = w;
    if (++pos >= len) pos = 0;
    return delayed - gain * w;
}

void EchoDeck::Pass::reset()
{
    for (auto* f : { &dc, &styleHp, &userHp, &styleLp, &recon, &userLp }) f->reset();
    std::fill (ap1.buf.begin(), ap1.buf.end(), 0.0f);
    std::fill (ap2.buf.begin(), ap2.buf.end(), 0.0f);
    holdPhase = 1.0f;
    held = 0.0f;
}

void EchoDeck::Pass::setFilters (const Shape& s)
{
    styleHp.copyCoefs (s.styleHp);
    userHp.copyCoefs (s.userHp);
    styleLp.copyCoefs (s.styleLp);
    recon.copyCoefs (s.styleLp);
    userLp.copyCoefs (s.userLp);
}

float EchoDeck::Pass::run (float x, const Shape& s)
{
    x = dc.hp (x);
    if (s.styleHpOn) x = styleHp.hp (x);
    if (s.userHpOn) x = userHp.hp (x);
    if (s.diffusion > 0.001f)
        x = ap2.process (ap1.process (x, s.diffusion), s.diffusion);

    const float d = s.drive;
    switch (s.sat)
    {
        case 1:  x = std::tanh (d * x) / d; break;                                     // tape
        case 2:                                                                          // valve: asymmetric, unity slope at rest
        {
            constexpr float bias = 0.22f;
            const float t0 = std::tanh (d * bias);
            x = (std::tanh (d * (x + bias)) - t0) / (d * (1.0f - t0 * t0));
            break;
        }
        case 3:                                                                          // bucket brigade: compander squash
        {
            const float c = x / (1.0f + 0.35f * std::abs (x));
            x = std::tanh (d * c) / d * (1.0f + 0.35f * std::abs (c));
            break;
        }
        case 4:                                                                          // converter / small-speaker clip
        {
            const float v = d * x, v2 = v * v;
            x = v / std::sqrt (std::sqrt (1.0f + v2 * v2)) / d;
            break;
        }
        default: break;
    }

    if (s.styleLpOn) x = styleLp.lp (x);
    if (s.holdInc < 1.0f)
    {
        holdPhase += s.holdInc;
        if (holdPhase >= 1.0f) { holdPhase -= 1.0f; held = x; }
        x = recon.lp (held);
    }
    if (s.userLpOn) x = userLp.lp (x);
    return bound (x);
}

void EchoDeck::Line::prepare (int size, double sr)
{
    a.assign ((size_t) size, 0.0f);
    b.assign ((size_t) size, 0.0f);
    passA.ap1.prepare ((int) (0.0071 * sr)); passA.ap2.prepare ((int) (0.0113 * sr));
    passB.ap1.prepare ((int) (0.0089 * sr)); passB.ap2.prepare ((int) (0.0137 * sr));
    clear();
}

void EchoDeck::Line::clear()
{
    std::fill (a.begin(), a.end(), 0.0f);
    std::fill (b.begin(), b.end(), 0.0f);
    passA.reset();
    passB.reset();
}

float EchoDeck::readHermite (const std::vector<float>& buf, double d) const
{
    d = juce::jlimit (3.0, (double) bufMask - 4.0, d);
    const double pos = (double) writePos - d;
    const double fl = std::floor (pos);
    const float f = (float) (pos - fl);
    const int i = (int) fl;
    const float xm1 = buf[(size_t) ((i - 1) & bufMask)], x0 = buf[(size_t) (i & bufMask)];
    const float x1 = buf[(size_t) ((i + 1) & bufMask)], x2 = buf[(size_t) ((i + 2) & bufMask)];
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * f + c2) * f + c1) * f + x0;
}

// ---- lifecycle ---------------------------------------------------------------------------------------------------
void EchoDeck::prepareToPlay (double sr, int)
{
    sampleRate = sr;
    const int size = juce::nextPowerOfTwo ((int) (sr * bufferSeconds) + 64);
    bufMask = size - 1;
    writePos = 0;
    for (auto& l : lines) l.prepare (size, sr);
    dcTemplate.set (sr, 15.0f, 0.707f);
    for (auto& l : lines)
        for (auto* p : { &l.passA, &l.passB }) p->dc.copyCoefs (dcTemplate);
    for (auto* s : { &dryGain, &wetGain, &inGain, &outGain, &fbGain, &holdMix, &widthS })
        s->reset (sr, 0.04);
    firstBlock = true;
    duckEnv = 0.0f; duckGain = 1.0f; hissEnv = 0.0f;
    drift[0] = drift[1] = driftTarget[0] = driftTarget[1] = 0.0f;
}

void EchoDeck::releaseResources()
{
    for (auto& l : lines) { l.a.clear(); l.a.shrink_to_fit(); l.b.clear(); l.b.shrink_to_fit(); }
    bufMask = 0;
}

double EchoDeck::targetMs (double bpm) const
{
    const int tm = (int) param ("timeMode");
    double ms = tm == milliseconds ? (double) param ("timeMs")
                                   : noteBeats ((int) param ("note"), tm) * 60000.0 / juce::jlimit (20.0, 400.0, bpm);
    return juce::jlimit (1.0, maxEchoMs, ms);
}

void EchoDeck::updateShape (double ms, double sr)
{
    const auto& st = styles[juce::jlimit (0, numStyles - 1, (int) param ("style"))];
    shape.sat = st.sat;
    shape.drive = st.drive * std::pow (10.0f, (param ("saturation") - 0.3f) * 1.2f);
    shape.diffusion = juce::jlimit (0.0f, 0.62f, (st.diffusion + param ("diffusion")) * 0.7f);

    float lpHz = st.lpHz;
    shape.holdInc = 1.0f;
    if (st.holdHz < 0.0f)
    {
        // a 4096-stage bucket brigade: clock = stages / (2 x delay); its filters track the clock
        const double clock = 4096.0 / (2.0 * juce::jmax (0.001, ms / 1000.0));
        lpHz = (float) juce::jlimit (1300.0, (double) st.lpHz, clock * 0.38);
        shape.holdInc = (float) juce::jmin (1.0, clock / sr);
    }
    else if (st.holdHz > 0.0f)
        shape.holdInc = (float) juce::jmin (1.0, st.holdHz / sr);

    const float lowCut = param ("lowCut"), highCut = param ("highCut");
    shape.styleHpOn = st.hpHz > 20.0f;
    shape.userHpOn = lowCut > 20.5f;
    shape.styleLpOn = lpHz < 19500.0f || shape.holdInc < 1.0f;
    shape.userLpOn = highCut < 19500.0f;
    shape.styleHp.set (sr, st.hpHz, 0.6f);
    shape.styleLp.set (sr, lpHz, st.lpQ);
    shape.userHp.set (sr, lowCut, 0.62f);
    shape.userLp.set (sr, highCut, 0.62f);
    for (auto& l : lines) { l.passA.setFilters (shape); l.passB.setFilters (shape); }
}

// ---- audio -------------------------------------------------------------------------------------------------------
void EchoDeck::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nd;
    const int n = buffer.getNumSamples(), numCh = buffer.getNumChannels();
    if (numCh == 0 || n == 0 || bufMask == 0) return;
    float* L = buffer.getWritePointer (0);
    float* R = numCh > 1 ? buffer.getWritePointer (1) : nullptr;

    const double sr = sampleRate;
    const int mode = (int) param ("mode");
    const auto& st = styles[juce::jlimit (0, numStyles - 1, (int) param ("style"))];
    const double bpm = (int) param ("tempoSrc") == 1 ? (double) param ("tapBpm") : hostTempo();
    tempoNow.store ((float) bpm);

    double T = targetMs (bpm);
    const int steps = (int) param ("rhythmLen") + 1;
    if (mode == rhythm)
        T = juce::jmin (T, maxEchoMs / (double) steps);       // the whole pattern fits in the tape
    echoMs.store ((float) T);
    const double T2 = juce::jlimit (1.0, maxEchoMs, T * (double) param ("ratio"));

    const double groove = param ("groove") / 3.0;            // up to a triplet swing
    const double msToS = sr / 1000.0;
    const double targetA = T * (1.0 + groove) * msToS, targetB = T * (1.0 - groove) * msToS;
    const double targetA2 = T2 * (1.0 + groove) * msToS, targetB2 = T2 * (1.0 - groove) * msToS;
    const double targetStep = T * msToS;
    const double targetFeel = param ("feel") * juce::jmin (30.0, 0.2 * juce::jmin (T, mode == dual ? T2 : T)) * msToS;

    const float mix = param ("mix");
    dryGain.setTargetValue (juce::jmin (1.0f, 2.0f * (1.0f - mix)));
    wetGain.setTargetValue (juce::jmin (1.0f, 2.0f * mix));
    inGain.setTargetValue (juce::Decibels::decibelsToGain (param ("inputDb")));
    outGain.setTargetValue (juce::Decibels::decibelsToGain (param ("outputDb")));
    const bool hold = param ("hold") > 0.5f;
    fbGain.setTargetValue (param ("feedback"));
    holdMix.setTargetValue (hold ? 1.0f : 0.0f);
    widthS.setTargetValue (param ("width"));
    static const char* const tapIds[numTaps] = { "tap1", "tap2", "tap3", "tap4", "tap5", "tap6", "tap7", "tap8" };
    for (int k = 0; k < numTaps; ++k)
        tapLevel[(size_t) k] = param (tapIds[k]);

    if (firstBlock)
    {
        tA = targetA; tB = targetB; tA2 = targetA2; tB2 = targetB2; stepT = targetStep; feelOffset = targetFeel;
        for (auto* s : { &dryGain, &wetGain, &inGain, &outGain, &fbGain, &holdMix, &widthS })
            s->setCurrentAndTargetValue (s->getTargetValue());
        firstBlock = false;
    }

    // tape transport
    const double glide = 1.0 - std::exp (-1.0 / (st.glideSec * sr));
    const float wobble = param ("wobble");
    const float wowAmp = (float) (sr * st.wowPct * 0.01 / (twoPi * st.wowHz)) * wobble;
    const float flutAmp = (float) (sr * st.flutPct * 0.01 / (twoPi * st.flutHz)) * wobble;
    const float driftAmp = (float) (sr * st.driftPct * 0.01 / (twoPi * 0.25)) * wobble;
    const double wowInc[2] = { twoPi * st.wowHz / sr, twoPi * st.wowHz * 1.031 / sr };
    const double flutInc[2] = { twoPi * st.flutHz / sr, twoPi * st.flutHz * 0.977 / sr };
    const float driftCoef = (float) (1.0 - std::exp (-twoPi * 0.3 / sr));
    const int driftEvery = (int) (0.17 * sr);

    const float duck = param ("duck");
    const float duckAtt = (float) (1.0 - std::exp (-1.0 / (0.004 * sr))), duckRel = (float) (1.0 - std::exp (-1.0 / (0.22 * sr)));
    const float duckSmooth = (float) (1.0 - std::exp (-1.0 / (0.03 * sr)));
    const float hissRel = (float) std::exp (-1.0 / (0.6 * sr));
    const float quant = st.bits > 0 ? 2.0f / (float) (1 << st.bits) : 0.0f;

    float inPeak = 0.0f, wetPeak = 0.0f;

    for (int start = 0; start < n; start += chunk)
    {
        const int end = juce::jmin (n, start + chunk);
        updateShape (tA / msToS / (1.0 + groove), sr);

        for (int i = start; i < end; ++i)
        {
            tA += (targetA - tA) * glide;   tB += (targetB - tB) * glide;
            tA2 += (targetA2 - tA2) * glide; tB2 += (targetB2 - tB2) * glide;
            stepT += (targetStep - stepT) * glide;
            feelOffset += (targetFeel - feelOffset) * glide;

            // wow + flutter + slow random drift, slightly different on each side
            if (++driftCount >= driftEvery)
            {
                driftCount = 0;
                driftTarget[0] = rng.nextFloat() * 2.0f - 1.0f;
                driftTarget[1] = 0.6f * driftTarget[0] + 0.4f * (rng.nextFloat() * 2.0f - 1.0f);
            }
            double mod[2];
            for (int c = 0; c < 2; ++c)
            {
                wowPhase[c] += wowInc[c];  if (wowPhase[c] > twoPi) wowPhase[c] -= twoPi;
                flutPhase[c] += flutInc[c]; if (flutPhase[c] > twoPi) flutPhase[c] -= twoPi;
                drift[c] += (driftTarget[c] - drift[c]) * driftCoef;
                mod[c] = wowAmp * std::sin (wowPhase[c]) + flutAmp * std::sin (flutPhase[c]) + driftAmp * drift[c];
            }

            const float xl = L[i], xr = R != nullptr ? R[i] : xl;
            const float ig = inGain.getNextValue(), fb = fbGain.getNextValue(), hm = holdMix.getNextValue();
            const float send = 1.0f - hm;
            const float sl = xl * ig * send, sr2 = xr * ig * send;
            inPeak = juce::jmax (inPeak, std::abs (xl), std::abs (xr));

            // ducking follows what you play (the dry input)
            const float lvl = juce::jmax (std::abs (xl), std::abs (xr));
            duckEnv += (lvl - duckEnv) * (lvl > duckEnv ? duckAtt : duckRel);
            const float duckTarget = 1.0f - duck * 0.92f * juce::jlimit (0.0f, 1.0f, duckEnv * 4.0f);
            duckGain += (duckTarget - duckGain) * duckSmooth;

            auto runLine = [&] (Line& ln, float x, double ta, double tb, float& outA, float& outB)
            {
                const float aLoop = readHermite (ln.a, ta), bLoop = readHermite (ln.b, tb);
                outA = std::abs (feelOffset) < 0.01 ? aLoop : readHermite (ln.a, ta + feelOffset);
                outB = std::abs (feelOffset) < 0.01 ? bLoop : readHermite (ln.b, tb + feelOffset);
                float aIn = ln.passA.run (x + fb * bLoop, shape);
                float bIn = ln.passB.run (fb * aLoop, shape);
                if (hm > 0.0f) { aIn += (bLoop - aIn) * hm; bIn += (aLoop - bIn) * hm; }
                ln.a[(size_t) writePos] = aIn;
                ln.b[(size_t) writePos] = bIn;
            };

            auto runRhythm = [&] (Line& ln, float x, double m) -> float
            {
                const double step = stepT, cycle = step * steps;
                const float loopBack = readHermite (ln.a, cycle + m);
                float out = 0.0f;
                for (int k = 1; k <= steps; ++k)
                {
                    const float lv = tapLevel[(size_t) (k - 1)];
                    if (lv <= 0.0001f) continue;
                    const double t = k * step + ((k & 1) ? groove * step : 0.0) + feelOffset + m;
                    out += lv * readHermite (ln.a, t);
                }
                float in = ln.passA.run (x + fb * loopBack, shape);
                if (hm > 0.0f) in += (loopBack - in) * hm;
                ln.a[(size_t) writePos] = in;
                return out;
            };

            float wl = 0.0f, wr = 0.0f;
            const float width = widthS.getNextValue();
            switch (mode)
            {
                case dual:
                {
                    float a, b, a2, b2;
                    runLine (lines[0], sl, tA + mod[0], tB + mod[0], a, b);
                    runLine (lines[1], sr2, tA2 + mod[1], tB2 + mod[1], a2, b2);
                    wl = a + b; wr = a2 + b2;
                    break;
                }
                case pingPong:
                {
                    float a, b;
                    runLine (lines[0], 0.5f * (sl + sr2), tA + mod[0], tB + mod[0], a, b);
                    const float hard = 0.5f + 0.5f * width, soft = 0.5f - 0.5f * width;
                    wl = a * hard + b * soft;
                    wr = b * hard + a * soft;
                    // keep lines[1] quiet (a mode switch shouldn't replay old echoes)
                    lines[1].a[(size_t) writePos] = 0.0f; lines[1].b[(size_t) writePos] = 0.0f;
                    break;
                }
                case rhythm:
                    wl = runRhythm (lines[0], sl, mod[0]);
                    wr = runRhythm (lines[1], sr2, mod[1]);
                    lines[0].b[(size_t) writePos] = 0.0f; lines[1].b[(size_t) writePos] = 0.0f;
                    break;
                default:
                {
                    float a, b, a2, b2;
                    runLine (lines[0], sl, tA + mod[0], tB + mod[0], a, b);
                    runLine (lines[1], sr2, tA + mod[1], tB + mod[1], a2, b2);
                    wl = a + b; wr = a2 + b2;
                    break;
                }
            }
            if (mode != pingPong)
            {
                const float m = 0.5f * (wl + wr), s = 0.5f * (wl - wr) * width;
                wl = m + s; wr = m - s;
            }

            // playback noise of the machine, only while there are echoes; converter steps for the digital ones
            const float wa = juce::jmax (std::abs (wl), std::abs (wr));
            hissEnv = juce::jmax (wa, hissEnv * hissRel);
            if (st.hiss > 0.0f)
            {
                const float h = st.hiss * juce::jmin (1.0f, hissEnv * 30.0f);
                wl += h * (rng.nextFloat() * 2.0f - 1.0f);
                wr += h * (rng.nextFloat() * 2.0f - 1.0f);
            }
            if (quant > 0.0f && hissEnv > 1.0e-4f)
            {
                const float d1 = (rng.nextFloat() - rng.nextFloat()) * quant;
                const float d2 = (rng.nextFloat() - rng.nextFloat()) * quant;
                wl = std::round ((wl + d1) / quant) * quant;
                wr = std::round ((wr + d2) / quant) * quant;
            }

            const float og = outGain.getNextValue() * duckGain;
            wl *= og; wr *= og;
            if (! std::isfinite (wl) || ! std::isfinite (wr))
            {
                wl = wr = 0.0f;
                for (auto& l : lines) l.clear();
            }
            wetPeak = juce::jmax (wetPeak, std::abs (wl), std::abs (wr));

            const float dg = dryGain.getNextValue(), wg = wetGain.getNextValue();
            if (R != nullptr)
            {
                L[i] = xl * dg + wl * wg;
                R[i] = xr * dg + wr * wg;
            }
            else
                L[i] = xl * dg + 0.5f * (wl + wr) * wg;

            writePos = (writePos + 1) & bufMask;
        }
    }

    for (int c = 2; c < numCh; ++c) buffer.clear (c, 0, n);
    inLevel.store (juce::jmax (inPeak, inLevel.load() * 0.8f));
    echoLevel.store (juce::jmax (wetPeak, echoLevel.load() * 0.8f));
    tapeTravel.store (std::fmod (tapeTravel.load() + n / juce::jmax (1.0, tA) * 0.035, 1.0e6));
}

// ---- tap tempo ---------------------------------------------------------------------------------------------------
void EchoDeck::tap()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (! taps.empty() && now - taps.back() > 2000.0) taps.clear();
    taps.push_back (now);
    if (taps.size() > 5) taps.erase (taps.begin());
    if (taps.size() < 2) return;
    const double avg = (taps.back() - taps.front()) / (double) (taps.size() - 1);
    if ((int) param ("timeMode") == milliseconds)
        setParam ("timeMs", (float) juce::jlimit (1.0, maxEchoMs, avg));
    else
    {
        setParam ("tapBpm", (float) juce::jlimit (40.0, 240.0, 60000.0 / avg));
        setParam ("tempoSrc", 1.0f);
    }
}

// ---- presets -------------------------------------------------------------------------------------------------
namespace
{
    struct EchoPreset
    {
        const char* name;
        int style, mode, timeMode;
        float ms;
        int note;
        float fb, mix, lowCut, highCut, sat;
        float groove = 0.0f, feel = 0.0f, wobble = 1.0f, diffusion = 0.0f, width = 1.0f, duck = 0.0f, ratio = 0.75f;
        int steps = 8;
        const char* pattern = "..X..x.X";   // X = full, x = 65%, - = 35%, . = off
    };

    const EchoPreset echoPresets[] = {
        { "Studio Tape: Dotted 1/8",      0, EchoDeck::single,   EchoDeck::dotted,       375, 3, 0.32f, 0.25f, 120, 9000, 0.30f },
        { "Slapback (50s)",               2, EchoDeck::single,   EchoDeck::milliseconds,  95, 3, 0.06f, 0.32f,  90, 7000, 0.45f },
        { "Vocal Throw 1/4",              0, EchoDeck::pingPong, EchoDeck::note,         500, 4, 0.38f, 0.22f, 250, 6000, 0.30f, 0, 0, 1.0f, 0, 0.9f, 0.6f },
        { "Tape Echo Wash",               1, EchoDeck::single,   EchoDeck::dotted,       500, 4, 0.72f, 0.35f, 150, 4500, 0.50f, 0, 0, 1.2f, 0.2f },
        { "Dub Runaway",                  1, EchoDeck::single,   EchoDeck::dotted,       375, 3, 1.06f, 0.40f, 180, 3200, 0.70f, 0, 0, 1.3f },
        { "Worn Tape Lullaby",            3, EchoDeck::single,   EchoDeck::note,         500, 4, 0.45f, 0.30f, 200, 3800, 0.40f, 0, 0, 1.4f },
        { "Cassette Dream",               4, EchoDeck::pingPong, EchoDeck::dotted,       500, 4, 0.50f, 0.35f, 180, 3500, 0.35f, 0, 0, 1.2f },
        { "Bucket Brigade Dark",          5, EchoDeck::single,   EchoDeck::milliseconds, 420, 3, 0.55f, 0.30f, 100, 6000, 0.45f },
        { "Bucket Brigade Doubler",       5, EchoDeck::dual,     EchoDeck::milliseconds,  22, 3, 0.05f, 0.40f,  80, 9000, 0.30f, 0, 0, 1.8f, 0, 1.0f, 0, 1.45f },
        { "Oil Can Swirl",                6, EchoDeck::single,   EchoDeck::note,         250, 3, 0.55f, 0.35f, 250, 5000, 0.35f, 0, 0, 1.2f, 0.3f },
        { "Ping-Pong 1/8",                0, EchoDeck::pingPong, EchoDeck::note,         250, 3, 0.45f, 0.28f, 150, 8000, 0.30f },
        { "Ping-Pong Dotted Wide",        2, EchoDeck::pingPong, EchoDeck::dotted,       375, 3, 0.50f, 0.30f, 150, 7000, 0.40f },
        { "Dual: 3 Against 4",            0, EchoDeck::dual,     EchoDeck::note,         500, 4, 0.35f, 0.28f, 150, 8000, 0.30f, 0, 0, 1.0f, 0, 1.0f, 0, 0.75f },
        { "Double Tracker (ADT)",         0, EchoDeck::dual,     EchoDeck::milliseconds,  28, 3, 0.00f, 0.45f,  60, 12000, 0.25f, 0, 0, 1.6f, 0, 1.0f, 0, 1.3f },
        { "Rhythm: 3-3-2",                0, EchoDeck::rhythm,   EchoDeck::note,         125, 2, 0.35f, 0.30f, 150, 8000, 0.30f, 0, 0, 1.0f, 0, 1.0f, 0, 0.75f, 8, "..X..x.X" },
        { "Rhythm: Gallop",               2, EchoDeck::rhythm,   EchoDeck::note,         125, 2, 0.40f, 0.30f, 150, 7000, 0.40f, 0, 0, 1.0f, 0, 1.0f, 0, 0.75f, 4, ".XxX" },
        { "Rhythm: Dotted Cascade",       1, EchoDeck::rhythm,   EchoDeck::note,         250, 3, 0.50f, 0.32f, 200, 5000, 0.45f, 0, 0, 1.1f, 0, 1.0f, 0, 0.75f, 6, "..X..x" },
        { "Rhythm: Stutter Build",        7, EchoDeck::rhythm,   EchoDeck::note,         125, 2, 0.30f, 0.35f, 200, 9000, 0.25f, 0, 0, 0.0f, 0, 1.0f, 0, 0.75f, 8, "-xX-xX-X" },
        { "Swing Eighths",                0, EchoDeck::single,   EchoDeck::note,         250, 3, 0.45f, 0.25f, 150, 8000, 0.30f, 0.6f },
        { "Early Digital Lead",           7, EchoDeck::pingPong, EchoDeck::note,         500, 4, 0.40f, 0.25f, 120, 10000, 0.20f },
        { "Clean Digital 1/4",            8, EchoDeck::single,   EchoDeck::note,         500, 4, 0.35f, 0.25f,  20, 20000, 0.30f },
        { "Radio Echo",                   9, EchoDeck::single,   EchoDeck::milliseconds, 280, 3, 0.50f, 0.35f, 400, 3000, 0.40f },
        { "Psych Guitar Wash",            1, EchoDeck::pingPong, EchoDeck::dotted,       375, 3, 0.62f, 0.38f, 200, 4000, 0.55f, 0, 0, 1.5f, 0.25f },
        { "Bass Echo (tight lows)",       0, EchoDeck::single,   EchoDeck::note,         250, 3, 0.25f, 0.20f, 350, 5000, 0.30f, 0, 0, 1.0f, 0, 1.0f, 0.5f },
        { "Ducked Vocal Echo",            2, EchoDeck::single,   EchoDeck::note,         500, 4, 0.40f, 0.30f, 300, 6500, 0.30f, 0, 0, 1.0f, 0, 1.0f, 0.85f },
        { "Lazy Behind-the-Beat",         0, EchoDeck::single,   EchoDeck::note,         250, 3, 0.40f, 0.28f, 150, 7000, 0.35f, 0, 0.7f },
        { "Short Tape Flange",            0, EchoDeck::single,   EchoDeck::milliseconds, 4.5f, 3, 0.60f, 0.50f, 60, 12000, 0.30f, 0, 0, 2.0f },
        { "Ambient Tape Smear",             3, EchoDeck::single,   EchoDeck::dotted,       500, 4, 0.85f, 0.45f, 200, 3500, 0.40f, 0, 0, 1.3f, 0.35f },
    };
}

juce::StringArray EchoDeck::getProgramNames()
{
    juce::StringArray s;
    for (auto& p : echoPresets) s.add (p.name);
    return s;
}

void EchoDeck::loadProgram (int i)
{
    if (! juce::isPositiveAndBelow (i, (int) std::size (echoPresets))) return;
    const auto& p = echoPresets[i];
    setParam ("style", (float) p.style);
    setParam ("mode", (float) p.mode);
    setParam ("timeMode", (float) p.timeMode);
    setParam ("timeMs", p.ms);
    setParam ("note", (float) p.note);
    setParam ("feedback", p.fb);
    setParam ("mix", p.mix);
    setParam ("lowCut", p.lowCut);
    setParam ("highCut", p.highCut);
    setParam ("saturation", p.sat);
    setParam ("groove", p.groove);
    setParam ("feel", p.feel);
    setParam ("wobble", p.wobble);
    setParam ("diffusion", p.diffusion);
    setParam ("width", p.width);
    setParam ("duck", p.duck);
    setParam ("ratio", p.ratio);
    setParam ("rhythmLen", (float) (p.steps - 1));
    setParam ("inputDb", 0.0f);
    setParam ("outputDb", 0.0f);
    setParam ("hold", 0.0f);
    setParam ("tempoSrc", 0.0f);
    for (int k = 0; k < numTaps; ++k)
    {
        const char c = k < (int) std::strlen (p.pattern) ? p.pattern[k] : '.';
        setParam ("tap" + juce::String (k + 1), c == 'X' ? 1.0f : c == 'x' ? 0.65f : c == '-' ? 0.35f : 0.0f);
    }
}

} // namespace wis::daw
