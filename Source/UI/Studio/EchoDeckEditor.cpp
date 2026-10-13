#include "EditorParts.h"
#include "Daw/Plugins/EchoDeck.h"

namespace wis::daw
{

/*  Echo Deck's front panel: an enamelled studio rack unit. Everything is drawn in a fixed 1000 x 560 design
    space and scaled to the editor, so it looks the same docked, floating or full screen. Static parts (enamel,
    bays, engraving, screws) are cached in an image; the displays, reels, VU needle and rhythm grid are live. */

namespace
{
    namespace ec
    {
        const juce::Colour enamelTop    { 0xff24494e };
        const juce::Colour enamelBottom { 0xff13292c };
        const juce::Colour bay          { 0xff10221f };
        const juce::Colour bayEdge      { 0xff0a1716 };
        const juce::Colour cream        { 0xffede3cc };
        const juce::Colour creamDim     { 0xffa9a28f };
        const juce::Colour brass        { 0xffc8a15a };
        const juce::Colour amber        { 0xffffb13b };
        const juce::Colour amberDim     { 0xff4a3010 };
        const juce::Colour glass        { 0xff120a05 };
        const juce::Colour ledRed       { 0xffff4433 };
        const juce::Colour tape         { 0xff3a2a1e };
    }

    constexpr float designW = 1000.0f, designH = 560.0f;
    constexpr int headerH = 36;

    juce::Font engraved (float h, bool bold = true) { return uiFont (h, bold).withExtraKerningFactor (0.12f); }

    juce::Rectangle<float> rf (float x, float y, float w, float h) { return { x, y, w, h }; }

    // ---- knobs ---------------------------------------------------------------------------------------------
    class FaceLnF : public juce::LookAndFeel_V4
    {
    public:
        FaceLnF()
        {
            setColour (juce::BubbleComponent::backgroundColourId, ec::glass);
            setColour (juce::BubbleComponent::outlineColourId, ec::amber.withAlpha (0.6f));
            setColour (juce::TooltipWindow::textColourId, ec::amber);
        }

        void drawBubble (juce::Graphics& g, juce::BubbleComponent&, const juce::Point<float>&, const juce::Rectangle<float>& body) override
        {
            g.setColour (ec::glass);
            g.fillRoundedRectangle (body, 4.0f);
            g.setColour (ec::amber.withAlpha (0.6f));
            g.drawRoundedRectangle (body.reduced (0.5f), 4.0f, 1.0f);
        }
        juce::Font getSliderPopupFont (juce::Slider&) override { return uiFont (13.0f, true); }
        int getSliderPopupPlacement (juce::Slider&) override { return juce::BubbleComponent::above; }

        void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s) override
        {
            const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
            const float rad = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f - 1.0f;
            const auto c = bounds.getCentre();
            const bool big = (bool) s.getProperties()["big"];
            const float angle = start + pos * (end - start);

            // scale ticks
            g.setColour (ec::cream.withAlpha (0.6f));
            for (int i = 0; i <= 10; ++i)
            {
                const float a = start + (end - start) * (float) i / 10.0f;
                const float r0 = rad * ((i == 0 || i == 5 || i == 10) ? 0.83f : 0.88f);
                g.drawLine (c.x + std::sin (a) * r0, c.y - std::cos (a) * r0, c.x + std::sin (a) * rad, c.y - std::cos (a) * rad,
                            juce::jmax (1.0f, rad * 0.035f));
            }

            // knurled skirt
            const float skirt = rad * 0.76f;
            juce::ColourGradient sg (juce::Colour (0xff3a3a3a), c.x - skirt * 0.5f, c.y - skirt * 0.6f, juce::Colour (0xff070707), c.x + skirt * 0.6f, c.y + skirt, true);
            g.setGradientFill (sg);
            g.fillEllipse (juce::Rectangle<float> (skirt * 2.0f, skirt * 2.0f).withCentre (c));
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            const int teeth = big ? 60 : 36;
            for (int i = 0; i < teeth; ++i)
            {
                const float a = juce::MathConstants<float>::twoPi * (float) i / (float) teeth + angle;
                g.drawLine (c.x + std::sin (a) * skirt * 0.9f, c.y - std::cos (a) * skirt * 0.9f, c.x + std::sin (a) * skirt, c.y - std::cos (a) * skirt, 1.0f);
            }
            // brass index on the skirt
            g.setColour (ec::brass);
            g.fillEllipse (juce::Rectangle<float> (rad * 0.09f, rad * 0.09f).withCentre ({ c.x + std::sin (angle) * skirt * 0.86f, c.y - std::cos (angle) * skirt * 0.86f }));

            if (big)
            {
                // black bakelite cap with an ivory pointer
                const float cr = skirt * 0.72f;
                juce::ColourGradient cg (juce::Colour (0xff35302b), c.x - cr * 0.4f, c.y - cr * 0.6f, juce::Colour (0xff0a0908), c.x + cr * 0.5f, c.y + cr, true);
                g.setGradientFill (cg);
                g.fillEllipse (juce::Rectangle<float> (cr * 2.0f, cr * 2.0f).withCentre (c));
                g.setColour (juce::Colours::white.withAlpha (0.08f));
                g.drawEllipse (juce::Rectangle<float> (cr * 2.0f, cr * 2.0f).withCentre (c).reduced (0.5f), 1.0f);
                juce::Path pointer;
                const float base = cr * 0.2f, tip = cr * 0.07f;
                pointer.startNewSubPath (-base, cr * 0.22f);
                pointer.lineTo (-tip, -skirt * 0.97f);
                pointer.quadraticTo (0.0f, -skirt * 1.0f, tip, -skirt * 0.97f);
                pointer.lineTo (base, cr * 0.22f);
                pointer.quadraticTo (0.0f, cr * 0.3f, -base, cr * 0.22f);
                pointer.closeSubPath();
                const auto tf = juce::AffineTransform::rotation (angle).translated (c);
                g.setColour (juce::Colours::black.withAlpha (0.55f));
                g.fillPath (pointer, tf.translated (rad * 0.02f, rad * 0.04f));
                juce::ColourGradient pg (juce::Colour (0xfff6eedb), c.x - cr, c.y - cr, juce::Colour (0xffbfb294), c.x + cr, c.y + cr, false);
                g.setGradientFill (pg);
                g.fillPath (pointer, tf);
                g.setColour (juce::Colours::black.withAlpha (0.3f));
                g.strokePath (pointer, juce::PathStrokeType (0.8f), tf);
                g.setColour (ec::brass.darker (0.2f));
                g.fillEllipse (juce::Rectangle<float> (cr * 0.22f, cr * 0.22f).withCentre (c));
            }
            else
            {
                const float cr = skirt * 0.74f;
                juce::ColourGradient cg (juce::Colour (0xff35302b), c.x - cr * 0.4f, c.y - cr * 0.6f, juce::Colour (0xff0a0908), c.x + cr * 0.5f, c.y + cr, true);
                g.setGradientFill (cg);
                g.fillEllipse (juce::Rectangle<float> (cr * 2.0f, cr * 2.0f).withCentre (c));
                g.setColour (juce::Colours::white.withAlpha (0.08f));
                g.drawEllipse (juce::Rectangle<float> (cr * 2.0f, cr * 2.0f).withCentre (c).reduced (0.5f), 1.0f);
                g.setColour (ec::cream);
                g.drawLine (c.x + std::sin (angle) * cr * 0.25f, c.y - std::cos (angle) * cr * 0.25f,
                            c.x + std::sin (angle) * cr * 0.95f, c.y - std::cos (angle) * cr * 0.95f, juce::jmax (1.5f, rad * 0.06f));
            }
        }
    };

    // ---- illuminated keys -------------------------------------------------------------------------------------
    class Key : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        Key (juce::String t, bool isRound = false) : text (std::move (t)), round (isRound) { setRepaintsOnMouseActivity (true); }

        std::function<bool()> isOn;
        std::function<void()> onClick;
        juce::Colour lit = ec::amber;

        void refresh()
        {
            const bool on = isOn && isOn();
            if (on != lastOn) { lastOn = on; repaint(); }
        }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced (1.0f);
            const bool on = isOn && isOn();
            lastOn = on;
            const bool down = isMouseButtonDown();
            if (round)
            {
                const float d = juce::jmin (r.getWidth(), r.getHeight());
                auto c = juce::Rectangle<float> (d, d).withCentre (r.getCentre());
                g.setColour (juce::Colours::black.withAlpha (0.6f));
                g.fillEllipse (c.translated (0.0f, 2.0f));
                juce::ColourGradient bg (juce::Colour (0xffd9d0bb), c.getX(), c.getY(), juce::Colour (0xff8f8673), c.getRight(), c.getBottom(), false);
                g.setGradientFill (bg);
                g.fillEllipse (c);
                auto cap = c.reduced (d * (down ? 0.13f : 0.11f));
                juce::ColourGradient cg (juce::Colour (0xff2a2622), cap.getX(), cap.getY(), juce::Colour (0xff0c0a09), cap.getRight(), cap.getBottom(), false);
                g.setGradientFill (cg);
                g.fillEllipse (cap);
                auto led = juce::Rectangle<float> (d * 0.16f, d * 0.16f).withCentre ({ cap.getCentreX(), cap.getY() + cap.getHeight() * 0.3f });
                if (on)
                {
                    g.setColour (lit.withAlpha (0.35f));
                    g.fillEllipse (led.expanded (d * 0.08f));
                }
                g.setColour (on ? lit : lit.darker (2.2f));
                g.fillEllipse (led);
                g.setColour (ec::cream);
                g.setFont (engraved (d * 0.17f));
                g.drawText (text, cap.withTrimmedTop (cap.getHeight() * 0.45f), juce::Justification::centredTop);
                return;
            }
            const float cr = juce::jmin (5.0f, r.getHeight() * 0.25f);
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.fillRoundedRectangle (r.translated (0.0f, down ? 0.5f : 1.5f), cr);
            if (down) r = r.translated (0.0f, 1.0f);
            juce::ColourGradient bg (juce::Colour (on ? 0xff2d2418 : 0xff23211e), r.getX(), r.getY(), juce::Colour (0xff0c0b0a), r.getX(), r.getBottom(), false);
            g.setGradientFill (bg);
            g.fillRoundedRectangle (r, cr);
            g.setColour (juce::Colours::white.withAlpha (isMouseOver() ? 0.14f : 0.07f));
            g.drawRoundedRectangle (r.reduced (0.5f), cr, 1.0f);
            // LED strip
            auto led = r.withHeight (juce::jmax (2.0f, r.getHeight() * 0.12f)).reduced (r.getWidth() * 0.3f, 0.0f).translated (0.0f, r.getHeight() * 0.14f);
            if (on)
            {
                g.setColour (lit.withAlpha (0.3f));
                g.fillRoundedRectangle (led.expanded (3.0f, 2.0f), 2.0f);
            }
            g.setColour (on ? lit : lit.darker (2.4f));
            g.fillRoundedRectangle (led, 1.0f);
            g.setColour (on ? ec::cream : ec::creamDim);
            g.setFont (engraved (juce::jlimit (7.0f, 15.0f, r.getHeight() * 0.36f)));
            g.drawFittedText (text, r.withTrimmedTop (r.getHeight() * 0.3f).toNearestInt(), juce::Justification::centred, 1, 0.7f);
        }

        void mouseDown (const juce::MouseEvent&) override { repaint(); if (onClick) onClick(); }
        void mouseUp (const juce::MouseEvent&) override { repaint(); }

    private:
        juce::String text;
        bool round = false, lastOn = false;
    };

    struct KnobSpec
    {
        const char* id;
        const char* label;
        float cx, cy, r;
        const char* lo;
        const char* hi;
        bool big = false;
    };

    const KnobSpec knobSpecs[] = {
        { "mix",        "MIX",        80.0f, 384.0f, 30.0f, "DRY", "WET" },
        { "feedback",   "FEEDBACK",  175.0f, 382.0f, 34.0f, "0", "MAX" },
        { "ratio",      "DUAL",      276.0f, 384.0f, 30.0f, "1/4", "x2" },
        { "lowCut",     "LOW CUT",   755.0f, 268.0f, 38.0f, "20", "2k" },
        { "highCut",    "HIGH CUT",  895.0f, 268.0f, 38.0f, "800", "20k" },
        { "inputDb",    "INPUT",     722.0f, 384.0f, 30.0f, "-24", "+24" },
        { "saturation", "SATURATION",825.0f, 382.0f, 34.0f, "MIN", "MAX" },
        { "outputDb",   "OUTPUT",    928.0f, 384.0f, 30.0f, "-24", "+24" },
        { "groove",     "GROOVE",     66.0f, 502.0f, 24.0f, "PUSH", "SWING" },
        { "feel",       "FEEL",      146.0f, 502.0f, 24.0f, "EARLY", "LATE" },
        { "wobble",     "WOBBLE",    226.0f, 502.0f, 24.0f, "0", "x2" },
        { "diffusion",  "DIFFUSION", 306.0f, 502.0f, 24.0f, "0", "MAX" },
        { "width",      "WIDTH",     386.0f, 502.0f, 24.0f, "MONO", "WIDE" },
        { "duck",       "DUCK",      466.0f, 502.0f, 24.0f, "OFF", "MAX" },
    };

    // design-space areas
    const auto timeDisplay  = rf (36, 96, 278, 62);
    const auto timeKnobArea = rf (117, 198, 116, 116);
    const auto tapeWindow   = rf (360, 94, 280, 124);
    const auto styleDisplay = rf (398, 228, 204, 36);
    const auto blurbArea    = rf (360, 268, 280, 36);
    const auto bpmDisplay   = rf (440, 356, 120, 44);
    const auto vuArea       = rf (690, 96, 270, 104);
    const auto rhythmTray   = rf (605, 432, 375, 114);
    const auto rhythmGrid   = rf (620, 462, 345, 76);
    const auto stepsDisplay = rf (880, 437, 40, 18);
    const auto ratioTray    = rf (241, 330, 70, 90);

    const char* const styleBadges[] = { "15 IPS", "TAPE LOOP", "VALVE", "7.5 IPS", "CASSETTE", "BBD 4096", "OIL CAN", "12-BIT", "32-BIT", "8 kHz" };

    class EchoDeckEditor : public juce::AudioProcessorEditor, private juce::Timer
    {
    public:
        explicit EchoDeckEditor (EchoDeck& e) : AudioProcessorEditor (e), deck (e), header (e, "Echo Deck")
        {
            addAndMakeVisible (header);
            header.onPresetChanged = [this] { refreshAll(); };

            for (auto& spec : knobSpecs)
            {
                auto* s = knobs.add (new juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox));
                setupKnob (*s, spec.big);
                attachments.add (new juce::AudioProcessorValueTreeState::SliderAttachment (deck.state, spec.id, *s));
                setDefault (*s, spec.id);
            }
            setupKnob (timeKnob, true);
            attachTimeKnob();
            timeKnob.setTooltip ("Echo time: milliseconds, or a note value locked to the tempo (MS / NOTE / DOT / TRIP)");

            const char* timeModes[] = { "MS", "NOTE", "DOT", "TRIP" };
            for (int i = 0; i < 4; ++i)
                addKey (timeModes[i], [this, i] { return (int) deck.param ("timeMode") == i; }, [this, i] { deck.setParam ("timeMode", (float) i); });
            const char* modes[] = { "SINGLE", "DUAL", "PING-PONG", "RHYTHM" };
            const char* modeTips[] = { "One echo per side (true stereo)", "Two echo times: left at the time, right at the time x the DUAL knob",
                                       "Echoes bounce left, right, left...", "An 8-step pattern of echoes (set it in RHYTHM ECHO), repeating with the feedback" };
            for (int i = 0; i < 4; ++i)
                addKey (modes[i], [this, i] { return (int) deck.param ("mode") == i; }, [this, i] { deck.setParam ("mode", (float) i); })->setTooltip (modeTips[i]);
            prevStyle = addKey ("<", {}, [this] { stepStyle (-1); });
            nextStyle = addKey (">", {}, [this] { stepStyle (1); });
            tapKey = keys.add (new Key ("TAP", true));
            tapKey->lit = ec::ledRed;
            tapKey->isOn = [this] { return blink(); };
            tapKey->onClick = [this] { deck.tap(); };
            tapKey->setTooltip ("Tap a few times: in MS mode it sets the echo time, otherwise the tempo the notes follow");
            addAndMakeVisible (tapKey);
            addKey ("SONG", [this] { return (int) deck.param ("tempoSrc") == 0; }, [this] { deck.setParam ("tempoSrc", 0.0f); })
                ->setTooltip ("Notes follow the song's tempo (120 BPM in Play Along)");
            addKey ("TAP", [this] { return (int) deck.param ("tempoSrc") == 1; }, [this] { deck.setParam ("tempoSrc", 1.0f); })
                ->setTooltip ("Notes follow the tapped tempo");
            holdKey = addKey ("HOLD", [this] { return deck.param ("hold") > 0.5f; }, [this] { deck.setParam ("hold", deck.param ("hold") > 0.5f ? 0.0f : 1.0f); });
            holdKey->lit = ec::ledRed;
            holdKey->setTooltip ("Freezes what's in the echo: it loops forever, new playing goes through dry");
            stepsDown = addKey ("-", {}, [this] { deck.setParam ("rhythmLen", juce::jmax (0.0f, deck.param ("rhythmLen") - 1.0f)); });
            stepsUp = addKey ("+", {}, [this] { deck.setParam ("rhythmLen", juce::jmin (7.0f, deck.param ("rhythmLen") + 1.0f)); });

            setResizable (true, false);
            setResizeLimits (640, headerH + 358, 1600, headerH + 896);
            setSize ((int) designW, headerH + (int) designH);
            startTimerHz (30);
        }

        ~EchoDeckEditor() override
        {
            stopTimer();
            for (auto* k : knobs) k->setLookAndFeel (nullptr);
            timeKnob.setLookAndFeel (nullptr);
            timeAttach.reset();
        }

        void resized() override
        {
            header.setBounds (getLocalBounds().removeFromTop (headerH).reduced (10, 0));
            auto area = getLocalBounds().withTrimmedTop (headerH).toFloat();
            scale = juce::jmax (0.05f, juce::jmin (area.getWidth() / designW, area.getHeight() / designH));
            const float ox = area.getX() + (area.getWidth() - designW * scale) * 0.5f;
            const float oy = area.getY() + (area.getHeight() - designH * scale) * 0.5f;
            toScreen = juce::AffineTransform::scale (scale).translated (ox, oy);

            for (int i = 0; i < knobs.size(); ++i)
            {
                const auto& sp = knobSpecs[i];
                knobs[i]->setBounds (screen (rf (sp.cx - sp.r, sp.cy - sp.r, sp.r * 2.0f, sp.r * 2.0f)));
            }
            timeKnob.setBounds (screen (timeKnobArea));
            for (int i = 0; i < 4; ++i) keys[i]->setBounds (screen (rf (36.0f + (float) i * 71.3f, 164.0f, 64.0f, 24.0f)));
            for (int i = 0; i < 4; ++i) keys[4 + i]->setBounds (screen (rf (360.0f + (float) i * 71.5f, 310.0f, 66.0f, 26.0f)));
            prevStyle->setBounds (screen (rf (360, 228, 34, 36)));
            nextStyle->setBounds (screen (rf (606, 228, 34, 36)));
            tapKey->setBounds (screen (rf (370, 348, 60, 60)));
            keys[11]->setBounds (screen (rf (572, 354, 68, 21)));
            keys[12]->setBounds (screen (rf (572, 380, 68, 21)));
            holdKey->setBounds (screen (rf (512, 480, 64, 40)));
            stepsDown->setBounds (screen (rf (854, 436, 22, 20)));
            stepsUp->setBounds (screen (rf (924, 436, 22, 20)));
            background = {};
            refreshAll();
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (theme::bg);
            if (background.isNull() || background.getWidth() != getWidth() || background.getHeight() != getHeight())
                renderBackground();
            g.drawImageAt (background, 0, 0);

            juce::Graphics::ScopedSaveState ss (g);
            g.addTransform (toScreen);
            paintTimeDisplay (g);
            paintTape (g);
            paintStyle (g);
            paintBpm (g);
            paintVu (g);
            paintRhythm (g);
            if ((int) deck.param ("mode") != EchoDeck::dual)
            {
                g.setColour (ec::bay.withAlpha (0.55f));
                g.fillRect (ratioTray.withTrimmedTop (2.0f));
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            const auto d = design (e.position);
            if (vuArea.contains (d)) { meterShowsEcho = ! meterShowsEcho; repaint (screen (vuArea)); return; }
            if (styleDisplay.contains (d)) { styleMenu(); return; }
            if (rhythmGrid.contains (d)) { dragTap = -1; setTapFrom (d); }
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            const auto d = design (e.position);
            if (rhythmGrid.expanded (0.0f, 40.0f).contains (d) || dragTap >= 0) setTapFrom (d);
        }
        void mouseUp (const juce::MouseEvent&) override { dragTap = -1; }
        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            const auto d = design (e.position);
            if (rhythmGrid.contains (d))
            {
                const int col = columnAt (d);
                deck.setParam ("tap" + juce::String (col + 1), deck.param (("tap" + juce::String (col + 1)).toRawUTF8()) > 0.01f ? 0.0f : 1.0f);
                repaint (screen (rhythmTray));
            }
        }

    private:
        EchoDeck& deck;
        FaceLnF lnf;
        EditorHeader header;
        juce::OwnedArray<juce::Slider> knobs;
        juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> attachments;
        juce::Slider timeKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> timeAttach;
        int timeAttachedTo = -1;
        juce::OwnedArray<Key> keys;
        Key* prevStyle = nullptr; Key* nextStyle = nullptr; Key* tapKey = nullptr; Key* holdKey = nullptr;
        Key* stepsDown = nullptr; Key* stepsUp = nullptr;
        juce::AffineTransform toScreen;
        float scale = 1.0f;
        juce::Image background;
        float needle = -20.0f;
        bool meterShowsEcho = false;
        int dragTap = -1;
        juce::String lastKey;

        juce::Rectangle<int> screen (juce::Rectangle<float> r) const { return r.transformedBy (toScreen).getSmallestIntegerContainer(); }
        juce::Point<float> design (juce::Point<float> p) const { return p.transformedBy (toScreen.inverted()); }

        void setupKnob (juce::Slider& s, bool big)
        {
            s.setLookAndFeel (&lnf);
            s.getProperties().set ("big", big);
            s.setRotaryParameters (juce::MathConstants<float>::pi * 1.22f, juce::MathConstants<float>::pi * 2.78f, true);
            s.setPopupDisplayEnabled (true, true, this);
            s.setMouseDragSensitivity (220);
            addAndMakeVisible (s);
        }

        void setDefault (juce::Slider& s, const juce::String& id)
        {
            if (auto* p = deck.state.getParameter (id))
                s.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
        }

        Key* addKey (const juce::String& text, std::function<bool()> on, std::function<void()> click)
        {
            auto* k = keys.add (new Key (text));
            k->isOn = std::move (on);
            k->onClick = [this, click] { click(); refreshAll(); };
            addAndMakeVisible (k);
            return k;
        }

        bool blink() const
        {
            const double period = juce::jmax (40.0, (double) deck.getEchoMs());
            return std::fmod (juce::Time::getMillisecondCounterHiRes(), period) < juce::jmin (90.0, period * 0.4);
        }

        void stepStyle (int dir)
        {
            const int n = EchoDeck::styleNames().size();
            deck.setParam ("style", (float) (((int) deck.param ("style") + dir + n) % n));
        }

        void styleMenu()
        {
            juce::PopupMenu m;
            const auto names = EchoDeck::styleNames();
            const int cur = (int) deck.param ("style");
            for (int i = 0; i < names.size(); ++i) m.addItem (i + 1, names[i], true, i == cur);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (screen (styleDisplay))),
                             [safe = juce::Component::SafePointer<EchoDeckEditor> (this)] (int r)
                             {
                                 if (safe != nullptr && r > 0) { safe->deck.setParam ("style", (float) (r - 1)); safe->refreshAll(); }
                             });
        }

        int columnAt (juce::Point<float> d) const
        {
            return juce::jlimit (0, EchoDeck::numTaps - 1, (int) ((d.x - rhythmGrid.getX()) / (rhythmGrid.getWidth() / EchoDeck::numTaps)));
        }

        void setTapFrom (juce::Point<float> d)
        {
            const int col = dragTap >= 0 && ! rhythmGrid.contains (d) ? dragTap : columnAt (d);
            dragTap = col;
            const auto bar = rhythmGrid.reduced (0.0f, 6.0f).withTrimmedBottom (12.0f);
            const float v = juce::jlimit (0.0f, 1.0f, (bar.getBottom() - d.y) / bar.getHeight());
            deck.setParam ("tap" + juce::String (col + 1), v < 0.04f ? 0.0f : v);
            repaint (screen (rhythmTray));
        }

        void refreshAll()
        {
            attachTimeKnob();
            for (auto* k : keys) k->refresh();
            repaint();
        }

        /** The big knob sets milliseconds or a note value, whichever the time mode uses. */
        void attachTimeKnob()
        {
            const int want = (int) deck.param ("timeMode") == EchoDeck::milliseconds ? 0 : 1;
            if (want == timeAttachedTo) return;
            timeAttach.reset();
            const juce::String id = want == 0 ? "timeMs" : "note";
            timeAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (deck.state, id, timeKnob);
            setDefault (timeKnob, id);
            timeAttachedTo = want;
        }

        void timerCallback() override
        {
            attachTimeKnob();
            for (auto* k : keys) k->refresh();
            tapKey->repaint();
            knobs[2]->setAlpha ((int) deck.param ("mode") == EchoDeck::dual ? 1.0f : 0.35f);

            // a cheap fingerprint of what the displays show: repaint them only when it changes
            juce::String key;
            for (auto* id : { "mode", "style", "timeMode", "note", "timeMs", "tempoSrc", "tapBpm", "rhythmLen", "hold",
                              "tap1", "tap2", "tap3", "tap4", "tap5", "tap6", "tap7", "tap8" })
                key << juce::String (deck.param (id), 2) << ";";
            key << juce::String (deck.getTempo(), 1);
            if (key != lastKey)
            {
                lastKey = key;
                header.refreshPreset();
                repaint (screen (timeDisplay)); repaint (screen (styleDisplay)); repaint (screen (blurbArea));
                repaint (screen (bpmDisplay)); repaint (screen (rhythmTray)); repaint (screen (ratioTray));
                repaint (screen (tapeWindow));
            }

            // VU ballistics (about 300 ms), 0 VU = -12 dBFS
            const float level = meterShowsEcho ? deck.getEchoLevel() : deck.getInputLevel();
            const float db = juce::jlimit (-22.0f, 4.0f, juce::Decibels::gainToDecibels (level, -60.0f) + 12.0f);
            needle += (db - needle) * 0.22f;
            repaint (screen (vuArea));
            repaint (screen (tapeWindow));
        }

        // ---- static panel ---------------------------------------------------------------------------------------
        void renderBackground()
        {
            background = juce::Image (juce::Image::ARGB, juce::jmax (1, getWidth()), juce::jmax (1, getHeight()), true);
            juce::Graphics g (background);
            paintEditorBackground (g, getLocalBounds(), ec::brass);
            g.addTransform (toScreen);

            // the enamelled front panel
            const auto plate = rf (4, 2, designW - 8, designH - 4);
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.fillRoundedRectangle (plate.translated (0, 3), 14.0f);
            juce::ColourGradient eg (ec::enamelTop, 0, plate.getY(), ec::enamelBottom, 0, plate.getBottom(), false);
            g.setGradientFill (eg);
            g.fillRoundedRectangle (plate, 14.0f);
            g.setColour (juce::Colours::white.withAlpha (0.018f));
            for (float y = plate.getY() + 2.0f; y < plate.getBottom(); y += 3.0f)
                g.drawHorizontalLine ((int) y, plate.getX() + 6.0f, plate.getRight() - 6.0f);
            g.setColour (juce::Colours::white.withAlpha (0.09f));
            g.drawRoundedRectangle (plate.reduced (0.5f), 14.0f, 1.0f);

            // screws
            for (auto p : std::initializer_list<juce::Point<float>> { { 20.0f, 18.0f }, { designW - 20.0f, 18.0f }, { 20.0f, designH - 18.0f }, { designW - 20.0f, designH - 18.0f } })
                screw (g, p);

            // name plate
            g.setColour (ec::cream);
            g.setFont (engraved (30.0f).withExtraKerningFactor (0.28f));
            g.drawText ("ECHO DECK", rf (40, 10, 400, 36), juce::Justification::centredLeft);
            g.setColour (ec::creamDim);
            g.setFont (engraved (10.5f, false).withExtraKerningFactor (0.3f));
            g.drawText ("TAPE  -  VALVE  -  BUCKET BRIGADE  -  DIGITAL   ECHO PROCESSOR", rf (42, 44, 520, 16), juce::Justification::centredLeft);
            auto badge = rf (786, 16, 176, 30);
            juce::ColourGradient bg (ec::brass.brighter (0.35f), badge.getX(), badge.getY(), ec::brass.darker (0.5f), badge.getX(), badge.getBottom(), false);
            g.setGradientFill (bg);
            g.fillRoundedRectangle (badge, 4.0f);
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.drawRoundedRectangle (badge, 4.0f, 1.0f);
            g.setColour (juce::Colour (0xff2a1d08));
            g.setFont (engraved (11.5f).withExtraKerningFactor (0.2f));
            g.drawText ("WOMANINSTEM  ED-1", badge, juce::Justification::centred);

            bayBox (g, rf (20, 70, 310, 350), "ECHO");
            bayBox (g, rf (345, 70, 310, 350), "TRANSPORT");
            bayBox (g, rf (670, 70, 310, 350), "TONE  -  DRIVE");
            bayBox (g, rf (20, 432, 570, 114), "TWEAK");
            bayBox (g, rhythmTray, "RHYTHM ECHO");

            // knob legends
            for (auto& sp : knobSpecs)
            {
                g.setColour (ec::cream);
                g.setFont (engraved (sp.r < 26.0f ? 9.5f : 10.5f));
                g.drawText (sp.label, rf (sp.cx - 60.0f, sp.cy - sp.r - 15.0f, 120.0f, 12.0f), juce::Justification::centred);
                g.setColour (ec::creamDim);
                g.setFont (engraved (7.5f, false));
                if (sp.r < 26.0f)
                {
                    // small knobs sit close together: range under the knob
                    const auto under = rf (sp.cx - 32.0f, sp.cy + sp.r + 1.0f, 64.0f, 10.0f);
                    g.drawText (sp.lo, under, juce::Justification::centredLeft);
                    g.drawText (sp.hi, under, juce::Justification::centredRight);
                }
                else
                {
                    const float yy = sp.cy + sp.r * 0.72f;
                    g.drawText (sp.lo, rf (sp.cx - sp.r - 34.0f, yy, 34.0f, 10.0f), juce::Justification::centredRight);
                    g.drawText (sp.hi, rf (sp.cx + sp.r, yy, 40.0f, 10.0f), juce::Justification::centredLeft);
                }
            }
            g.setColour (ec::cream);
            g.setFont (engraved (10.5f));
            g.drawText ("ECHO TIME", rf (115, 315, 120, 12), juce::Justification::centred);
            g.setFont (engraved (8.5f));
            g.setColour (ec::creamDim);
            g.drawText ("STEPS", rf (800, 437, 50, 18), juce::Justification::centredRight);
            g.drawText ("TEMPO", rf (572, 340, 68, 12), juce::Justification::centred);
            g.drawText ("STYLE", rf (360, 218, 280, 10), juce::Justification::centred);

            // dark glass areas
            for (auto r : { timeDisplay, styleDisplay, bpmDisplay, stepsDisplay })
                glassBox (g, r);
            glassBox (g, tapeWindow);
        }

        static void screw (juce::Graphics& g, juce::Point<float> p)
        {
            auto r = juce::Rectangle<float> (12.0f, 12.0f).withCentre (p);
            juce::ColourGradient sg (ec::brass.brighter (0.4f), r.getX(), r.getY(), ec::brass.darker (0.7f), r.getRight(), r.getBottom(), false);
            g.setGradientFill (sg);
            g.fillEllipse (r);
            g.setColour (juce::Colour (0xff3a2a10));
            g.drawLine (p.x - 4.0f, p.y - 1.5f, p.x + 4.0f, p.y + 1.5f, 1.4f);
            g.drawLine (p.x - 1.5f, p.y + 4.0f, p.x + 1.5f, p.y - 4.0f, 1.4f);
        }

        static void bayBox (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title)
        {
            g.setColour (juce::Colours::white.withAlpha (0.06f));
            g.drawRoundedRectangle (r.translated (0, 1.0f), 10.0f, 1.0f);
            g.setColour (ec::bay.withAlpha (0.82f));
            g.fillRoundedRectangle (r, 10.0f);
            g.setColour (ec::bayEdge);
            g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.2f);
            g.setColour (ec::brass);
            g.fillRect (rf (r.getX() + 14.0f, r.getY() + 13.0f, 3.0f, 9.0f));
            g.setColour (ec::cream.withAlpha (0.9f));
            g.setFont (engraved (10.0f).withExtraKerningFactor (0.25f));
            g.drawText (title, rf (r.getX() + 22.0f, r.getY() + 10.0f, 200.0f, 14.0f), juce::Justification::centredLeft);
        }

        static void glassBox (juce::Graphics& g, juce::Rectangle<float> r)
        {
            g.setColour (juce::Colours::black.withAlpha (0.7f));
            g.fillRoundedRectangle (r.expanded (2.0f), 6.0f);
            g.setColour (ec::glass);
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (juce::Colours::white.withAlpha (0.08f));
            g.drawRoundedRectangle (r.expanded (2.0f).translated (0.0f, 1.0f), 6.0f, 1.0f);
        }

        // ---- live parts -------------------------------------------------------------------------------------------
        static void glowText (juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, juce::Font f, juce::Justification j, juce::Colour c = ec::amber)
        {
            g.setFont (f);
            g.setColour (c.withAlpha (0.18f));
            for (auto o : std::initializer_list<juce::Point<float>> { { -1.0f, 0.0f }, { 1.0f, 0.0f }, { 0.0f, -1.0f }, { 0.0f, 1.0f } })
                g.drawText (t, r.translated (o.x, o.y), j);
            g.setColour (c);
            g.drawText (t, r, j);
        }

        void paintTimeDisplay (juce::Graphics& g)
        {
            const int tm = (int) deck.param ("timeMode");
            const float ms = tm == EchoDeck::milliseconds ? deck.param ("timeMs") : (float) deck.targetMs (deck.getTempo());
            juce::String big, small;
            const juce::String msText = ms >= 1000.0f ? juce::String (ms / 1000.0f, 2) + " s" : juce::String (ms, ms < 10.0f ? 1 : 0) + " ms";
            if (tm == EchoDeck::milliseconds)
            {
                big = msText;
                small = "FREE TIME";
            }
            else
            {
                big = EchoDeck::noteNames()[(int) deck.param ("note")] + (tm == EchoDeck::dotted ? " DOT" : tm == EchoDeck::triplet ? " TRIP" : "");
                small = msText + "  @ " + juce::String (deck.getTempo(), 1) + " BPM " + ((int) deck.param ("tempoSrc") == 1 ? "TAP" : "SONG");
            }
            const int mode = (int) deck.param ("mode");
            if (mode == EchoDeck::dual) small << "   R x" << juce::String (deck.param ("ratio"), 2);
            if (mode == EchoDeck::rhythm) small << "   " << juce::String ((int) deck.param ("rhythmLen") + 1) << " STEPS";
            const auto r = timeDisplay.reduced (10.0f, 4.0f);
            glowText (g, big, r.withHeight (36.0f), uiFont (28.0f, true), juce::Justification::centredLeft);
            glowText (g, small, r.withTrimmedTop (38.0f), engraved (9.5f), juce::Justification::centredLeft, ec::amber.withAlpha (0.75f));
            if (deck.isHolding())
                glowText (g, "HOLD", r.withHeight (36.0f), engraved (11.0f), juce::Justification::topRight, ec::ledRed);
        }

        void paintStyle (juce::Graphics& g)
        {
            const int s = (int) deck.param ("style");
            glowText (g, EchoDeck::styleNames()[s].toUpperCase(), styleDisplay, engraved (14.0f), juce::Justification::centred);
            g.setColour (ec::creamDim);
            g.setFont (uiFont (10.5f));
            g.drawFittedText (EchoDeck::styleBlurbs()[s], blurbArea.toNearestInt(), juce::Justification::centred, 2, 0.9f);
        }

        void paintBpm (juce::Graphics& g)
        {
            const auto r = bpmDisplay.reduced (8.0f, 2.0f);
            glowText (g, juce::String (deck.getTempo(), 1), r.withTrimmedRight (34.0f), uiFont (24.0f, true), juce::Justification::centredRight);
            glowText (g, "BPM", r.withTrimmedLeft (r.getWidth() - 30.0f), engraved (9.0f), juce::Justification::centredRight, ec::amber.withAlpha (0.7f));
            const auto sr = stepsDisplay;
            glowText (g, juce::String ((int) deck.param ("rhythmLen") + 1), sr, uiFont (13.0f, true), juce::Justification::centred);
        }

        void paintTape (juce::Graphics& g)
        {
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (tapeWindow.toNearestInt());
            const auto w = tapeWindow;
            const double travel = deck.getTapeTravel();
            const float angle = (float) std::fmod (travel * juce::MathConstants<double>::twoPi * 6.0, juce::MathConstants<double>::twoPi);
            const juce::Point<float> left (w.getX() + 70.0f, w.getY() + 54.0f), right (w.getRight() - 70.0f, w.getY() + 54.0f);
            const float rr = 44.0f, pack = rr * 0.74f;

            // tape path over the heads
            const float headY = w.getBottom() - 20.0f;
            juce::Path tapePath;
            tapePath.startNewSubPath (left.x + 8.0f, left.y + pack);
            tapePath.lineTo (w.getCentreX() - 46.0f, headY);
            tapePath.lineTo (w.getCentreX() + 46.0f, headY);
            tapePath.lineTo (right.x - 8.0f, right.y + pack);
            g.setColour (ec::tape);
            g.strokePath (tapePath, juce::PathStrokeType (2.5f));
            for (int i = 0; i < 3; ++i)
            {
                auto head = rf (w.getCentreX() - 34.0f + (float) i * 25.0f, headY + 1.5f, 16.0f, 12.0f);
                juce::ColourGradient hg (juce::Colour (0xffb9bec2), head.getX(), head.getY(), juce::Colour (0xff4b4f53), head.getX(), head.getBottom(), false);
                g.setGradientFill (hg);
                g.fillRoundedRectangle (head, 2.0f);
            }
            g.setColour (juce::Colour (0xff9aa0a5));
            g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre ({ w.getCentreX() + 52.0f, headY + 3.0f }));

            for (auto c : { left, right })
            {
                g.setColour (ec::tape);
                g.fillEllipse (juce::Rectangle<float> (pack * 2.0f, pack * 2.0f).withCentre (c));
                juce::Path flange;
                flange.addEllipse (juce::Rectangle<float> (rr * 2.0f, rr * 2.0f).withCentre (c));
                flange.setUsingNonZeroWinding (false);
                for (int k = 0; k < 3; ++k)
                {
                    const float a0 = angle + (float) k * juce::MathConstants<float>::twoPi / 3.0f;
                    juce::Path hole;
                    hole.addPieSegment (juce::Rectangle<float> (rr * 1.76f, rr * 1.76f).withCentre (c), a0, a0 + 1.15f, 0.42f);
                    flange.addPath (hole);
                }
                g.setColour (juce::Colour (0xffc9cdd1).withAlpha (0.85f));
                g.fillPath (flange);
                g.setColour (juce::Colours::black.withAlpha (0.4f));
                g.strokePath (flange, juce::PathStrokeType (1.0f));
                auto hub = juce::Rectangle<float> (rr * 0.36f, rr * 0.36f).withCentre (c);
                g.setColour (juce::Colour (0xff2b2b2b));
                g.fillEllipse (hub);
                g.setColour (juce::Colour (0xffd5d9dc));
                for (int k = 0; k < 3; ++k)
                {
                    const float a = angle + (float) k * juce::MathConstants<float>::twoPi / 3.0f;
                    g.drawLine (c.x, c.y, c.x + std::sin (a) * rr * 0.18f, c.y - std::cos (a) * rr * 0.18f, 2.0f);
                }
            }

            // glass reflection + the machine's badge
            juce::ColourGradient refl (juce::Colours::white.withAlpha (0.07f), w.getX(), w.getY(), juce::Colours::transparentWhite, w.getX(), w.getCentreY(), false);
            g.setGradientFill (refl);
            g.fillRect (w);
            glowText (g, styleBadges[juce::jlimit (0, 9, (int) deck.param ("style"))], w.reduced (8.0f, 6.0f), engraved (8.5f), juce::Justification::topRight, ec::amber.withAlpha (0.8f));
            if (deck.isHolding())
                glowText (g, "LOOP HELD", w.reduced (8.0f, 6.0f), engraved (8.5f), juce::Justification::topLeft, ec::ledRed);
        }

        void paintVu (juce::Graphics& g)
        {
            const auto r = vuArea;
            g.setColour (juce::Colours::black.withAlpha (0.7f));
            g.fillRoundedRectangle (r.expanded (3.0f), 6.0f);
            juce::ColourGradient face (juce::Colour (0xfff3e7c6), r.getX(), r.getY(), juce::Colour (0xffd8c69b), r.getX(), r.getBottom(), false);
            g.setGradientFill (face);
            g.fillRoundedRectangle (r, 4.0f);

            const juce::Point<float> pivot (r.getCentreX(), r.getBottom() + 34.0f);
            const float radius = r.getHeight() + 6.0f;
            auto angleFor = [] (float db)
            {
                // VU scale: linear in amplitude, -20..+3 across +-48 degrees
                const float a = std::pow (10.0f, db / 20.0f);
                const float lo = std::pow (10.0f, -1.0f), hi = std::pow (10.0f, 0.15f);
                return juce::degreesToRadians (-48.0f + 96.0f * (a - lo) / (hi - lo));
            };
            juce::Path arc, red;
            arc.addCentredArc (pivot.x, pivot.y, radius - 26.0f, radius - 26.0f, 0.0f, angleFor (-20.0f), angleFor (0.0f), true);
            red.addCentredArc (pivot.x, pivot.y, radius - 26.0f, radius - 26.0f, 0.0f, angleFor (0.0f), angleFor (3.0f), true);
            g.setColour (juce::Colour (0xff2a2418));
            g.strokePath (arc, juce::PathStrokeType (1.5f));
            g.setColour (juce::Colour (0xffc0392b));
            g.strokePath (red, juce::PathStrokeType (4.0f));
            g.setFont (uiFont (8.5f, true));
            for (float db : { -20.0f, -10.0f, -7.0f, -5.0f, -3.0f, -1.0f, 0.0f, 1.0f, 2.0f, 3.0f })
            {
                const float a = angleFor (db);
                const float r0 = radius - 30.0f, r1 = radius - 22.0f;
                g.setColour (db >= 0.0f ? juce::Colour (0xffc0392b) : juce::Colour (0xff2a2418));
                g.drawLine (pivot.x + std::sin (a) * r0, pivot.y - std::cos (a) * r0, pivot.x + std::sin (a) * r1, pivot.y - std::cos (a) * r1, 1.2f);
                const auto lp = juce::Point<float> (pivot.x + std::sin (a) * (radius - 13.0f), pivot.y - std::cos (a) * (radius - 13.0f));
                g.drawText (db > 0.0f ? "+" + juce::String ((int) db) : juce::String ((int) db), juce::Rectangle<float> (24.0f, 10.0f).withCentre (lp), juce::Justification::centred);
            }
            g.setColour (juce::Colour (0xff2a2418));
            g.setFont (engraved (15.0f));
            g.drawText ("VU", r.withTrimmedTop (r.getHeight() * 0.48f).withTrimmedBottom (12.0f), juce::Justification::centred);
            g.setFont (engraved (7.5f));
            g.drawText (meterShowsEcho ? "ECHO  (click: input)" : "INPUT  (click: echo)", r.withTrimmedTop (r.getHeight() - 14.0f), juce::Justification::centred);

            {
                juce::Graphics::ScopedSaveState ss (g);
                g.reduceClipRegion (r.toNearestInt());
                const float a = angleFor (juce::jlimit (-22.0f, 4.0f, needle));
                g.setColour (juce::Colours::black.withAlpha (0.18f));
                g.drawLine (pivot.x + 2.0f, pivot.y, pivot.x + 2.0f + std::sin (a) * (radius - 8.0f), pivot.y - std::cos (a) * (radius - 8.0f) + 2.0f, 2.0f);
                g.setColour (juce::Colour (0xff141210));
                g.drawLine (pivot.x, pivot.y, pivot.x + std::sin (a) * (radius - 8.0f), pivot.y - std::cos (a) * (radius - 8.0f), 1.4f);
                juce::ColourGradient refl (juce::Colours::white.withAlpha (0.22f), r.getX(), r.getY(), juce::Colours::transparentWhite, r.getX(), r.getCentreY(), false);
                g.setGradientFill (refl);
                g.fillRect (r.withHeight (r.getHeight() * 0.45f));
            }
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.drawRoundedRectangle (r, 4.0f, 1.0f);
        }

        void paintRhythm (juce::Graphics& g)
        {
            const bool active = (int) deck.param ("mode") == EchoDeck::rhythm;
            const int steps = (int) deck.param ("rhythmLen") + 1;
            const float colW = rhythmGrid.getWidth() / (float) EchoDeck::numTaps;
            const auto bars = rhythmGrid.reduced (0.0f, 6.0f).withTrimmedBottom (12.0f);
            for (int k = 0; k < EchoDeck::numTaps; ++k)
            {
                auto col = rf (rhythmGrid.getX() + k * colW + 3.0f, rhythmGrid.getY(), colW - 6.0f, rhythmGrid.getHeight());
                const bool inPattern = k < steps;
                g.setColour (ec::glass.withAlpha (inPattern ? 1.0f : 0.5f));
                g.fillRoundedRectangle (col, 3.0f);
                const float lv = deck.param (("tap" + juce::String (k + 1)).toRawUTF8());
                if (lv > 0.0f)
                {
                    auto bar = rf (col.getX() + 5.0f, bars.getBottom() - bars.getHeight() * lv, col.getWidth() - 10.0f, bars.getHeight() * lv);
                    const auto c = inPattern ? ec::amber : ec::amberDim;
                    g.setColour (c.withAlpha (0.22f));
                    g.fillRoundedRectangle (bar.expanded (2.0f), 2.0f);
                    g.setColour (c);
                    g.fillRoundedRectangle (bar, 2.0f);
                }
                g.setColour (inPattern ? ec::cream.withAlpha (0.8f) : ec::creamDim.withAlpha (0.4f));
                g.setFont (engraved (8.5f));
                g.drawText (juce::String (k + 1), col.withTrimmedTop (col.getHeight() - 13.0f), juce::Justification::centred);
            }
            if (! active)
            {
                g.setColour (ec::bay.withAlpha (0.5f));
                g.fillRect (rhythmGrid.expanded (2.0f));
                g.setColour (ec::creamDim);
                g.setFont (engraved (8.5f));
                g.drawText ("PICK RHYTHM MODE TO HEAR THE PATTERN", rhythmGrid.withHeight (14.0f).translated (0.0f, -1.0f), juce::Justification::centred);
            }
        }
    };
}

void installEchoDeckEditor()
{
    EchoDeck::editorFactory = [] (EchoDeck& e) -> juce::AudioProcessorEditor* { return new EchoDeckEditor (e); };
}

} // namespace wis::daw
