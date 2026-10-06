#include "VocalTune.h"
#include "MidiEffects.h"

namespace wis::daw
{

std::function<juce::AudioProcessorEditor* (VocalTune&)> VocalTune::editorFactory;

namespace
{
    constexpr int ringSize = 1 << 14;            // input / output history (power of two)
    constexpr int ringMask = ringSize - 1;
    constexpr int decimation = 4;                // pitch detection runs at a quarter of the sample rate
    constexpr int maxMarks = 512;

    // vocal ranges: lowest / highest fundamental searched
    struct Range { const char* name; float lo, hi; };
    const Range ranges[] = { { "Soprano", 180.0f, 1100.0f }, { "Alto / Tenor", 95.0f, 800.0f }, { "Low Male", 70.0f, 500.0f }, { "Instrument", 60.0f, 1200.0f } };
}

struct VocalTune::Impl
{
    double sr = 48000.0;
    int latency = 2048;
    int maxPeriod = 600, minPeriod = 40, maxHalf = 900;
    float loHz = 95.0f, hiHz = 800.0f;

    std::vector<float> inL, inR, outL, outR, wsum;   // rings
    std::vector<float> dec;                          // decimated mono ring
    juce::int64 t = 0;                               // absolute input sample count
    juce::int64 decCount = 0;
    float decAcc = 0.0f;
    int decPhase = 0;

    // pitch tracking
    int hopCounter = 0;
    float period = 256.0f;        // samples at the full rate
    bool voiced = false;
    float lastMidi = -1.0f, target = -1.0f, corr = 0.0f;

    // analysis marks (input positions) and their periods
    struct Mark { double pos; float period; };
    std::vector<Mark> marks = std::vector<Mark> (maxMarks);
    int markHead = 0, markCount = 0;
    double nextMark = 0.0;
    double nextSynth = 0.0;

    void init (double rate, int rangeIndex)
    {
        sr = rate;
        const auto& r = ranges[juce::jlimit (0, 3, rangeIndex)];
        loHz = r.lo; hiHz = r.hi;
        maxPeriod = (int) std::ceil (sr / loHz);
        minPeriod = (int) std::floor (sr / hiHz);
        maxHalf = (int) std::ceil (maxPeriod * 1.45);           // grains for up to +-6 semitones of correction
        latency = 2 * maxHalf + maxPeriod / 2 + 64;
        for (auto* v : { &inL, &inR, &outL, &outR, &wsum }) v->assign ((size_t) ringSize, 0.0f);
        dec.assign ((size_t) ringSize, 0.0f);
        t = 0; decCount = 0; decAcc = 0; decPhase = 0; hopCounter = 0;
        period = 256.0f; voiced = false; lastMidi = -1.0f; target = -1.0f; corr = 0.0f;
        markHead = 0; markCount = 0; nextMark = 0.0;
        nextSynth = latency;   // the first grain is centred on output time = latency
    }

    /** YIN on the decimated signal: returns the period in full-rate samples, or 0 if unvoiced. */
    float detect()
    {
        const int w = juce::jmin ((int) (sr / decimation * 0.032), 1024);   // ~32 ms window
        const int maxLag = juce::jmin (w - 2, (int) std::ceil (sr / decimation / loHz) + 2);
        const int minLag = juce::jmax (2, (int) std::floor (sr / decimation / hiHz));
        if (decCount < w + maxLag) return 0.0f;
        const juce::int64 end = decCount;
        auto x = [&] (juce::int64 i) { return dec[(size_t) (i & ringMask)]; };

        float energy = 0.0f;
        for (int i = 0; i < w; ++i) { const float v = x (end - w + i); energy += v * v; }
        if (energy / w < 1.0e-6f) return 0.0f;   // silence

        static thread_local std::vector<float> d;
        d.assign ((size_t) maxLag + 2, 0.0f);
        const juce::int64 start = end - w - maxLag;
        for (int lag = 1; lag <= maxLag + 1; ++lag)
        {
            float sum = 0.0f;
            for (int i = 0; i < w; ++i)
            {
                const float diff = x (start + i) - x (start + i + lag);
                sum += diff * diff;
            }
            d[(size_t) lag] = sum;
        }
        // cumulative mean normalised difference
        float running = 0.0f;
        d[0] = 1.0f;
        for (int lag = 1; lag <= maxLag + 1; ++lag)
        {
            running += d[(size_t) lag];
            d[(size_t) lag] = running > 0.0f ? d[(size_t) lag] * lag / running : 1.0f;
        }
        int best = -1;
        for (int lag = minLag; lag <= maxLag; ++lag)
            if (d[(size_t) lag] < 0.15f)
            {
                while (lag + 1 <= maxLag && d[(size_t) lag + 1] < d[(size_t) lag]) ++lag;
                best = lag;
                break;
            }
        if (best < 0) return 0.0f;
        // parabolic refinement
        double lag = best;
        const float a = d[(size_t) best - 1], b = d[(size_t) best], c = d[(size_t) best + 1];
        const float den = a - 2.0f * b + c;
        if (std::abs (den) > 1.0e-9f) lag += 0.5 * (a - c) / den;
        return (float) (lag * decimation);
    }

    void pushMark (double pos, float p)
    {
        const int idx = (markHead + markCount) % maxMarks;
        marks[(size_t) idx] = { pos, p };
        if (markCount < maxMarks) ++markCount; else markHead = (markHead + 1) % maxMarks;
    }

    const Mark* nearestMark (double a)
    {
        // drop marks that are well behind
        while (markCount > 2 && marks[(size_t) ((markHead + 1) % maxMarks)].pos < a - maxPeriod * 2)
        {
            markHead = (markHead + 1) % maxMarks;
            --markCount;
        }
        const Mark* best = nullptr;
        double bestDist = 1.0e18;
        for (int k = 0; k < markCount; ++k)
        {
            const auto& m = marks[(size_t) ((markHead + k) % maxMarks)];
            const double dd = std::abs (m.pos - a);
            if (dd < bestDist) { bestDist = dd; best = &m; }
            else if (m.pos > a) break;
        }
        return best;
    }
};

static prm::Layout tuneLayout()
{
    prm::Layout l;
    juce::StringArray keys { "Song Key", "C", "C#/Db", "D", "D#/Eb", "E", "F", "F#/Gb", "G", "G#/Ab", "A", "A#/Bb", "B" };
    prm::addChoice (l, "key", "Key", keys, 0);
    prm::addChoice (l, "scale", "Scale", MidiFxBase::scaleNames(), 0);
    prm::addMs (l, "speed", "Retune Speed", 0.0f, 400.0f, 25.0f);
    prm::addPercent (l, "amount", "Amount", 1.0f);
    prm::addChoice (l, "range", "Voice", { "Soprano", "Alto / Tenor", "Low Male", "Instrument" }, 1);
    prm::addFloat (l, "transpose", "Transpose", -12.0f, 12.0f, 0.0f, "st", 0.0f, 0);
    prm::addPercent (l, "mix", "Mix", 1.0f);
    return l;
}

VocalTune::VocalTune() : BuiltinProcessor ("autotune", "Vocal Tune", false, tuneLayout()), impl (std::make_unique<Impl>()) {}
VocalTune::~VocalTune() = default;

juce::StringArray VocalTune::getProgramNames()
{
    return { "Natural Correction", "Pop Polish", "Hard Tune (Robot)", "Chromatic Gentle", "Minor Key Trap", "Octave Down Monster" };
}

void VocalTune::loadProgram (int index)
{
    struct P { int scale; float speed, amount, transpose; };
    static const P p[] = { { 0, 60, 0.8f, 0 }, { 0, 20, 1.0f, 0 }, { 0, 0, 1.0f, 0 }, { 13, 40, 0.7f, 0 }, { 1, 0, 1.0f, 0 }, { 13, 10, 1.0f, -12 } };
    const auto& x = p[juce::jlimit (0, 5, index)];
    setParam ("key", 0.0f);
    setParam ("scale", (float) x.scale);
    setParam ("speed", x.speed);
    setParam ("amount", x.amount);
    setParam ("transpose", x.transpose);
}

void VocalTune::prepareToPlay (double sr, int)
{
    sampleRate = sr;
    currentRange = (int) param ("range");
    impl->init (sr, currentRange);
    setLatencySamples (impl->latency);
}

void VocalTune::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nd;
    auto& m = *impl;
    if ((int) param ("range") != currentRange)
    {
        currentRange = (int) param ("range");
        m.init (sampleRate, currentRange);
        setLatencySamples (m.latency);
    }

    const int n = buffer.getNumSamples();
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (buffer.getNumChannels() > 1 ? 1 : 0);
    const int keyChoice = (int) param ("key");
    const int key = keyChoice == 0 ? songKey.load() : keyChoice - 1;
    int scale = (int) param ("scale");
    if (keyChoice == 0 && scale <= 1) scale = songScale.load() == 1 ? 1 : 0;
    const float speedMs = param ("speed");
    const float amount = param ("amount");
    const float transpose = param ("transpose");
    const float mix = param ("mix");
    const int hop = 256;
    const float tau = speedMs * 0.001f;
    const float smooth = tau <= 0.0005f ? 1.0f : 1.0f - std::exp (-(float) hop / (tau * (float) sampleRate));

    for (int i = 0; i < n; ++i)
    {
        // ---- input ----
        const float l = L[i], r = R[i];
        const size_t wi = (size_t) (m.t & ringMask);
        m.inL[wi] = l; m.inR[wi] = r;
        m.decAcc += 0.5f * (l + r);
        if (++m.decPhase == decimation)
        {
            m.dec[(size_t) (m.decCount & ringMask)] = m.decAcc / decimation;
            ++m.decCount;
            m.decAcc = 0.0f; m.decPhase = 0;
        }

        // ---- pitch, every hop ----
        if (++m.hopCounter >= hop)
        {
            m.hopCounter = 0;
            const float p = m.detect();
            m.voiced = p > 0.0f;
            if (m.voiced)
            {
                m.period = p;
                const float f0 = (float) sampleRate / p;
                const float midi = 69.0f + 12.0f * std::log2 (f0 / 440.0f);
                // target: nearest note in the scale, with a little hysteresis so it doesn't flicker
                const int snapped = MidiFxBase::snapToScale ((int) std::round (midi), key, scale);
                if (m.target < 0.0f || std::abs (midi - m.target) > 0.65f) m.target = (float) snapped;
                const float desired = (m.target - midi) * amount + transpose;
                m.corr += (desired - m.corr) * smooth;
                detectedNote = midi;
                targetNote = m.target;
            }
            else
            {
                m.corr += (transpose - m.corr) * 0.2f;
                m.target = -1.0f;
                detectedNote = -1.0f;
                targetNote = -1.0f;
            }
            correction = m.corr;
        }

        // ---- analysis marks: one per period (or every 256 samples when there's no pitch) ----
        const float markPeriod = m.voiced ? juce::jlimit ((float) m.minPeriod, (float) m.maxPeriod, m.period) : 256.0f;
        while (m.nextMark <= (double) m.t - m.maxHalf)
        {
            m.pushMark (m.nextMark, markPeriod);
            m.nextMark += markPeriod;
        }

        // ---- synthesis: place grains whose start has come into view ----
        const float ratio = juce::jlimit (0.7f, 1.43f, std::pow (2.0f, m.corr / 12.0f));
        while (m.nextSynth - m.maxHalf <= (double) m.t)
        {
            const double a = m.nextSynth - m.latency;   // the input moment this output grain represents
            const auto* mark = m.nearestMark (a);
            const float P = mark != nullptr ? mark->period : 256.0f;
            const double centreIn = mark != nullptr ? mark->pos : a;
            const float spacing = std::abs (ratio - 1.0f) < 1.0e-3f || ! m.voiced ? P : P / ratio;
            const int half = juce::jmin (m.maxHalf, (int) std::ceil (juce::jmax (P, spacing)));
            const double centreOut = m.nextSynth;
            for (int j = -half; j < half; ++j)
            {
                const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * (float) (j + half) / (float) half);
                const juce::int64 src = (juce::int64) std::llround (centreIn) + j;
                const juce::int64 dst = (juce::int64) std::llround (centreOut) + j;
                if (src < 0 || src > m.t || dst < m.t) continue;
                const size_t s = (size_t) (src & ringMask), d = (size_t) (dst & ringMask);
                m.outL[d] += m.inL[s] * w;
                m.outR[d] += m.inR[s] * w;
                m.wsum[d] += w;
            }
            m.nextSynth += spacing;
        }

        // ---- output (latency samples behind the input) ----
        const size_t oi = (size_t) (m.t & ringMask);
        const float ws = m.wsum[oi];
        const juce::int64 dryIndex = m.t - m.latency;
        const float dryL = dryIndex >= 0 ? m.inL[(size_t) (dryIndex & ringMask)] : 0.0f;
        const float dryR = dryIndex >= 0 ? m.inR[(size_t) (dryIndex & ringMask)] : 0.0f;
        float wetL = dryL, wetR = dryR;
        if (ws > 0.2f) { wetL = m.outL[oi] / ws; wetR = m.outR[oi] / ws; }
        m.outL[oi] = 0.0f; m.outR[oi] = 0.0f; m.wsum[oi] = 0.0f;
        L[i] = wetL * mix + dryL * (1.0f - mix);
        R[i] = wetR * mix + dryR * (1.0f - mix);
        ++m.t;
    }
    if (buffer.getNumChannels() > 1 && R == L) buffer.copyFrom (1, 0, buffer, 0, 0, n);
}

} // namespace wis::daw
