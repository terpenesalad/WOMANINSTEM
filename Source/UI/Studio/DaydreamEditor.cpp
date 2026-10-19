#include "EditorParts.h"
#include "Daw/Instruments/Daydream.h"
#include "Daw/Model/DaydreamChords.h"

namespace wis::daw
{

/*  Daydream's window. PLAY: the eight chord buttons (they latch when clicked; they light when held on
    the MIDI keyboard too), the screen (the chord, its notes, a keyboard, a scope), the voicing and bass
    ladders, the key, a one-octave keyboard to play roots with the mouse, then the sound, performance,
    bass, FX, tempo and beat, and loop controls. SOUND: the synth engine. EFFECTS: the eight effects.
    SETTINGS: how it plays (chord position, spread, button keys...). */

namespace
{
    namespace dd
    {
        const juce::Colour body      { 0xff1c2227 };
        const juce::Colour bodyLight { 0xff262e35 };
        const juce::Colour card      { 0xff20282e };
        const juce::Colour edge      { 0xff343f48 };
        const juce::Colour cream     { 0xfff1e8d6 };
        const juce::Colour creamDim  { 0xffa59d8e };
        const juce::Colour coral     { 0xffff7a59 };
        const juce::Colour lilac     { 0xffb7a4ff };
        const juce::Colour mint      { 0xff8cf5c4 };
        const juce::Colour mintDim   { 0xff2c5a47 };
        const juce::Colour screen    { 0xff0b1411 };
        const juce::Colour typeKey   { 0xff2f3a42 };
        const juce::Colour colourKey { 0xff3a3446 };
    }

    juce::Font labelFont (float h, bool bold = true) { return uiFont (h, bold).withExtraKerningFactor (0.08f); }

    /** The key to spell a chord's notes in: its root's major key, or the relative major for minor-third chords (Cm: Eb). */
    int majorKeyFor (int root, const std::vector<int>& iv)
    {
        const bool minorThird = std::find (iv.begin(), iv.end(), 3) != iv.end() && std::find (iv.begin(), iv.end(), 4) == iv.end();
        return (root + (minorThird ? 3 : 0)) % 12;
    }

    class DreamKnobLnF : public juce::LookAndFeel_V4
    {
    public:
        void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override
        {
            const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.0f);
            const float r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
            const auto c = b.getCentre();
            juce::Path arcBg, arc;
            arcBg.addCentredArc (c.x, c.y, r - 2.0f, r - 2.0f, 0.0f, start, end, true);
            arc.addCentredArc (c.x, c.y, r - 2.0f, r - 2.0f, 0.0f, start, start + pos * (end - start), true);
            g.setColour (dd::edge);
            g.strokePath (arcBg, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (dd::coral);
            g.strokePath (arc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            const float kr = r * 0.72f;
            juce::ColourGradient grad (dd::cream, c.x - kr * 0.4f, c.y - kr * 0.6f, dd::creamDim, c.x + kr * 0.5f, c.y + kr, true);
            g.setGradientFill (grad);
            g.fillEllipse (juce::Rectangle<float> (kr * 2.0f, kr * 2.0f).withCentre (c));
            const float a = start + pos * (end - start);
            g.setColour (dd::body);
            g.drawLine (c.x + std::sin (a) * kr * 0.25f, c.y - std::cos (a) * kr * 0.25f, c.x + std::sin (a) * kr * 0.85f, c.y - std::cos (a) * kr * 0.85f, 2.5f);
        }
    };
    DreamKnobLnF& knobLnF() { static DreamKnobLnF l; return l; }

    /** Fills a combo with a choice parameter's choices and attaches it. */
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> bindCombo (BuiltinProcessor& p, juce::ComboBox& box, const juce::String& id)
    {
        box.clear (juce::dontSendNotification);
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (p.state.getParameter (id)))
            box.addItemList (c->choices, 1);
        return std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.state, id, box);
    }

    /** A small knob with its name under it (re-bindable to another parameter). */
    class SmallKnob : public juce::Component
    {
    public:
        explicit SmallKnob (BuiltinProcessor& p) : proc (p)
        {
            slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            slider.setPopupDisplayEnabled (true, true, nullptr);
            slider.setLookAndFeel (&knobLnF());
            addAndMakeVisible (slider);
            label.setJustificationType (juce::Justification::centred);
            label.setFont (labelFont (11.0f));
            label.setColour (juce::Label::textColourId, dd::creamDim);
            label.setInterceptsMouseClicks (false, false);
            addAndMakeVisible (label);
        }
        ~SmallKnob() override { attachment.reset(); slider.setLookAndFeel (nullptr); }

        void bind (const juce::String& id, const juce::String& name)
        {
            if (id == boundId) { label.setText (name, juce::dontSendNotification); return; }
            attachment.reset();
            boundId = id;
            label.setText (name, juce::dontSendNotification);
            if (proc.state.getParameter (id) != nullptr)
                attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.state, id, slider);
        }
        void resized() override
        {
            auto r = getLocalBounds();
            label.setBounds (r.removeFromBottom (14));
            slider.setBounds (r);
        }
        juce::Slider slider;

    private:
        BuiltinProcessor& proc;
        juce::Label label;
        juce::String boundId;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    // ---- a chord button ---------------------------------------------------------------------------------
    class ChordButton : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        ChordButton (Daydream& d, int b) : dream (d), button (b)
        {
            setTooltip ("Click to latch " + daydream::buttonLongName (b) + " on (click again to let go). On a MIDI keyboard, hold its key instead.");
        }
        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced (3.0f);
            const bool lit = (dream.getHeldButtons() >> button) & 1u;
            const bool latched = (dream.getLatched() >> button) & 1u;
            const bool isType = button < 4;
            const auto base = isType ? dd::typeKey : dd::colourKey;
            const auto glow = isType ? dd::coral : dd::lilac;
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillRoundedRectangle (r.translated (0.0f, 2.0f), 9.0f);
            g.setColour (lit ? glow : base);
            g.fillRoundedRectangle (r, 9.0f);
            if (lit)
            {
                g.setColour (juce::Colours::white.withAlpha (0.18f));
                g.fillRoundedRectangle (r.withHeight (r.getHeight() * 0.45f), 9.0f);
            }
            g.setColour (lit ? dd::body : dd::cream);
            g.setFont (labelFont (juce::jmin (26.0f, r.getHeight() * 0.42f)));
            g.drawText (daydream::buttonName (button), r.withTrimmedBottom (r.getHeight() * 0.25f), juce::Justification::centred);
            g.setFont (labelFont (10.5f, false));
            g.setColour ((lit ? dd::body : dd::creamDim).withAlpha (0.9f));
            const int note = dream.buttonNote (button);
            juce::String sub = latched ? "LATCHED" : (note >= 0 ? daydream::noteName (note) : juce::String());
            g.drawText (sub, r.removeFromBottom (r.getHeight() * 0.32f), juce::Justification::centred);
            if (latched)
            {
                g.setColour (dd::mint);
                g.fillEllipse (r.getRight() - 14.0f, r.getY() + 6.0f, 7.0f, 7.0f);
            }
        }
        void mouseDown (const juce::MouseEvent&) override { dream.toggleLatched (button); repaint(); }

    private:
        Daydream& dream;
        const int button;
    };

    // ---- the one-octave keyboard --------------------------------------------------------------------------
    class RootKeys : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit RootKeys (Daydream& d) : dream (d) { setTooltip ("Play a root: the chord buttons held (or Key Mode) decide the chord."); }
        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced (2.0f);
            const float ww = r.getWidth() / 7.0f;
            static const int whites[] = { 0, 2, 4, 5, 7, 9, 11 };
            static const int blacks[] = { 1, 3, -1, 6, 8, 10 };
            for (int i = 0; i < 7; ++i)
            {
                auto k = juce::Rectangle<float> (r.getX() + i * ww, r.getY(), ww - 3.0f, r.getHeight());
                const bool down = pressed == whites[i];
                g.setColour (down ? dd::coral : dd::cream);
                g.fillRoundedRectangle (k, 5.0f);
                g.setColour (dd::creamDim);
                g.setFont (labelFont (12.0f));
                g.drawText (daydream::noteName (60 + whites[i]).dropLastCharacters (1), k.removeFromBottom (24.0f), juce::Justification::centred);
            }
            for (int i = 0; i < 6; ++i)
            {
                if (blacks[i] < 0) continue;
                auto k = juce::Rectangle<float> (r.getX() + (i + 1) * ww - ww * 0.3f - 1.5f, r.getY(), ww * 0.6f, r.getHeight() * 0.58f);
                g.setColour (pressed == blacks[i] ? dd::coral : dd::body);
                g.fillRoundedRectangle (k, 4.0f);
            }
        }
        int keyAt (juce::Point<float> p) const
        {
            auto r = getLocalBounds().toFloat().reduced (2.0f);
            const float ww = r.getWidth() / 7.0f;
            static const int whites[] = { 0, 2, 4, 5, 7, 9, 11 };
            static const int blacks[] = { 1, 3, -1, 6, 8, 10 };
            if (p.y < r.getY() + r.getHeight() * 0.58f)
                for (int i = 0; i < 6; ++i)
                    if (blacks[i] >= 0)
                    {
                        const float x0 = r.getX() + (i + 1) * ww - ww * 0.3f - 1.5f;
                        if (p.x >= x0 && p.x < x0 + ww * 0.6f) return blacks[i];
                    }
            return whites[juce::jlimit (0, 6, (int) ((p.x - r.getX()) / ww))];
        }
        void mouseDown (const juce::MouseEvent& e) override
        {
            pressed = keyAt (e.position);
            dream.uiNote (60 + pressed, true, 0.55f + 0.4f * juce::jlimit (0.0f, 1.0f, e.position.y / (float) getHeight()));
            repaint();
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            const int k = keyAt (e.position);
            if (k == pressed) return;
            dream.uiNote (60 + pressed, false);
            pressed = k;
            dream.uiNote (60 + pressed, true, 0.8f);
            repaint();
        }
        void mouseUp (const juce::MouseEvent&) override
        {
            if (pressed >= 0) dream.uiNote (60 + pressed, false);
            pressed = -1;
            repaint();
        }

    private:
        Daydream& dream;
        int pressed = -1;
    };

    // ---- the ladders: voicing and bass voicing -----------------------------------------------------------
    class Ladder : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        Ladder (Daydream& d, const char* id, const juce::String& title, int range) : dream (d), paramId (id), name (title), span (range) {}
        int value() const { return juce::roundToInt (dream.param (paramId)); }
        void set (int v) { dream.setParam (paramId, (float) juce::jlimit (-span, span, v)); repaint(); }
        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            g.setColour (dd::creamDim);
            g.setFont (labelFont (11.0f));
            g.drawText (name, r.removeFromLeft (64.0f), juce::Justification::centredLeft);
            auto valueArea = r.removeFromRight (40.0f);
            const int v = value();
            g.setColour (dd::cream);
            g.setFont (labelFont (15.0f));
            g.drawText ((v > 0 ? "+" : "") + juce::String (v), valueArea, juce::Justification::centred);
            track = r.reduced (18.0f, 0.0f);
            // minus / plus
            g.setColour (dd::cream.withAlpha (0.8f));
            g.setFont (labelFont (18.0f));
            g.drawText ("-", r.withWidth (16.0f), juce::Justification::centred);
            g.drawText ("+", r.withLeft (r.getRight() - 16.0f), juce::Justification::centred);
            const int n = span * 2 + 1;
            const float step = track.getWidth() / (float) (n - 1);
            for (int i = 0; i < n; ++i)
            {
                const int at = i - span;
                const float x = track.getX() + step * (float) i;
                const bool on = (v >= 0 && at >= 0 && at <= v) || (v < 0 && at <= 0 && at >= v);
                const float h = at == 0 ? 0.8f : (at % 4 == 0 ? 0.6f : 0.4f);
                g.setColour (on ? dd::coral : dd::edge);
                g.fillRoundedRectangle (x - 1.5f, r.getCentreY() - r.getHeight() * h * 0.5f, 3.0f, r.getHeight() * h, 1.5f);
            }
            const float vx = track.getX() + step * (float) (v + span);
            g.setColour (dd::cream);
            g.fillEllipse (vx - 6.0f, r.getCentreY() - 6.0f, 12.0f, 12.0f);
        }
        void mouseDown (const juce::MouseEvent& e) override
        {
            auto r = getLocalBounds().toFloat();
            r.removeFromLeft (64.0f); r.removeFromRight (40.0f);
            if (e.position.x < r.getX() + 16.0f) { set (value() - 1); return; }
            if (e.position.x > r.getRight() - 16.0f) { set (value() + 1); return; }
            dragging = true;
            mouseDrag (e);
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (! dragging || track.getWidth() <= 0.0f) return;
            const float step = track.getWidth() / (float) (span * 2);
            set (juce::roundToInt ((e.position.x - track.getX()) / step) - span);
        }
        void mouseUp (const juce::MouseEvent&) override { dragging = false; }
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
        {
            if (w.deltaY != 0.0f) set (value() + (w.deltaY > 0 ? 1 : -1));
        }
        void mouseDoubleClick (const juce::MouseEvent&) override { set (0); }

    private:
        Daydream& dream;
        const char* paramId;
        juce::String name;
        int span;
        juce::Rectangle<float> track;
        bool dragging = false;
    };

    // ---- the screen ---------------------------------------------------------------------------------------
    enum ViewMode { viewChord, viewKeys, viewNotes, viewScope, viewAll };

    class Screen : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit Screen (Daydream& d) : dream (d) { setTooltip ("Click to change what the screen shows: chord, keyboard, notes, scope, everything."); }
        static inline int mode = viewAll;

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            g.setColour (dd::screen);
            g.fillRoundedRectangle (r, 10.0f);
            g.setColour (dd::mintDim);
            g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.2f);
            r = r.reduced (14.0f, 10.0f);

            const auto disp = dream.getDisplay();
            const bool keyMode = dream.param ("keyMode") > 0.5f;
            const int tonic = dream.getKeyTonic();
            const int spellKey = keyMode ? tonic : -1;

            // status line
            auto top = r.removeFromTop (16.0f);
            g.setFont (labelFont (11.5f));
            g.setColour (dd::mint.withAlpha (0.75f));
            if (keyMode)
                g.drawText ("KEY " + daydream::noteName (60 + tonic).dropLastCharacters (1) + " " + daydream::scaleNames()[dream.getKeyScale()].toUpperCase(),
                            top, juce::Justification::centredLeft);
            const int tr = juce::roundToInt (dream.param ("transpose"));
            juce::String right = juce::String (juce::roundToInt (dream.getCurrentBpm())) + " BPM";
            if (tr != 0) right = "TR " + juce::String (tr > 0 ? "+" : "") + juce::String (tr) + "   " + right;
            g.drawText (right, top, juce::Justification::centredRight);
            g.drawText (Daydream::perfNames()[(int) dream.param ("perf")].toUpperCase(), top, juce::Justification::centred);

            juce::String name = "-";
            std::vector<int> iv (disp.intervals.begin(), disp.intervals.begin() + disp.numIntervals);
            if (disp.chord)
            {
                const int bassPc = disp.bass >= 0 && juce::roundToInt (dream.param ("bassVoicing")) != 0 ? disp.bass % 12 : -1;
                name = daydream::chordName (disp.root, iv, spellKey, bassPc);
            }
            else if (disp.numNotes > 0)
            {
                juce::StringArray n;
                for (int i = 0; i < disp.numNotes; ++i) n.add (daydream::noteName (disp.notes[(size_t) i], spellKey));
                name = n.joinIntoString ("  ");
            }

            auto drawKeys = [&] (juce::Rectangle<float> k)
            {
                const int lo = 36, hi = 84;
                int whites = 0;
                for (int n = lo; n < hi; ++n) if (! juce::MidiMessage::isMidiNoteBlack (n)) ++whites;
                const float ww = k.getWidth() / (float) whites;
                const int bass = dream.getBassSounding();
                int wi = 0;
                for (int n = lo; n < hi; ++n)
                    if (! juce::MidiMessage::isMidiNoteBlack (n))
                    {
                        auto key = juce::Rectangle<float> (k.getX() + wi * ww, k.getY(), ww - 1.0f, k.getHeight());
                        g.setColour (dream.isSounding (n) ? dd::mint : n == bass ? dd::lilac : dd::mintDim.withAlpha (0.55f));
                        g.fillRect (key);
                        ++wi;
                    }
                wi = 0;
                for (int n = lo; n < hi; ++n)
                {
                    if (! juce::MidiMessage::isMidiNoteBlack (n)) { ++wi; continue; }
                    auto key = juce::Rectangle<float> (k.getX() + wi * ww - ww * 0.32f, k.getY(), ww * 0.64f, k.getHeight() * 0.6f);
                    g.setColour (dream.isSounding (n) ? dd::mint : n == bass ? dd::lilac : dd::screen);
                    g.fillRect (key);
                }
            };
            auto drawNotes = [&] (juce::Rectangle<float> a)
            {
                juce::StringArray n;
                const int count = disp.chord ? disp.numNotes : 0;
                const int noteKey = spellKey >= 0 ? spellKey : majorKeyFor (disp.root, iv);
                for (int i = 0; i < count; ++i) n.add (daydream::noteName (disp.notes[(size_t) i], noteKey));
                if (disp.chord && disp.bass >= 0) n.insert (0, "bass " + daydream::noteName (disp.bass, noteKey) + "  |");
                g.setColour (dd::mint.withAlpha (0.85f));
                g.setFont (labelFont (14.0f, false));
                g.drawFittedText (n.joinIntoString ("  "), a.toNearestInt(), juce::Justification::centred, 2);
            };
            auto drawScope = [&] (juce::Rectangle<float> a)
            {
                float buf[512];
                dream.getScope (buf, 512);
                // start at a rising zero crossing so the wave stands still
                int start = 0;
                for (int i = 1; i < 256; ++i) if (buf[i - 1] <= 0.0f && buf[i] > 0.0f) { start = i; break; }
                juce::Path p;
                const int len = 256;
                float peak = 0.05f;
                for (int i = 0; i < len; ++i) peak = juce::jmax (peak, std::abs (buf[start + i]));
                for (int i = 0; i < len; ++i)
                {
                    const float x = a.getX() + a.getWidth() * (float) i / (float) (len - 1);
                    const float y = a.getCentreY() - buf[start + i] / peak * a.getHeight() * 0.45f;
                    if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
                }
                g.setColour (dd::mint.withAlpha (0.25f));
                g.strokePath (p, juce::PathStrokeType (5.0f));
                g.setColour (dd::mint);
                g.strokePath (p, juce::PathStrokeType (1.6f));
            };
            auto drawName = [&] (juce::Rectangle<float> a, float size)
            {
                g.setColour (dd::mint);
                g.setFont (labelFont (juce::jmin (size, a.getHeight() * 0.9f)));
                g.drawFittedText (name, a.toNearestInt(), juce::Justification::centred, 1, 0.6f);
            };

            switch (mode)
            {
                case viewChord: drawName (r, 76.0f); break;
                case viewKeys:  drawName (r.removeFromTop (r.getHeight() * 0.35f), 40.0f); r.removeFromTop (6.0f); drawKeys (r); break;
                case viewNotes: drawName (r.removeFromTop (r.getHeight() * 0.55f), 56.0f); drawNotes (r); break;
                case viewScope: drawScope (r.reduced (0.0f, 4.0f)); drawName (r.removeFromBottom (24.0f), 18.0f); break;
                default:
                {
                    drawName (r.removeFromTop (r.getHeight() * 0.42f), 54.0f);
                    drawNotes (r.removeFromTop (22.0f));
                    r.removeFromTop (6.0f);
                    drawKeys (r);
                    break;
                }
            }
        }
        void mouseDown (const juce::MouseEvent&) override { mode = (mode + 1) % 5; repaint(); }

    private:
        Daydream& dream;
    };

    // ---- the loop button: a ring that fills as the loop goes round --------------------------------------
    class LoopButton : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit LoopButton (Daydream& d) : dream (d)
        {
            setTooltip ("Loop: press to record (a bar of count-in first, or in Free it starts with your first note), press again to play it, "
                        "again to pause. Overdub adds a layer, Undo takes the last layer off.");
        }
        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced (4.0f);
            const float s = juce::jmin (r.getWidth(), r.getHeight());
            auto c = r.withSizeKeepingCentre (s, s);
            const auto st = dream.getLoopState();
            const bool rec = st == Daydream::loopRecording || st == Daydream::loopOverdub || st == Daydream::loopCountIn || st == Daydream::loopArmed;
            g.setColour (dd::edge);
            g.drawEllipse (c.reduced (3.0f), 5.0f);
            juce::Path arc;
            const float prog = dream.getLoopProgress();
            if (st != Daydream::loopIdle)
            {
                arc.addCentredArc (c.getCentreX(), c.getCentreY(), s * 0.5f - 3.0f, s * 0.5f - 3.0f, 0.0f, 0.0f, juce::MathConstants<float>::twoPi * prog, true);
                g.setColour (rec ? dd::coral : dd::mint);
                g.strokePath (arc, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            g.setColour (rec ? dd::coral : dd::cream);
            auto inner = c.reduced (s * 0.3f);
            if (st == Daydream::loopPlaying)
            {
                juce::Path p; p.addTriangle (inner.getX(), inner.getY(), inner.getX(), inner.getBottom(), inner.getRight(), inner.getCentreY());
                g.fillPath (p);
            }
            else if (st == Daydream::loopPaused)
            {
                g.fillRect (inner.withWidth (inner.getWidth() * 0.35f));
                g.fillRect (inner.withLeft (inner.getRight() - inner.getWidth() * 0.35f));
            }
            else g.fillEllipse (inner);
        }
        void mouseDown (const juce::MouseEvent&) override { dream.loopButton(); }

    private:
        Daydream& dream;
    };

    /** A rounded card with a title, for the bottom row. */
    void paintCard (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title)
    {
        auto f = r.toFloat();
        g.setColour (dd::card);
        g.fillRoundedRectangle (f, 10.0f);
        g.setColour (dd::edge);
        g.drawRoundedRectangle (f.reduced (0.5f), 10.0f, 1.0f);
        g.setColour (dd::creamDim);
        g.setFont (labelFont (11.0f));
        g.drawText (title, r.reduced (12, 6).removeFromTop (14), juce::Justification::centredLeft);
    }

    // =========================================================================================================
    class DaydreamEditor : public juce::AudioProcessorEditor, private juce::Timer
    {
    public:
        explicit DaydreamEditor (Daydream& d)
            : juce::AudioProcessorEditor (d), dream (d), header (d, "Daydream"),
              screen (d), keys (d), loopBtn (d),
              voicing (d, "voicing", "VOICING", 12), bassLadder (d, "bassVoicing", "BASS", 8),
              perfKnob (d), fxKnob (d), bassVol (d), bpmKnob (d), volume (d),
              analoguePanel (d, { "waveA", "waveB", "semiB", "fineB", "mixB", "sub", "noise", "pw", "unison", "detune" }, dd::coral),
              fmPanel (d, { "algorithm", "op1Ratio", "op1Level", "op1Attack", "op1Decay", "op1Sustain", "op2Ratio", "op2Level", "op2Attack", "op2Decay", "op2Sustain",
                            "op3Ratio", "op3Level", "op3Attack", "op3Decay", "op3Sustain", "op4Ratio", "op4Level", "op4Attack", "op4Decay", "op4Sustain",
                            "feedback", "fmVelocity" }, dd::coral),
              reedPanel (d, { "bark", "reedTone", "reedDecay", "mechanics" }, dd::coral),
              sharedPanel (d, { "filterType", "cutoff", "reso", "envAmount", "keyTrack", "velCutoff", "synthDrive", "attack", "decay", "sustain", "release",
                                "fAttack", "fDecay", "fSustain", "fRelease", "lfoRate", "lfoDelay", "vibrato", "lfoFilter", "lfoPwm", "lfoAmp", "drift",
                                "velSens", "width" }, dd::coral),
              enginePanel (d, { "engine", "trim" }, dd::coral),
              settingsPanel (d, { "position", "centre", "spread", "sus2", "buttonZone", "extAdd", "hold", "strumMs", "strumDir", "slop", "arpRate", "arpOrder",
                                  "gate", "pattern", "harpMs", "harpOctaves", "swing", "bassOctave", "bassGlide", "bassTone", "beatKit", "beatVolume",
                                  "loopLength", "lockPerf", "lockFx" }, dd::coral)
        {
            addAndMakeVisible (header);
            header.onPresetChanged = [this] { refreshSound(); };
            for (auto* t : { &playTab, &soundTab, &fxTab, &settingsTab })
            {
                t->setClickingTogglesState (false);
                t->setWantsKeyboardFocus (false);
                header.addButton (*t, 86);
            }
            page = juce::jlimit (0, 3, d.editorPage);
            playTab.onClick = [this] { page = dream.editorPage = 0; layoutPages(); };
            soundTab.onClick = [this] { page = dream.editorPage = 1; layoutPages(); };
            fxTab.onClick = [this] { page = dream.editorPage = 2; layoutPages(); };
            settingsTab.onClick = [this] { page = dream.editorPage = 3; layoutPages(); };

            // ---- PLAY page --------------------------------------------------------------------------------
            for (int b = 0; b < daydream::numButtons; ++b) addChildComponent (chordButtons.add (new ChordButton (d, b)));
            addChildComponent (screen);
            addChildComponent (keys);
            addChildComponent (voicing);
            addChildComponent (bassLadder);
            voicing.setTooltip ("Voicing: each step up moves the chord's lowest note to the top (down: the highest to the bottom). Drag, scroll or click -/+; double-click for 0.");
            bassLadder.setTooltip ("Bass voicing: walks the bass note up or down through the chord's notes (a slash chord). Double-click for 0.");

            keyMode.setButtonText ("KEY MODE");
            keyMode.setTooltip ("Key Mode: every key plays the chord the key's scale makes on it; colour buttons still add 6ths, 7ths and 9ths, and chord buttons override.");
            followKey.setButtonText ("Song's key");
            followKey.setTooltip ("Use the song's key (set in the Studio) instead");
            for (auto* t : { &keyMode, &followKey }) addChildComponent (t);
            keyModeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (d.state, "keyMode", keyMode);
            followAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (d.state, "followSongKey", followKey);
            keyAtt = bindCombo (d, keyBox, "key");
            scaleAtt = bindCombo (d, scaleBox, "scale");
            for (auto* c : { &keyBox, &scaleBox }) addChildComponent (c);
            transpose.setSliderStyle (juce::Slider::IncDecButtons);
            transpose.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 44, 24);
            transpose.setTooltip ("Transpose everything (semitones)");
            transposeAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (d.state, "transpose", transpose);
            transpose.setRange (-12.0, 12.0, 1.0);
            addChildComponent (transpose);

            // sound card
            for (auto* b : { &prevSound, &nextSound })
            {
                b->setWantsKeyboardFocus (false);
                addChildComponent (b);
            }
            prevSound.setButtonText ("<");
            nextSound.setButtonText (">");
            prevSound.onClick = [this] { stepSound (-1); };
            nextSound.onClick = [this] { stepSound (1); };
            soundName.setFont (labelFont (17.0f));
            soundName.setColour (juce::Label::textColourId, dd::cream);
            soundName.setJustificationType (juce::Justification::centred);
            soundName.setMinimumHorizontalScale (0.6f);
            soundInfo.setFont (labelFont (11.5f, false));
            soundInfo.setColour (juce::Label::textColourId, dd::creamDim);
            soundInfo.setJustificationType (juce::Justification::centredTop);
            for (auto* l : { &soundName, &soundInfo }) { l->setInterceptsMouseClicks (false, false); addChildComponent (l); }

            // performance card
            perfAtt = bindCombo (d, perfBox, "perf");
            addChildComponent (perfBox);
            addChildComponent (perfDetail);
            addChildComponent (perfKnob);
            perfHint.setFont (uiFont (12.0f));
            perfHint.setColour (juce::Label::textColourId, dd::creamDim);
            perfHint.setJustificationType (juce::Justification::topLeft);
            perfHint.setInterceptsMouseClicks (false, false);
            addChildComponent (perfHint);
            lockPerf.setButtonText ("Lock");
            lockPerf.setTooltip ("Keep this performance when you change the sound");
            lockPerfAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (d.state, "lockPerf", lockPerf);
            addChildComponent (lockPerf);

            // bass card
            bassModeAtt = bindCombo (d, bassMode, "bassMode");
            bassSoundAtt = bindCombo (d, bassSound, "bassSound");
            for (auto* c : { &bassMode, &bassSound }) addChildComponent (c);
            bassVol.bind ("bassVolume", "LEVEL");
            addChildComponent (bassVol);

            // FX card
            fxFocusAtt = bindCombo (d, fxFocus, "fxFocus");
            addChildComponent (fxFocus);
            addChildComponent (fxKnob);
            lockFx.setButtonText ("Lock");
            lockFx.setTooltip ("Keep these effects when you change the sound");
            lockFxAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (d.state, "lockFx", lockFx);
            addChildComponent (lockFx);

            // tempo + beat card
            tempoAtt = bindCombo (d, tempoSrc, "tempoSrc");
            beatAtt = bindCombo (d, beatBox, "beat");
            for (auto* c : { &tempoSrc, &beatBox }) addChildComponent (c);
            bpmKnob.bind ("bpm", "BPM");
            addChildComponent (bpmKnob);
            beatOn.setButtonText ("Play Beat");
            beatOn.setClickingTogglesState (true);
            beatOn.setWantsKeyboardFocus (false);
            beatOnAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (d.state, "beatOn", beatOn);
            addChildComponent (beatOn);

            // loop card
            addChildComponent (loopBtn);
            for (auto* b : { &overdub, &undo, &clear })
            {
                b->setWantsKeyboardFocus (false);
                addChildComponent (b);
            }
            overdub.setButtonText ("Overdub");
            undo.setButtonText ("Undo");
            clear.setButtonText ("Clear");
            overdub.onClick = [this] { dream.overdubButton(); };
            undo.onClick = [this] { dream.loopUndo(); };
            clear.onClick = [this] { dream.loopClear(); };
            loopLenAtt = bindCombo (d, loopLen, "loopLength");
            addChildComponent (loopLen);
            loopStatus.setFont (labelFont (11.0f, false));
            loopStatus.setColour (juce::Label::textColourId, dd::creamDim);
            loopStatus.setJustificationType (juce::Justification::centred);
            addChildComponent (loopStatus);

            volume.bind ("volume", "VOLUME");
            addChildComponent (volume);

            // ---- SOUND, EFFECTS, SETTINGS -----------------------------------------------------------------
            soundView.setViewedComponent (&soundContent, false);
            soundView.setScrollBarsShown (true, false);
            addChildComponent (soundView);
            for (auto* p : { &enginePanel, &analoguePanel, &fmPanel, &reedPanel, &sharedPanel }) soundContent.addAndMakeVisible (p);
            for (auto* l : { &engineTitle, &sharedTitle })
            {
                l->setFont (labelFont (12.0f));
                l->setColour (juce::Label::textColourId, dd::creamDim);
                soundContent.addAndMakeVisible (l);
            }
            sharedTitle.setText ("FILTER, ENVELOPES AND MOVEMENT", juce::dontSendNotification);

            fxView.setViewedComponent (&fxContent, false);
            fxView.setScrollBarsShown (true, false);
            addChildComponent (fxView);
            const std::vector<std::pair<const char*, juce::StringArray>> groups {
                { "DRIVE", { "drive", "driveTone" } }, { "TREMOLO", { "tremolo", "tremRate", "tremPan" } },
                { "PHASER", { "phaser", "phaserRate", "phaserFeedback" } }, { "FLANGER", { "flanger", "flangerRate", "flangerFeedback" } },
                { "CHORUS", { "chorus", "chorusRate", "chorusDepth" } }, { "ENSEMBLE", { "ensemble" } },
                { "DELAY", { "delay", "delayTime", "delayFeedback", "delayTone", "pingPong" } }, { "REVERB", { "reverb", "reverbSize", "reverbDamp" } },
            };
            for (auto& [title, ids] : groups)
            {
                auto* l = fxTitles.add (new juce::Label ({}, title));
                l->setFont (labelFont (12.0f));
                l->setColour (juce::Label::textColourId, dd::coral);
                fxContent.addAndMakeVisible (l);
                fxContent.addAndMakeVisible (fxPanels.add (new ParamPanel (d, ids, dd::coral)));
            }

            settingsView.setViewedComponent (&settingsContent, false);
            settingsView.setScrollBarsShown (true, false);
            addChildComponent (settingsView);
            settingsContent.addAndMakeVisible (settingsPanel);
            settingsHelp.setText ("On a MIDI keyboard the chord buttons are the eight white keys from the octave you pick (C2 to C3 by default): "
                                  "C = Maj, D = Min, E = Sus, F = Dim, G = 6, A = m7, B = M7, C = 9. Hold them with your left hand and play roots anywhere else "
                                  "with your right; they're recorded with the notes, so a recorded part plays its chords back. "
                                  "Chord Position \"Stay in One Place\" keeps every chord around the centre note, so progressions move smoothly.",
                                  juce::dontSendNotification);
            settingsHelp.setFont (uiFont (13.0f));
            settingsHelp.setColour (juce::Label::textColourId, dd::creamDim);
            settingsHelp.setJustificationType (juce::Justification::topLeft);
            settingsContent.addAndMakeVisible (settingsHelp);

            refreshSound();
            setResizable (true, false);
            setResizeLimits (900, 600, 2000, 1400);
            setSize (1120, 700);
            layoutPages();
            startTimerHz (30);
        }

        ~DaydreamEditor() override { stopTimer(); }

        void paint (juce::Graphics& g) override
        {
            juce::ColourGradient grad (dd::bodyLight, 0.0f, 0.0f, dd::body, 0.0f, (float) getHeight(), false);
            g.setGradientFill (grad);
            g.fillAll();
            g.setColour (dd::coral);
            g.fillRect (getLocalBounds().removeFromTop (3));
            if (page != 0) return;
            for (auto& [r, t] : cards) paintCard (g, r, t);
            g.setColour (dd::creamDim);
            g.setFont (labelFont (11.0f));
            g.drawText ("CHORD", typeLabel, juce::Justification::centred);
            g.drawText ("COLOUR", colourLabel, juce::Justification::centred);
            g.drawText ("ROOT", rootLabel, juce::Justification::centredLeft);
        }

        void resized() override { layoutPages(); }

    private:
        Daydream& dream;
        EditorHeader header;
        juce::TextButton playTab { "PLAY" }, soundTab { "SOUND" }, fxTab { "EFFECTS" }, settingsTab { "SETTINGS" };
        int page = 0;

        juce::OwnedArray<ChordButton> chordButtons;
        Screen screen;
        RootKeys keys;
        LoopButton loopBtn;
        Ladder voicing, bassLadder;
        juce::ToggleButton keyMode, followKey, lockPerf, lockFx;
        juce::ComboBox keyBox, scaleBox, perfBox, perfDetail, bassMode, bassSound, fxFocus, tempoSrc, beatBox, loopLen;
        juce::Slider transpose;
        juce::TextButton prevSound, nextSound, overdub, undo, clear, beatOn;
        juce::Label soundName, soundInfo, loopStatus, perfHint;
        SmallKnob perfKnob, fxKnob, bassVol, bpmKnob, volume;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> keyModeAtt, followAtt, lockPerfAtt, lockFxAtt, beatOnAtt;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> keyAtt, scaleAtt, perfAtt, perfDetailAtt, bassModeAtt, bassSoundAtt,
                                                                                 fxFocusAtt, tempoAtt, beatAtt, loopLenAtt;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> transposeAtt;
        juce::String perfDetailId;
        std::vector<std::pair<juce::Rectangle<int>, juce::String>> cards;
        juce::Rectangle<int> typeLabel, colourLabel, rootLabel;

        juce::Viewport soundView, fxView, settingsView;
        juce::Component soundContent, fxContent, settingsContent;
        ParamPanel analoguePanel, fmPanel, reedPanel, sharedPanel, enginePanel, settingsPanel;
        juce::Label engineTitle, sharedTitle, settingsHelp;
        juce::OwnedArray<juce::Label> fxTitles;
        juce::OwnedArray<ParamPanel> fxPanels;
        int shownEngine = -1, shownPerf = -1, shownFocus = -1;

        void stepSound (int dir)
        {
            const int n = dream.getNumPrograms();
            dream.setCurrentProgram ((dream.getCurrentProgram() + dir + n) % n);
            header.refreshPreset();
            refreshSound();
        }

        void refreshSound()
        {
            const int p = dream.getCurrentProgram();
            soundName.setText (dream.getProgramName (p), juce::dontSendNotification);
            soundInfo.setText (Daydream::presetCategory (p) + "  -  " + daydream::engineNames()[(int) dream.param ("engine")] + "  -  "
                               + juce::String (p + 1) + " of " + juce::String (dream.getNumPrograms()), juce::dontSendNotification);
        }

        void refreshPerf()
        {
            const int perf = (int) dream.param ("perf");
            if (perf == shownPerf) return;
            shownPerf = perf;
            // the performance knob and detail box follow the mode
            juce::String knobId, knobName, comboId;
            switch (perf)
            {
                case Daydream::perfStrum: case Daydream::perfStrum2: knobId = "strumMs"; knobName = "SPEED"; comboId = "strumDir"; break;
                case Daydream::perfSlop: knobId = "slop"; knobName = "SLOP"; break;
                case Daydream::perfArp: case Daydream::perfArp2: knobId = "gate"; knobName = "GATE"; comboId = "arpRate"; break;
                case Daydream::perfPattern: knobId = "swing"; knobName = "SWING"; comboId = "pattern"; break;
                case Daydream::perfHarp: knobId = "harpMs"; knobName = "SPEED"; comboId = "harpOctaves"; break;
                default: break;
            }
            perfKnob.setVisible (page == 0 && knobId.isNotEmpty());
            if (knobId.isNotEmpty()) perfKnob.bind (knobId, knobName);
            perfDetailAtt.reset();
            perfDetail.clear (juce::dontSendNotification);
            perfDetailId = comboId;
            if (comboId.isNotEmpty()) perfDetailAtt = bindCombo (dream, perfDetail, comboId);
            perfDetail.setVisible (page == 0 && comboId.isNotEmpty());
            static const char* hints[] = { "The whole chord, for as long as you hold it.", "", "", "Each note a little late and a little different, like a loose strum.",
                                           "", "", "", "" };
            perfHint.setText (hints[juce::jlimit (0, 7, perf)], juce::dontSendNotification);
            perfHint.setVisible (page == 0 && comboId.isEmpty());
            if (! perfArea.isEmpty()) layoutPerfCard();
        }

        void refreshFocus()
        {
            const int f = (int) dream.param ("fxFocus");
            if (f == shownFocus) return;
            shownFocus = f;
            static const char* ids[] = { "drive", "tremolo", "phaser", "flanger", "chorus", "ensemble", "delay", "reverb" };
            fxKnob.bind (ids[juce::jlimit (0, 7, f)], "AMOUNT");
        }

        juce::Rectangle<int> perfArea;
        void layoutPerfCard()
        {
            auto a = perfArea;
            perfBox.setBounds (a.removeFromTop (26));
            a.removeFromTop (6);
            auto lockRow = a.removeFromBottom (22);
            lockPerf.setBounds (lockRow.removeFromLeft (80));
            if (perfKnob.isVisible()) perfKnob.setBounds (a.removeFromRight (juce::jmin (70, a.getWidth() / 2)));
            perfHint.setBounds (a.withTrimmedRight (4));
            perfDetail.setBounds (a.removeFromTop (26).withTrimmedRight (6));
        }

        void layoutSoundPage (int width)
        {
            const int engine = (int) dream.param ("engine");
            shownEngine = engine;
            analoguePanel.setVisible (engine == 0);
            fmPanel.setVisible (engine == 1);
            reedPanel.setVisible (engine == 2);
            ParamPanel& eng = engine == 0 ? analoguePanel : engine == 1 ? fmPanel : reedPanel;
            engineTitle.setText (daydream::engineNames()[engine].toUpperCase() + " ENGINE", juce::dontSendNotification);
            int y = 0;
            const int w = width - 16;
            enginePanel.setBounds (0, y, w, enginePanel.heightFor (w)); y += enginePanel.getHeight() + 6;
            engineTitle.setBounds (4, y, w, 18); y += 20;
            eng.setBounds (0, y, w, eng.heightFor (w)); y += eng.getHeight() + 8;
            sharedTitle.setBounds (4, y, w, 18); y += 20;
            sharedPanel.setBounds (0, y, w, sharedPanel.heightFor (w)); y += sharedPanel.getHeight() + 10;
            soundContent.setSize (width - 4, y);
        }

        void layoutPages()
        {
            header.setBounds (getLocalBounds().removeFromTop (36).reduced (10, 0));
            playTab.setToggleState (page == 0, juce::dontSendNotification);
            soundTab.setToggleState (page == 1, juce::dontSendNotification);
            fxTab.setToggleState (page == 2, juce::dontSendNotification);
            settingsTab.setToggleState (page == 3, juce::dontSendNotification);

            const bool play = page == 0;
            for (auto* c : std::initializer_list<juce::Component*> { &screen, &keys, &voicing, &bassLadder, &keyMode, &followKey, &keyBox, &scaleBox, &transpose,
                                                                     &prevSound, &nextSound, &soundName, &soundInfo, &perfBox, &perfHint, &lockPerf, &bassMode,
                                                                     &bassSound, &bassVol, &fxFocus, &fxKnob, &lockFx, &tempoSrc, &beatBox, &bpmKnob, &beatOn,
                                                                     &loopBtn, &overdub, &undo, &clear, &loopLen, &loopStatus, &volume })
                c->setVisible (play);
            for (auto* b : chordButtons) b->setVisible (play);
            shownPerf = -1;
            refreshPerf();
            refreshFocus();
            soundView.setVisible (page == 1);
            fxView.setVisible (page == 2);
            settingsView.setVisible (page == 3);

            auto r = getLocalBounds().withTrimmedTop (42).reduced (12, 6);
            cards.clear();
            if (page == 1)
            {
                soundView.setBounds (r);
                layoutSoundPage (r.getWidth());
            }
            else if (page == 2)
            {
                fxView.setBounds (r);
                const int colW = (r.getWidth() - 30) / 2;
                int y[2] = { 0, 0 };
                for (int i = 0; i < fxPanels.size(); ++i)
                {
                    const int col = y[0] <= y[1] ? 0 : 1;
                    const int x = col * (colW + 14);
                    fxTitles[i]->setBounds (x + 4, y[col], colW, 18);
                    const int h = fxPanels[i]->heightFor (colW);
                    fxPanels[i]->setBounds (x, y[col] + 20, colW, h);
                    y[col] += 20 + h + 12;
                }
                fxContent.setSize (r.getWidth() - 4, juce::jmax (y[0], y[1]));
            }
            else if (page == 3)
            {
                settingsView.setBounds (r);
                const int w = r.getWidth() - 16;
                settingsHelp.setBounds (4, 0, w - 8, 70);
                settingsPanel.setBounds (0, 76, w, settingsPanel.heightFor (w));
                settingsContent.setSize (r.getWidth() - 4, settingsPanel.getBottom() + 10);
            }
            else
            {
                // bottom row of cards
                auto bottom = r.removeFromBottom (juce::jlimit (150, 200, r.getHeight() * 30 / 100));
                r.removeFromBottom (10);
                const int gap = 8;
                const float weights[] = { 1.25f, 1.25f, 1.05f, 0.85f, 1.15f, 1.15f, 0.5f };
                float total = 0; for (float w : weights) total += w;
                const int avail = bottom.getWidth() - gap * 6;
                juce::Rectangle<int> c[7];
                int x = bottom.getX();
                for (int i = 0; i < 7; ++i)
                {
                    const int w = (int) (avail * weights[i] / total);
                    c[i] = juce::Rectangle<int> (x, bottom.getY(), w, bottom.getHeight());
                    x += w + gap;
                }
                const char* titles[] = { "SOUND", "PERFORMANCE", "BASS", "FX", "TEMPO & BEAT", "LOOP", "" };
                for (int i = 0; i < 7; ++i) cards.push_back ({ c[i], titles[i] });

                auto inner = [] (juce::Rectangle<int> card) { return card.reduced (10, 8).withTrimmedTop (18); };
                {
                    auto a = inner (c[0]);
                    auto row = a.removeFromTop (juce::jmin (54, a.getHeight() / 2));
                    prevSound.setBounds (row.removeFromLeft (28).withSizeKeepingCentre (28, 30));
                    nextSound.setBounds (row.removeFromRight (28).withSizeKeepingCentre (28, 30));
                    soundName.setBounds (row);
                    soundInfo.setBounds (a);
                }
                perfArea = inner (c[1]);
                layoutPerfCard();
                {
                    auto a = inner (c[2]);
                    bassMode.setBounds (a.removeFromTop (26));
                    a.removeFromTop (6);
                    bassSound.setBounds (a.removeFromTop (26));
                    a.removeFromTop (4);
                    bassVol.setBounds (a.withSizeKeepingCentre (juce::jmin (70, a.getWidth()), a.getHeight()));
                }
                {
                    auto a = inner (c[3]);
                    fxFocus.setBounds (a.removeFromTop (26));
                    a.removeFromTop (4);
                    lockFx.setBounds (a.removeFromBottom (22).removeFromLeft (80));
                    fxKnob.setBounds (a.withSizeKeepingCentre (juce::jmin (76, a.getWidth()), a.getHeight()));
                }
                {
                    auto a = inner (c[4]);
                    tempoSrc.setBounds (a.removeFromTop (26));
                    a.removeFromTop (6);
                    beatBox.setBounds (a.removeFromTop (26));
                    a.removeFromTop (6);
                    bpmKnob.setBounds (a.removeFromLeft (juce::jmin (66, a.getWidth() / 2)));
                    beatOn.setBounds (a.withSizeKeepingCentre (a.getWidth() - 8, 28));
                    beatOn.setButtonText (beatOn.getWidth() >= 90 ? "Play Beat" : "Beat");
                }
                {
                    auto a = inner (c[5]);
                    loopLen.setBounds (a.removeFromTop (26));
                    a.removeFromTop (4);
                    loopStatus.setBounds (a.removeFromBottom (16));
                    auto left = a.removeFromLeft (juce::jmin (a.getHeight(), a.getWidth() * 45 / 100));
                    loopBtn.setBounds (left);
                    a.removeFromLeft (6);
                    const int bh = juce::jmin (26, (a.getHeight() - 8) / 3);
                    overdub.setBounds (a.removeFromTop (bh)); a.removeFromTop (4);
                    undo.setBounds (a.removeFromTop (bh)); a.removeFromTop (4);
                    clear.setBounds (a.removeFromTop (bh));
                }
                volume.setBounds (inner (c[6]).withTrimmedTop (-10));
                volume.bind ("volume", c[6].getWidth() >= 80 ? "VOLUME" : "VOL");

                // top: buttons | screen + ladders | key + keyboard
                auto left = r.removeFromLeft (juce::jmin (250, r.getWidth() / 4));
                r.removeFromLeft (14);
                auto right = r.removeFromRight (juce::jmin (330, r.getWidth() * 3 / 10));
                r.removeFromRight (14);

                auto labels = left.removeFromTop (16);
                typeLabel = labels.removeFromLeft (labels.getWidth() / 2);
                colourLabel = labels;
                const int bw = left.getWidth() / 2, bh = left.getHeight() / 4;
                for (int b = 0; b < 8; ++b)
                    chordButtons[b]->setBounds (left.getX() + (b / 4) * bw, left.getY() + (b % 4) * bh, bw, bh);

                auto ladders = r.removeFromBottom (76);
                voicing.setBounds (ladders.removeFromTop (36));
                ladders.removeFromTop (4);
                bassLadder.setBounds (ladders.removeFromTop (36));
                r.removeFromBottom (8);
                screen.setBounds (r);

                auto keyRow = right.removeFromTop (28);
                keyMode.setBounds (keyRow.removeFromLeft (100));
                followKey.setBounds (keyRow);
                right.removeFromTop (6);
                auto keyRow2 = right.removeFromTop (28);
                keyBox.setBounds (keyRow2.removeFromLeft (64));
                keyRow2.removeFromLeft (6);
                if (keyRow2.getWidth() >= 250)
                {
                    transpose.setBounds (keyRow2.removeFromRight (100));
                    keyRow2.removeFromRight (6);
                    scaleBox.setBounds (keyRow2);
                }
                else
                {
                    scaleBox.setBounds (keyRow2);
                    right.removeFromTop (6);
                    auto keyRow3 = right.removeFromTop (26);
                    transpose.setBounds (keyRow3.removeFromRight (100));
                }
                right.removeFromTop (10);
                rootLabel = right.removeFromTop (16);
                keys.setBounds (right);
            }
            repaint();
        }

        void timerCallback() override
        {
            if (page == 0)
            {
                screen.repaint();
                loopBtn.repaint();
                for (auto* b : chordButtons) b->repaint();
                refreshPerf();
                refreshFocus();
                if (soundName.getText() != dream.getProgramName (dream.getCurrentProgram())) refreshSound();
                const char* states[] = { "Press to record", "Waiting for your first note", "Count-in...", "Recording", "Playing", "Overdubbing", "Paused" };
                const auto st = dream.getLoopState();
                juce::String s = states[(int) st];
                if (dream.getLoopLayers() > 1) s << "  (" << dream.getLoopLayers() << " layers)";
                loopStatus.setText (s, juce::dontSendNotification);
                bpmKnob.setEnabled ((int) dream.param ("tempoSrc") == 1);
                overdub.setEnabled (st == Daydream::loopPlaying || st == Daydream::loopOverdub);
                overdub.setToggleState (st == Daydream::loopOverdub, juce::dontSendNotification);
                undo.setEnabled (dream.getLoopLayers() > 0);
                clear.setEnabled (st != Daydream::loopIdle);
            }
            else if (page == 1 && (int) dream.param ("engine") != shownEngine)
                layoutSoundPage (soundView.getWidth());
        }
    };
}

void installDaydreamEditor()
{
    Daydream::editorFactory = [] (Daydream& d) -> juce::AudioProcessorEditor* { return new DaydreamEditor (d); };
}

} // namespace wis::daw
