#include "CabSim.h"
#include "Separation/AudioFileLoader.h"
#include <complex>

namespace wis
{

using AC = juce::dsp::IIR::ArrayCoefficients<float>;

template <size_t N>
static void assign (juce::dsp::IIR::Filter<float>& f, const std::array<float, N>& c) { *f.coefficients = c; }

juce::StringArray cabTypeNames()
{
    return { "4x12 British", "4x12 Modern", "2x12 American", "2x12 British Blue", "1x12 Open Back", "1x12 Tweed",
             "Bass 1x15 Vintage", "Bass 2x15 60s", "Bass 4x10 + Horn", "Bass 8x10", "Bass 2x10 Modern", "Impulse Response" };
}

bool cabTypeIsBass (CabType t) { return t >= CabType::bass1x15Vintage && t <= CabType::bass2x10Modern; }

int migrateCabTypeV1 (int old)
{
    // 1.x - 3.0: 4x12 British, 2x12 American, 1x12 Open Back, 4x10 Bass, 8x10 Bass, Impulse Response
    static const CabType map[] = { CabType::brit4x12, CabType::american2x12, CabType::openBack1x12, CabType::bass4x10Horn,
                                   CabType::bass8x10, CabType::impulseResponse };
    return (int) map[juce::jlimit (0, 5, old)];
}

juce::StringArray micTypeNames()
{
    return { "Dynamic Mic", "Ribbon Mic", "Condenser Mic", "Dynamic + Ribbon" };
}

// =====================================================================================================
//  Impulse response design
// =====================================================================================================
namespace
{
    struct Bump { float hz, sigmaOct, db; };
    struct CabSpec
    {
        float hpHz, hpQ, lpHz, lpQ, lp2Hz;
        std::vector<Bump> bumps;
        float ripple;   // cone break-up irregularity, dB
    };

    CabSpec specFor (CabType t)
    {
        switch (t)
        {
            case CabType::brit4x12:        return { 80, 1.1f, 5000, 0.9f, 6500, { { 110, 0.5f, 2.5f }, { 500, 1.0f, -3.0f }, { 1400, 0.6f, 1.5f }, { 2500, 0.5f, 4.5f }, { 3800, 0.35f, 2.0f } }, 2.5f };
            case CabType::modern4x12:      return { 85, 1.2f, 5600, 0.8f, 7000, { { 100, 0.5f, 2.0f }, { 400, 1.0f, -4.0f }, { 1600, 0.5f, 2.0f }, { 2800, 0.45f, 5.0f }, { 4800, 0.3f, 2.5f } }, 3.0f };
            case CabType::american2x12:    return { 75, 0.9f, 6800, 0.75f, 8500, { { 130, 0.6f, 1.5f }, { 600, 1.0f, -3.0f }, { 2800, 0.6f, 3.0f }, { 5000, 0.5f, 1.0f } }, 2.0f };
            case CabType::britBlue2x12:    return { 95, 1.0f, 6500, 0.9f, 8000, { { 150, 0.6f, 1.0f }, { 450, 1.0f, -2.0f }, { 1800, 0.6f, 2.0f }, { 3200, 0.5f, 5.0f }, { 5200, 0.4f, 2.0f } }, 3.0f };
            case CabType::openBack1x12:    return { 100, 0.75f, 7000, 0.8f, 9000, { { 160, 0.6f, 1.0f }, { 400, 0.9f, -1.5f }, { 2000, 0.6f, 3.0f } }, 2.0f };
            case CabType::tweed1x12:       return { 90, 0.8f, 5000, 0.8f, 6500, { { 180, 0.7f, 1.5f }, { 800, 1.0f, -1.0f }, { 2200, 0.6f, 3.0f }, { 3600, 0.4f, 1.5f } }, 2.5f };
            case CabType::bass1x15Vintage: return { 45, 1.2f, 2600, 0.8f, 3500, { { 85, 0.6f, 4.0f }, { 250, 0.8f, 1.0f }, { 700, 1.0f, -2.0f }, { 1500, 0.6f, 1.0f } }, 1.5f };
            case CabType::bass2x15Sixties: return { 50, 1.1f, 3000, 0.9f, 4000, { { 100, 0.6f, 3.5f }, { 350, 0.9f, 1.0f }, { 900, 0.9f, -2.0f } }, 1.5f };
            case CabType::bass4x10Horn:    return { 50, 0.9f, 12000, 0.7f, 15000, { { 90, 0.6f, 2.5f }, { 500, 1.0f, -3.0f }, { 2500, 0.6f, 2.0f }, { 4000, 0.5f, -3.0f } }, 1.5f };
            case CabType::bass8x10:        return { 42, 1.1f, 4200, 0.8f, 5200, { { 75, 0.6f, 4.0f }, { 250, 0.8f, 2.0f }, { 500, 0.9f, -3.0f }, { 1800, 0.6f, 1.5f } }, 2.0f };
            case CabType::bass2x10Modern:  return { 55, 0.9f, 9000, 0.7f, 12000, { { 100, 0.6f, 2.0f }, { 600, 1.0f, -2.0f }, { 2500, 0.6f, 1.5f } }, 1.0f };
            case CabType::impulseResponse:
            case CabType::count:
            default:                       return { 20, 0.7f, 20000, 0.7f, 20000, {}, 0.0f };
        }
    }

    inline double hp2 (double f, double f0, double q) { const double r = f / f0, r2 = r * r; return r2 / std::sqrt ((1 - r2) * (1 - r2) + (r / q) * (r / q)); }
    inline double lp2 (double f, double f0, double q) { const double r = f / f0, r2 = r * r; return 1.0 / std::sqrt ((1 - r2) * (1 - r2) + (r / q) * (r / q)); }
    inline double lp1 (double f, double f0) { const double r = f / f0; return 1.0 / std::sqrt (1 + r * r); }
    inline double bumpDb (double f, const Bump& b) { const double o = std::log2 (f / b.hz) / b.sigmaOct; return b.db * std::exp (-0.5 * o * o); }
}

juce::AudioBuffer<float> CabSim::designImpulseResponse (CabType type, MicType mic, float pos, float room, double sr)
{
    constexpr int order = 13, N = 1 << order;
    const auto spec = specFor (type);
    pos = juce::jlimit (0.0f, 1.0f, pos);
    room = juce::jlimit (0.0f, 1.0f, room);

    // cone break-up: a fixed, speaker-specific set of narrow peaks and dips
    std::vector<Bump> ripple;
    {
        juce::Random rng (1234 + (int) type * 97);
        for (int i = 0; i < 12; ++i)
        {
            const float hz = 1200.0f * std::pow (7.0f, rng.nextFloat());
            ripple.push_back ({ hz, 0.04f + 0.08f * rng.nextFloat(), spec.ripple * (rng.nextFloat() * 2.0f - 1.0f) * (1.0f - 0.6f * pos) });
        }
    }
    const double posLp = 12000.0 * std::pow (3200.0 / 12000.0, (double) std::pow (pos, 0.8f));
    const double proximity = 1.0 - room;

    auto micGainDb = [&] (double f, MicType m) -> double
    {
        switch (m)
        {
            case MicType::ribbon:
                return juce::Decibels::gainToDecibels (lp1 (f, 7000.0)) + bumpDb (f, { 200, 1.0f, 1.5f }) + bumpDb (f, { 3000, 0.6f, -1.5f })
                       + 4.0 * proximity * lp1 (f, 140.0);
            case MicType::condenser:
                return bumpDb (f, { 10000, 0.8f, 2.0f }) + bumpDb (f, { 4000, 0.8f, 1.0f }) + 1.0 * proximity * lp1 (f, 120.0);
            case MicType::dynamic:
            default:
                return bumpDb (f, { 5000, 0.5f, 3.5f }) + juce::Decibels::gainToDecibels (lp2 (f, 13000.0, 0.7)) + 3.0 * proximity * lp1 (f, 150.0);
        }
    };

    auto magnitude = [&] (double f) -> double
    {
        f = juce::jmax (1.0, f);
        double db = 0.0;
        for (auto& b : spec.bumps) db += bumpDb (f, { b.hz, b.sigmaOct, b.db * (b.hz > 1000 ? 1.0f - 0.5f * pos : 1.0f) });
        for (auto& b : ripple) db += bumpDb (f, b);
        db += bumpDb (f, { 250, 0.8f, 2.0f * pos });                     // off-axis: more low-mid body
        double g = juce::Decibels::decibelsToGain (db);
        g *= hp2 (f, spec.hpHz, spec.hpQ) * lp2 (f, spec.lpHz, spec.lpQ) * lp1 (f, spec.lp2Hz) * lp1 (f, posLp);
        if (mic == MicType::dynamicPlusRibbon)
            g *= 0.5 * (juce::Decibels::decibelsToGain (micGainDb (f, MicType::dynamic)) + juce::Decibels::decibelsToGain (micGainDb (f, MicType::ribbon)));
        else
            g *= juce::Decibels::decibelsToGain (micGainDb (f, mic));
        return juce::jmax (1.0e-5, g);
    };

    // minimum-phase impulse response from the magnitude (real cepstrum method)
    juce::dsp::FFT fft (order);
    std::vector<std::complex<float>> a ((size_t) N), b ((size_t) N);
    for (int k = 0; k <= N / 2; ++k)
    {
        const float lm = (float) std::log (magnitude (k * sr / N));
        a[(size_t) k] = lm;
        if (k > 0 && k < N / 2) a[(size_t) (N - k)] = lm;
    }
    fft.perform (a.data(), b.data(), true);                        // cepstrum
    // JUCE's inverse FFT is scaled by 1/N
    for (int n = 0; n < N; ++n)
    {
        const float c = b[(size_t) n].real();
        a[(size_t) n] = n == 0 || n == N / 2 ? c : (n < N / 2 ? 2.0f * c : 0.0f);
    }
    fft.perform (a.data(), b.data(), false);
    for (auto& z : b) z = std::exp (z);
    fft.perform (b.data(), a.data(), true);

    const int L = juce::jmin (N, (int) (sr * 0.045));
    const int fade = L / 6;
    std::vector<float> ir ((size_t) L);
    for (int n = 0; n < L; ++n)
    {
        float w = 1.0f;
        if (n > L - fade) w = 0.5f + 0.5f * std::cos (juce::MathConstants<float>::pi * (float) (n - (L - fade)) / (float) fade);
        ir[(size_t) n] = a[(size_t) n].real() * w;
    }

    // room: a few early reflections off the floor and walls, each a little duller
    static const float delaysMs[] = { 2.7f, 4.9f, 7.3f, 10.6f, 14.1f, 18.8f, 24.0f };
    static const float gains[] = { 0.45f, 0.36f, 0.3f, 0.24f, 0.18f, 0.13f, 0.09f };
    const int maxDelay = (int) (sr * 0.025);
    juce::AudioBuffer<float> out (1, L + (room > 0.001f ? maxDelay : 0));
    out.clear();
    auto* o = out.getWritePointer (0);
    for (int n = 0; n < L; ++n) o[n] = ir[(size_t) n];
    if (room > 0.001f)
    {
        for (int r = 0; r < 7; ++r)
        {
            const int d = (int) (delaysMs[r] * 0.001 * sr);
            const float g = room * gains[r] * (r % 2 == 0 ? 1.0f : -1.0f);
            const float coeff = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * (4500.0f - 450.0f * r) / (float) sr);
            float lp = 0.0f;
            for (int n = 0; n < L && n + d < out.getNumSamples(); ++n)
            {
                lp += (ir[(size_t) n] - lp) * coeff;
                o[n + d] += lp * g;
            }
        }
    }

    // normalise: about unity gain through the mids, so cabinets swap without big level jumps
    double sum = 0.0;
    int count = 0;
    for (double f = 300.0; f <= 3000.0; f *= 1.06)
    {
        std::complex<double> h (0.0, 0.0);
        const double w = juce::MathConstants<double>::twoPi * f / sr;
        for (int n = 0; n < out.getNumSamples(); ++n) h += (double) o[n] * std::polar (1.0, -w * n);
        sum += std::abs (h);
        ++count;
    }
    const float norm = (float) (count / juce::jmax (1.0e-9, sum));
    out.applyGain (norm);
    return out;
}

// =====================================================================================================
//  Background builder
// =====================================================================================================
struct CabSim::Builder : public juce::Thread
{
    explicit Builder (CabSim& o) : juce::Thread ("Cab IR builder"), owner (o) {}
    void run() override
    {
        while (! threadShouldExit())
        {
            wait (15);
            owner.conv.collectGarbage();
            const int want = owner.wantedKey.load();
            if (want < 0 || want == owner.builtKey.load() || ! owner.conv.isIdle()) continue;
            CabType t; MicType m; float pos, room;
            decode (want, t, m, pos, room);
            auto ir = designImpulseResponse (t, m, pos, room, owner.sampleRate);
            if (owner.conv.offer (Convolver::Kernel::make (ir.getReadPointer (0), ir.getNumSamples())))
                owner.builtKey = want;
        }
    }
    static void decode (int key, CabType& t, MicType& m, float& pos, float& room)
    {
        room = (float) (key % 21) / 20.0f; key /= 21;
        pos = (float) (key % 21) / 20.0f;  key /= 21;
        m = (MicType) (key % 4);           key /= 4;
        t = (CabType) key;
    }
    CabSim& owner;
};

int CabSim::keyFor (CabType t, MicType m, float pos, float room)
{
    const int pq = juce::roundToInt (juce::jlimit (0.0f, 1.0f, pos) * 20.0f);
    const int rq = juce::roundToInt (juce::jlimit (0.0f, 1.0f, room) * 20.0f);
    return (((int) t * 4 + (int) m) * 21 + pq) * 21 + rq;
}

CabSim::CabSim() : builder (std::make_unique<Builder> (*this)) {}

CabSim::~CabSim()
{
    builder->stopThread (2000);
}

void CabSim::prepare (double sr, int maxBlock)
{
    builder->stopThread (2000);
    sampleRate = sr;
    for (auto* f : { &userLowCut, &userHighCut, &userHighCut2 }) { f->coefficients->coefficients.ensureStorageAllocated (8); f->reset(); }
    rebuildNow();
    convolution.prepare ({ sr, (juce::uint32) maxBlock, 1 });
    dirty = true;
    updateFilters();
    builder->startThread (juce::Thread::Priority::low);
}

void CabSim::rebuildNow()
{
    int key = wantedKey.load();
    if (key < 0) key = keyFor (type == CabType::impulseResponse ? CabType::brit4x12 : type, MicType::dynamic, 0.3f, 0.1f);
    CabType kt; MicType km; float kp, kr;
    Builder::decode (key, kt, km, kp, kr);
    auto ir = designImpulseResponse (kt, km, kp, kr, sampleRate);
    conv.reset (Convolver::Kernel::make (ir.getReadPointer (0), ir.getNumSamples()));
    wantedKey = key;
    builtKey = key;
}

void CabSim::reset()
{
    userLowCut.reset(); userHighCut.reset(); userHighCut2.reset();
    convolution.reset();
}

void CabSim::setParameters (CabType t, float lo, float hi, MicType mic, float micPos, float room)
{
    if (lo != lowCut || hi != highCut)
        dirty = true;
    type = t; lowCut = lo; highCut = hi;
    if (t != CabType::impulseResponse)
        wantedKey = keyFor (t, mic, micPos, room);
}

void CabSim::updateFilters()
{
    if (! dirty) return;
    dirty = false;
    const float nyq = (float) (sampleRate * 0.45);
    assign (userLowCut,   AC::makeHighPass (sampleRate, juce::jlimit (10.0f, 1000.0f, lowCut), 0.707f));
    assign (userHighCut,  AC::makeLowPass  (sampleRate, juce::jlimit (1000.0f, nyq, highCut), 0.54f));
    assign (userHighCut2, AC::makeLowPass  (sampleRate, juce::jlimit (1000.0f, nyq, highCut), 1.31f));   // 4th-order Butterworth pair
}

bool CabSim::loadImpulseResponse (const juce::File& file)
{
    if (! file.existsAsFile())
        return false;

    std::unique_ptr<juce::AudioFormatReader> test (sharedFormatManager().createReaderFor (file));
    if (test == nullptr || test->lengthInSamples < 16)
        return false;

    // Cabinet IRs rarely need more than 500 ms; trimming keeps CPU low with long room IRs.
    convolution.loadImpulseResponse (file, juce::dsp::Convolution::Stereo::no, juce::dsp::Convolution::Trim::yes,
                                     (size_t) (sampleRate * 0.5), juce::dsp::Convolution::Normalise::yes);
    irFile = file;
    irReady = true;
    return true;
}

void CabSim::clearImpulseResponse()
{
    irReady = false;
    irFile = juce::File();
}

void CabSim::process (float* data, int numSamples)
{
    updateFilters();

    float* chans[] = { data };
    juce::dsp::AudioBlock<float> block (chans, 1, (size_t) numSamples);
    if (type == CabType::impulseResponse)
    {
        if (irReady.load())
        {
            convolution.process (juce::dsp::ProcessContextReplacing<float> (block));
            // IRs are normalised; bring them back to roughly the same loudness as the built-in cabs.
            juce::FloatVectorOperations::multiply (data, 0.8f, numSamples);
        }
    }
    else
    {
        conv.process (data, numSamples);
    }

    for (int i = 0; i < numSamples; ++i)
        data[i] = userHighCut2.processSample (userHighCut.processSample (userLowCut.processSample (data[i])));
}

} // namespace wis
