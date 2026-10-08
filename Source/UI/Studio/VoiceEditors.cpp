#include "EditorParts.h"
#include "Daw/Instruments/PianoRoom.h"
#include "Daw/Instruments/YetiVoice.h"
#include "Daw/Instruments/RoomIr.h"

namespace wis::daw
{

namespace
{
    /** A titled box holding a ParamPanel. */
    class Section : public juce::Component
    {
    public:
        Section (BuiltinProcessor& p, const juce::String& t, const juce::StringArray& ids, juce::Colour c)
            : title (t), colour (c), panel (p, ids, c)
        {
            addAndMakeVisible (panel);
        }
        int heightFor (int width) const { return 30 + panel.heightFor (width - 16) + 8; }
        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced (2.0f);
            g.setColour (theme::panelRaised);
            g.fillRoundedRectangle (r, 8.0f);
            g.setColour (colour.withAlpha (0.5f));
            g.drawRoundedRectangle (r, 8.0f, 1.0f);
            g.setColour (colour);
            g.fillRoundedRectangle (r.withHeight (3.0f).reduced (10.0f, 0.0f), 1.5f);
            g.setColour (theme::text);
            g.setFont (uiFont (12.5f, true));
            g.drawText (title, r.reduced (12.0f, 0.0f).withHeight (28.0f), juce::Justification::centredLeft);
        }
        void resized() override { panel.setBounds (getLocalBounds().reduced (8, 0).withTrimmedTop (28).withTrimmedBottom (6)); }

    private:
        juce::String title;
        juce::Colour colour;
        ParamPanel panel;
    };

    juce::Colour mix (juce::Colour a, juce::Colour b, float t) { return a.interpolatedWith (b, juce::jlimit (0.0f, 1.0f, t)); }
}

// =====================================================================================================
//  Piano Room
// =====================================================================================================
/** The piano in its space: a little picture of the room (or forest, canyon, church...) you chose. */
class PianoScene : public juce::Component, private juce::Timer
{
public:
    explicit PianoScene (PianoRoom& p) : piano (p) { startTimerHz (15); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        const int space = (int) piano.param ("space");
        const int model = (int) piano.param ("model");
        juce::Path clip;
        clip.addRoundedRectangle (r, 8.0f);
        g.reduceClipRegion (clip);
        paintSpace (g, r, space);

        // the piano
        const float lid = piano.param ("lid");
        const float baseY = r.getBottom() - r.getHeight() * 0.16f;
        const float cx = r.getCentreX();
        const float h = r.getHeight();
        const juce::Colour body (0xff111114), edge (0xff3a3a44);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillEllipse (cx - h * 0.95f, baseY - 4.0f, h * 1.9f, 12.0f);
        if (model == 2)
        {
            // upright
            auto box = juce::Rectangle<float> (cx - h * 0.55f, baseY - h * 0.62f, h * 1.1f, h * 0.62f);
            g.setColour (juce::Colour (0xff3b2416));
            g.fillRoundedRectangle (box, 4.0f);
            g.setColour (juce::Colour (0xff5a3a24));
            g.drawRoundedRectangle (box, 4.0f, 1.5f);
            auto keys = box.withY (box.getY() + box.getHeight() * 0.48f).withHeight (h * 0.07f).expanded (h * 0.04f, 0.0f);
            g.setColour (juce::Colours::white.withAlpha (0.9f));
            g.fillRect (keys);
            g.setColour (juce::Colours::black);
            for (int k = 0; k < 22; ++k) if (k % 7 != 2 && k % 7 != 6) g.fillRect (keys.getX() + keys.getWidth() * (k + 0.65f) / 22.0f, keys.getY(), keys.getWidth() / 40.0f, keys.getHeight() * 0.6f);
            g.setColour (juce::Colour (0xff2a190f));
            g.fillRect (box.getX() + 6.0f, baseY - 2.0f, 6.0f, 2.0f);
        }
        else if (model == 4)
        {
            // toy piano
            auto box = juce::Rectangle<float> (cx - h * 0.3f, baseY - h * 0.32f, h * 0.6f, h * 0.32f);
            g.setColour (juce::Colour (0xffe2457a));
            g.fillRoundedRectangle (box, 6.0f);
            g.setColour (juce::Colours::white);
            auto keys = box.withY (box.getY() + 6.0f).withHeight (h * 0.07f).reduced (8.0f, 0.0f);
            g.fillRect (keys);
            g.setColour (juce::Colours::black);
            for (int k = 0; k < 10; ++k) if (k % 7 != 2 && k % 7 != 6) g.fillRect (keys.getX() + keys.getWidth() * (k + 0.65f) / 10.0f, keys.getY(), keys.getWidth() / 22.0f, keys.getHeight() * 0.6f);
        }
        else
        {
            // grand, side view, lid propped open as far as the Lid knob says
            const float len = h * 1.55f, top = baseY - h * 0.38f;
            juce::Path p;
            p.startNewSubPath (cx - len * 0.5f, top);
            p.lineTo (cx + len * 0.15f, top);
            p.cubicTo (cx + len * 0.35f, top, cx + len * 0.32f, top + h * 0.1f, cx + len * 0.5f, top + h * 0.1f);
            p.lineTo (cx + len * 0.5f, top + h * 0.17f);
            p.lineTo (cx - len * 0.5f, top + h * 0.17f);
            p.closeSubPath();
            g.setColour (body);
            g.fillPath (p);
            g.setColour (edge);
            g.strokePath (p, juce::PathStrokeType (1.5f));
            // lid
            const float angle = juce::jmap (lid, 0.0f, 1.0f, 0.02f, 0.62f);
            juce::Path lidPath;
            lidPath.addRectangle (0.0f, -3.0f, len * 0.92f, 4.0f);
            g.setColour (juce::Colour (0xff1c1c22));
            g.fillPath (lidPath, juce::AffineTransform::rotation (-angle).translated (cx - len * 0.5f, top));
            if (lid > 0.1f)
            {
                g.setColour (edge);
                g.drawLine (cx + len * 0.05f, top, cx + len * 0.05f - std::sin (angle) * 4.0f, top - std::sin (angle) * len * 0.45f, 2.0f);
            }
            // keyboard and legs
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.fillRect (cx - len * 0.56f, top + h * 0.03f, len * 0.06f, h * 0.05f);
            g.setColour (body);
            for (float lx : { -0.45f, 0.1f, 0.45f })
                g.fillRect (cx + len * lx, top + h * 0.17f, h * 0.035f, baseY - (top + h * 0.17f));
            if (model == 3)
            {
                g.setColour (theme::accent2.withAlpha (0.8f));
                g.setFont (uiFont (10.5f, true));
                g.drawText ("MODELLED", juce::Rectangle<float> (cx - len * 0.3f, top + h * 0.03f, len * 0.6f, h * 0.12f), juce::Justification::centred);
            }
        }

        // caption
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        auto cap = r.removeFromBottom (22.0f);
        g.fillRect (cap);
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.setFont (uiFont (12.0f, true));
        g.drawText (roomir::spaceNames()[space] + "   -   " + roomir::spaceDescription (space), cap.reduced (10.0f, 0.0f), juce::Justification::centredLeft);
        g.setColour (theme::warn);
        const auto status = piano.getStatus();
        if (status != PianoRoom::modelNames()[model])
            g.drawText (status, cap.reduced (10.0f, 0.0f), juce::Justification::centredRight);
    }

private:
    void timerCallback() override
    {
        const int key = (int) piano.param ("space") * 1000 + (int) piano.param ("model") * 100 + (int) (piano.param ("lid") * 20.0f);
        const auto status = piano.getStatus();
        if (key != lastKey || status != lastStatus) { lastKey = key; lastStatus = status; repaint(); }
    }

    static void paintSpace (juce::Graphics& g, juce::Rectangle<float> r, int space)
    {
        using namespace roomir;
        const float w = r.getWidth(), h = r.getHeight(), x0 = r.getX(), y0 = r.getY();
        const float floorY = y0 + h * 0.84f;
        auto gradient = [&] (juce::uint32 top, juce::uint32 bottom)
        {
            g.setGradientFill (juce::ColourGradient (juce::Colour (top), 0, y0, juce::Colour (bottom), 0, y0 + h, false));
            g.fillRect (r);
        };
        juce::Random rnd (space * 13 + 5);
        switch (space)
        {
            case forest:
            {
                gradient (0xff0f2a22, 0xff28432c);
                for (int layer = 0; layer < 3; ++layer)
                    for (int t = 0; t < 14; ++t)
                    {
                        const float tx = x0 + w * rnd.nextFloat();
                        const float th = h * (0.45f + 0.35f * rnd.nextFloat()) * (1.0f + 0.25f * layer);
                        const auto c = mix (juce::Colour (0xff163a2a), juce::Colour (0xff0a1d14), (float) layer / 2.0f);
                        g.setColour (juce::Colour (0xff2b1d14).withMultipliedBrightness (0.6f + 0.2f * layer));
                        g.fillRect (tx - 3.0f - layer, floorY - th * 0.35f, 6.0f + 2.0f * layer, th * 0.35f);
                        juce::Path tri;
                        tri.addTriangle (tx, floorY - th, tx - th * 0.22f, floorY - th * 0.3f, tx + th * 0.22f, floorY - th * 0.3f);
                        g.setColour (c);
                        g.fillPath (tri);
                    }
                g.setColour (juce::Colour (0xff1d3318));
                g.fillRect (x0, floorY, w, h);
                for (int s = 0; s < 6; ++s)   // light through the trees
                {
                    g.setColour (juce::Colours::lightyellow.withAlpha (0.05f));
                    const float sx = x0 + w * (0.1f + 0.15f * s);
                    juce::Path beam;
                    beam.addTriangle (sx, y0, sx + 30.0f, y0, sx + 90.0f, floorY);
                    g.fillPath (beam);
                }
                break;
            }
            case canyon:
            {
                gradient (0xffe8955a, 0xfff2c27c);
                for (int layer = 0; layer < 3; ++layer)
                {
                    juce::Path cliff;
                    const float base = y0 + h * (0.35f + 0.15f * layer);
                    cliff.startNewSubPath (x0, y0 + h);
                    for (int i = 0; i <= 12; ++i)
                        cliff.lineTo (x0 + w * i / 12.0f, base - h * 0.12f * rnd.nextFloat() - (i % 4 == 0 ? h * 0.1f : 0.0f));
                    cliff.lineTo (x0 + w, y0 + h);
                    cliff.closeSubPath();
                    g.setColour (mix (juce::Colour (0xffb35a33), juce::Colour (0xff6e2f1d), (float) layer / 2.0f));
                    g.fillPath (cliff);
                }
                break;
            }
            case church:
            case cathedral:
            {
                gradient (space == cathedral ? 0xff1c1b26 : 0xff2a2420, 0xff3a332c);
                const int arches = space == cathedral ? 7 : 5;
                for (int a = 0; a < arches; ++a)
                {
                    const float ax = x0 + w * (a + 0.5f) / arches, aw = w / arches * 0.5f, top = y0 + h * (space == cathedral ? 0.08f : 0.2f);
                    juce::Path arch;
                    arch.startNewSubPath (ax - aw * 0.5f, floorY);
                    arch.lineTo (ax - aw * 0.5f, top + aw);
                    arch.quadraticTo (ax, top - aw * 0.3f, ax + aw * 0.5f, top + aw);
                    arch.lineTo (ax + aw * 0.5f, floorY);
                    g.setColour (juce::Colour (0xff5b4a8a).withAlpha (0.35f));
                    g.fillPath (arch);
                    g.setColour (juce::Colour (0xff8a7a5a).withAlpha (0.5f));
                    g.strokePath (arch, juce::PathStrokeType (2.0f));
                }
                g.setColour (juce::Colour (0xff4a3b2e));
                g.fillRect (x0, floorY, w, h);
                break;
            }
            case concertHall:
            {
                gradient (0xff3a1c1c, 0xff5a2a26);
                g.setColour (juce::Colour (0xff6b2020));
                for (int row = 0; row < 4; ++row)
                    for (int s = 0; s < 24; ++s)
                        g.fillRoundedRectangle (x0 + w * s / 24.0f + 2.0f, y0 + h * (0.25f + 0.1f * row), w / 30.0f, h * 0.06f, 2.0f);
                g.setColour (juce::Colour (0xff7a4a2a));
                g.fillRect (x0, floorY, w, h);
                break;
            }
            case woodenStudio:
            case bigLiveRoom:
            {
                gradient (0xff5a3b22, 0xff3b2614);
                const int slats = space == bigLiveRoom ? 40 : 26;
                for (int s = 0; s < slats; ++s)
                {
                    g.setColour (juce::Colour (0xff7a5230).withMultipliedBrightness (0.85f + 0.3f * rnd.nextFloat()));
                    g.fillRect (x0 + w * s / slats, y0, w / slats - 2.0f, floorY - y0);
                }
                g.setColour (juce::Colour (0xff2d3442));
                for (int p = 0; p < 4; ++p) g.fillRoundedRectangle (x0 + w * (0.08f + 0.25f * p), y0 + h * 0.15f, w * 0.12f, h * 0.3f, 4.0f);
                g.setColour (juce::Colour (0xff4a3020));
                g.fillRect (x0, floorY, w, h);
                break;
            }
            case livingRoom:
            {
                gradient (0xff6e5a48, 0xff4c3d32);
                g.setColour (juce::Colour (0xff8a3b2b));
                g.fillRoundedRectangle (x0 + w * 0.05f, floorY - h * 0.25f, w * 0.22f, h * 0.25f, 10.0f);   // sofa
                g.setColour (juce::Colour (0xfffff3c4).withAlpha (0.8f));
                g.fillEllipse (x0 + w * 0.85f, y0 + h * 0.2f, h * 0.12f, h * 0.12f);                         // lamp
                g.setColour (juce::Colour (0xff7a4d2a));
                g.fillRect (x0, floorY, w, h);
                g.setColour (juce::Colour (0xff3b5a7a));
                g.fillEllipse (x0 + w * 0.3f, floorY + 2.0f, w * 0.4f, h * 0.12f);                           // rug
                break;
            }
            case barClub:
            {
                gradient (0xff1a1010, 0xff2e1a14);
                for (int l = 0; l < 5; ++l)
                {
                    const float lx = x0 + w * (0.1f + 0.2f * l);
                    g.setColour (juce::Colour (0xffffb04a).withAlpha (0.18f));
                    g.fillEllipse (lx - 40.0f, y0 + h * 0.1f, 80.0f, 80.0f);
                    g.setColour (juce::Colour (0xffffc46a));
                    g.fillEllipse (lx - 5.0f, y0 + h * 0.2f, 10.0f, 10.0f);
                }
                g.setColour (juce::Colour (0xff3b2214));
                g.fillRect (x0, floorY, w, h);
                break;
            }
            case bathroom:
            {
                gradient (0xffd8ecef, 0xffb6d4d8);
                g.setColour (juce::Colours::white.withAlpha (0.7f));
                for (float tx = x0; tx < x0 + w; tx += 18.0f) g.drawVerticalLine ((int) tx, y0, floorY);
                for (float ty = y0; ty < floorY; ty += 18.0f) g.drawHorizontalLine ((int) ty, x0, x0 + w);
                g.setColour (juce::Colour (0xff8fb3b8));
                g.fillRect (x0, floorY, w, h);
                break;
            }
            case carPark:
            {
                gradient (0xff3a3d40, 0xff26292c);
                for (int p = 0; p < 5; ++p)
                {
                    g.setColour (juce::Colour (0xff5a5d60));
                    g.fillRect (x0 + w * (0.08f + 0.2f * p), y0, w * 0.04f, floorY - y0);
                    g.setColour (juce::Colour (0xfff2c94c));
                    g.fillRect (x0 + w * (0.08f + 0.2f * p), y0 + h * 0.5f, w * 0.04f, 4.0f);
                }
                g.setColour (juce::Colour (0xffe8f4ff).withAlpha (0.6f));
                for (int l = 0; l < 4; ++l) g.fillRect (x0 + w * (0.15f + 0.22f * l), y0 + 6.0f, w * 0.1f, 3.0f);
                g.setColour (juce::Colour (0xff2e3134));
                g.fillRect (x0, floorY, w, h);
                break;
            }
            case plate:
            case spring:
            {
                gradient (0xff1f2a36, 0xff131a22);
                g.setColour (juce::Colour (0xff8a9aaa).withAlpha (0.5f));
                if (space == plate) g.drawRect (juce::Rectangle<float> (x0 + w * 0.06f, y0 + h * 0.15f, w * 0.2f, h * 0.45f), 2.0f);
                else
                {
                    juce::Path coil;
                    for (int i = 0; i <= 80; ++i)
                    {
                        const float t = (float) i / 80.0f;
                        const float px = x0 + w * (0.05f + 0.22f * t), py = y0 + h * 0.3f + std::sin (t * 60.0f) * 8.0f;
                        if (i == 0) coil.startNewSubPath (px, py); else coil.lineTo (px, py);
                    }
                    g.strokePath (coil, juce::PathStrokeType (1.5f));
                }
                g.setColour (juce::Colour (0xff1a2128));
                g.fillRect (x0, floorY, w, h);
                break;
            }
            case vocalBooth:
            {
                gradient (0xff2a2e38, 0xff20232b);
                g.setColour (juce::Colour (0xff3a3f4c));
                for (float tx = x0; tx < x0 + w; tx += 14.0f)
                {
                    juce::Path wedge;
                    wedge.addTriangle (tx, floorY, tx + 7.0f, y0 + 10.0f, tx + 14.0f, floorY);
                    g.fillPath (wedge);
                }
                break;
            }
            default:
                gradient (0xff22252c, 0xff191b20);
                break;
        }
    }

    PianoRoom& piano;
    int lastKey = -1;
    juce::String lastStatus;
};

class PianoRoomEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PianoRoomEditor (PianoRoom& p)
        : AudioProcessorEditor (p), piano (p), header (p, "Piano Room"), scene (p),
          pianoSec (p, "PIANO", { "model", "hammer", "felt", "lid", "dynamics", "release", "pedal", "volume" }, theme::accent2),
          character (p, "CHARACTER", { "tune", "stretch", "detune", "tack", "age", "mechanics", "resonance" }, juce::Colour (0xfff97316)),
          tone (p, "TONE", { "mic", "bass", "mid", "treble", "width", "drive", "comp", "tape", "lofi" }, juce::Colour (0xff38bdf8)),
          space (p, "SPACE", { "space", "spaceMix", "size", "distance", "spaceTone", "predelay" }, juce::Colour (0xff2dd4bf))
    {
        header.onPresetChanged = [this] { description.setText (PianoRoom::presetDescription (piano.getCurrentProgram()), juce::dontSendNotification); scene.repaint(); };
        description.setText (PianoRoom::presetDescription (piano.getCurrentProgram()), juce::dontSendNotification);
        description.setFont (uiFont (12.5f));
        description.setColour (juce::Label::textColourId, theme::textDim);
        for (auto* c : std::initializer_list<juce::Component*> { &header, &scene, &description, &pianoSec, &character, &tone, &space })
            addAndMakeVisible (c);
        setResizable (true, false);
        setResizeLimits (900, 600, 1800, 1400);
        setSize (1120, 760);
        startTimerHz (4);
    }

    void paint (juce::Graphics& g) override { paintEditorBackground (g, getLocalBounds(), theme::accent2); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 8);
        header.setBounds (r.removeFromTop (34));
        description.setBounds (r.removeFromTop (22));
        r.removeFromTop (4);
        scene.setBounds (r.removeFromTop (juce::jlimit (120, 200, r.getHeight() / 4)));
        r.removeFromTop (8);
        auto left = r.removeFromLeft (r.getWidth() / 2 - 4);
        r.removeFromLeft (8);
        auto right = r;
        pianoSec.setBounds (left.removeFromTop (pianoSec.heightFor (left.getWidth())));
        left.removeFromTop (8);
        character.setBounds (left.removeFromTop (juce::jmin (left.getHeight(), character.heightFor (left.getWidth()))));
        tone.setBounds (right.removeFromTop (tone.heightFor (right.getWidth())));
        right.removeFromTop (8);
        space.setBounds (right.removeFromTop (juce::jmin (right.getHeight(), space.heightFor (right.getWidth()))));
    }

private:
    void timerCallback() override { scene.repaint(); }

    PianoRoom& piano;
    EditorHeader header;
    juce::Label description;
    PianoScene scene;
    Section pianoSec, character, tone, space;
};

// =====================================================================================================
//  Yodel Yeti
// =====================================================================================================
/** The yeti himself, drawn live: his mouth makes the vowels, he bobs on every note, his brows and eyes follow
    the pitch, he sways with the vibrato, closes his eyes on long notes and puts his hands together to pray
    (or meditates when nobody is playing). */
class YetiScene : public juce::Component, private juce::Timer
{
public:
    explicit YetiScene (YetiVoice& v) : yeti (v)
    {
        for (int i = 0; i < 40; ++i) flakes.push_back ({ rnd.nextFloat(), rnd.nextFloat(), 0.3f + 0.7f * rnd.nextFloat() });
        startTimerHz (30);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        juce::Path clip;
        clip.addRoundedRectangle (r, 10.0f);
        g.reduceClipRegion (clip);
        const float W = r.getWidth(), H = r.getHeight();

        // ---- dusk in the mountains ----
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2b1d4e), 0, 0, juce::Colour (0xfff08a5d), 0, H * 0.75f, false));
        g.fillRect (r);
        g.setColour (juce::Colour (0xfffff2d6).withAlpha (0.85f));
        g.fillEllipse (W * 0.72f, H * 0.08f, W * 0.12f, W * 0.12f);
        auto mountain = [&] (float cx, float peakY, float halfW, juce::Colour c)
        {
            juce::Path m;
            m.addTriangle (cx - halfW, H * 0.78f, cx, peakY, cx + halfW, H * 0.78f);
            g.setColour (c);
            g.fillPath (m);
            juce::Path cap;
            cap.addTriangle (cx - halfW * 0.22f, peakY + (H * 0.78f - peakY) * 0.22f, cx, peakY, cx + halfW * 0.22f, peakY + (H * 0.78f - peakY) * 0.22f);
            g.setColour (juce::Colours::white.withAlpha (0.9f));
            g.fillPath (cap);
        };
        mountain (W * 0.2f, H * 0.28f, W * 0.35f, juce::Colour (0xff4a3a6e));
        mountain (W * 0.8f, H * 0.2f, W * 0.4f, juce::Colour (0xff3d2f5e));
        mountain (W * 0.5f, H * 0.36f, W * 0.3f, juce::Colour (0xff5a4680));
        g.setColour (juce::Colour (0xffe9eef7));
        g.fillRect (0.0f, H * 0.78f, W, H * 0.22f);

        // snow
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        for (auto& f : flakes) g.fillEllipse (f.x * W, f.y * H, 2.5f * f.s, 2.5f * f.s);

        // ---- the yeti ----
        const float unit = juce::jmin (W, H) / 100.0f;
        const float cx = W * 0.5f, feetY = H * 0.9f;
        const float bodyH = 50.0f * unit, bodyW = 46.0f * unit;
        const float bob = bobPos * unit * 3.0f;
        const float breathe = 1.0f + 0.012f * std::sin (time * 1.6f);
        juce::Graphics::ScopedSaveState ss (g);
        g.addTransform (juce::AffineTransform::rotation (sway * 0.035f, cx, feetY));

        const juce::Colour fur (0xfff1f5fb), furShade (0xffc9d5e6), face (0xff8fa6c2), paw (0xffb8c7da);
        // shadow and feet
        g.setColour (juce::Colours::black.withAlpha (0.18f));
        g.fillEllipse (cx - bodyW * 0.55f, feetY - 3.0f * unit, bodyW * 1.1f, 6.0f * unit);
        g.setColour (furShade);
        g.fillEllipse (cx - bodyW * 0.42f, feetY - 7.0f * unit, 16.0f * unit, 8.0f * unit);
        g.fillEllipse (cx + bodyW * 0.42f - 16.0f * unit, feetY - 7.0f * unit, 16.0f * unit, 8.0f * unit);

        // shaggy body: an egg with fur tufts round the edge
        const float bodyTop = feetY - bodyH * breathe - bob - 3.0f * unit;
        auto bodyR = juce::Rectangle<float> (cx - bodyW * 0.5f, bodyTop, bodyW, bodyH * breathe);
        juce::Path bodyP;
        bodyP.addEllipse (bodyR);
        for (int t = 0; t < 26; ++t)
        {
            const float a = juce::MathConstants<float>::twoPi * (float) t / 26.0f;
            const float tx = bodyR.getCentreX() + std::cos (a) * bodyW * 0.48f, ty = bodyR.getCentreY() + std::sin (a) * bodyH * 0.47f * breathe;
            bodyP.addEllipse (tx - 3.2f * unit, ty - 3.2f * unit, 6.4f * unit, 6.4f * unit);
        }
        g.setColour (fur);
        g.fillPath (bodyP);
        g.setColour (furShade.withAlpha (0.5f));
        g.fillEllipse (bodyR.reduced (bodyW * 0.18f, bodyH * 0.2f).translated (0.0f, bodyH * 0.18f));   // belly

        // face patch
        const float faceCy = bodyTop + bodyH * 0.3f;
        auto faceR = juce::Rectangle<float> (cx - 17.0f * unit, faceCy - 11.0f * unit, 34.0f * unit, 25.0f * unit);
        g.setColour (face);
        g.fillRoundedRectangle (faceR, 11.0f * unit);

        // hat: a red knitted beanie with a pom-pom
        const float hatY = bodyTop - 2.0f * unit;
        juce::Path hat;
        hat.addPieSegment (juce::Rectangle<float> (cx - 15.0f * unit, hatY - 8.0f * unit, 30.0f * unit, 22.0f * unit), -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, 0.0f);
        g.setColour (juce::Colour (0xffd63b3b));
        g.fillPath (hat);
        g.setColour (juce::Colour (0xfff7d4d4));
        g.fillRoundedRectangle (cx - 16.0f * unit, hatY + 1.5f * unit, 32.0f * unit, 4.0f * unit, 2.0f * unit);
        g.setColour (juce::Colour (0xffb32b2b));
        for (int s = 0; s < 6; ++s) g.drawLine (cx - 11.0f * unit + s * 4.4f * unit, hatY - 4.0f * unit, cx - 11.0f * unit + s * 4.4f * unit, hatY + 1.5f * unit, 1.0f);
        g.setColour (juce::Colours::white);
        g.fillEllipse (cx - 4.0f * unit, hatY - 13.0f * unit - bob * 0.3f, 8.0f * unit, 8.0f * unit);

        // eyes: follow the pitch, blink, close in bliss on long notes
        const float pitch = yeti.face.pitch.load();
        const float eyeY = faceCy - 3.0f * unit;
        const bool bliss = blissful > 0.5f;
        for (float side : { -1.0f, 1.0f })
        {
            const float ex = cx + side * 7.0f * unit;
            if (bliss || blink > 0.0f)
            {
                g.setColour (juce::Colour (0xff1d2433));
                juce::Path lidLine;
                lidLine.addCentredArc (ex, eyeY, 3.6f * unit, 2.2f * unit, 0.0f, juce::MathConstants<float>::pi * 0.6f, juce::MathConstants<float>::pi * 1.4f, true);
                g.strokePath (lidLine, juce::PathStrokeType (1.6f * unit * 0.5f + 1.0f));
            }
            else
            {
                g.setColour (juce::Colours::white);
                g.fillEllipse (ex - 3.8f * unit, eyeY - 4.2f * unit, 7.6f * unit, 8.4f * unit);
                g.setColour (juce::Colour (0xff1d2433));
                const float py = eyeY + (0.5f - pitch) * 3.0f * unit;
                g.fillEllipse (ex - 2.1f * unit, py - 2.3f * unit, 4.2f * unit, 4.6f * unit);
                g.setColour (juce::Colours::white);
                g.fillEllipse (ex - 0.6f * unit, py - 1.8f * unit, 1.4f * unit, 1.4f * unit);
            }
            // bushy brows rise with the pitch
            const float browY = eyeY - 6.5f * unit - pitch * 2.5f * unit - level * 1.0f * unit;
            juce::Path brow;
            brow.startNewSubPath (ex - 4.5f * unit, browY + side * 0.0f + 1.0f * unit);
            brow.quadraticTo (ex, browY - 1.5f * unit, ex + 4.5f * unit, browY + 1.0f * unit);
            g.setColour (fur);
            g.strokePath (brow, juce::PathStrokeType (2.6f * unit, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // mouth: the vowel shapes it, the loudness opens it, "mmm" closes it
        const float vowel = shownVowel;   // 0 U, .25 O, .5 A, .75 E, 1 I
        const float open = juce::jlimit (0.0f, 1.0f, shownMouth);
        const float wide = juce::jmap (vowel, 0.0f, 1.0f, 0.45f, 1.25f);
        const float tallByVowel = vowel < 0.5f ? juce::jmap (vowel, 0.0f, 0.5f, 0.7f, 1.0f) : juce::jmap (vowel, 0.5f, 1.0f, 1.0f, 0.35f);
        const float mw = 9.0f * unit * wide, mh = 9.5f * unit * tallByVowel * open;
        const float my = faceCy + 6.5f * unit;
        if (mh < 0.8f * unit)
        {
            juce::Path closed;
            closed.startNewSubPath (cx - mw * 0.5f, my);
            closed.quadraticTo (cx, my + 1.5f * unit, cx + mw * 0.5f, my);
            g.setColour (juce::Colour (0xff3a1f2c));
            g.strokePath (closed, juce::PathStrokeType (1.4f * unit * 0.6f + 1.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        else
        {
            auto m = juce::Rectangle<float> (cx - mw * 0.5f, my - mh * 0.45f, mw, mh);
            g.setColour (juce::Colour (0xff4a1c2c));
            g.fillEllipse (m);
            g.setColour (juce::Colour (0xffe06a8a));
            g.fillEllipse (m.withTrimmedTop (mh * 0.55f).reduced (mw * 0.18f, 0.0f));   // tongue
            g.setColour (juce::Colours::white);
            for (float side : { -1.0f, 1.0f })                                          // two little fangs
            {
                juce::Path fang;
                const float fx = cx + side * mw * 0.22f;
                fang.addTriangle (fx - 1.3f * unit, m.getY() + 0.6f * unit, fx + 1.3f * unit, m.getY() + 0.6f * unit, fx, m.getY() + 3.0f * unit);
                g.fillPath (fang);
            }
        }

        // arms: hanging and swinging, thrown up on a new note, palms together on long notes and when meditating
        const float pray = prayPos, up = armsUp;
        for (float side : { -1.0f, 1.0f })
        {
            const juce::Point<float> shoulder (cx + side * bodyW * 0.38f, bodyTop + bodyH * 0.48f);
            const juce::Point<float> handDown (cx + side * bodyW * 0.55f, bodyTop + bodyH * 0.82f + std::sin (time * 2.0f + side) * unit);
            const juce::Point<float> handPray (cx + side * 2.2f * unit, bodyTop + bodyH * 0.6f);
            const juce::Point<float> handUp (cx + side * bodyW * 0.62f, bodyTop + bodyH * 0.18f);
            auto hand = handDown + (handPray - handDown) * pray;
            hand = hand + (handUp - hand) * up;
            // the elbow hangs below the arm (outwards when the hands go up)
            const juce::Point<float> elbow ((shoulder.x + hand.x) * 0.5f + side * (4.0f + 5.0f * up) * unit,
                                            juce::jmax (shoulder.y, hand.y) + (8.0f - 10.0f * up) * unit);
            juce::Path arm;
            arm.startNewSubPath (shoulder);
            arm.quadraticTo (elbow, hand);
            g.setColour (furShade.darker (0.2f));
            g.strokePath (arm, juce::PathStrokeType (9.5f * unit, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (fur);
            g.strokePath (arm, juce::PathStrokeType (7.5f * unit, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            // paw: upright when praying, round otherwise
            g.setColour (paw);
            const float pw = juce::jmap (pray, 6.0f, 4.0f) * unit, ph = juce::jmap (pray, 6.0f, 8.5f) * unit;
            g.fillRoundedRectangle (hand.x - pw * 0.5f, hand.y - ph * 0.6f, pw, ph, pw * 0.45f);
            g.setColour (furShade.darker (0.25f));
            for (int f = 1; f < 3; ++f)
                g.drawLine (hand.x - pw * 0.5f + pw * f / 3.0f, hand.y - ph * 0.55f, hand.x - pw * 0.5f + pw * f / 3.0f, hand.y - ph * 0.25f, 1.0f);
        }

        // ---- singing: note rings floating out of his mouth, echoes fading as they drift ----
        for (auto& ring : rings)
        {
            const float a = juce::jlimit (0.0f, 1.0f, ring.life);
            g.setColour (juce::Colour (0xfffff3b0).withAlpha (a * 0.8f));
            const float rad = (1.0f - ring.life) * 22.0f * unit + 3.0f * unit;
            g.drawEllipse (cx + ring.dx * (1.0f - ring.life) * 40.0f * unit - rad, my - (1.0f - ring.life) * 30.0f * unit - rad * 0.6f, rad * 2.0f, rad * 1.2f, 1.5f);
            g.setFont (uiFont (9.0f * unit * 0.5f + 8.0f, true));
            g.drawText (ring.syllable, juce::Rectangle<float> (cx + ring.dx * (1.0f - ring.life) * 50.0f * unit - 20.0f, my - (1.0f - ring.life) * 34.0f * unit - 18.0f, 40.0f, 14.0f), juce::Justification::centred);
        }
    }

    void mouseDown (const juce::MouseEvent&) override { blink = 0.2f; armsUp = 1.0f; }   // poke him

private:
    struct Flake { float x, y, s; };
    struct Ring { float life = 1.0f, dx = 0.0f; juce::String syllable; };

    void timerCallback() override
    {
        const float dt = 1.0f / 30.0f;
        time += dt;
        auto& f = yeti.face;
        level += 0.3f * (f.level.load() - level);
        shownVowel += 0.35f * (f.vowel.load() - shownVowel);
        shownMouth += 0.45f * (f.mouth.load() * juce::jlimit (0.0f, 1.0f, level * 1.4f) - shownMouth);
        sway += 0.25f * (f.vibrato.load() * level - sway);

        // a bounce on every new note
        const int notes = f.notes.load();
        if (notes != lastNotes)
        {
            lastNotes = notes;
            bobVel += 1.6f;
            armsUp = juce::jmax (armsUp, 0.55f);
            static const char* syllables[] = { "oo", "oh", "ah", "eh", "ee" };
            rings.push_back ({ 1.0f, rnd.nextFloat() * 0.8f - 0.2f, syllables[juce::jlimit (0, 4, juce::roundToInt (f.vowel.load() * 4.0f))] });
        }
        bobVel += (-bobPos * 40.0f - bobVel * 6.0f) * dt;
        bobPos += bobVel * dt;
        armsUp = juce::jmax (0.0f, armsUp - dt * 1.8f);

        // echo rings fade more slowly when the delay is loud
        const float echo = f.echo.load();
        for (auto& ring : rings) ring.life -= dt * (0.9f - 0.6f * echo);
        rings.erase (std::remove_if (rings.begin(), rings.end(), [] (const Ring& rr) { return rr.life <= 0.0f; }), rings.end());
        if (rings.size() > 24) rings.erase (rings.begin());

        // long notes: eyes close and hands come together; idle for a while: he meditates
        const float held = f.heldSeconds.load();
        const bool singing = f.sounding.load() > 0 && level > 0.05f;
        idle = singing ? 0.0f : idle + dt;
        const bool prayTarget = (singing && held > 0.9f) || idle > 3.5f;
        prayPos += (prayTarget ? 1.2f : 2.5f) * dt * ((prayTarget ? 1.0f : 0.0f) - prayPos);
        blissful = (singing && held > 1.6f) || idle > 4.5f ? 1.0f : 0.0f;

        // blinking
        if (blink > 0.0f) blink -= dt;
        else if (rnd.nextFloat() < dt * 0.35f) blink = 0.14f;

        for (auto& fl : flakes)
        {
            fl.y += dt * 0.04f * fl.s;
            fl.x += dt * 0.01f * std::sin (time + fl.s * 10.0f);
            if (fl.y > 0.8f) { fl.y = 0.0f; fl.x = rnd.nextFloat(); }
        }
        repaint();
    }

    YetiVoice& yeti;
    juce::Random rnd;
    std::vector<Flake> flakes;
    std::vector<Ring> rings;
    float time = 0.0f, level = 0.0f, shownVowel = 0.5f, shownMouth = 0.0f, sway = 0.0f;
    float bobPos = 0.0f, bobVel = 0.0f, armsUp = 0.0f, prayPos = 1.0f, blink = 0.0f, blissful = 1.0f, idle = 10.0f;
    int lastNotes = 0;
};

/** Vowel (left to right) and pitch (up and down): click and drag to make him sing without a keyboard. */
class VowelPad : public juce::Component, private juce::Timer
{
public:
    explicit VowelPad (YetiVoice& v) : yeti (v) { startTimerHz (30); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (theme::bg);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (theme::outline);
        g.drawRoundedRectangle (r, 8.0f, 1.0f);
        g.setFont (uiFont (13.0f, true));
        const char* vowels[] = { "U", "O", "A", "E", "I" };
        for (int i = 0; i < 5; ++i)
        {
            const float x = r.getX() + r.getWidth() * (0.04f + 0.92f * i / 4.0f);
            g.setColour (theme::outline.withAlpha (0.6f));
            g.drawVerticalLine ((int) x, r.getY() + 4.0f, r.getBottom() - 4.0f);
            g.setColour (theme::textDim);
            g.drawText (vowels[i], juce::Rectangle<float> (x - 10.0f, r.getBottom() - 20.0f, 20.0f, 16.0f), juce::Justification::centred);
        }
        g.setColour (theme::textFaint);
        g.setFont (uiFont (11.0f));
        g.drawText ("click and drag to sing   (vowel  <->,  pitch  ^)", r.reduced (10.0f, 6.0f), juce::Justification::topLeft);
        const float vx = yeti.padDown.load() ? yeti.padX.load() : yeti.face.vowel.load();
        const float vy = yeti.padDown.load() ? yeti.padY.load() : yeti.face.pitch.load();
        const auto dot = juce::Point<float> (r.getX() + r.getWidth() * (0.04f + 0.92f * vx), r.getBottom() - r.getHeight() * (0.1f + 0.8f * vy));
        g.setColour (theme::accent2.withAlpha (0.25f + 0.5f * yeti.face.level.load()));
        g.fillEllipse (juce::Rectangle<float> (28.0f, 28.0f).withCentre (dot));
        g.setColour (theme::accent2.brighter (0.3f));
        g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre (dot));
    }

    void mouseDown (const juce::MouseEvent& e) override { set (e); yeti.padDown = true; }
    void mouseDrag (const juce::MouseEvent& e) override { set (e); }
    void mouseUp (const juce::MouseEvent&) override { yeti.padDown = false; }

private:
    void set (const juce::MouseEvent& e)
    {
        auto r = getLocalBounds().toFloat();
        const float x = juce::jlimit (0.0f, 1.0f, ((e.position.x - r.getX()) / r.getWidth() - 0.04f) / 0.92f);
        const float y = juce::jlimit (0.0f, 1.0f, ((r.getBottom() - e.position.y) / r.getHeight() - 0.1f) / 0.8f);
        yeti.padX = x;
        yeti.padY = y;
        if ((int) yeti.param ("vowelMode") == 0) yeti.setParam ("vowel", x);
    }
    void timerCallback() override { repaint(); }
    YetiVoice& yeti;
};

class YetiEditor : public juce::AudioProcessorEditor
{
public:
    explicit YetiEditor (YetiVoice& y)
        : AudioProcessorEditor (y), header (y, "Yodel Yeti"), scene (y), pad (y),
          voice (y, "VOICE", { "voice", "size", "tension", "breath", "mmm", "throat", "overtone", "growl" }, juce::Colour (0xff60a5fa)),
          singing (y, "SINGING", { "mono", "glide", "vibDepth", "vibRate", "vibDelay", "choir", "choirDetune", "attack", "release" }, theme::accent),
          vowels (y, "VOWELS", { "vowelMode", "vowel", "vowelRate", "vowelDepth" }, juce::Colour (0xfffbbf24)),
          echo (y, "ECHO & MOUNTAINS", { "delayTime", "delayFeedback", "delayMix", "delayTone", "pingpong", "reverb", "volume" }, juce::Colour (0xff2dd4bf))
    {
        for (auto* c : std::initializer_list<juce::Component*> { &header, &scene, &pad, &voice, &singing, &vowels, &echo })
            addAndMakeVisible (c);
        setResizable (true, false);
        setResizeLimits (1100, 760, 1900, 1400);
        setSize (1320, 860);
    }

    void paint (juce::Graphics& g) override { paintEditorBackground (g, getLocalBounds(), juce::Colour (0xff60a5fa)); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 8);
        header.setBounds (r.removeFromTop (34));
        r.removeFromTop (8);
        scene.setBounds (r.removeFromLeft (juce::jmin (400, r.getWidth() / 3)));
        r.removeFromLeft (10);
        pad.setBounds (r.removeFromTop (juce::jlimit (110, 170, r.getHeight() / 5)));
        r.removeFromTop (8);
        auto left = r.removeFromLeft (r.getWidth() / 2 - 4);
        r.removeFromLeft (8);
        voice.setBounds (left.removeFromTop (voice.heightFor (left.getWidth())));
        left.removeFromTop (8);
        vowels.setBounds (left.removeFromTop (juce::jmin (left.getHeight(), vowels.heightFor (left.getWidth()))));
        singing.setBounds (r.removeFromTop (singing.heightFor (r.getWidth())));
        r.removeFromTop (8);
        echo.setBounds (r.removeFromTop (juce::jmin (r.getHeight(), echo.heightFor (r.getWidth()))));
    }

private:
    EditorHeader header;
    YetiScene scene;
    VowelPad pad;
    Section voice, singing, vowels, echo;
};

void installVoiceEditors()
{
    PianoRoom::editorFactory = [] (PianoRoom& p) -> juce::AudioProcessorEditor* { return new PianoRoomEditor (p); };
    YetiVoice::editorFactory = [] (YetiVoice& y) -> juce::AudioProcessorEditor* { return new YetiEditor (y); };
}

} // namespace wis::daw
