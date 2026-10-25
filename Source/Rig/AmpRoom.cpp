#include "AmpRoom.h"
#include "SpeakerModel.h"
#include "Daw/Instruments/RoomIr.h"
#include <complex>
#include <map>
#include <mutex>

namespace wis
{

using cd = std::complex<double>;
static constexpr double soundSpeed = 343.0;
static constexpr double twoPi = juce::MathConstants<double>::twoPi;

namespace
{
    struct RoomDef
    {
        const char* name;
        const char* description;
        double L, W, H;                  // room (m): length along the cab's axis, width, height
        double cabX;                     // cab's front from the wall behind it (m)
        double micDist, micHeight, spacing;   // room mics: distance in front of the cab, height, pair spacing (m)
        double rtLow, rtMid, rtHigh;     // reverb time (s) below 250 Hz, mids, above 4 kHz
        double scattering;               // share of each bounce that goes diffuse instead of mirror-like
    };

    const std::vector<RoomDef>& rooms()
    {
        static const std::vector<RoomDef> r {
            { "Studio Live Room", "A wood-and-plaster tracking room: the classic close mic plus room mics sound",
              7.5, 6.0, 3.2, 2.0, 2.2, 1.6, 0.6, 0.62, 0.55, 0.42, 0.45 },
            { "Small Room", "A bedroom or rehearsal room: close walls, short and a bit boxy",
              4.2, 3.4, 2.5, 0.9, 1.4, 1.3, 0.5, 0.45, 0.38, 0.28, 0.5 },
            { "Big Live Room", "A large studio hall with a high ceiling: big, open and controlled",
              13.0, 9.5, 5.5, 3.5, 3.6, 2.0, 0.8, 1.15, 1.0, 0.75, 0.4 },
            { "Concrete Garage", "Bare concrete walls and floor: loud, bright and ringing, the garage-rock room",
              6.0, 5.4, 2.7, 1.5, 2.0, 1.4, 0.6, 1.2, 1.35, 1.05, 0.2 },
            { "Hall Stage", "On stage in a theatre: the amp fills a big hall",
              22.0, 16.0, 9.0, 5.0, 6.0, 2.5, 1.2, 1.9, 1.75, 1.25, 0.45 },
        };
        return r;
    }

    /** Wall reflection factor (amplitude) per bounce for a reverb time, from Eyring's formula. */
    double reflectionFor (const RoomDef& r, double rt)
    {
        const double V = r.L * r.W * r.H, S = 2.0 * (r.L * r.W + r.L * r.H + r.W * r.H);
        const double alpha = juce::jlimit (0.02, 0.95, 1.0 - std::exp (-0.161 * V / (S * juce::jmax (0.05, rt))));
        return std::sqrt (1.0 - alpha);
    }

    double bandInterp (double f, double lo, double mid, double hi)
    {
        // smooth in log frequency: lo below 250 Hz, mid around 1 kHz, hi above 4 kHz
        const double lf = std::log2 (juce::jmax (1.0, f));
        const double a = juce::jlimit (0.0, 1.0, (lf - std::log2 (250.0)) / 2.0);
        const double b = juce::jlimit (0.0, 1.0, (lf - std::log2 (1000.0)) / 2.0);
        return b > 0.0 ? mid + (hi - mid) * b : lo + (mid - lo) * a;
    }

    std::vector<cd> minimumPhase (const std::vector<double>& mag, int N)
    {
        int order = 0; while ((1 << order) < N) ++order;
        juce::dsp::FFT fft (order);
        std::vector<std::complex<float>> a ((size_t) N), b ((size_t) N);
        for (int k = 0; k <= N / 2; ++k)
        {
            const float lm = (float) std::log (juce::jmax (1.0e-7, mag[(size_t) k]));
            a[(size_t) k] = lm;
            if (k > 0 && k < N / 2) a[(size_t) (N - k)] = lm;
        }
        fft.perform (a.data(), b.data(), true);
        for (int n = 0; n < N; ++n)
        {
            const float c = b[(size_t) n].real();
            a[(size_t) n] = n == 0 || n == N / 2 ? c : (n < N / 2 ? 2.0f * c : 0.0f);
        }
        fft.perform (a.data(), b.data(), false);
        std::vector<cd> out ((size_t) N / 2 + 1);
        for (int k = 0; k <= N / 2; ++k) out[(size_t) k] = std::exp (cd (b[(size_t) k]));
        return out;
    }

    std::mutex irCacheLock;
    constexpr double roomTrim = 1.06;  // room mics against the close mic (rigtest: level kept at half and full)
    std::map<std::tuple<int, int, int>, std::shared_ptr<const juce::AudioBuffer<float>>> irCache;
}

juce::StringArray AmpRoom::roomNames()
{
    juce::StringArray n;
    for (auto& r : rooms()) n.add (r.name);
    return n;
}

juce::String AmpRoom::roomDescription (int room)
{
    return juce::isPositiveAndBelow (room, (int) rooms().size()) ? juce::String (rooms()[(size_t) room].description) : juce::String();
}

juce::AudioBuffer<float> AmpRoom::designImpulseResponse (int roomIndex, CabType cab, double sr)
{
    if (cab == CabType::impulseResponse || cab == CabType::count) cab = CabType::brit4x12;
    roomIndex = juce::jlimit (0, (int) rooms().size() - 1, roomIndex);
    const auto key = std::make_tuple (roomIndex, (int) cab, (int) sr);
    {
        std::lock_guard<std::mutex> l (irCacheLock);
        if (auto it = irCache.find (key); it != irCache.end()) return juce::AudioBuffer<float> (*it->second);
    }

    const auto& r = rooms()[(size_t) roomIndex];
    const auto geo = speaker::geometry (cab);
    const double rtMax = juce::jmax (r.rtLow, r.rtMid, r.rtHigh);
    const double seconds = juce::jlimit (0.3, 3.0, rtMax * 1.1 + 0.08);
    const int len = (int) (seconds * sr);
    int N = 1; while (N < len + (int) (0.1 * sr)) N <<= 1;   // room for the speaker's own (short) response without wrapping
    const int K = N / 2 + 1;
    const int Kimg = juce::jmin (K, (int) (16000.0 * N / sr) + 1);   // the image sources above 16 kHz: the speaker has nothing there
    auto hz = [&] (int k) { return k * sr / N; };

    const double bLo = reflectionFor (r, r.rtLow), bMid = reflectionFor (r, r.rtMid), bHi = reflectionFor (r, r.rtHigh);
    constexpr int maxOrder = 14;
    std::vector<std::vector<double>> bouncePow ((size_t) maxOrder + 1, std::vector<double> ((size_t) Kimg, 1.0));   // absorption after n bounces
    for (int k = 0; k < Kimg; ++k)
    {
        const double b = bandInterp (hz (k), bLo, bMid, bHi);
        for (int n = 1; n <= maxOrder; ++n) bouncePow[(size_t) n][(size_t) k] = bouncePow[(size_t) n - 1][(size_t) k] * b;
    }

    const double V = r.L * r.W * r.H;
    const double mixingTime = 0.001 * std::sqrt (V) * 1.3;        // rule of thumb: when the reflections become a diffuse tail
    const double cabY = r.W * 0.47;                               // the amp a little off centre, like in a real room
    const double cabZ = geo.height;
    const double micX = r.cabX + r.micDist;

    std::vector<double> radius ((size_t) Kimg);   // the part of the cone still radiating (beams less than a piston)
    for (int k = 0; k < Kimg; ++k) radius[(size_t) k] = speaker::radiatingRadius (cab, hz (k));
    std::vector<std::vector<cd>> H (2, std::vector<cd> ((size_t) K, cd()));
    double earlyEnergy[2] = { 0, 0 };
    double directDist = 0.0;

    // ---- image sources: the walls, floor and ceiling ----
    const double maxPath = r.micDist + soundSpeed * mixingTime * 2.5;
    for (int ch = 0; ch < 2; ++ch)
    {
        const double my = cabY + (ch == 0 ? -0.5 : 0.5) * r.spacing, mz = r.micHeight;
        directDist = std::sqrt (r.micDist * r.micDist + (my - cabY) * (my - cabY) + (mz - cabZ) * (mz - cabZ));
        const int nx = (int) std::ceil (maxPath / r.L) + 1, ny = (int) std::ceil (maxPath / r.W) + 1, nz = (int) std::ceil (maxPath / r.H) + 1;
        for (int ix = -nx; ix <= nx; ++ix)
            for (int iy = -ny; iy <= ny; ++iy)
                for (int iz = -nz; iz <= nz; ++iz)
                {
                    const int order = std::abs (ix) + std::abs (iy) + std::abs (iz);
                    if (order > maxOrder) continue;
                    const double x = ix * r.L + ((ix & 1) == 0 ? r.cabX : r.L - r.cabX);
                    const double y = iy * r.W + ((iy & 1) == 0 ? cabY : r.W - cabY);
                    const double z = iz * r.H + ((iz & 1) == 0 ? cabZ : r.H - cabZ);
                    const double dx = micX - x, dy = my - y, dz = mz - z;
                    const double d = std::sqrt (dx * dx + dy * dy + dz * dz);
                    if (d > maxPath) continue;
                    // the image faces +x, or -x after an odd number of end-wall bounces
                    const double axis = (ix & 1) == 0 ? 1.0 : -1.0;
                    const double cosTh = juce::jlimit (-1.0, 1.0, axis * dx / juce::jmax (1.0e-6, d));
                    const double sinTh = std::sqrt (juce::jmax (0.0, 1.0 - cosTh * cosTh));
                    // mirror-like part of the reflection (the rest is scattered into the diffuse tail); the floor bounce stays specular
                    const int scatterOrder = order - (std::abs (iz) == 1 && std::abs (ix) + std::abs (iy) == 0 ? 1 : 0);
                    const double g = std::pow (1.0 - r.scattering, (double) juce::jmax (0, scatterOrder)) / juce::jmax (0.3, d);
                    const double delay = d / soundSpeed;
                    const cd step = std::polar (1.0, -twoPi * (sr / N) * delay);
                    cd rot (1.0, 0.0);
                    double eSum = 0.0;
                    const auto& absorb = bouncePow[(size_t) order];
                    for (int k = 0; k < Kimg; ++k)
                    {
                        const double f = hz (k);
                        const double ka = twoPi * f / soundSpeed * radius[(size_t) k];
                        cd dir;
                        if (cosTh >= 0.0)
                            dir = speaker::jinc (ka * sinTh);                         // the speaker's beaming
                        else if (geo.closedBack)
                            dir = (0.35 + 0.15 * (1.0 + cosTh)) / cd (1.0, f / 250.0);   // only lows wrap around a closed box
                        else
                            dir = -0.85 * speaker::jinc (ka * sinTh) / cd (1.0, f / 3000.0);   // an open back: the rear wave, inverted
                        const cd v = g * dir * absorb[(size_t) k] * rot;
                        H[(size_t) ch][(size_t) k] += v;
                        if (f > 300.0 && f < 3000.0) eSum += std::norm (v);
                        rot *= step;
                        if ((k & 1023) == 1023) rot /= std::abs (rot);
                    }
                    earlyEnergy[ch] += eSum;
                }
    }

    // ---- the diffuse tail: its level against the direct sound follows the room (Sabine room constant) ----
    {
        const double S = 2.0 * (r.L * r.W + r.L * r.H + r.W * r.H);
        const double alpha = 1.0 - bMid * bMid;
        const double roomConstant = S * alpha / juce::jmax (0.05, 1.0 - alpha);
        const double Q = 3.0;                                          // a guitar cab is directional
        const double revOverDirect = 16.0 * juce::MathConstants<double>::pi * directDist * directDist / (Q * roomConstant);
        const double tailStart = directDist / soundSpeed + mixingTime * 0.6;
        const int tStart = (int) (tailStart * sr);
        const double build = juce::jmax (0.004, mixingTime) * sr;
        const double fPow = 2.5 * soundSpeed / (twoPi * geo.radius);   // above this the speaker beams: less power into the room

        for (int ch = 0; ch < 2; ++ch)
        {
            // energy of the direct sound in the mids, measured the same way as the early part
            int bins = 0;
            for (int k = 0; k < K; ++k) if (hz (k) > 300.0 && hz (k) < 3000.0) ++bins;
            const double directE = bins / (directDist * directDist);
            const double tailE = juce::jmax (0.3 * revOverDirect * directE, revOverDirect * directE - (earlyEnergy[ch] - directE));

            juce::Random rnd (4242 + ch * 977 + roomIndex * 31);
            std::vector<float> noise ((size_t) N, 0.0f);
            const double kL = 6.907755 / (r.rtLow * sr), kM = 6.907755 / (r.rtMid * sr), kH = 6.907755 / (r.rtHigh * sr);
            const double cLo = std::exp (-twoPi * 250.0 / sr), cHi = std::exp (-twoPi * 4000.0 / sr);
            double zLo = 0, zHi = 0;
            for (int i = tStart; i < juce::jmin (N, len); ++i)
            {
                const double n = rnd.nextFloat() + rnd.nextFloat() - 1.0;
                zLo = n + cLo * (zLo - n);
                zHi = n + cHi * (zHi - n);
                const double lo = zLo, hi = n - zHi, mid = n - lo - hi;
                const double t = i - tStart;
                const double env = 1.0 - std::exp (-t / build);
                noise[(size_t) i] = (float) (env * (lo * std::exp (-kL * t) + mid * std::exp (-kM * t) + hi * std::exp (-kH * t)));
            }
            int order = 0; while ((1 << order) < N) ++order;
            juce::dsp::FFT fft (order);
            std::vector<std::complex<float>> a ((size_t) N), b ((size_t) N);
            for (int i = 0; i < N; ++i) a[(size_t) i] = noise[(size_t) i];
            fft.perform (a.data(), b.data(), false);
            double e = 0.0;
            for (int k = 0; k < K; ++k)
            {
                const double f = hz (k);
                const cd v = cd (b[(size_t) k]) / cd (1.0, f / fPow);
                b[(size_t) k] = std::complex<float> ((float) v.real(), (float) v.imag());
                if (f > 300.0 && f < 3000.0) e += std::norm (v);
            }
            const double scale = std::sqrt (tailE / juce::jmax (1.0e-20, e));
            for (int k = 0; k < K; ++k) H[(size_t) ch][(size_t) k] += scale * cd (b[(size_t) k]);
        }
    }

    // ---- through the speaker's own voice, into omni room mics (flat, a touch of air) ----
    const auto farMag = speaker::farFieldMagnitude (cab, sr, N);
    std::vector<double> mag ((size_t) K);
    for (int k = 0; k < K; ++k)
    {
        const double f = hz (k);
        const double o = (f / 9000.0);
        mag[(size_t) k] = farMag[(size_t) k] * (1.0 + 0.2 * o * o / (1.0 + o * o));
    }
    const auto spk = minimumPhase (mag, N);

    juce::AudioBuffer<float> out (2, len);
    double midPow = 0.0;
    int midCount = 0;
    int order = 0; while ((1 << order) < N) ++order;
    juce::dsp::FFT fft (order);
    std::vector<std::complex<float>> a ((size_t) N), b ((size_t) N);
    for (int ch = 0; ch < 2; ++ch)
    {
        for (int k = 0; k < K; ++k)
        {
            const cd v = H[(size_t) ch][(size_t) k] * spk[(size_t) k];
            a[(size_t) k] = std::complex<float> ((float) v.real(), (float) v.imag());
            if (k > 0 && k < N / 2) a[(size_t) (N - k)] = std::conj (a[(size_t) k]);
            const double f = hz (k);
            if (f > 300.0 && f < 3000.0) { midPow += std::norm (v) * 1000.0 / f; midCount += 1; }   // weighted per octave, like the close mic
        }
        a[0] = std::complex<float> (a[0].real(), 0.0f);
        a[(size_t) N / 2] = std::complex<float> (a[(size_t) N / 2].real(), 0.0f);
        fft.perform (a.data(), b.data(), true);
        auto* o = out.getWritePointer (ch);
        const int fade = len / 10;
        for (int i = 0; i < len; ++i)
        {
            float w = 1.0f;
            if (i > len - fade) w = 0.5f + 0.5f * std::cos (juce::MathConstants<float>::pi * (float) (i - (len - fade)) / (float) fade);
            o[i] = b[(size_t) i].real() * w;
        }
    }
    // about the close mic's loudness through the mids (power, weighted per octave)
    double norm = 0.0;
    for (int k = 0; k < K; ++k) { const double f = hz (k); if (f > 300.0 && f < 3000.0) norm += 2.0 * 1000.0 / f; }   // both channels
    out.applyGain ((float) std::sqrt (norm / juce::jmax (1.0e-20, midPow)) * roomTrim);

    {
        std::lock_guard<std::mutex> l (irCacheLock);
        if (irCache.size() > 32) irCache.clear();
        irCache[key] = std::make_shared<const juce::AudioBuffer<float>> (out);
    }
    return out;
}

// =====================================================================================================
static juce::AudioBuffer<float> silentIr (int channels)
{
    juce::AudioBuffer<float> b (channels, 1);
    b.clear();
    return b;
}

struct AmpRoom::Builder : public juce::Thread
{
    explicit Builder (AmpRoom& o) : juce::Thread ("Amp room builder"), owner (o) {}
    void run() override
    {
        while (! threadShouldExit())
        {
            wait (20);
            owner.handoff.collect();
            const int want = owner.wantedKey.load();
            if (want < 0 || want == owner.designedKey.load()) continue;
            auto ir = designImpulseResponse (want / 100, (CabType) (want % 100), owner.sampleRate);
            if (threadShouldExit()) break;
            owner.handoff.post (std::move (ir), owner.sampleRate, want, true, false);
            owner.designedKey = want;
        }
    }
    AmpRoom& owner;
};

AmpRoom::AmpRoom() : builder (std::make_unique<Builder> (*this)) {}

AmpRoom::~AmpRoom()
{
    builder->stopThread (4000);
}

void AmpRoom::prepare (double sr, int maxBlock)
{
    builder->stopThread (4000);
    const bool sameRate = sr == sampleRate;
    sampleRate = sr;
    maxBlockSize = maxBlock;
    stereo.setSize (2, maxBlock, false, true, false);
    // JUCE's prepare() applies a pending load at once and rebuilds the engine from it: keep the room it had (an offline
    // bounce starts with it), or start silent (a fresh convolution would pass the raw amp straight through)
    const int k = installedKey.load();
    if (sameRate && k >= 0)
    {
        convolution.loadImpulseResponse (designImpulseResponse (k / 100, (CabType) (k % 100), sr), sr, juce::dsp::Convolution::Stereo::yes,
                                         juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
    }
    else
    {
        handoff.discard();
        designedKey = installedKey = -1;
        convolution.loadImpulseResponse (silentIr (2), sr, juce::dsp::Convolution::Stereo::yes, juce::dsp::Convolution::Trim::no,
                                         juce::dsp::Convolution::Normalise::no);
    }
    convolution.prepare ({ sr, (juce::uint32) maxBlock, 2 });
    builder->startThread (juce::Thread::Priority::low);
}

void AmpRoom::reset()
{
    convolution.reset();
}

void AmpRoom::setRoom (int room, CabType cab)
{
    if (cab == CabType::impulseResponse || cab == CabType::count) cab = CabType::brit4x12;
    wantedKey = juce::jlimit (0, (int) rooms().size() - 1, room) * 100 + (int) cab;
}

void AmpRoom::process (const float* in, float* outL, float* outR, int n)
{
    if (const int k = handoff.install (convolution); k >= 0)
        installedKey = k;
    if (n > stereo.getNumSamples() || installedKey.load() < 0)
    {
        juce::FloatVectorOperations::clear (outL, n);
        juce::FloatVectorOperations::clear (outR, n);
        return;
    }
    stereo.copyFrom (0, 0, in, n);
    stereo.copyFrom (1, 0, in, n);
    juce::dsp::AudioBlock<float> block (stereo.getArrayOfWritePointers(), 2, (size_t) n);
    convolution.process (juce::dsp::ProcessContextReplacing<float> (block));
    juce::FloatVectorOperations::copy (outL, stereo.getReadPointer (0), n);
    juce::FloatVectorOperations::copy (outR, stereo.getReadPointer (1), n);
}

// =====================================================================================================
//  Space reverb
// =====================================================================================================
static int spaceKey (int type, float size01, float damping01, float predelayMs)
{
    const int t = juce::jlimit (1, 4, type);
    const int sq = juce::roundToInt (juce::jlimit (0.0f, 1.0f, size01) * 20.0f);
    const int dq = juce::roundToInt (juce::jlimit (0.0f, 1.0f, damping01) * 20.0f);
    const int pq = juce::roundToInt (juce::jlimit (0.0f, 250.0f, predelayMs) / 5.0f);
    return ((t * 21 + sq) * 21 + dq) * 51 + pq;
}

juce::AudioBuffer<float> SpaceReverb::designImpulseResponse (int type, float size01, float damping01, float predelayMs, double sr)
{
    using namespace daw::roomir;
    static const int spaces[] = { woodenStudio, woodenStudio, plate, spring, concertHall };
    type = juce::jlimit (1, 4, type);
    // the size knob scales each space around its natural size; damping darkens the tail
    const float size = type == 3 ? 0.7f + 0.6f * size01 : 0.45f + 1.1f * size01;
    return design (spaces[type], sr, size, 1.0f - juce::jlimit (0.0f, 1.0f, damping01), predelayMs, 7u);
}

struct SpaceReverb::Builder : public juce::Thread
{
    explicit Builder (SpaceReverb& o) : juce::Thread ("Reverb builder"), owner (o) {}
    void run() override
    {
        while (! threadShouldExit())
        {
            wait (25);
            owner.handoff.collect();
            const int want = owner.wantedKey.load();
            if (want < 0 || want == owner.designedKey.load()) continue;
            int k = want;
            const int pq = k % 51; k /= 51;
            const int dq = k % 21; k /= 21;
            const int sq = k % 21; k /= 21;
            auto ir = designImpulseResponse (k, (float) sq / 20.0f, (float) dq / 20.0f, (float) pq * 5.0f, owner.sampleRate);
            if (threadShouldExit()) break;
            owner.handoff.post (std::move (ir), owner.sampleRate, want, true, false);
            owner.designedKey = want;
        }
    }
    SpaceReverb& owner;
};

SpaceReverb::SpaceReverb() : builder (std::make_unique<Builder> (*this)) {}
SpaceReverb::~SpaceReverb() { builder->stopThread (4000); }

void SpaceReverb::prepare (double sr, int maxBlock)
{
    builder->stopThread (4000);
    const bool sameRate = sr == sampleRate;
    sampleRate = sr;
    const int k = installedKey.load();
    if (sameRate && k >= 0)
    {
        // keep the space it had (re-sent: a load still in flight is applied by prepare() too)
        designedKey = -1;   // the builder sends the wanted space again
    }
    else
    {
        handoff.discard();
        designedKey = installedKey = -1;
        convolution.loadImpulseResponse (silentIr (2), sr, juce::dsp::Convolution::Stereo::yes, juce::dsp::Convolution::Trim::no,
                                         juce::dsp::Convolution::Normalise::no);
    }
    convolution.prepare ({ sr, (juce::uint32) maxBlock, 2 });
    builder->startThread (juce::Thread::Priority::low);
}

void SpaceReverb::reset() { convolution.reset(); }

void SpaceReverb::setSpace (int type, float size01, float damping01, float predelayMs)
{
    wantedKey = spaceKey (type, size01, damping01, predelayMs);
}

void SpaceReverb::process (float* l, float* r, int n)
{
    if (const int k = handoff.install (convolution); k >= 0)
        installedKey = k;
    if (installedKey.load() < 0)
    {
        juce::FloatVectorOperations::clear (l, n);
        juce::FloatVectorOperations::clear (r, n);
        return;
    }
    float* chans[] = { l, r };
    juce::dsp::AudioBlock<float> block (chans, 2, (size_t) n);
    convolution.process (juce::dsp::ProcessContextReplacing<float> (block));
}

} // namespace wis
