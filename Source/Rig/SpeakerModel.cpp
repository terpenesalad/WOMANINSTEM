#include "SpeakerModel.h"
#include <juce_dsp/juce_dsp.h>
#include <complex>
#include <map>
#include <mutex>
#include <memory>
#include <tuple>

namespace wis::speaker
{

using cd = std::complex<double>;
using cf = std::complex<float>;
static constexpr double soundSpeed = 343.0;
static constexpr double twoPi = juce::MathConstants<double>::twoPi;

// =====================================================================================================
//  The speakers
// =====================================================================================================
namespace
{
    struct BoxMode { double hz, q, gain; };
    struct Spec
    {
        double a, vc, cap, dome, depth;         // cone radius, voice coil radius, dust cap radius, dome height, cone depth (m)
        double fb, cb, al, R;                   // break-up onset (Hz), bending-wave speed at 1 kHz (m/s), loss at 1 kHz (1/m), surround reflection
        double capF, capQ, capG;                // dust cap resonance
        std::vector<std::pair<double, double>> others;   // the cab's other speakers (offsets in m)
        bool closed;
        std::vector<BoxMode> modes;             // the box's internal standing waves (and a port)
        std::vector<std::pair<double, double>> voicing;  // measured-style response at the reference position (Hz, dB)
        double height;                          // miked speaker's centre above the floor (m)
        double modeAmp;                         // how strongly the cone's break-up modes show
    };

    // Voicings follow the published response curves of each speaker family (closed-back Greenback-style 4x12,
    // Vintage-30-style 4x12, open-back American 2x12, alnico Blue 2x12, open-back 1x12, Jensen-style tweed 1x12,
    // and the classic bass cabs). They describe the speaker, not the mic.
    const Spec& specFor (CabType t)
    {
        static const std::vector<Spec> specs {
            // brit4x12
            { .125, .022, .050, .012, .058, 900, 110, 3.6, .63, 5200, 3.0, .5, { { .36, 0 }, { 0, .36 }, { .36, .36 } }, true,
              { { 245, 7, .22 }, { 355, 8, -.16 }, { 575, 9, .12 } },
              { { 40, -22 }, { 60, -10 }, { 80, -3 }, { 100, 1 }, { 115, 2 }, { 150, 0 }, { 250, -1 }, { 400, -2.5 }, { 600, -2 }, { 800, -1 }, { 1000, 0 },
                { 1400, 2 }, { 2000, 4 }, { 2500, 5 }, { 3000, 3 }, { 3500, 2 }, { 4000, 2.5 }, { 4500, 0 }, { 5000, -3 }, { 6000, -10 }, { 7000, -18 },
                { 8000, -25 }, { 10000, -36 }, { 12000, -44 }, { 16000, -55 } }, .60, .42 },
            // modern4x12
            { .125, .022, .050, .012, .058, 1000, 120, 3.0, .68, 5600, 3.0, .6, { { .37, 0 }, { 0, .37 }, { .37, .37 } }, true,
              { { 232, 7, .20 }, { 340, 8, -.15 }, { 520, 9, .12 } },
              { { 40, -20 }, { 60, -9 }, { 80, -2 }, { 100, 1.5 }, { 115, 1.5 }, { 150, 0 }, { 250, -2 }, { 400, -4.5 }, { 600, -3.5 }, { 800, -1.5 },
                { 1000, 0 }, { 1500, 2 }, { 2000, 3 }, { 2500, 7 }, { 3000, 5 }, { 3500, 2 }, { 4000, 4 }, { 4500, 6 }, { 5000, 4 }, { 5500, -1 },
                { 6000, -6 }, { 7000, -15 }, { 8000, -24 }, { 10000, -35 }, { 12000, -44 }, { 16000, -55 } }, .60, .45 },
            // american2x12 (open back)
            { .125, .025, .055, .010, .055, 1300, 160, 2.4, .58, 6500, 2.5, .6, { { .36, 0 } }, false, {},
              { { 40, -28 }, { 60, -16 }, { 80, -8 }, { 100, -3 }, { 130, 0 }, { 160, 1 }, { 250, 0 }, { 400, -2 }, { 600, -2 }, { 800, -1 }, { 1000, 0 },
                { 1500, 1 }, { 2000, 2 }, { 3000, 3 }, { 4000, 2 }, { 5000, 1 }, { 6000, -2 }, { 7000, -6 }, { 8000, -11 }, { 10000, -21 }, { 12000, -31 },
                { 16000, -45 } }, .45, .36 },
            // britBlue2x12 (open back, alnico)
            { .125, .022, .045, .010, .058, 1100, 130, 2.7, .68, 6000, 4.0, .9, { { .37, 0 } }, false, {},
              { { 40, -28 }, { 60, -17 }, { 80, -9 }, { 100, -4 }, { 130, -1 }, { 160, 0 }, { 250, -1 }, { 400, -3 }, { 600, -2 }, { 800, 0 }, { 1000, 0 },
                { 1500, 2 }, { 2000, 4 }, { 2500, 5 }, { 3000, 6 }, { 3500, 7 }, { 4000, 5 }, { 5000, 2 }, { 6000, -3 }, { 7000, -9 }, { 8000, -16 },
                { 10000, -29 }, { 12000, -39 }, { 16000, -52 } }, .45, .45 },
            // openBack1x12
            { .125, .022, .050, .012, .058, 1000, 120, 3.0, .63, 5400, 3.0, .5, {}, false, {},
              { { 40, -30 }, { 60, -18 }, { 80, -10 }, { 100, -5 }, { 130, -1 }, { 160, 0 }, { 250, 0 }, { 400, -1 }, { 600, -1 }, { 800, 0 }, { 1000, 0 },
                { 1500, 1 }, { 2000, 3 }, { 2500, 4 }, { 3000, 3 }, { 4000, 2 }, { 5000, -1 }, { 6000, -6 }, { 7000, -13 }, { 8000, -20 }, { 10000, -31 },
                { 12000, -41 }, { 16000, -53 } }, .40, .40 },
            // tweed1x12
            { .125, .019, .045, .012, .060, 800, 100, 4.2, .58, 4800, 2.5, .4, {}, false, {},
              { { 40, -30 }, { 60, -18 }, { 80, -10 }, { 100, -5 }, { 130, -2 }, { 160, 0 }, { 250, 1 }, { 400, 0 }, { 600, 0 }, { 800, 1 }, { 1000, 0 },
                { 1500, 2 }, { 2000, 4 }, { 2500, 4 }, { 3000, 3 }, { 3500, 1 }, { 4000, 0 }, { 5000, -5 }, { 6000, -11 }, { 7000, -18 }, { 8000, -25 },
                { 10000, -36 }, { 12000, -46 }, { 16000, -58 } }, .40, .40 },
            // bass1x15Vintage (ported)
            { .165, .038, .065, .015, .085, 500, 90, 4.8, .58, 2800, 2.0, .3, {}, true,
              { { 45, 3, .35 }, { 215, 6, .15 }, { 420, 7, -.10 } },
              { { 25, -24 }, { 35, -10 }, { 45, -2 }, { 55, 1 }, { 70, 2 }, { 90, 1.5 }, { 120, .5 }, { 200, 0 }, { 300, 0 }, { 450, -1 }, { 650, -2 },
                { 850, -1 }, { 1000, 0 }, { 1500, 0 }, { 2000, -3 }, { 2500, -8 }, { 3000, -14 }, { 4000, -24 }, { 5000, -32 }, { 7000, -44 }, { 10000, -56 } }, .40, .30 },
            // bass2x15Sixties (sealed column)
            { .165, .038, .065, .015, .085, 550, 95, 4.2, .58, 3000, 2.0, .3, { { 0, .42 } }, true,
              { { 160, 6, .20 }, { 300, 7, -.14 }, { 480, 8, .10 } },
              { { 25, -28 }, { 35, -16 }, { 45, -7 }, { 55, -2 }, { 70, 1 }, { 90, 2 }, { 120, 1 }, { 200, 0 }, { 350, 1 }, { 600, -1 }, { 1000, 0 },
                { 1500, -1 }, { 2000, -3 }, { 3000, -10 }, { 4000, -18 }, { 5000, -26 }, { 7000, -38 }, { 10000, -50 } }, .80, .30 },
            // bass4x10Horn (ported, horn above 3.5 kHz)
            { .105, .025, .045, .010, .045, 1100, 140, 3.0, .63, 4500, 2.5, .4, { { .29, 0 }, { 0, .29 }, { .29, .29 } }, true,
              { { 48, 3, .30 }, { 190, 6, .16 }, { 380, 7, -.12 } },
              { { 25, -28 }, { 35, -14 }, { 45, -4 }, { 55, 0 }, { 70, 1 }, { 100, 1 }, { 200, 0 }, { 500, -2 }, { 800, -1 }, { 1000, 0 }, { 1500, 1 },
                { 2000, 2 }, { 3000, 0 }, { 4000, -2 }, { 5000, -2 }, { 6000, -3 }, { 8000, -5 }, { 10000, -8 }, { 12000, -14 }, { 16000, -28 } }, .75, .32 },
            // bass8x10 (sealed)
            { .105, .025, .045, .010, .045, 1000, 130, 3.6, .63, 4200, 2.5, .3, { { .27, 0 }, { 0, .27 }, { .27, .27 }, { 0, .54 }, { .27, .54 } }, true,
              { { 132, 6, .20 }, { 262, 7, -.15 }, { 410, 8, .12 } },
              { { 25, -28 }, { 35, -16 }, { 45, -7 }, { 55, -2 }, { 70, 1 }, { 85, 2 }, { 110, 1 }, { 200, 1 }, { 300, 1 }, { 500, -2 }, { 800, -1 },
                { 1000, 0 }, { 1500, 1 }, { 2000, 0 }, { 3000, -4 }, { 4000, -10 }, { 5000, -18 }, { 6000, -24 }, { 8000, -34 }, { 10000, -44 } }, 1.10, .32 },
            // bass2x10Modern (ported, tweeter)
            { .105, .025, .045, .010, .045, 1200, 150, 2.7, .63, 5000, 2.5, .4, { { .28, 0 } }, true,
              { { 50, 3, .25 }, { 210, 6, .14 }, { 430, 7, -.10 } },
              { { 25, -28 }, { 35, -16 }, { 45, -6 }, { 55, -1 }, { 70, .5 }, { 90, 1 }, { 200, 0 }, { 600, -2 }, { 1000, 0 }, { 2000, 1 }, { 3000, 1 },
                { 4000, 0 }, { 6000, -1 }, { 8000, -3 }, { 10000, -6 }, { 12000, -10 }, { 16000, -22 } }, .50, .28 },
        };
        const int i = juce::jlimit (0, (int) specs.size() - 1, (int) t);
        return specs[(size_t) i];
    }

    // ---- small helpers ----------------------------------------------------------------------------
    double voicingDb (const Spec& s, double f)
    {
        // linear in log frequency, extrapolated with the end slopes
        const auto& v = s.voicing;
        const double lf = std::log (juce::jmax (1.0, f));
        auto L = [&] (size_t i) { return std::log (v[i].first); };
        if (lf <= L (0)) return v[0].second + (lf - L (0)) * (v[1].second - v[0].second) / (L (1) - L (0));
        const size_t n = v.size();
        if (lf >= L (n - 1)) return v[n - 1].second + (lf - L (n - 1)) * (v[n - 1].second - v[n - 2].second) / (L (n - 1) - L (n - 2));
        for (size_t i = 1; i < n; ++i)
            if (lf <= L (i))
                return v[i - 1].second + (lf - L (i - 1)) / (L (i) - L (i - 1)) * (v[i].second - v[i - 1].second);
        return v[n - 1].second;
    }

    double gaussBump (double f, double hz, double sigmaOct, double db) { const double o = std::log2 (f / hz) / sigmaOct; return db * std::exp (-0.5 * o * o); }
    double hp2Db (double f, double f0, double q) { const double r = f / f0, r2 = r * r; return 20.0 * std::log10 (r2 / std::sqrt ((1 - r2) * (1 - r2) + (r / q) * (r / q)) + 1e-12); }
    double lp2Db (double f, double f0, double q) { const double r = f / f0, r2 = r * r; return 20.0 * std::log10 (1.0 / std::sqrt ((1 - r2) * (1 - r2) + (r / q) * (r / q))); }
    double lp1Db (double f, double f0) { const double r = f / f0; return 20.0 * std::log10 (1.0 / std::sqrt (1 + r * r)); }

    /** A microphone's far-field response (dB) and how much of it is pressure gradient (0 omni .. 1 figure-8). */
    double micDb (MicType m, double f, double& gradient)
    {
        switch (m)
        {
            case MicType::ribbon:      // figure-8 ribbon: flat, a dark top
                gradient = 1.0;
                return lp1Db (f, 9000) + lp1Db (f, 14000) + gaussBump (f, 4000, .7, -1.0) + hp2Db (f, 25, .7);
            case MicType::condenser:   // large-diaphragm cardioid: flat with a little air
                gradient = 0.5;
                return gaussBump (f, 10000, .8, 2.5) + gaussBump (f, 5000, .9, 1.0) + hp2Db (f, 30, .7);
            case MicType::dynamic:
            case MicType::dynamicPlusRibbon:
            case MicType::count:
            default:                   // the classic cardioid instrument dynamic: bass roll-off (made for close use), presence peak
                gradient = 0.5;
                return hp2Db (f, 90, .6) + gaussBump (f, 5800, .45, 5.5) + gaussBump (f, 8500, .2, -2.5) + lp2Db (f, 14500, .8);
        }
    }

    double capsuleRadius (MicType m) { return m == MicType::ribbon ? .012 : m == MicType::condenser ? .0125 : .009; }

    /** Proximity effect of a mic with this gradient share at distance r (relative to its far-field response). */
    double proximity (double f, double r, double gradient)
    {
        const double kr = twoPi * juce::jmax (1.0, f) * r / soundSpeed;
        return std::abs (cd (1.0 - gradient, 0.0) + gradient * (1.0 + 1.0 / cd (0.0, kr)));
    }

    struct Grid
    {
        double sr; int N; int bins;   // bins computed with physics: 0..bins-1 (to ~20 kHz)
        double hz (int k) const { return k * sr / N; }
    };

    int fftSizeFor (double sr) { return sr <= 50000.0 ? 16384 : sr <= 100000.0 ? 32768 : 65536; }
    Grid gridFor (double sr) { Grid g { sr, fftSizeFor (sr), 0 }; g.bins = juce::jmin (g.N / 2 + 1, (int) (juce::jmin (20000.0, sr * 0.5) * g.N / sr) + 1); return g; }

    double gauss (juce::Random& r)
    {
        const double u1 = juce::jmax (1.0e-9, (double) r.nextFloat()), u2 = r.nextFloat();
        return std::sqrt (-2.0 * std::log (u1)) * std::cos (twoPi * u2);
    }

    /** The cone heard at one point: Rayleigh integral over the cone surface (rings x segments). Adds into out. */
    void coneField (const Spec& s, double x0, double y0, double dist, const Grid& g, juce::uint32 seed, std::vector<cd>& out)
    {
        constexpr int nr = 24, naz = 28;
        const double a = s.a, vc = s.vc, L = a - vc, dr = a / nr;
        const int K = g.bins;
        std::vector<double> beta ((size_t) K);
        std::vector<cd> gam ((size_t) K), denom ((size_t) K), capRes ((size_t) K);
        const double R = s.R, Rv = 0.5, W0 = (1.0 + R) / (1.0 - R * Rv);
        for (int k = 0; k < K; ++k)
        {
            const double f = juce::jmax (1.0, g.hz (k));
            const double fb4 = std::pow (f / s.fb, 4.0);
            beta[(size_t) k] = fb4 / (1.0 + fb4);                                 // how far the cone has stopped moving as one piece
            const double cb = s.cb * std::sqrt (f / 1000.0);                       // bending waves are dispersive
            gam[(size_t) k] = cd (s.al * std::pow (f / 1000.0, 0.7), twoPi * f / cb);
            denom[(size_t) k] = 1.0 - R * Rv * std::exp (-2.0 * gam[(size_t) k] * L);
            const double rr = f / s.capF;
            capRes[(size_t) k] = 1.0 + s.capG * (1.0 / cd (1.0 - rr * rr, rr / s.capQ) - 1.0);
        }

        std::vector<cf> s1 ((size_t) K), s2 ((size_t) K);
        std::vector<cd> v ((size_t) K);
        const double capEdgeDepth = s.depth * (a - s.cap) / (a - vc);
        for (int i = 0; i < nr; ++i)
        {
            const double r = (i + 0.5) * dr;
            const double depth = r >= s.cap ? s.depth * (a - r) / (a - vc) : capEdgeDepth - s.dome * (1.0 - (r / s.cap) * (r / s.cap));
            for (int k = 0; k < K; ++k)
            {
                if (r >= s.cap)
                {
                    const double sx = r - vc;
                    const cd wb = (std::exp (-gam[(size_t) k] * sx) + R * std::exp (-gam[(size_t) k] * (2.0 * L - sx))) / denom[(size_t) k];
                    v[(size_t) k] = (1.0 - beta[(size_t) k]) + beta[(size_t) k] * wb / W0;
                }
                else
                {
                    v[(size_t) k] = capRes[(size_t) k];
                }
            }
            std::fill (s1.begin(), s1.end(), cf());
            std::fill (s2.begin(), s2.end(), cf());
            juce::Random rnd ((juce::int64) seed * 7919 + i * 104729 + 17);
            const double dA = r * dr * twoPi / naz;
            for (int j = 0; j < naz; ++j)
            {
                const double phi = (j + 0.5) * twoPi / naz;
                const double x = r * std::cos (phi) - x0, y = r * std::sin (phi) - y0, z = dist + depth;
                const double Rd = std::sqrt (x * x + y * y + z * z);
                const float w = (float) (dA / Rd);
                const cf irregular ((float) gauss (rnd), (float) gauss (rnd));     // the cone isn't perfectly round
                const cd stepD = std::polar (1.0, -twoPi * (g.sr / g.N) * Rd / soundSpeed);
                const cf step ((float) stepD.real(), (float) stepD.imag());
                cf z1 (1.0f, 0.0f);
                for (int k = 0; k < K; ++k)
                {
                    const cf term = w * z1;
                    s1[(size_t) k] += term;
                    s2[(size_t) k] += irregular * term;
                    z1 *= step;
                    if ((k & 511) == 511) z1 /= std::abs (z1);      // keep the rotation on the unit circle
                }
            }
            for (int k = 0; k < K; ++k)
                out[(size_t) k] += v[(size_t) k] * (cd (s1[(size_t) k]) + 0.45 * beta[(size_t) k] * cd (s2[(size_t) k]));
        }
    }

    /** What a microphone diaphragm (not a point) hears of the cone. */
    std::vector<cd> capsuleField (const Spec& s, double rho, double dist, double capsule, const Grid& g, juce::uint32 seed)
    {
        std::vector<cd> acc ((size_t) g.bins);
        const double pts[5][2] = { { 0, 0 }, { .7, 0 }, { -.7, 0 }, { 0, .7 }, { 0, -.7 } };
        for (auto& p : pts)
            coneField (s, rho + p[0] * capsule, p[1] * capsule, dist, g, seed, acc);
        for (auto& c : acc) c /= 5.0;
        return acc;
    }

    /** Smooths a magnitude over 1/n octave (power average). */
    std::vector<double> smoothOct (const std::vector<double>& mag, const Grid& g, double frac)
    {
        const int K = (int) mag.size();
        std::vector<double> cs ((size_t) K + 1, 0.0), out ((size_t) K);
        for (int k = 0; k < K; ++k) cs[(size_t) k + 1] = cs[(size_t) k] + mag[(size_t) k] * mag[(size_t) k];
        for (int k = 0; k < K; ++k)
        {
            if (k == 0) { out[0] = mag[0]; continue; }
            const double f = g.hz (k);
            int lo = (int) std::floor (f * std::pow (2.0, -0.5 / frac) * g.N / g.sr);
            int hi = (int) std::ceil (f * std::pow (2.0, 0.5 / frac) * g.N / g.sr);
            lo = juce::jlimit (1, K - 1, lo);
            hi = juce::jlimit (lo + 1, K, hi + 1);
            out[(size_t) k] = std::sqrt ((cs[(size_t) hi] - cs[(size_t) lo]) / (hi - lo));
        }
        return out;
    }

    std::vector<cd> minimumPhase (const std::vector<double>& mag, int N);

    /** A magnitude known up to bin K-1, held flat above (to the Nyquist bin N/2). */
    std::vector<double> extendToNyquist (const std::vector<double>& m, const Grid& g)
    {
        std::vector<double> out ((size_t) g.N / 2 + 1);
        for (int k = 0; k <= g.N / 2; ++k) out[(size_t) k] = m[(size_t) juce::jmin (k, (int) m.size() - 1)];
        return out;
    }

    constexpr double refPos = 0.3, refKnob = 0.1;   // reference: cap edge, 3 cm back
    double rhoFor (const Spec& s, double pos01) { return s.a * 0.92 * pos01; }

    // ---- caches (the builder thread and the message thread may both design) ----
    std::mutex cacheLock;
    // smoothed responses are kept as minimum-phase spectra (k = 0..N/2): dividing or multiplying by a zero-phase curve
    // would smear energy to before the sound arrives
    std::map<std::tuple<int, int>, std::shared_ptr<const std::vector<cd>>> farCache;              // (type, sr)
    std::map<std::tuple<int, int, int>, std::shared_ptr<const std::vector<cd>>> refCache;         // (type, capsule, sr)
    std::map<std::tuple<int, int, int, int, int>, std::shared_ptr<const juce::AudioBuffer<float>>> irCache;

    /** One speaker on its own, 2 m away on axis, 1/3-octave smoothed and normalised at low frequencies. */
    std::shared_ptr<const std::vector<cd>> farSmooth (CabType t, const Grid& g)
    {
        const auto key = std::make_tuple ((int) t, (int) g.sr);
        {
            std::lock_guard<std::mutex> l (cacheLock);
            if (auto it = farCache.find (key); it != farCache.end()) return it->second;
        }
        const auto& s = specFor (t);
        std::vector<cd> far ((size_t) g.bins);
        coneField (s, 0, 0, 2.0, g, 3u + (juce::uint32) t, far);
        std::vector<double> mag ((size_t) g.bins);
        for (int k = 0; k < g.bins; ++k) mag[(size_t) k] = std::abs (far[(size_t) k]);
        auto sm = smoothOct (mag, g, 3.0);
        const int k100 = juce::jmax (1, (int) (100.0 * g.N / g.sr));
        const double ref = juce::jmax (1.0e-12, sm[(size_t) k100]);
        for (auto& m : sm) m /= ref;
        auto p = std::make_shared<const std::vector<cd>> (minimumPhase (extendToNyquist (sm, g), g.N));
        std::lock_guard<std::mutex> l (cacheLock);
        farCache[key] = p;
        return p;
    }

    /** The cab's other speakers, heard from the mic: off axis, through each one's own far-field response. */
    void addOthers (const Spec& s, double dist, double rho, const std::vector<cd>& farS, const Grid& g, std::vector<cd>& out)
    {
        for (auto& o : s.others)
        {
            const double lat = std::hypot (o.first - rho, o.second), ax = dist + s.depth * 0.5;
            const double d = std::hypot (lat, ax), th = std::atan2 (lat, ax);
            // the mic's pattern and the baffle's diffraction keep the other speakers a little quieter than free-field
            const double offMic = 0.5 + 0.5 * std::cos (th);
            const double area = 0.75 * offMic * juce::MathConstants<double>::pi * s.a * s.a / d;
            for (int k = 0; k < g.bins; ++k)
            {
                const double f = g.hz (k);
                const double aEff = f <= s.fb ? s.a : juce::jmax (s.cap, s.a * std::sqrt (s.fb / f));
                out[(size_t) k] += area * farS[(size_t) k] * jinc (twoPi * f / soundSpeed * aEff * std::sin (th))
                                   * std::polar (1.0, -twoPi * f * d / soundSpeed);
            }
        }
    }

    /** 1/3-octave magnitude of the reference mic (cap edge, 3 cm back, incl. the other speakers): what the voicing describes. */
    std::shared_ptr<const std::vector<cd>> referenceSmooth (CabType t, MicType capsuleMic, const Grid& g)
    {
        const auto key = std::make_tuple ((int) t, (int) capsuleMic, (int) g.sr);
        {
            std::lock_guard<std::mutex> l (cacheLock);
            if (auto it = refCache.find (key); it != refCache.end()) return it->second;
        }
        const auto& s = specFor (t);
        const auto farS = farSmooth (t, g);
        const double dist = micDistanceMetres ((float) refKnob), rho = rhoFor (s, refPos);
        auto h = capsuleField (s, rho, dist, capsuleRadius (capsuleMic), g, 11u + (juce::uint32) t);
        addOthers (s, dist, rho, *farS, g, h);
        std::vector<double> mag ((size_t) g.bins);
        for (int k = 0; k < g.bins; ++k) mag[(size_t) k] = std::abs (h[(size_t) k]);
        auto p = std::make_shared<const std::vector<cd>> (minimumPhase (extendToNyquist (smoothOct (mag, g, 3.0), g), g.N));
        std::lock_guard<std::mutex> l (cacheLock);
        refCache[key] = p;
        return p;
    }

    /** Minimum-phase spectrum (k = 0..N/2) for a magnitude, by the real cepstrum. */
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
        fft.perform (a.data(), b.data(), true);                        // cepstrum (JUCE's inverse is scaled by 1/N)
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
}

// =====================================================================================================
double jinc (double x) noexcept
{
    // 2 J1(x) / x, with J1 from the classic rational / asymptotic approximations
    const double ax = std::abs (x);
    if (ax < 1.0e-6) return 1.0;
    double j1;
    if (ax < 8.0)
    {
        const double y = x * x;
        const double n = x * (72362614232.0 + y * (-7895059235.0 + y * (242396853.1 + y * (-2972611.439 + y * (15704.48260 + y * (-30.16036606))))));
        const double d = 144725228442.0 + y * (2300535178.0 + y * (18583304.74 + y * (99447.43394 + y * (376.9991397 + y))));
        j1 = n / d;
    }
    else
    {
        const double z = 8.0 / ax, y = z * z, xx = ax - 2.356194491;
        const double p = 1.0 + y * (0.183105e-2 + y * (-0.3516396496e-4 + y * (0.2457520174e-5 + y * (-0.240337019e-6))));
        const double q = 0.04687499995 + y * (-0.2002690873e-3 + y * (0.8449199096e-5 + y * (-0.88228987e-6 + y * 0.105787412e-6)));
        j1 = std::sqrt (0.636619772 / ax) * (std::cos (xx) * p - z * std::sin (xx) * q);
        if (x < 0.0) j1 = -j1;
    }
    return 2.0 * j1 / x;
}

double radiatingRadius (CabType t, double hz) noexcept
{
    const auto& s = specFor (t);
    if (hz <= s.fb) return s.a;
    return juce::jmax (s.cap, s.a * std::sqrt (s.fb / hz));
}

double offAxis (CabType t, double hz, double angle) noexcept
{
    const double ka = twoPi * hz / soundSpeed * radiatingRadius (t, hz);
    return jinc (ka * std::sin (juce::jlimit (0.0, juce::MathConstants<double>::halfPi, angle)));
}

float micDistanceMetres (float d01) noexcept
{
    return 0.02f * std::pow (50.0f, juce::jlimit (0.0f, 1.0f, d01));
}

Geometry geometry (CabType t)
{
    const auto& s = specFor (t);
    return { s.a, s.closed, s.height, 1 + (int) s.others.size() };
}

std::vector<double> farFieldMagnitude (CabType t, double sr, int N)
{
    const auto& s = specFor (t);
    const auto g = gridFor (sr);
    const auto farS = farSmooth (t, g);
    const auto refS = referenceSmooth (t, MicType::dynamic, g);
    std::vector<double> out ((size_t) N / 2 + 1);
    const int k200 = juce::jmax (1, (int) (200.0 * g.N / g.sr));
    const double norm = std::abs ((*refS)[(size_t) k200]) / juce::jmax (1.0e-12, std::abs ((*farS)[(size_t) k200]));
    for (int k = 0; k <= N / 2; ++k)
    {
        const double f = k * sr / N;
        const int kg = juce::jlimit (0, g.bins - 1, (int) std::lround (f * g.N / g.sr));
        // the voicing (defined at the close mic) moved to the far field by the physics' far / near ratio
        const double ratio = std::abs ((*farS)[(size_t) kg]) / juce::jmax (1.0e-12, std::abs ((*refS)[(size_t) kg])) * norm;
        out[(size_t) k] = std::pow (10.0, voicingDb (s, juce::jmax (1.0, f)) / 20.0) * juce::jlimit (0.03, 4.0, ratio);
    }
    return out;
}

juce::AudioBuffer<float> designCloseMic (CabType type, MicType mic, float pos01, float dist01, double sr)
{
    pos01 = juce::jlimit (0.0f, 1.0f, pos01);
    dist01 = juce::jlimit (0.0f, 1.0f, dist01);
    const auto cacheKey = std::make_tuple ((int) type, (int) mic, juce::roundToInt (pos01 * 20.0f), juce::roundToInt (dist01 * 20.0f), (int) sr);
    {
        std::lock_guard<std::mutex> l (cacheLock);
        if (auto it = irCache.find (cacheKey); it != irCache.end())
            return juce::AudioBuffer<float> (*it->second);
    }

    const auto& s = specFor (type);
    const auto g = gridFor (sr);
    const int N = g.N, K = g.bins;
    const MicType capsuleMic = mic == MicType::dynamicPlusRibbon ? MicType::dynamic : mic;
    const double dist = micDistanceMetres (dist01), rho = rhoFor (s, pos01);

    const auto farS = farSmooth (type, g);
    const auto refS = referenceSmooth (type, capsuleMic, g);

    // ---- the cone, the cab's other speakers, the floor and the room ----
    auto h = capsuleField (s, rho, dist, capsuleRadius (capsuleMic), g, 11u + (juce::uint32) type);
    addOthers (s, dist, rho, *farS, g, h);
    {
        const double direct = dist + s.depth * 0.5;
        struct Refl { double path, gain; };
        const Refl refl[] = { { std::hypot (direct, 2.0 * s.height), .75 },    // floor
                              { std::hypot (direct, 2.0 * 2.2), .50 },         // ceiling
                              { std::hypot (direct, 2.0 * 1.6), .45 },         // side wall
                              { direct + 2.0 * 1.2 + 0.3, .35 } };             // the wall behind the mic
        const double area = juce::MathConstants<double>::pi * s.a * s.a;
        for (auto& r : refl)
        {
            const double th = std::atan2 (std::sqrt (juce::jmax (0.0, r.path * r.path - direct * direct)), direct);
            for (int k = 0; k < K; ++k)
            {
                const double f = g.hz (k);
                const double aEff = f <= s.fb ? s.a : juce::jmax (s.cap, s.a * std::sqrt (s.fb / f));
                h[(size_t) k] += area / r.path * r.gain * (*farS)[(size_t) k] * jinc (twoPi * f / soundSpeed * aEff * std::sin (juce::jmin (th, juce::MathConstants<double>::halfPi)))
                                 * std::polar (1.0, -twoPi * f * r.path / soundSpeed) / cd (1.0, f / 6000.0);
            }
        }
    }

    // ---- cone break-up modes: their strength at the mic follows the mode shape under it ----
    std::vector<cd> modes ((size_t) K, cd (1.0, 0.0));
    {
        juce::Random rnd (1000 + (juce::int64) type * 37);
        const double lo = s.fb * 1.1, hi = 9000.0;
        const int n = 22;
        const double local = 1.0 / (1.0 + (dist / 0.12) * (dist / 0.12));    // further back, the modes average out
        const double rpos = rho / s.a;
        for (int i = 0; i < n; ++i)
        {
            const double fm = lo * std::pow (hi / lo, (i + 0.15 + 0.7 * rnd.nextDouble()) / n);
            const double q = 7.0 + 15.0 * rnd.nextDouble();
            const int nodes = 1 + rnd.nextInt (4);
            const double shape = std::cos (juce::MathConstants<double>::pi * nodes * rpos + twoPi * rnd.nextDouble());
            const double sign = rnd.nextBool() ? 1.0 : -1.0;
            const double amp = s.modeAmp * (0.5 + 0.5 * rnd.nextDouble()) * (local * shape + (1.0 - local) * 0.25 * sign);
            for (int k = 0; k < K; ++k)
            {
                const double rr = g.hz (k) / fm;
                modes[(size_t) k] += amp * cd (0.0, rr / q) / cd (1.0 - rr * rr, rr / q);
            }
        }
    }

    // ---- magnitude: the speaker's voicing x the mic (far-field response + proximity at this distance) ----
    std::vector<double> mag ((size_t) N / 2 + 1);
    // close to a big cone the wavefront is nearly flat: the proximity effect follows the distance to the cone's edge
    const double rEff = std::hypot (dist + s.depth * 0.5, s.a);
    auto micMag = [&] (MicType m, double f)
    {
        double grad = 0.5;
        const double db = micDb (m, f, grad);
        return std::pow (10.0, db / 20.0) * proximity (f, rEff, grad) / proximity (1000.0, rEff, grad);
    };
    for (int k = 0; k <= N / 2; ++k)
    {
        const double f = juce::jmax (1.0, k * sr / N);
        const double m = mic == MicType::dynamicPlusRibbon ? 0.5 * (micMag (MicType::dynamic, f) + micMag (MicType::ribbon, f)) : micMag (mic, f);
        mag[(size_t) k] = std::pow (10.0, voicingDb (s, f) / 20.0) * m;
    }
    auto H = minimumPhase (mag, N);

    // ---- physics relative to the reference (keeps the fine structure, the position and distance changes), box modes ----
    // drop the flight time but keep 0.75 ms of lead-in (the cone's spread of path lengths arrives a little early)
    const double tShift = (dist + s.depth * 0.3) / soundSpeed - 0.00075;
    for (int k = 0; k <= N / 2; ++k)
    {
        const double f = k * sr / N;
        const int kp = juce::jmin (k, K - 1);
        const cd ref = (*refS)[(size_t) k];
        cd phys = h[(size_t) kp] / (std::abs (ref) > 1.0e-12 ? ref : cd (1.0e-12, 0.0)) * modes[(size_t) kp];
        if (k >= K) phys = std::abs (h[(size_t) kp] / (*refS)[(size_t) kp]);
        cd box (1.0, 0.0);
        for (auto& bm : s.modes)
        {
            const double rr = juce::jmax (1.0e-6, f) / bm.hz;
            box += bm.gain * cd (0.0, rr / bm.q) / cd (1.0 - rr * rr, rr / bm.q);
        }
        H[(size_t) k] *= phys * box * std::polar (1.0, twoPi * f * tShift);
    }

    // ---- to the time domain ----
    int order = 0; while ((1 << order) < N) ++order;
    juce::dsp::FFT fft (order);
    std::vector<std::complex<float>> a ((size_t) N), b ((size_t) N);
    for (int k = 0; k <= N / 2; ++k)
    {
        a[(size_t) k] = std::complex<float> ((float) H[(size_t) k].real(), (float) H[(size_t) k].imag());
        if (k > 0 && k < N / 2) a[(size_t) (N - k)] = std::conj (a[(size_t) k]);
    }
    a[(size_t) N / 2] = std::complex<float> (a[(size_t) N / 2].real(), 0.0f);
    fft.perform (a.data(), b.data(), true);

    const int len = juce::jmin (N, (int) (sr * 0.07));
    const int fade = len / 5;
    juce::AudioBuffer<float> out (1, len);
    auto* o = out.getWritePointer (0);
    for (int n = 0; n < len; ++n)
    {
        float w = 1.0f;
        if (n > len - fade) w = 0.5f + 0.5f * std::cos (juce::MathConstants<float>::pi * (float) (n - (len - fade)) / (float) fade);
        o[n] = b[(size_t) n].real() * w;
    }
    // a speaker passes no DC: take out what the truncation left behind as a very slow (< 15 Hz) bump
    {
        double dc = 0.0, wsum = 0.0;
        for (int n = 0; n < len; ++n) dc += o[n];
        std::vector<double> hann ((size_t) len);
        for (int n = 0; n < len; ++n) { hann[(size_t) n] = 0.5 - 0.5 * std::cos (twoPi * (n + 0.5) / len); wsum += hann[(size_t) n]; }
        for (int n = 0; n < len; ++n) o[n] -= (float) (dc * hann[(size_t) n] / wsum);
    }

    // about unity gain through the mids, so cabinets, mics and positions swap without big level jumps
    double sum = 0.0;
    int count = 0;
    for (double f = 300.0; f <= 3000.0; f *= 1.06)
    {
        cd acc (0.0, 0.0);
        const double w = twoPi * f / sr;
        for (int n = 0; n < len; ++n) acc += (double) o[n] * std::polar (1.0, -w * n);
        sum += std::abs (acc);
        ++count;
    }
    out.applyGain ((float) (count / juce::jmax (1.0e-9, sum)));

    {
        std::lock_guard<std::mutex> l (cacheLock);
        if (irCache.size() > 96) irCache.clear();
        irCache[cacheKey] = std::make_shared<const juce::AudioBuffer<float>> (out);
    }
    return out;
}

} // namespace wis::speaker
