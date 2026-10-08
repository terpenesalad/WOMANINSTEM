#pragma once

#include <juce_graphics/juce_graphics.h>
#include <juce_data_structures/juce_data_structures.h>
#include <vector>

namespace wis
{

/** How the oscilloscope draws. Saved in the settings file as "scope.*". */
struct ScopeSettings
{
    enum Shape { swirl = 0, xy, wave, numShapes };
    enum Palette { tame = 0, phosphor, aqua, hotPink, amber, rainbow, numPalettes };

    int shape = swirl;
    int palette = tame;
    bool autoSize = true;
    bool mirror = false;
    float size = 1.0f;       // zoom, 0.25 .. 4
    float trail = 0.45f;     // 0 .. 1: how long the beam glows (0.03 s .. 3 s)
    float glow = 0.6f;       // 0 .. 1: bloom around the beam
    float spin = 0.0f;       // -1 .. 1: slow rotation, up to half a turn per second
    float tangle = 0.35f;    // Swirl: 0 = clean circles .. 1 = knotted scribbles. Wave: time window.
    float beam = 0.5f;       // 0 .. 1: brightness

    static juce::StringArray shapeNames();
    static juce::StringArray paletteNames();

    void save (juce::PropertiesFile&) const;
    void load (const juce::PropertiesFile&);
};

/** A software "phosphor screen": every audio sample moves a beam that deposits light, the light fades
    with time, and bright areas bloom. Beam brightness falls with speed like a real CRT, so slow parts of the
    figure glow and fast jumps leave faint threads.

    Usage per video frame: beginFrame (dt) -> addSamples (...) -> renderTo (image). No GUI dependencies
    beyond juce::Image, so it can be driven from tests. */
class ScopeRenderer
{
public:
    ScopeRenderer();

    /** Size of the light buffer (pixels). The window scales the image up, so this can be smaller than the screen. */
    void setSize (int width, int height);
    int getWidth() const  { return w; }
    int getHeight() const { return h; }

    void clear();

    /** Fades the screen by dt seconds of persistence and advances spin / colour cycling. */
    void beginFrame (double dtSeconds, const ScopeSettings& s);

    /** Draws two channels of audio (a = left / you, b = right / the song). */
    void addSamples (const float* a, const float* b, int n, double sampleRate, const ScopeSettings& s);

    /** Tone-maps the light buffer, adds the bloom and writes an ARGB image of getWidth() x getHeight(). */
    void renderTo (juce::Image& image, const ScopeSettings& s);

    /** Current auto-size level (peak follower), for tests / the UI. */
    float getLevel() const { return level; }
    /** Average brightness of the last rendered image (0..1). */
    float getLastMeanBrightness() const { return meanBrightness; }

private:
    struct Colour3 { float r, g, b; };
    Colour3 beamColour (float speed) const;
    void deposit (float x0, float y0, float x1, float y1, float energy, Colour3 c);
    void splat (float px, float py, float e, Colour3 c);
    void drawTo (float nx, float ny, float energy, Colour3 c, const ScopeSettings& s);
    void drawWave (const ScopeSettings& s, double sampleRate);

    int w = 0, h = 0;
    std::vector<float> light;          // r g b per pixel
    std::vector<float> small, smallTmp;// bloom buffer (quarter resolution, r g b)
    int sw = 0, sh = 0;
    std::vector<int> colX0, colX1;     // compose: per-column bloom indices, fraction and vignette
    std::vector<float> colFx, colVig, bloomRow;

    // signal state
    struct Allpass { float a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0; float process (float x) noexcept; };
    Allpass pathI[4], pathQ[4];
    float qDelay = 0.0f;
    std::vector<float> history;        // mono history for Tangle and Wave (power of two)
    int histPos = 0;
    float level = 0.05f;               // auto-size follower
    double time = 0.0, spinAngle = 0.0;
    double lastRate = 48000.0;
    int palette = 0;
    bool penDown = false;
    float lastPx[4] {}, lastPy[4] {};  // beam position per mirror copy (pixels)
    float lastNx = 0, lastNy = 0;
    juce::uint32 frameCount = 0;
    float meanBrightness = 0.0f;
    double frameDt = 1.0 / 60.0, tau = 0.3;
};

} // namespace wis
