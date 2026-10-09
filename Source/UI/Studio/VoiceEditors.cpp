#include "EditorParts.h"
#include "Daw/Instruments/PianoRoom.h"
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

void installVoiceEditors()
{
    PianoRoom::editorFactory = [] (PianoRoom& p) -> juce::AudioProcessorEditor* { return new PianoRoomEditor (p); };
}

} // namespace wis::daw
