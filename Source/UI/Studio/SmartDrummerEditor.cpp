#include "EditorParts.h"
#include "Daw/Instruments/SmartDrummer.h"
#include "Daw/Instruments/RoomIr.h"

namespace wis::daw
{

/*  Smart Drummer's window: a DRUMMER page (style, how it plays, the song as it heard it with its sections)
    and a KIT page (every piece of the kit: where it comes from, tune, decay, level, pan, mute; then the
    room, punch, drive and tone). */

namespace
{
    const juce::Colour drumAccent { 0xffff9f43 };

    juce::Colour levelColour (int level)
    {
        switch (level)
        {
            case 0:  return juce::Colour (0xff5b8def);
            case 1:  return juce::Colour (0xff3fbf8f);
            case 2:  return drumAccent;
            case 3:  return juce::Colour (0xffef5b5b);
            default: return theme::textFaint;
        }
    }
    juce::String levelName (int level)
    {
        switch (level) { case 0: return "quiet"; case 1: return "groove"; case 2: return "full"; case 3: return "peak"; default: return "rest"; }
    }

    // ---- the song as the drummer heard it --------------------------------------------------------------
    class SongView : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit SongView (SmartDrummer& d) : drummer (d)
        {
            setTooltip ("The song as the drummer heard it: how loud and busy each bar is, and its sections. "
                        "Click a section to choose how the drummer plays it: quiet, groove, full, peak or rest (click on to get back to the song's own).");
        }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            g.setColour (theme::panelRaised);
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (theme::outline);
            g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
            if (! drummer.hasSongMap())
            {
                g.setColour (theme::textDim);
                g.setFont (uiFont (14.0f));
                g.drawFittedText ("Press Play the Song: the drummer listens to everything else in the song (MIDI and audio: where the accents and the bass land, "
                                  "the swing, the quiet and loud parts) and plays along, one clip per section.",
                                  getLocalBounds().reduced (24, 10), juce::Justification::centred, 3);
                return;
            }
            const auto& m = drummer.getSongMap();
            if (m.isEmpty())
            {
                g.setColour (theme::textDim);
                g.setFont (uiFont (14.0f));
                g.drawFittedText ("There's nothing else in the song yet, so the drummer played 8 bars of the style. Record or add some parts, then Play the Song again.",
                                  getLocalBounds().reduced (24, 10), juce::Justification::centred, 2);
                return;
            }
            area = r.reduced (8.0f, 6.0f);
            const float barW = area.getWidth() / (float) juce::jmax (1, m.bars);
            const auto strip = area.withHeight (30.0f);
            const auto graph = area.withTrimmedTop (36.0f);

            // energy per bar, and where the low end hits (dots)
            for (int b = 0; b < m.bars; ++b)
            {
                const auto& bar = m.bar[(size_t) b];
                const float x = graph.getX() + b * barW;
                const float h = graph.getHeight() * (0.06f + 0.94f * bar.energy);
                g.setColour ((bar.active ? drumAccent : theme::textFaint).withAlpha (bar.active ? 0.35f + 0.4f * bar.density : 0.15f));
                g.fillRect (x + 0.5f, graph.getBottom() - h, juce::jmax (1.0f, barW - 1.0f), h);
            }
            // sections
            sectionRects.clear();
            for (auto& s : m.sections)
            {
                const auto rect = juce::Rectangle<float> (strip.getX() + s.startBar * barW, strip.getY(), s.bars * barW, strip.getHeight()).reduced (1.0f, 0.0f);
                const int over = drummer.getSectionLevel (s.startBar);
                const int level = over >= 0 ? over : (s.silent ? 4 : juce::jlimit (0, 3, s.intensity + juce::roundToInt (drummer.param ("energy") * 1.4f)));
                const auto c = levelColour (level);
                g.setColour (c.withAlpha (0.28f));
                g.fillRoundedRectangle (rect, 4.0f);
                g.setColour (c);
                g.drawRoundedRectangle (rect, 4.0f, over >= 0 ? 2.0f : 1.0f);
                g.setFont (uiFont (12.0f, true));
                g.setColour (theme::text);
                g.drawFittedText (s.name + "  -  " + levelName (level) + (over >= 0 ? " (yours)" : ""), rect.reduced (5.0f, 2.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
                sectionRects.push_back ({ rect, s.startBar });
            }
            // bar numbers
            g.setColour (theme::textFaint);
            g.setFont (uiFont (10.0f));
            const int every = barW > 22.0f ? 4 : barW > 10.0f ? 8 : 16;
            for (int b = 0; b < m.bars; b += every)
                g.drawText (juce::String (b + 1), juce::Rectangle<float> (graph.getX() + b * barW + 2.0f, graph.getY(), 40.0f, 12.0f), juce::Justification::topLeft);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            for (auto& [rect, bar] : sectionRects)
                if (rect.contains (e.position))
                {
                    // auto -> quiet -> verse -> chorus -> peak -> rest -> auto
                    const int cur = drummer.getSectionLevel (bar);
                    const int next = cur < 0 ? 0 : cur >= 4 ? -1 : cur + 1;
                    drummer.setSectionLevel (bar, next);
                    repaint();
                    if (onChanged) onChanged();
                    return;
                }
        }

        std::function<void()> onChanged;

    private:
        SmartDrummer& drummer;
        juce::Rectangle<float> area;
        std::vector<std::pair<juce::Rectangle<float>, int>> sectionRects;
    };

    // ---- one piece of the kit -----------------------------------------------------------------------------
    class PieceRow : public juce::Component
    {
    public:
        PieceRow (SmartDrummer& d, int p) : drummer (d), piece (p)
        {
            const juce::String id = SmartDrummer::pieceId (p);
            play.setButtonText (SmartDrummer::pieceName (p));
            play.setTooltip ("Hear the " + SmartDrummer::pieceName (p).toLowerCase());
            play.onClick = [this] { drummer.audition (piece, 0.85f); };
            play.setWantsKeyboardFocus (false);
            addAndMakeVisible (play);
            juce::StringArray src { "From the kit" };
            src.addArray (SmartDrummer::sourceNames());
            source.addItemList (src, 1);
            source.setTooltip ("Which kit this piece comes from");
            addAndMakeVisible (source);
            srcAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (d.state, id + "Src", source);
            source.onChange = [this] { juce::Timer::callAfterDelay (350, [safe = juce::Component::SafePointer<PieceRow> (this)] { if (safe != nullptr) safe->drummer.audition (safe->piece, 0.85f); }); };
            const char* knobIds[] = { "Tune", "Decay", "Level", "Pan" };
            for (int k = 0; k < 4; ++k)
            {
                auto* s = knobs.add (new juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox));
                s->setPopupDisplayEnabled (true, true, nullptr);
                s->setColour (juce::Slider::rotarySliderFillColourId, drumAccent);
                s->setColour (juce::Slider::thumbColourId, drumAccent);
                s->setTooltip (knobIds[k]);
                addAndMakeVisible (s);
                attach.add (new juce::AudioProcessorValueTreeState::SliderAttachment (d.state, id + knobIds[k], *s));
                if (auto* prm = d.state.getParameter (id + knobIds[k]))
                    s->setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
            }
            mute.setButtonText ("M");
            mute.setTooltip ("Mute this piece");
            mute.setClickingTogglesState (true);
            mute.setColour (juce::TextButton::buttonOnColourId, theme::bad);
            addAndMakeVisible (mute);
            muteAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (d.state, id + "Mute", mute);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (0, 2);
            play.setBounds (r.removeFromLeft (112));
            r.removeFromLeft (6);
            source.setBounds (r.removeFromLeft (juce::jmax (120, r.getWidth() - 4 * 40 - 34)).reduced (0, 2));
            r.removeFromLeft (4);
            for (auto* k : knobs) k->setBounds (r.removeFromLeft (40));
            mute.setBounds (r.removeFromLeft (30).reduced (2, 3));
        }

    private:
        SmartDrummer& drummer;
        int piece;
        juce::TextButton play, mute;
        juce::ComboBox source;
        juce::OwnedArray<juce::Slider> knobs;
        juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> srcAttach;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> muteAttach;
    };

    // ---- the editor --------------------------------------------------------------------------------------
    class SmartDrummerEditor : public juce::AudioProcessorEditor, private juce::ChangeListener, private juce::Timer
    {
    public:
        explicit SmartDrummerEditor (SmartDrummer& d)
            : AudioProcessorEditor (d), drummer (d), header (d, "Smart Drummer"),
              playing (d, { "complexity", "energy", "follow", "fills", "humanize", "feel", "swing", "swingAuto", "timeFeel", "cymbals", "endHit", "autoWrite" }, drumAccent),
              bus (d, { "kit", "matchKit", "room", "roomMix", "punch", "drive", "tone", "volume" }, drumAccent),
              song (d)
        {
            header.addButton (drummerTab, 96);
            header.addButton (kitTab, 70);
            addAndMakeVisible (header);
            header.onPresetChanged = [this] { refreshStyles(); written = settingsKey(); };
            for (auto* b : { &drummerTab, &kitTab })
            {
                b->setClickingTogglesState (false);
                b->setColour (juce::TextButton::buttonOnColourId, drumAccent.withAlpha (0.85f));
                b->setWantsKeyboardFocus (false);
            }
            drummerTab.onClick = [this] { showKit = false; drummer.editorPage = 0; layoutPages(); };
            kitTab.onClick = [this] { showKit = true; drummer.editorPage = 1; layoutPages(); };
            showKit = drummer.editorPage == 1;

            const auto names = drummer::styleNames();
            for (int i = 0; i < names.size(); ++i)
            {
                auto* b = styleButtons.add (new juce::TextButton (names[i]));
                b->setClickingTogglesState (false);
                b->setColour (juce::TextButton::buttonOnColourId, drumAccent);
                b->setColour (juce::TextButton::textColourOnId, juce::Colours::black);
                b->setTooltip (drummer::styles()[(size_t) i].description);
                b->setWantsKeyboardFocus (false);
                b->onClick = [this, i] { pickStyle (i); };
                addChildComponent (b);
            }
            styleInfo.setFont (uiFont (14.0f));
            styleInfo.setColour (juce::Label::textColourId, theme::textDim);
            styleInfo.setJustificationType (juce::Justification::topLeft);
            addChildComponent (styleInfo);

            playSong.setButtonText ("Play the Song");
            playSong.setTooltip ("Listen to everything else in the song and write a drum part for it (one clip per section, on this track)");
            playSong.setColour (juce::TextButton::buttonColourId, drumAccent);
            playSong.setColour (juce::TextButton::textColourOffId, juce::Colours::black);
            playSong.onClick = [this] { drummer.playSong (true); written = settingsKey(); };
            anotherTake.setButtonText ("Another Take");
            anotherTake.setTooltip ("Same style and settings, a different performance");
            anotherTake.onClick = [this]
            {
                drummer.setParam ("seed", (float) (juce::roundToInt (drummer.param ("seed")) % 9999 + 1));
                if (drummer.hasSongMap()) drummer.rewrite(); else drummer.playSong (true);
                written = settingsKey();
            };
            for (auto* b : { &playSong, &anotherTake }) { b->setWantsKeyboardFocus (false); addChildComponent (b); }
            status.setFont (uiFont (12.5f));
            status.setColour (juce::Label::textColourId, theme::textDim);
            status.setJustificationType (juce::Justification::centredLeft);
            addChildComponent (status);
            addChildComponent (playing);
            addChildComponent (song);
            song.onChanged = [this] { if (drummer.param ("autoWrite") > 0.5f) drummer.rewrite(); written = settingsKey(); };

            for (int p = 0; p < SmartDrummer::numPieces; ++p) addChildComponent (rows.add (new PieceRow (drummer, p)));
            addChildComponent (bus);
            resetKit.setButtonText ("Reset the Kit to the Style");
            resetKit.setTooltip ("Puts back the kit, room and tone that go with the current style");
            resetKit.onClick = [this] { drummer.applyStyleKit ((int) drummer.param ("style")); };
            resetKit.setWantsKeyboardFocus (false);
            addChildComponent (resetKit);
            kitHint.setText ("Click a piece's name to hear it. Mix and match: a kick from the Power Kit, a snare from the Jazz Kit, hats from a drum machine. "
                             "Knobs: tune, decay, level, pan.", juce::dontSendNotification);
            kitHint.setFont (uiFont (12.0f));
            kitHint.setColour (juce::Label::textColourId, theme::textDim);
            addChildComponent (kitHint);

            drummer.addChangeListener (this);
            changeListenerCallback (nullptr);
            written = settingsKey();
            refreshStyles();
            setResizable (true, false);
            setResizeLimits (720, 520, 2000, 1400);
            setSize (1080, 660);
            layoutPages();
            startTimerHz (5);
        }

        ~SmartDrummerEditor() override
        {
            stopTimer();
            drummer.removeChangeListener (this);
        }

        void paint (juce::Graphics& g) override
        {
            paintEditorBackground (g, getLocalBounds(), drumAccent);
            if (showKit)
            {
                g.setColour (theme::textFaint);
                g.setFont (uiFont (11.0f));
                for (auto [x, w] : columnHeads)
                {
                    juce::Rectangle<int> r (x, rows[0]->getY() - 16, w, 14);
                    g.drawText ("PIECE", r.removeFromLeft (112), juce::Justification::centredLeft);
                    r.removeFromLeft (6);
                    g.drawText ("FROM", r.removeFromLeft (r.getWidth() - 4 * 40 - 34), juce::Justification::centredLeft);
                    r.removeFromLeft (4);
                    for (auto* t : { "TUNE", "DECAY", "LEVEL", "PAN" }) g.drawText (t, r.removeFromLeft (40), juce::Justification::centred);
                }
            }
        }

        void resized() override { layoutPages(); }

    private:
        SmartDrummer& drummer;
        EditorHeader header;
        juce::TextButton drummerTab { "DRUMMER" }, kitTab { "KIT" }, playSong, anotherTake, resetKit;
        juce::OwnedArray<juce::TextButton> styleButtons;
        juce::Label styleInfo, status, kitHint;
        ParamPanel playing, bus;
        SongView song;
        juce::OwnedArray<PieceRow> rows;
        std::vector<std::pair<int, int>> columnHeads;
        bool showKit = false;
        juce::String written, pendingKey;
        int stableTicks = 0;

        void layoutPages()
        {
            header.setBounds (getLocalBounds().removeFromTop (36).reduced (10, 0));
            drummerTab.setToggleState (! showKit, juce::dontSendNotification);
            kitTab.setToggleState (showKit, juce::dontSendNotification);
            for (auto* c : std::initializer_list<juce::Component*> { &styleInfo, &playSong, &anotherTake, &status, &playing, &song })
                c->setVisible (! showKit);
            for (auto* b : styleButtons) b->setVisible (! showKit);
            for (auto* r : rows) r->setVisible (showKit);
            for (auto* c : std::initializer_list<juce::Component*> { &bus, &resetKit, &kitHint }) c->setVisible (showKit);

            auto r = getLocalBounds().withTrimmedTop (40).reduced (12, 6);
            if (! showKit)
            {
                auto bottom = r.removeFromBottom (juce::jlimit (150, 320, (int) (r.getHeight() * 0.44f)));
                auto actions = bottom.removeFromTop (34);
                playSong.setBounds (actions.removeFromLeft (170));
                actions.removeFromLeft (8);
                anotherTake.setBounds (actions.removeFromLeft (130));
                actions.removeFromLeft (12);
                status.setBounds (actions);
                bottom.removeFromTop (6);
                song.setBounds (bottom);
                r.removeFromBottom (8);

                auto left = r.removeFromLeft (juce::jmin (420, r.getWidth() / 2));
                const int cols = 3, rowsN = (styleButtons.size() + cols - 1) / cols;
                const int bh = juce::jlimit (24, 34, (left.getHeight() - 60) / rowsN);
                auto grid = left.removeFromTop (bh * rowsN);
                const int bw = grid.getWidth() / cols;
                for (int i = 0; i < styleButtons.size(); ++i)
                    styleButtons[i]->setBounds (grid.getX() + (i % cols) * bw, grid.getY() + (i / cols) * bh, bw - 4, bh - 4);
                left.removeFromTop (6);
                styleInfo.setBounds (left);
                r.removeFromLeft (12);
                playing.setBounds (r);
            }
            else
            {
                auto top = r.removeFromBottom (juce::jmin (bus.heightFor (r.getWidth() - 200), 220));
                kitHint.setBounds (r.removeFromTop (22));
                r.removeFromTop (20);
                const int perCol = (SmartDrummer::numPieces + 1) / 2;
                const int colW = (r.getWidth() - 16) / 2;
                const int rowH = juce::jlimit (26, 36, r.getHeight() / perCol);
                columnHeads.clear();
                for (int p = 0; p < rows.size(); ++p)
                {
                    const int col = p / perCol, row = p % perCol;
                    rows[p]->setBounds (r.getX() + col * (colW + 16), r.getY() + row * rowH, colW, rowH);
                }
                columnHeads.push_back ({ r.getX(), colW });
                columnHeads.push_back ({ r.getX() + colW + 16, colW });
                resetKit.setBounds (top.removeFromRight (190).withSizeKeepingCentre (190, 30));
                top.removeFromRight (10);
                bus.setBounds (top);
            }
            repaint();
        }

        void refreshStyles()
        {
            const int s = (int) drummer.param ("style");
            for (int i = 0; i < styleButtons.size(); ++i) styleButtons[i]->setToggleState (i == s, juce::dontSendNotification);
            styleInfo.setText (drummer::styles()[(size_t) juce::jlimit (0, (int) drummer::styles().size() - 1, s)].description, juce::dontSendNotification);
        }

        void pickStyle (int i)
        {
            drummer.setParam ("style", (float) i);
            if (drummer.param ("matchKit") > 0.5f) drummer.applyStyleKit (i);
            refreshStyles();
            if (drummer.hasSongMap()) { drummer.rewrite(); written = settingsKey(); }
        }

        /** Everything that changes the part (not the kit). */
        juce::String settingsKey() const
        {
            juce::String k;
            for (auto id : { "style", "complexity", "energy", "follow", "fills", "swingAuto", "swing", "humanize", "feel", "timeFeel", "cymbals", "endHit", "seed" })
                k << juce::String (drummer.param (id), 3) << ",";
            return k;
        }

        void changeListenerCallback (juce::ChangeBroadcaster*) override
        {
            status.setText (drummer.getStatus(), juce::dontSendNotification);
            playSong.setEnabled (! drummer.isBusy());
            playSong.setButtonText (drummer.isBusy() ? "Listening..." : "Play the Song");
            song.repaint();
        }

        void timerCallback() override
        {
            refreshStyles();
            // turn a knob and the part follows (once it's settled for a moment)
            const auto key = settingsKey();
            if (key != written && drummer.param ("autoWrite") > 0.5f && drummer.hasSongMap() && ! drummer.isBusy())
            {
                if (key != pendingKey) { pendingKey = key; stableTicks = 0; }
                else if (++stableTicks >= 2) { drummer.rewrite(); written = key; }
            }
        }
    };
}

void installSmartDrummerEditor()
{
    SmartDrummer::editorFactory = [] (SmartDrummer& d) -> juce::AudioProcessorEditor* { return new SmartDrummerEditor (d); };
}

} // namespace wis::daw
