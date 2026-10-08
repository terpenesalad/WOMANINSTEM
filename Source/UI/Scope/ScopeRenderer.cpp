#include "ScopeRenderer.h"
#include <cmath>

namespace wis
{

// =====================================================================================================
//  Settings
// =====================================================================================================
juce::StringArray ScopeSettings::shapeNames()   { return { "Swirl", "XY", "Wave" }; }
juce::StringArray ScopeSettings::paletteNames() { return { "Tame", "Phosphor", "Aqua", "Hot pink", "Amber", "Rainbow" }; }

void ScopeSettings::save (juce::PropertiesFile& p) const
{
    p.setValue ("scope.shape", shape);
    p.setValue ("scope.palette", palette);
    p.setValue ("scope.autoSize", autoSize);
    p.setValue ("scope.mirror", mirror);
    p.setValue ("scope.size", size);
    p.setValue ("scope.trail", trail);
    p.setValue ("scope.glow", glow);
    p.setValue ("scope.spin", spin);
    p.setValue ("scope.tangle", tangle);
    p.setValue ("scope.beam", beam);
}

void ScopeSettings::load (const juce::PropertiesFile& p)
{
    const ScopeSettings d;
    shape    = juce::jlimit (0, numShapes - 1,   p.getIntValue ("scope.shape", d.shape));
    palette  = juce::jlimit (0, numPalettes - 1, p.getIntValue ("scope.palette", d.palette));
    autoSize = p.getBoolValue ("scope.autoSize", d.autoSize);
    mirror   = p.getBoolValue ("scope.mirror", d.mirror);
    size     = juce::jlimit (0.25f, 4.0f, (float) p.getDoubleValue ("scope.size", d.size));
    trail    = juce::jlimit (0.0f, 1.0f, (float) p.getDoubleValue ("scope.trail", d.trail));
    glow     = juce::jlimit (0.0f, 1.0f, (float) p.getDoubleValue ("scope.glow", d.glow));
    spin     = juce::jlimit (-1.0f, 1.0f, (float) p.getDoubleValue ("scope.spin", d.spin));
    tangle   = juce::jlimit (0.0f, 1.0f, (float) p.getDoubleValue ("scope.tangle", d.tangle));
    beam     = juce::jlimit (0.0f, 1.0f, (float) p.getDoubleValue ("scope.beam", d.beam));
}

// =====================================================================================================
//  Renderer
// =====================================================================================================
namespace
{
    constexpr int historySize = 1 << 15;   // mono history: Wave window (up to 2 x 80 ms at 192 kHz) and Tangle delay
    constexpr float tangleDelaySeconds = 0.003f;
    constexpr float levelFloor = 0.015f;   // auto-size never magnifies more than this (keeps hiss from filling the screen)
    constexpr float fill = 0.92f;          // auto-size: the figure's peak reaches just inside the circle
    constexpr double levelRelease = 0.8;   // auto-size release (seconds): the figure breathes with the playing
    constexpr float beamEnergy = 32.0f;    // calibrated so a steady circle at default brightness has a white-hot core
    constexpr float waveEnergy = 5.0f;

    // Olli Niemitalo's 90-degree phase difference network: two all-pass chains whose outputs stay ~90 degrees
    // apart from ~20 Hz to ~20 kHz. Path I vs path Q (one sample later) = the analytic signal of the input,
    // so a sine draws a circle and its radius follows the loudness.
    constexpr float coeffI[4] = { 0.6923878f, 0.9360654322959f, 0.9882295226860f, 0.9987488452737f };
    constexpr float coeffQ[4] = { 0.4021921162426f, 0.8561710882420f, 0.9722909545651f, 0.9952884791278f };

    inline float lerp (float a, float b, float t) noexcept { return a + (b - a) * t; }

    /** 1 - exp (-x) for x >= 0, from a table (the compose loop runs it three times per pixel). */
    struct ToneCurve
    {
        static constexpr int size = 2048;
        static constexpr float maxX = 8.0f;
        float table[size + 2];
        ToneCurve() { for (int i = 0; i <= size + 1; ++i) table[i] = 1.0f - std::exp (-maxX * (float) i / (float) size); }
        float operator() (float x) const noexcept
        {
            if (! (x > 0.0f)) return 0.0f;
            const float f = x * ((float) size / maxX);
            if (f >= (float) size) return table[size];
            const int i = (int) f;
            return lerp (table[i], table[i + 1], f - (float) i);
        }
    };
    const ToneCurve& toneCurve() { static const ToneCurve t; return t; }
}

float ScopeRenderer::Allpass::process (float x) noexcept
{
    const float y = a2 * (x + y2) - x2;
    x2 = x1; x1 = x;
    y2 = y1; y1 = y;
    return y;
}

ScopeRenderer::ScopeRenderer()
{
    for (int i = 0; i < 4; ++i)
    {
        pathI[i].a2 = coeffI[i] * coeffI[i];
        pathQ[i].a2 = coeffQ[i] * coeffQ[i];
    }
    history.assign ((size_t) historySize, 0.0f);
    setSize (320, 240);
}

void ScopeRenderer::setSize (int width, int height)
{
    width = juce::jmax (8, width);
    height = juce::jmax (8, height);
    if (width == w && height == h) return;
    w = width;
    h = height;
    light.assign ((size_t) (w * h * 3), 0.0f);
    sw = (w + 3) / 4;
    sh = (h + 3) / 4;
    small.assign ((size_t) (sw * sh * 3), 0.0f);
    colX0.clear();   // compose tables are rebuilt for the new size
    smallTmp.assign (small.size(), 0.0f);
    penDown = false;
}

void ScopeRenderer::clear()
{
    std::fill (light.begin(), light.end(), 0.0f);
    penDown = false;
}

void ScopeRenderer::beginFrame (double dt, const ScopeSettings& s)
{
    dt = juce::jlimit (0.0, 0.25, dt);
    frameDt = dt;
    tau = 0.03 * std::pow (100.0, (double) s.trail);   // 0.03 .. 3 s
    const float decay = (float) std::exp (-dt / tau);
    for (auto& v : light) v *= decay;

    time += dt;
    spinAngle = std::fmod (spinAngle + (double) s.spin * juce::MathConstants<double>::pi * dt, juce::MathConstants<double>::twoPi);
    palette = s.palette;
}

ScopeRenderer::Colour3 ScopeRenderer::beamColour (float speed) const
{
    speed = juce::jlimit (0.0f, 1.0f, speed);
    const float slow = 0.5f + 0.5f * (float) std::sin (time * juce::MathConstants<double>::twoPi / 16.0);
    switch (palette)
    {
        case ScopeSettings::phosphor: return { 0.30f, 1.00f, 0.38f };
        case ScopeSettings::aqua:     return { lerp (0.10f, 0.42f, speed), lerp (0.85f, 0.55f, speed), 1.0f };
        case ScopeSettings::hotPink:  return { lerp (1.00f, 0.56f, speed), lerp (0.30f, 0.36f, speed), lerp (0.56f, 1.00f, speed) };
        case ScopeSettings::amber:    return { 1.00f, lerp (0.62f, 0.45f, speed), 0.14f };
        case ScopeSettings::rainbow:
        {
            const float hue = (float) std::fmod (time * 0.07 + speed * 0.3, 1.0);
            const auto c = juce::Colour::fromHSV (hue, 0.85f, 1.0f, 1.0f);
            return { c.getFloatRed(), c.getFloatGreen(), c.getFloatBlue() };
        }
        case ScopeSettings::tame:
        default:
        {
            // lime for slow / low parts, aqua for fast / high ones (and the balance drifts slowly). Pushed apart
            // with a smoothstep so the picture has both colours rather than one minty average.
            const float raw = juce::jlimit (0.0f, 1.0f, 0.5f + 0.5f * (slow - 0.5f) + 1.1f * (speed - 0.4f));
            const float t = raw * raw * (3.0f - 2.0f * raw);
            return { lerp (0.66f, 0.04f, t), lerp (1.00f, 0.86f, t), lerp (0.10f, 1.00f, t) };
        }
    }
}

void ScopeRenderer::splat (float px, float py, float e, Colour3 c)
{
    if (! (px >= 0.0f && py >= 0.0f && px < (float) (w - 1) && py < (float) (h - 1))) return;
    const int x0 = (int) px, y0 = (int) py;
    const float fx = px - (float) x0, fy = py - (float) y0;
    const float w00 = (1 - fx) * (1 - fy) * e, w10 = fx * (1 - fy) * e, w01 = (1 - fx) * fy * e, w11 = fx * fy * e;
    float* p = light.data() + ((size_t) y0 * (size_t) w + (size_t) x0) * 3;
    float* q = p + (size_t) w * 3;
    p[0] += c.r * w00; p[1] += c.g * w00; p[2] += c.b * w00;
    p[3] += c.r * w10; p[4] += c.g * w10; p[5] += c.b * w10;
    q[0] += c.r * w01; q[1] += c.g * w01; q[2] += c.b * w01;
    q[3] += c.r * w11; q[4] += c.g * w11; q[5] += c.b * w11;
}

void ScopeRenderer::deposit (float x0, float y0, float x1, float y1, float energy, Colour3 c)
{
    const float len = std::hypot (x1 - x0, y1 - y0);
    const int steps = juce::jlimit (1, 4096, (int) std::ceil (len));
    const float e = energy / (float) steps;   // the beam's light is spread over its path: fast = faint
    const float dx = (x1 - x0) / (float) steps, dy = (y1 - y0) / (float) steps;
    for (int k = 1; k <= steps; ++k)
        splat (x0 + dx * (float) k, y0 + dy * (float) k, e, c);
}

void ScopeRenderer::drawTo (float nx, float ny, float energy, Colour3 c, const ScopeSettings& s)
{
    const float cx = (float) w * 0.5f, cy = (float) h * 0.5f;
    const float radius = (float) juce::jmin (w, h) * 0.46f;
    const float cs = (float) std::cos (spinAngle), sn = (float) std::sin (spinAngle);
    const int copies = s.mirror ? 4 : 1;
    for (int k = 0; k < copies; ++k)
    {
        const float mx = (k & 1) ? -nx : nx;
        const float my = (k & 2) ? -ny : ny;
        const float rx = mx * cs - my * sn, ry = mx * sn + my * cs;
        const float px = juce::jlimit (-(float) w, 2.0f * (float) w, cx + rx * radius);
        const float py = juce::jlimit (-(float) h, 2.0f * (float) h, cy - ry * radius);
        if (penDown)
            deposit (lastPx[k], lastPy[k], px, py, energy, c);
        lastPx[k] = px;
        lastPy[k] = py;
    }
    penDown = true;
}

void ScopeRenderer::addSamples (const float* a, const float* b, int n, double sampleRate, const ScopeSettings& s)
{
    if (n <= 0 || a == nullptr) return;
    if (b == nullptr) b = a;

    if (std::abs (sampleRate - lastRate) > 1.0)
    {
        lastRate = sampleRate;
        for (auto& ap : pathI) { ap.x1 = ap.x2 = ap.y1 = ap.y2 = 0; }
        for (auto& ap : pathQ) { ap.x1 = ap.x2 = ap.y1 = ap.y2 = 0; }
        penDown = false;
    }
    const float sr = (float) juce::jmax (8000.0, sampleRate);

    // auto size: fast attack, ~1.5 s release
    float pk = 0.0f;
    for (int i = 0; i < n; ++i) pk = juce::jmax (pk, std::abs (a[i]), std::abs (b[i]));
    if (s.shape == ScopeSettings::swirl) pk *= 1.1f;   // the analytic signal's envelope sits a little above the peaks
    if (pk > level) level += (pk - level) * 0.6f;
    else level = juce::jmax (pk, level * (float) std::exp (-(double) n / (levelRelease * sr)));
    level = juce::jmax (level, levelFloor * 0.25f);
    const float gain = s.autoSize ? s.size * fill / juce::jmax (level, levelFloor) : s.size;

    const float radius = (float) juce::jmin (w, h) * 0.46f;
    const float beamGain = 0.15f + 1.7f * s.beam;
    const float energy = beamGain * beamEnergy * radius / (sr * (float) tau);
    const int delay = juce::jlimit (1, historySize - 1, (int) (tangleDelaySeconds * sr));

    for (int i = 0; i < n; ++i)
    {
        const float m = 0.5f * (a[i] + b[i]);
        history[(size_t) histPos] = m;
        histPos = (histPos + 1) & (historySize - 1);

        if (s.shape == ScopeSettings::wave) continue;

        float x, y;
        if (s.shape == ScopeSettings::swirl)
        {
            float vi = m, vq = m;
            for (auto& ap : pathI) vi = ap.process (vi);
            for (auto& ap : pathQ) vq = ap.process (vq);
            const float q = qDelay;
            qDelay = vq;
            const float delayed = history[(size_t) ((histPos - 1 - delay) & (historySize - 1))];
            x = vi;
            y = lerp (q, delayed * 1.2f, s.tangle);
        }
        else
        {
            x = a[i];
            y = b[i];
        }

        const float nx = x * gain, ny = y * gain;
        const float speed = std::hypot (nx - lastNx, ny - lastNy) / 0.03f;
        lastNx = nx; lastNy = ny;
        drawTo (nx, ny, energy, beamColour (speed), s);
    }

    if (s.shape == ScopeSettings::wave)
    {
        // Wave keeps its own gain: follow the mono signal
        drawWave (s, sampleRate);
        penDown = false;
    }
}

void ScopeRenderer::drawWave (const ScopeSettings& s, double sampleRate)
{
    const float sr = (float) juce::jmax (8000.0, sampleRate);
    const float windowSec = 0.005f * std::pow (16.0f, s.tangle);   // 5 .. 80 ms
    const int len = juce::jlimit (16, historySize / 3, (int) (windowSec * sr));
    const int newest = (histPos - 1) & (historySize - 1);
    auto at = [&] (int i) { return history[(size_t) (i & (historySize - 1))]; };

    // trigger: the latest rising zero crossing of the low end (so the picture locks to the bass note, not to
    // every harmonic) that still leaves a full window after it
    const int searchFrom = newest - 2 * len - 64, searchTo = newest - len;
    const float coeff = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 150.0f / sr);
    float lp = at (searchFrom - 256), prevLp = lp;
    for (int k = searchFrom - 255; k < searchFrom; ++k) lp += coeff * (at (k) - lp);
    int start = searchTo;
    for (int k = searchFrom; k <= searchTo; ++k)
    {
        prevLp = lp;
        lp += coeff * (at (k) - lp);
        if (prevLp < 0.0f && lp >= 0.0f) start = k;   // keep the latest one
    }

    const float gain = s.autoSize ? s.size * fill / juce::jmax (level, levelFloor) : s.size;
    const float spanX = (float) w * 0.47f / ((float) juce::jmin (w, h) * 0.46f);   // normalised units across the width
    const float beamGain = 0.15f + 1.7f * s.beam;
    const float perFrame = beamGain * waveEnergy * (float) w * (float) (frameDt / tau);
    const int points = juce::jmin (len, 4096);
    const float energy = perFrame / (float) points;

    penDown = false;
    for (int p = 0; p < points; ++p)
    {
        const float t = (float) p / (float) (points - 1);
        const int idx = start + (int) (t * (float) (len - 1));
        const float nx = (t * 2.0f - 1.0f) * spanX;
        const float ny = at (idx) * gain;
        const float speed = std::abs (ny - lastNy) / 0.03f * 0.5f;
        lastNy = ny;
        drawTo (nx, ny, energy, beamColour (speed), s);
    }
}

void ScopeRenderer::renderTo (juce::Image& image, const ScopeSettings& s)
{
    if (! image.isValid() || image.getWidth() != w || image.getHeight() != h || image.getFormat() != juce::Image::ARGB)
        image = juce::Image (juce::Image::ARGB, w, h, false, juce::SoftwareImageType());

    // ---- bloom: quarter resolution, clipped, two box blurs ----
    std::fill (small.begin(), small.end(), 0.0f);
    for (int y = 0; y < h; ++y)
    {
        const float* src = light.data() + (size_t) y * (size_t) w * 3;
        float* dst = small.data() + (size_t) (y / 4) * (size_t) sw * 3;
        for (int x = 0; x < w; ++x)
        {
            float* d = dst + (x / 4) * 3;
            d[0] += juce::jmin (src[x * 3 + 0], 3.0f);
            d[1] += juce::jmin (src[x * 3 + 1], 3.0f);
            d[2] += juce::jmin (src[x * 3 + 2], 3.0f);
        }
    }
    const int rb = juce::jmax (2, sw / 60);
    auto boxBlur = [&] (std::vector<float>& from, std::vector<float>& to, bool horizontal)
    {
        const int outer = horizontal ? sh : sw, inner = horizontal ? sw : sh;
        const size_t stride = horizontal ? 3 : (size_t) sw * 3;
        const float norm = 1.0f / (float) (2 * rb + 1);
        for (int o = 0; o < outer; ++o)
        {
            const size_t base = horizontal ? (size_t) o * (size_t) sw * 3 : (size_t) o * 3;
            for (int c = 0; c < 3; ++c)
            {
                float acc = 0.0f;
                for (int k = -rb; k <= rb; ++k)
                    acc += from[base + (size_t) juce::jlimit (0, inner - 1, k) * stride + (size_t) c];
                for (int i = 0; i < inner; ++i)
                {
                    to[base + (size_t) i * stride + (size_t) c] = acc * norm;
                    acc += from[base + (size_t) juce::jmin (inner - 1, i + rb + 1) * stride + (size_t) c]
                         - from[base + (size_t) juce::jmax (0, i - rb) * stride + (size_t) c];
                }
            }
        }
    };
    for (int pass = 0; pass < 2; ++pass)
    {
        boxBlur (small, smallTmp, true);
        boxBlur (smallTmp, small, false);
    }

    // ---- compose: background + 1 - exp (-(light + bloom)) ----
    const Colour3 bg = palette == ScopeSettings::tame ? Colour3 { 0.030f, 0.024f, 0.075f } : Colour3 { 0.010f, 0.012f, 0.016f };
    const float bgv[3] = { bg.r, bg.g, bg.b };
    const float bloomGain = s.glow * 0.12f;   // the quarter-res sum covers 16 pixels
    const float cx = (float) w * 0.5f, cy = (float) h * 0.5f, invDiag2 = 1.0f / (cx * cx + cy * cy);
    const auto& tone = toneCurve();
    juce::int64 total = 0;

    // per-column tables: bloom interpolation and vignette
    if ((int) colX0.size() != w)
    {
        colX0.resize ((size_t) w); colX1.resize ((size_t) w); colFx.resize ((size_t) w); colVig.resize ((size_t) w);
        for (int x = 0; x < w; ++x)
        {
            const float sx = juce::jlimit (0.0f, (float) (sw - 1), ((float) x + 0.5f) / 4.0f - 0.5f);
            colX0[(size_t) x] = (int) sx * 3;
            colX1[(size_t) x] = juce::jmin (sw - 1, (int) sx + 1) * 3;
            colFx[(size_t) x] = sx - (float) (int) sx;
            const float dx = (float) x - cx;
            colVig[(size_t) x] = 0.35f * dx * dx * invDiag2;
        }
    }
    bloomRow.resize ((size_t) sw * 3);

    const juce::Image::BitmapData bd (image, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < h; ++y)
    {
        const float* src = light.data() + (size_t) y * (size_t) w * 3;
        auto* line = bd.getLinePointer (y);
        // bloom: lerp two quarter-res rows once, then interpolate across
        const float sy = juce::jlimit (0.0f, (float) (sh - 1), ((float) y + 0.5f) / 4.0f - 0.5f);
        const int y0 = (int) sy, y1 = juce::jmin (sh - 1, y0 + 1);
        const float fy = sy - (float) y0;
        const float* r0 = small.data() + (size_t) y0 * (size_t) sw * 3;
        const float* r1 = small.data() + (size_t) y1 * (size_t) sw * 3;
        for (int k = 0; k < sw * 3; ++k) bloomRow[(size_t) k] = bloomGain * lerp (r0[k], r1[k], fy);
        const float dy = (float) y - cy;
        const float rowVig = 1.0f - 0.35f * dy * dy * invDiag2;
        const juce::uint32 rowSeed = (juce::uint32) (y * 9277) + frameCount * 26699u;

        for (int x = 0; x < w; ++x)
        {
            const float* b0 = bloomRow.data() + colX0[(size_t) x];
            const float* b1 = bloomRow.data() + colX1[(size_t) x];
            const float fx = colFx[(size_t) x];
            const float vig = rowVig - colVig[(size_t) x];
            // a touch of noise so the dark gradient doesn't show 8-bit rings on a projector
            const float dither = (float) ((((juce::uint32) x * 1973u + rowSeed) * 2654435761u) >> 24) * (1.0f / 255.0f) - 0.5f;
            const float* p = src + x * 3;
            int out[3];
            for (int c = 0; c < 3; ++c)
            {
                const float lit = p[c] + b0[c] + (b1[c] - b0[c]) * fx;
                const float base = bgv[c] * vig;
                const float v = (base + (1.0f - base) * tone (lit)) * 255.0f + dither;
                out[c] = juce::jlimit (0, 255, (int) (v + 0.5f));
            }
            total += out[0] + out[1] + out[2];
            auto* px = reinterpret_cast<juce::PixelARGB*> (line + x * bd.pixelStride);
            px->setARGB (255, (juce::uint8) out[0], (juce::uint8) out[1], (juce::uint8) out[2]);
        }
    }
    meanBrightness = (float) (total / (3.0 * 255.0 * (double) w * (double) h));
    ++frameCount;
}

} // namespace wis
