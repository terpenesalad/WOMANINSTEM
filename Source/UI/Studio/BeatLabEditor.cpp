#include "EditorParts.h"
#include "Daw/Instruments/BeatLab.h"

namespace wis::daw
{

namespace
{
    bool isAudioPath (const juce::String& path) { return juce::File (path).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg;m4a"); }

    juce::Colour laneColour (int lane)
    {
        static const juce::uint32 c[] = { 0xffff4d8d, 0xffffa34d, 0xfffbd24d, 0xff7ee06a, 0xff4dd8c8, 0xff4da3ff, 0xff9a7cff, 0xffe07cff };
        return juce::Colour (c[lane % 8]);
    }

    enum EditMode { modeTrig = 0, modeVelocity, modeRatchet, modePitch, modeProb, modeNudge };
}

class BeatLabEditor;

// =====================================================================================================
//  Lane header: LED, name (click = hear it), mute / solo; drop audio files here
// =====================================================================================================
class LaneStrip : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    LaneStrip (BeatLabEditor& o, int l);
    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    bool isInterestedInFileDrag (const juce::StringArray& files) override { return files.size() > 0 && isAudioPath (files[0]); }
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;

    float flash = 0.0f;
private:
    BeatLabEditor& owner;
    const int lane;
    bool dragOver = false;
    juce::TextButton mute { "M" }, solo { "S" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> muteA, soloA;
};

// =====================================================================================================
//  The step grid
// =====================================================================================================
class StepGrid : public juce::Component
{
public:
    explicit StepGrid (BeatLabEditor& o) : owner (o) {}
    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent&) override { dragLane = -1; }
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent&) override { hover = {}; repaint(); }

    static constexpr int cols = 16;

private:
    bool cellAt (juce::Point<int> p, int& lane, int& step) const;
    juce::Rectangle<float> cellRect (int lane, int col) const;
    void stepMenu (int lane, int step);
    int valueOf (const BeatLab::Step& s, int mode) const;
    void setValue (BeatLab::Step& s, int mode, int v) const;
    static juce::Range<int> rangeOf (int mode);

    BeatLabEditor& owner;
    int dragLane = -1, dragStep = -1, startValue = 0, startY = 0;
    bool paintOn = true;
    juce::Point<int> hover { -1, -1 };
};

// =====================================================================================================
//  Performance: XY filter pad and hold-to-play buttons
// =====================================================================================================
class XYPad : public juce::Component
{
public:
    explicit XYPad (BeatLab& b) : bl (b) {}
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (theme::bg);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (theme::outline);
        g.drawRoundedRectangle (r, 6.0f, 1.0f);
        g.drawVerticalLine ((int) r.getCentreX(), r.getY() + 4, r.getBottom() - 4);
        g.setColour (theme::textFaint);
        g.setFont (uiFont (10.0f, true));
        g.drawText ("LOW-PASS", r.reduced (6, 4), juce::Justification::bottomLeft);
        g.drawText ("HIGH-PASS", r.reduced (6, 4), juce::Justification::bottomRight);
        g.drawText ("FILTER SWEEP  (up = resonance)", r.reduced (6, 4), juce::Justification::centredTop);
        const float x = r.getX() + (bl.param ("perfFilter") * 0.5f + 0.5f) * r.getWidth();
        const float y = r.getBottom() - bl.param ("perfReso") * r.getHeight();
        g.setColour (theme::accent.withAlpha (held ? 0.9f : 0.5f));
        g.fillEllipse (x - 8, y - 8, 16, 16);
    }
    void mouseDown (const juce::MouseEvent& e) override { held = true; mouseDrag (e); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        bl.setParam ("perfFilter", juce::jlimit (-1.0f, 1.0f, ((float) e.x - r.getX()) / r.getWidth() * 2.0f - 1.0f));
        bl.setParam ("perfReso", juce::jlimit (0.0f, 1.0f, (r.getBottom() - (float) e.y) / r.getHeight()));
        repaint();
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        held = false;
        if (! e.mods.isShiftDown()) bl.setParam ("perfFilter", 0.0f);   // springs back (hold Shift to leave it)
        repaint();
    }
private:
    BeatLab& bl;
    bool held = false;
};

class HoldButton : public juce::Component
{
public:
    HoldButton (BeatLab& b, int p, juce::String t, juce::Colour c) : bl (b), perf (p), text (std::move (t)), colour (c) {}
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        const bool on = bl.getPerformance() == perf;
        g.setColour (on ? colour : theme::panelRaised);
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (on ? colour.brighter() : colour.withAlpha (0.6f));
        g.drawRoundedRectangle (r, 5.0f, 1.2f);
        g.setColour (on ? juce::Colours::black : theme::text);
        g.setFont (uiFont (12.0f, true));
        g.drawFittedText (text, r.toNearestInt().reduced (4, 2), juce::Justification::centred, 2);
    }
    void mouseDown (const juce::MouseEvent&) override { bl.setPerformance (perf); repaint(); }
    void mouseUp (const juce::MouseEvent&) override { if (bl.getPerformance() == perf) bl.setPerformance (BeatLab::perfNone); repaint(); }
private:
    BeatLab& bl;
    int perf;
    juce::String text;
    juce::Colour colour;
};

// =====================================================================================================
//  The editor
// =====================================================================================================
class BeatLabEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit BeatLabEditor (BeatLab& b)
        : AudioProcessorEditor (b), bl (b), header (b, "Beat Lab"), grid (*this), xy (b),
          groove (b, { "sync", "dlyDiv", "keysLane", "swing", "tempo", "mutate", "chaos", "shuffle", "revSize", "dlyFb", "volume" }, theme::accent)
    {
        header.onPresetChanged = [this] { selectLane (selectedLane); refreshAll(); };
        header.addButton (toSong, 150);
        toSong.setTooltip ("Puts the pattern you're editing into the song as a MIDI clip on this track, at the playhead "
                           "(the Beat Lab then plays it from the clip; its own sequencer pauses)");
        toSong.onClick = [this] { patternToSong(); };
        addAndMakeVisible (header);

        // transport
        play.setClickingTogglesState (false);
        play.onClick = [this]
        {
            bl.startStop();
        };
        play.setTooltip ("Start / stop the Beat Lab on its own. With \"Play with the Song\" on it also follows the song's transport.");
        addAndMakeVisible (play);

        for (int p = 0; p < BeatLab::numPatterns; ++p)
        {
            auto* btn = patternButtons.add (new juce::TextButton (juce::String::charToString ((juce::juce_wchar) ('A' + p))));
            btn->setRadioGroupId (101);
            btn->setClickingTogglesState (true);
            btn->onClick = [this, p]
            {
                if (juce::ModifierKeys::currentModifiers.isShiftDown())
                {
                    bl.copyPattern (bl.getPatternEditing(), p);
                    status ("Copied pattern " + patternName (bl.getPatternEditing()) + " to " + patternName (p));
                }
                bl.setParam ("pattern", (float) p);
                grid.repaint();
            };
            btn->setTooltip ("Pattern " + patternName (p) + ". Shift-click to copy the current pattern here. Patterns switch at the next bar.");
            addAndMakeVisible (btn);
        }
        chainBox.addItemList ({ "Chain: Off", "Chain: A-B", "Chain: A-D", "Chain: A-H" }, 1);
        chainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (bl.state, "chain", chainBox);
        chainBox.setTooltip ("Play patterns one after another, a bar each");
        addAndMakeVisible (chainBox);

        static const char* modeNames[] = { "Trig", "Velocity", "Ratchet", "Pitch", "Chance", "Nudge" };
        static const char* modeTips[] = { "Click steps on and off (drag to paint)",
                                          "Drag a step up / down: how hard it hits",
                                          "Drag a step up / down: rolls - up to 16 hits inside one step. Right-click for pitch-up / down / fading rolls",
                                          "Drag a step up / down: pitch in semitones",
                                          "Drag a step up / down: the chance it plays (great for evolving grooves)",
                                          "Drag a step up / down: push it early or late (micro-timing)" };
        for (int m = 0; m < 6; ++m)
        {
            auto* btn = modeButtons.add (new juce::TextButton (modeNames[m]));
            btn->setRadioGroupId (102);
            btn->setClickingTogglesState (true);
            btn->setTooltip (modeTips[m]);
            btn->onClick = [this, m] { editMode = m; grid.repaint(); };
            btn->setColour (juce::TextButton::buttonOnColourId, theme::accent2);
            addAndMakeVisible (btn);
        }
        modeButtons[0]->setToggleState (true, juce::dontSendNotification);

        for (int p = 0; p < 4; ++p)
        {
            auto* btn = pageButtons.add (new juce::TextButton (juce::String (p + 1)));
            btn->setTooltip ("Steps " + juce::String (p * 16 + 1) + "-" + juce::String (p * 16 + 16) + " (lanes can be up to 64 steps long)");
            btn->setRadioGroupId (103);
            btn->setClickingTogglesState (true);
            btn->onClick = [this, p] { page = p; grid.repaint(); };
            addAndMakeVisible (btn);
        }
        pageButtons[0]->setToggleState (true, juce::dontSendNotification);
        follow.setToggleState (true, juce::dontSendNotification);
        follow.setTooltip ("Turn the page with the playhead");
        addAndMakeVisible (follow);

        randomBtn.onClick = [this] { randomMenu(); };
        clearBtn.onClick = [this] { patternMenu(); };
        addAndMakeVisible (randomBtn);
        addAndMakeVisible (clearBtn);

        for (int l = 0; l < BeatLab::numLanes; ++l) addAndMakeVisible (strips.add (new LaneStrip (*this, l)));
        addAndMakeVisible (grid);

        // lane inspector
        laneTitle.setFont (uiFont (14.0f, true));
        addAndMakeVisible (laneTitle);
        kitBox.addItemList (BeatLab::kitNames(), 1);
        kitBox.onChange = [this]
        {
            if (updatingBoxes) return;
            if (bl.laneHasUserSample (selectedLane)) bl.useSynthSound (selectedLane);
            bl.setParam (BeatLab::laneParam (selectedLane, "kit"), (float) kitBox.getSelectedItemIndex());
            refreshVoiceBox();
        };
        voiceBox.onChange = [this]
        {
            if (updatingBoxes) return;
            if (bl.laneHasUserSample (selectedLane)) bl.useSynthSound (selectedLane);
            bl.setParam (BeatLab::laneParam (selectedLane, "voice"), (float) voiceBox.getSelectedItemIndex());
            juce::Timer::callAfterDelay (40, [safe = juce::Component::SafePointer<BeatLabEditor> (this)]
            {
                if (safe != nullptr) { safe->bl.triggerLane (safe->selectedLane, 0.85f); safe->refreshLaneTitle(); }
            });
        };
        kitBox.setTooltip ("Sound set for this lane: four vintage drum machines, or the Glitch Lab (clicks, zaps, FM blips, bit-crushed hits)");
        addAndMakeVisible (kitBox);
        addAndMakeVisible (voiceBox);
        modeBox.addItemList ({ "One Shot", "Loop: Slices", "Loop: Repitch" }, 1);
        modeBox.setTooltip ("One Shot plays the sound on each step.  Loop: Slices chops a loop across the lane's steps so it stays in time at any tempo "
                            "(each step plays its own slice - move steps around to re-arrange the beat).  Loop: Repitch plays it like a record, faster or slower with the tempo.");
        addAndMakeVisible (modeBox);
        loadBtn.onClick = [this] { chooseSample (selectedLane); };
        loadBtn.setTooltip ("Load your own sample or loop onto this lane (or drag audio files onto a lane name)");
        synthBtn.onClick = [this] { bl.useSynthSound (selectedLane); selectLane (selectedLane); };
        addAndMakeVisible (loadBtn);
        addAndMakeVisible (synthBtn);

        // groove / glitch / performance
        grooveTitle.setText ("GROOVE  &  GLITCH", juce::dontSendNotification);
        perfTitle.setText ("PERFORM  (hold)", juce::dontSendNotification);
        for (auto* l : { &grooveTitle, &perfTitle })
        {
            l->setFont (uiFont (11.0f, true));
            l->setColour (juce::Label::textColourId, theme::textDim);
            addAndMakeVisible (l);
        }
        addAndMakeVisible (groove);
        addAndMakeVisible (xy);
        const juce::Colour pc[] = { theme::warn, theme::warn, theme::warn, theme::warn, theme::warn, theme::bad, theme::accent2, theme::good };
        const char* pn[] = { "Stutter 1/4", "Stutter 1/8", "Stutter 1/16", "Stutter 1/32", "Stutter 1/64", "Tape Stop", "Reverse", "Fill (chaos)" };
        for (int i = 0; i < 8; ++i) addAndMakeVisible (holdButtons.add (new HoldButton (bl, BeatLab::perfStutter4 + i, pn[i], pc[i])));

        hint.setText ("Click steps to make a beat.  Drop samples or loops on a lane name.  Lanes can have different lengths (Steps) for "
                      "shifting polyrhythms.  Mutate varies the beat every bar; Chaos adds rolls, reversals and pitch jumps; Break Shuffle "
                      "re-orders loop slices.  Lanes also play from MIDI notes C2-G2, and the chosen lane plays melodically from C3 up.",
                      juce::dontSendNotification);
        hint.setFont (uiFont (11.0f));
        hint.setColour (juce::Label::textColourId, theme::textDim);
        hint.setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (hint);

        selectLane (0);
        refreshAll();
        setSize (1220, 800);
        startTimerHz (30);
    }

    ~BeatLabEditor() override { bl.setPerformance (BeatLab::perfNone); }

    void paint (juce::Graphics& g) override
    {
        paintEditorBackground (g, getLocalBounds(), theme::accent);
        g.setColour (theme::panelRaised);
        g.fillRoundedRectangle (inspectorArea.toFloat(), 6.0f);
        g.fillRoundedRectangle (bottomArea.toFloat(), 6.0f);
        // step numbers above the grid
        g.setFont (uiFont (10.0f, true));
        const auto gb = grid.getBounds();
        const float cw = gb.getWidth() / (float) StepGrid::cols;
        for (int c = 0; c < StepGrid::cols; ++c)
        {
            g.setColour (c % 4 == 0 ? theme::textDim : theme::textFaint);
            g.drawText (juce::String (page * 16 + c + 1), juce::Rectangle<float> (gb.getX() + c * cw, (float) gb.getY() - 16.0f, cw, 14.0f), juce::Justification::centred);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 8);
        header.setBounds (r.removeFromTop (34));
        r.removeFromTop (6);
        auto bar = r.removeFromTop (30);
        play.setBounds (bar.removeFromLeft (110).reduced (0, 1));
        bar.removeFromLeft (10);
        for (auto* b : patternButtons) b->setBounds (bar.removeFromLeft (30).reduced (1, 1));
        bar.removeFromLeft (6);
        chainBox.setBounds (bar.removeFromLeft (124).reduced (0, 2));
        bar.removeFromLeft (14);
        for (auto* b : modeButtons) b->setBounds (bar.removeFromLeft (68).reduced (1, 1));
        clearBtn.setBounds (bar.removeFromRight (80).reduced (1, 1));
        randomBtn.setBounds (bar.removeFromRight (96).reduced (1, 1));

        r.removeFromTop (4);
        auto numbers = r.removeFromTop (22);   // page buttons, then the step numbers above the grid
        follow.setBounds (numbers.removeFromLeft (70));
        for (int p = 0; p < 4; ++p) pageButtons[p]->setBounds (numbers.removeFromLeft (40).reduced (1, 1));
        auto gridArea = r.removeFromTop (8 * 32);
        auto stripArea = gridArea.removeFromLeft (230);
        for (int l = 0; l < BeatLab::numLanes; ++l) strips[l]->setBounds (stripArea.getX(), stripArea.getY() + l * 32, stripArea.getWidth(), 32);
        gridArea.removeFromLeft (6);
        grid.setBounds (gridArea);

        r.removeFromTop (8);
        inspectorArea = r.removeFromTop (196);
        auto ins = inspectorArea.reduced (10, 6);
        auto top = ins.removeFromTop (28);
        laneTitle.setBounds (top.removeFromLeft (260));
        kitBox.setBounds (top.removeFromLeft (170).reduced (2, 1));
        voiceBox.setBounds (top.removeFromLeft (150).reduced (2, 1));
        modeBox.setBounds (top.removeFromLeft (140).reduced (2, 1));
        top.removeFromLeft (8);
        loadBtn.setBounds (top.removeFromLeft (130).reduced (2, 1));
        synthBtn.setBounds (top.removeFromLeft (120).reduced (2, 1));
        ins.removeFromTop (4);
        if (lanePanel != nullptr) lanePanel->setBounds (ins);

        r.removeFromTop (8);
        bottomArea = r.removeFromTop (juce::jmin (r.getHeight() - 30, 184));
        auto bot = bottomArea.reduced (10, 6);
        auto left = bot.removeFromLeft (720);
        grooveTitle.setBounds (left.removeFromTop (16));
        groove.setBounds (left);
        bot.removeFromLeft (10);
        perfTitle.setBounds (bot.removeFromTop (16));
        xy.setBounds (bot.removeFromLeft (190).reduced (0, 2));
        bot.removeFromLeft (8);
        const int bw = bot.getWidth() / 4, bh = juce::jmin (52, bot.getHeight() / 2);
        for (int i = 0; i < 8; ++i) holdButtons[i]->setBounds (bot.getX() + (i % 4) * bw, bot.getY() + (i / 4) * bh, bw, bh);
        r.removeFromTop (4);
        hint.setBounds (r);
    }

    // ---- shared with the strips and the grid ----
    BeatLab& bl;
    int selectedLane = 0, editMode = modeTrig, page = 0;

    void selectLane (int l)
    {
        selectedLane = juce::jlimit (0, BeatLab::numLanes - 1, l);
        juce::StringArray ids;
        for (auto n : { "rate", "choke", "reverse", "vol", "pan", "tune", "decay", "filter", "reso", "drive", "crush", "rev", "dly", "steps" })
            ids.add (BeatLab::laneParam (selectedLane, n));
        lanePanel = std::make_unique<ParamPanel> (bl, ids, laneColour (selectedLane));
        addAndMakeVisible (*lanePanel);
        modeAttachment.reset();
        modeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (bl.state, BeatLab::laneParam (selectedLane, "mode"), modeBox);
        refreshVoiceBox();
        refreshLaneTitle();
        for (auto* s : strips) s->repaint();
        grid.repaint();
        resized();
        repaint();
    }

    void refreshLaneTitle()
    {
        laneTitle.setText ("Lane " + juce::String (selectedLane + 1) + ":  " + bl.laneName (selectedLane), juce::dontSendNotification);
        laneTitle.setColour (juce::Label::textColourId, laneColour (selectedLane));
        for (auto* s : strips) s->repaint();
    }

    void refreshVoiceBox()
    {
        const juce::ScopedValueSetter<bool> svs (updatingBoxes, true);
        const int kit = (int) bl.param (BeatLab::laneParam (selectedLane, "kit").toRawUTF8());
        shownKit = kit;
        kitBox.setSelectedItemIndex (kit, juce::dontSendNotification);
        voiceBox.clear (juce::dontSendNotification);
        voiceBox.addItemList (BeatLab::voiceNamesForKit (kit), 1);
        const bool user = bl.laneHasUserSample (selectedLane);
        if (user) voiceBox.setText ("(your sample)", juce::dontSendNotification);
        else voiceBox.setSelectedItemIndex ((int) bl.param (BeatLab::laneParam (selectedLane, "voice").toRawUTF8()), juce::dontSendNotification);
        synthBtn.setEnabled (user);
    }

    void chooseSample (int lane)
    {
        chooser = std::make_unique<juce::FileChooser> ("Load a sample or loop onto lane " + juce::String (lane + 1),
                                                       juce::File::getSpecialLocation (juce::File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this, lane] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) loadFile (lane, fc.getResult()); });
    }

    void loadFile (int lane, const juce::File& f)
    {
        const auto err = bl.loadLaneFile (lane, f);
        if (err.isNotEmpty()) { juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Beat Lab", err); return; }
        const int mode = (int) bl.param (BeatLab::laneParam (lane, "mode").toRawUTF8());
        // a fresh loop on an empty lane: give it a step on every slice so it plays straight away
        if (mode != BeatLab::oneShot)
        {
            const int pat = bl.getPatternEditing();
            bool any = false;
            for (int s = 0; s < BeatLab::maxSteps; ++s) any = any || bl.getStep (pat, lane, s).on;
            if (! any)
            {
                const int len = (int) bl.param (BeatLab::laneParam (lane, "steps").toRawUTF8());
                for (int s = 0; s < len; ++s) { BeatLab::Step st; st.on = true; bl.setStep (pat, lane, s, st); }
            }
            status ("Loop chopped into " + juce::String ((int) bl.param (BeatLab::laneParam (lane, "steps").toRawUTF8()))
                    + " slices - it stays in time at any tempo. Move or delete steps to rearrange it, or turn up Break Shuffle.");
        }
        else status ("Loaded " + f.getFileNameWithoutExtension() + " onto lane " + juce::String (lane + 1));
        selectLane (lane);
    }

    void laneMenu (int lane, juce::Component* target)
    {
        selectLane (lane);
        const int pat = bl.getPatternEditing();
        const int len = (int) bl.param (BeatLab::laneParam (lane, "steps").toRawUTF8());
        juce::PopupMenu m, euclid, lengths, rnd;
        m.addSectionHeader ("Lane " + juce::String (lane + 1) + ": " + bl.laneName (lane));
        m.addItem (1, "Load Sample / Loop...");
        m.addItem (2, "Use Synth Sound", bl.laneHasUserSample (lane));
        m.addSeparator();
        for (int h = 1; h <= juce::jmin (16, len); ++h) euclid.addItem (100 + h, juce::String (h) + " hits in " + juce::String (len));
        m.addSubMenu ("Euclidean Rhythm", euclid);
        rnd.addItem (200, "Tame");
        rnd.addItem (201, "Groovy");
        rnd.addItem (202, "Wild (rolls, pitch, reverse)");
        m.addSubMenu ("Randomise Lane", rnd);
        m.addItem (3, "Shift Left");
        m.addItem (4, "Shift Right");
        m.addItem (5, "Clear Lane");
        for (int n : { 3, 4, 5, 6, 7, 8, 9, 11, 12, 13, 15, 16, 24, 32, 48, 64 })
            lengths.addItem (300 + n, juce::String (n) + " steps" + (n % 2 == 1 && n > 2 ? "  (polymeter)" : ""), true, n == len);
        m.addSubMenu ("Length", lengths);
        juce::Component::SafePointer<BeatLabEditor> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [safe, lane, pat, len] (int r)
        {
            if (safe == nullptr || r == 0) return;
            auto& b = safe->bl;
            if (r == 1) safe->chooseSample (lane);
            else if (r == 2) b.useSynthSound (lane);
            else if (r == 3 || r == 4)
            {
                std::vector<BeatLab::Step> s;
                for (int i = 0; i < len; ++i) s.push_back (b.getStep (pat, lane, i));
                for (int i = 0; i < len; ++i) b.setStep (pat, lane, i, s[(size_t) ((i + (r == 3 ? 1 : len - 1)) % len)]);
            }
            else if (r == 5) b.clearPattern (pat, lane);
            else if (r > 100 && r <= 116) b.euclid (pat, lane, r - 100);
            else if (r >= 200 && r <= 202) b.randomise (pat, lane, r == 200 ? 0.25f : r == 201 ? 0.4f : 0.45f, r == 200 ? 0.0f : r == 201 ? 0.3f : 0.9f);
            else if (r > 300) b.setParam (BeatLab::laneParam (lane, "steps"), (float) (r - 300));
            safe->selectLane (lane);
        });
    }

    void status (const juce::String& s) { hint.setText (s, juce::dontSendNotification); hint.setColour (juce::Label::textColourId, theme::text); }
    static juce::String patternName (int p) { return juce::String::charToString ((juce::juce_wchar) ('A' + p)); }
    int pageCount() const
    {
        int longest = 1;
        for (int l = 0; l < BeatLab::numLanes; ++l) longest = juce::jmax (longest, (int) bl.param (BeatLab::laneParam (l, "steps").toRawUTF8()));
        return (longest + 15) / 16;
    }
    juce::String modeHelp() const
    {
        static const char* h[] = { "click: on / off", "drag: velocity", "drag: rolls", "drag: semitones", "drag: chance %", "drag: timing" };
        return juce::String ("EDIT ") + h[editMode] + "   right-click: more";
    }

private:
    void refreshAll()
    {
        header.refreshPreset();
        for (int p = 0; p < BeatLab::numPatterns; ++p) patternButtons[p]->setToggleState (p == bl.getPatternEditing(), juce::dontSendNotification);
        refreshVoiceBox();
        refreshLaneTitle();
        grid.repaint();
    }

    void randomMenu()
    {
        juce::PopupMenu m;
        m.addItem (1, "Whole pattern: Tame");
        m.addItem (2, "Whole pattern: Groovy");
        m.addItem (3, "Whole pattern: Wild (glitch)");
        m.addSeparator();
        m.addItem (4, "Selected lane: Groovy");
        m.addItem (5, "Selected lane: Wild");
        m.addSeparator();
        m.addItem (6, "Random sounds (pick new synth sounds for every lane)");
        juce::Component::SafePointer<BeatLabEditor> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&randomBtn), [safe] (int r)
        {
            if (safe == nullptr || r == 0) return;
            auto& b = safe->bl;
            const int pat = b.getPatternEditing();
            juce::Random rng;
            if (r <= 3)
            {
                // denser hats, sparser kicks: keep it sounding like a beat
                static const float dens[] = { 0.3f, 0.25f, 0.55f, 0.15f, 0.2f, 0.2f, 0.15f, 0.15f };
                for (int l = 0; l < BeatLab::numLanes; ++l) b.randomise (pat, l, dens[l] * (r == 1 ? 0.8f : 1.0f), r == 1 ? 0.0f : r == 2 ? 0.25f : 0.85f);
            }
            else if (r <= 5) b.randomise (pat, safe->selectedLane, 0.4f, r == 4 ? 0.3f : 0.9f);
            else
                for (int l = 0; l < BeatLab::numLanes; ++l)
                    if (! b.laneHasUserSample (l))
                    {
                        const int kit = rng.nextInt (BeatLab::glitchKit + 1);
                        b.setParam (BeatLab::laneParam (l, "kit"), (float) kit);
                        b.setParam (BeatLab::laneParam (l, "voice"), (float) rng.nextInt (BeatLab::voiceNamesForKit (kit).size()));
                    }
            safe->selectLane (safe->selectedLane);
        });
    }

    void patternMenu()
    {
        juce::PopupMenu m, copyTo;
        const int pat = bl.getPatternEditing();
        for (int p = 0; p < BeatLab::numPatterns; ++p) if (p != pat) copyTo.addItem (10 + p, "Pattern " + patternName (p));
        m.addItem (1, "Clear pattern " + patternName (pat));
        m.addSubMenu ("Copy pattern " + patternName (pat) + " to", copyTo);
        m.addItem (2, "Clear every pattern");
        juce::Component::SafePointer<BeatLabEditor> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&clearBtn), [safe, pat] (int r)
        {
            if (safe == nullptr || r == 0) return;
            if (r == 1) safe->bl.clearPattern (pat);
            else if (r == 2) for (int p = 0; p < BeatLab::numPatterns; ++p) safe->bl.clearPattern (p);
            else if (r >= 10) safe->bl.copyPattern (pat, r - 10);
            safe->grid.repaint();
        });
    }

    void patternToSong()
    {
        if (! BeatLab::onPatternToSong) { status ("Open the Beat Lab from a track in the Studio to put patterns into the song."); return; }
        double len = 0;
        const auto seq = bl.patternToMidi (bl.getPatternEditing(), len);
        if (seq.getNumEvents() == 0) { status ("This pattern is empty - add some steps first."); return; }
        BeatLab::onPatternToSong (bl, seq, len);
        status ("Pattern " + patternName (bl.getPatternEditing()) + " is now a MIDI clip in the song (lane 1 = C2, lane 2 = C#2...). "
                "The Beat Lab's own sequencer is paused so it doesn't play twice - turn \"Play with the Song\" back on to jam again.");
    }

    void timerCallback() override
    {
        for (int l = 0; l < BeatLab::numLanes; ++l)
        {
            const float f = bl.laneFlash[(size_t) l].exchange (0.0f);
            auto* s = strips[l];
            if (f > 0.0f) s->flash = 1.0f;
            else if (s->flash > 0.0f) s->flash = juce::jmax (0.0f, s->flash - 0.15f);
            s->repaint();
        }
        const bool running = bl.isRunning();
        play.setButtonText (running ? juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xa0  Stop")) : juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xb6  Play")));
        play.setColour (juce::TextButton::buttonColourId, running ? theme::good.darker (0.4f) : theme::panelRaised);
        const int playing = bl.getPatternPlaying(), editing = bl.getPatternEditing();
        for (int p = 0; p < BeatLab::numPatterns; ++p)
        {
            patternButtons[p]->setToggleState (p == editing, juce::dontSendNotification);
            patternButtons[p]->setColour (juce::TextButton::buttonColourId, running && p == playing ? theme::good.darker (0.3f) : theme::panelRaised);
            patternButtons[p]->setColour (juce::TextButton::buttonOnColourId, running && p == playing ? theme::good : theme::accent);
        }
        const int pages = pageCount();
        for (int p = 0; p < 4; ++p) pageButtons[p]->setEnabled (p < pages);
        if (page >= pages) { page = pages - 1; pageButtons[page]->setToggleState (true, juce::dontSendNotification); }
        if (follow.getToggleState() && running)
        {
            const int st = bl.laneStep[(size_t) selectedLane].load();
            if (st >= 0 && st / 16 != page && st / 16 < pages) { page = st / 16; pageButtons[page]->setToggleState (true, juce::dontSendNotification); repaint(); }
        }
        if ((int) bl.param (BeatLab::laneParam (selectedLane, "kit").toRawUTF8()) != shownKit) refreshVoiceBox();
        grid.repaint();
        xy.repaint();
        for (auto* h : holdButtons) h->repaint();
        if (++ticks % 15 == 0) refreshLaneTitle();
    }

    EditorHeader header;
    juce::TextButton toSong { "Pattern to Song" };
    juce::TextButton play { "Play" };
    juce::OwnedArray<juce::TextButton> patternButtons, modeButtons, pageButtons;
    juce::ComboBox chainBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> chainAttachment, modeAttachment;
    juce::ToggleButton follow { "Follow" };
    juce::TextButton randomBtn { "Randomise..." }, clearBtn { "Pattern..." };
    juce::OwnedArray<LaneStrip> strips;
    StepGrid grid;
    juce::Label laneTitle, grooveTitle, perfTitle, hint;
    juce::ComboBox kitBox, voiceBox, modeBox;
    juce::TextButton loadBtn { "Load Sample..." }, synthBtn { "Synth Sound" };
    std::unique_ptr<ParamPanel> lanePanel;
    XYPad xy;
    ParamPanel groove;
    juce::OwnedArray<HoldButton> holdButtons;
    juce::Rectangle<int> inspectorArea, bottomArea;
    std::unique_ptr<juce::FileChooser> chooser;
    bool updatingBoxes = false;
    int shownKit = -1, ticks = 0;

    friend class StepGrid;
};

// ---- LaneStrip -----------------------------------------------------------------------------------------
LaneStrip::LaneStrip (BeatLabEditor& o, int l) : owner (o), lane (l)
{
    for (auto* b : { &mute, &solo })
    {
        b->setClickingTogglesState (true);
        addAndMakeVisible (b);
    }
    mute.setColour (juce::TextButton::buttonOnColourId, theme::warn.darker (0.2f));
    solo.setColour (juce::TextButton::buttonOnColourId, theme::good.darker (0.2f));
    muteA = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (owner.bl.state, BeatLab::laneParam (lane, "mute"), mute);
    soloA = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (owner.bl.state, BeatLab::laneParam (lane, "solo"), solo);
}

void LaneStrip::resized()
{
    auto r = getLocalBounds().reduced (2, 3);
    solo.setBounds (r.removeFromRight (26));
    r.removeFromRight (2);
    mute.setBounds (r.removeFromRight (26));
}

void LaneStrip::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f, 2.0f);
    const bool sel = owner.selectedLane == lane;
    const auto c = laneColour (lane);
    g.setColour (sel ? theme::panelRaised.brighter (0.08f) : theme::panelRaised);
    g.fillRoundedRectangle (r, 5.0f);
    if (sel || dragOver)
    {
        g.setColour (dragOver ? theme::good : c);
        g.drawRoundedRectangle (r, 5.0f, 1.5f);
    }
    // LED
    auto led = juce::Rectangle<float> (r.getX() + 7, r.getCentreY() - 5, 10, 10);
    g.setColour (c.withAlpha (0.25f + 0.75f * flash));
    g.fillEllipse (led);
    g.setColour (theme::textFaint);
    g.setFont (uiFont (10.0f, true));
    g.drawText (juce::String (lane + 1), juce::Rectangle<float> (r.getX() + 20, r.getY(), 14, r.getHeight()), juce::Justification::centred);
    const bool user = owner.bl.laneHasUserSample (lane);
    g.setColour (theme::text);
    g.setFont (uiFont (12.5f, sel));
    auto nameArea = juce::Rectangle<float> (r.getX() + 36, r.getY(), r.getWidth() - 36 - 60, r.getHeight());
    g.drawFittedText ((user ? juce::String (juce::CharPointer_UTF8 ("\xe2\x99\xab ")) : juce::String()) + owner.bl.laneName (lane),
                      nameArea.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
    const int len = (int) owner.bl.param (BeatLab::laneParam (lane, "steps").toRawUTF8());
    if (len != 16)
    {
        g.setColour (theme::textFaint);
        g.setFont (uiFont (9.5f));
        g.drawText (juce::String (len), nameArea.removeFromRight (18), juce::Justification::centredRight);
    }
}

void LaneStrip::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { owner.laneMenu (lane, this); return; }
    owner.bl.triggerLane (lane, 0.85f);
    if (owner.selectedLane != lane) owner.selectLane (lane);
}

void LaneStrip::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    owner.loadFile (lane, juce::File (files[0]));
}

// ---- StepGrid ------------------------------------------------------------------------------------------
juce::Rectangle<float> StepGrid::cellRect (int lane, int col) const
{
    const float cw = getWidth() / (float) cols, ch = getHeight() / (float) BeatLab::numLanes;
    return { col * cw, lane * ch, cw, ch };
}

bool StepGrid::cellAt (juce::Point<int> p, int& lane, int& step) const
{
    if (! getLocalBounds().contains (p)) return false;
    lane = juce::jlimit (0, BeatLab::numLanes - 1, (int) (p.y / (getHeight() / (float) BeatLab::numLanes)));
    const int col = juce::jlimit (0, cols - 1, (int) (p.x / (getWidth() / (float) cols)));
    step = owner.page * cols + col;
    return step < (int) owner.bl.param (BeatLab::laneParam (lane, "steps").toRawUTF8());
}

juce::Range<int> StepGrid::rangeOf (int mode)
{
    switch (mode)
    {
        case modeVelocity: return { 1, 127 };
        case modeRatchet:  return { 1, 16 };
        case modePitch:    return { -24, 24 };
        case modeProb:     return { 0, 100 };
        case modeNudge:    return { -8, 7 };
        default:           return { 0, 1 };
    }
}

int StepGrid::valueOf (const BeatLab::Step& s, int mode) const
{
    switch (mode)
    {
        case modeVelocity: return s.velocity;
        case modeRatchet:  return s.ratchet;
        case modePitch:    return s.pitch;
        case modeProb:     return s.probability;
        case modeNudge:    return s.nudge;
        default:           return s.on ? 1 : 0;
    }
}

void StepGrid::setValue (BeatLab::Step& s, int mode, int v) const
{
    const auto r = rangeOf (mode);
    v = juce::jlimit (r.getStart(), r.getEnd(), v);
    switch (mode)
    {
        case modeVelocity: s.velocity = v; break;
        case modeRatchet:  s.ratchet = v; break;
        case modePitch:    s.pitch = v; break;
        case modeProb:     s.probability = v; break;
        case modeNudge:    s.nudge = v; break;
        default:           s.on = v != 0; break;
    }
}

void StepGrid::paint (juce::Graphics& g)
{
    auto& bl = owner.bl;
    const int pat = bl.getPatternEditing();
    const int mode = owner.editMode;
    for (int l = 0; l < BeatLab::numLanes; ++l)
    {
        const int len = (int) bl.param (BeatLab::laneParam (l, "steps").toRawUTF8());
        const int perBeat = juce::jmax (1, BeatLab::stepsForRate ((int) bl.param (BeatLab::laneParam (l, "rate").toRawUTF8())) / 4);
        const int playing = bl.laneStep[(size_t) l].load();
        const auto c = laneColour (l);
        const bool muted = bl.param (BeatLab::laneParam (l, "mute").toRawUTF8()) > 0.5f;
        for (int col = 0; col < cols; ++col)
        {
            const int s = owner.page * cols + col;
            auto r = cellRect (l, col).reduced (2.0f, 3.0f);
            if (s >= len)
            {
                g.setColour (theme::bg.withAlpha (0.5f));
                g.fillRoundedRectangle (r, 3.0f);
                continue;
            }
            const auto st = bl.getStep (pat, l, s);
            const bool beatStart = (s / perBeat) % 2 == 0;
            g.setColour (beatStart ? theme::panelRaised.brighter (0.06f) : theme::panelRaised);
            g.fillRoundedRectangle (r, 3.0f);

            if (st.on)
            {
                auto on = c.withMultipliedSaturation (muted ? 0.2f : 1.0f).withAlpha (muted ? 0.4f : 1.0f);
                if (mode == modeTrig)
                {
                    g.setColour (on.withMultipliedBrightness (0.45f + 0.55f * st.velocity / 127.0f));
                    g.fillRoundedRectangle (r, 3.0f);
                }
                else
                {
                    // a value bar
                    const auto rg = rangeOf (mode);
                    const float v = (valueOf (st, mode) - rg.getStart()) / (float) juce::jmax (1, rg.getLength());
                    g.setColour (on.withAlpha (0.25f));
                    g.fillRoundedRectangle (r, 3.0f);
                    g.setColour (on);
                    if (mode == modePitch || mode == modeNudge)
                    {
                        const float zero = (0 - rg.getStart()) / (float) rg.getLength();
                        const float y0 = r.getBottom() - zero * r.getHeight(), y1 = r.getBottom() - v * r.getHeight();
                        g.fillRect (r.getX() + 2, juce::jmin (y0, y1), r.getWidth() - 4, juce::jmax (2.0f, std::abs (y1 - y0)));
                    }
                    else
                        g.fillRect (r.getX() + 2, r.getBottom() - v * r.getHeight(), r.getWidth() - 4, v * r.getHeight());
                    g.setColour (theme::text);
                    g.setFont (uiFont (10.0f, true));
                    const int val = valueOf (st, mode);
                    g.drawText ((mode == modePitch && val > 0 ? "+" : "") + juce::String (val) + (mode == modeProb ? "%" : mode == modeRatchet ? "x" : ""),
                                r, juce::Justification::centred);
                }
                // decorations: rolls, chance, pitch, reverse
                g.setColour (juce::Colours::black.withAlpha (0.7f));
                if (st.ratchet > 1 && mode != modeRatchet)
                {
                    const float w = (r.getWidth() - 6) / (float) st.ratchet;
                    for (int j = 0; j < st.ratchet; ++j) g.fillRect (r.getX() + 3 + j * w, r.getBottom() - 5, juce::jmax (1.0f, w - 1.0f), 3.0f);
                }
                g.setFont (uiFont (9.0f, true));
                if (st.probability < 100 && mode != modeProb) g.drawText (juce::String (st.probability) + "%", r.reduced (2, 1), juce::Justification::topRight);
                if (st.pitch != 0 && mode != modePitch) g.drawText ((st.pitch > 0 ? "+" : "") + juce::String (st.pitch), r.reduced (2, 1), juce::Justification::topLeft);
                if (st.reverse) g.drawText ("<", r.reduced (3, 0), juce::Justification::centredLeft);
                if (st.nudge != 0 && mode != modeNudge)
                    g.fillRect (st.nudge < 0 ? r.getX() : r.getRight() - 3, r.getY() + 3, 3.0f, r.getHeight() - 6);
            }
            if (s == playing)
            {
                g.setColour (juce::Colours::white.withAlpha (0.85f));
                g.drawRoundedRectangle (r, 3.0f, 1.6f);
            }
            else if (hover.x == l && hover.y == s)
            {
                g.setColour (theme::textDim);
                g.drawRoundedRectangle (r, 3.0f, 1.0f);
            }
        }
        if (l == owner.selectedLane)
        {
            auto row = cellRect (l, 0).withRight ((float) getWidth()).reduced (0.0f, 1.0f);
            g.setColour (c.withAlpha (0.35f));
            g.drawRoundedRectangle (row, 4.0f, 1.0f);
        }
    }
}

void StepGrid::mouseMove (const juce::MouseEvent& e)
{
    int l, s;
    const auto h = cellAt (e.getPosition(), l, s) ? juce::Point<int> (l, s) : juce::Point<int> (-1, -1);
    if (h != hover) { hover = h; repaint(); }
}

void StepGrid::mouseDown (const juce::MouseEvent& e)
{
    int l, s;
    if (! cellAt (e.getPosition(), l, s)) return;
    if (owner.selectedLane != l) owner.selectLane (l);
    if (e.mods.isPopupMenu()) { stepMenu (l, s); return; }
    auto& bl = owner.bl;
    const int pat = bl.getPatternEditing();
    auto st = bl.getStep (pat, l, s);
    dragLane = l; dragStep = s; startY = e.y;
    if (owner.editMode == modeTrig)
    {
        st.on = ! st.on;
        paintOn = st.on;
        bl.setStep (pat, l, s, st);
        if (st.on && ! bl.isRunning()) bl.triggerLane (l, st.velocity / 127.0f);
    }
    else
    {
        if (! st.on) { st.on = true; bl.setStep (pat, l, s, st); }
        startValue = valueOf (st, owner.editMode);
    }
    repaint();
}

void StepGrid::mouseDrag (const juce::MouseEvent& e)
{
    if (dragLane < 0) return;
    auto& bl = owner.bl;
    const int pat = bl.getPatternEditing();
    if (owner.editMode == modeTrig)
    {
        int l, s;
        if (cellAt (e.getPosition(), l, s))
        {
            auto st = bl.getStep (pat, l, s);
            if (st.on != paintOn) { st.on = paintOn; bl.setStep (pat, l, s, st); repaint(); }
        }
        return;
    }
    // drag up / down for the value; dragging sideways onto other steps paints the same value
    const auto rg = rangeOf (owner.editMode);
    const float pixelsPerUnit = juce::jlimit (2.0f, 12.0f, 140.0f / (float) rg.getLength());
    const int v = startValue + (int) std::round ((startY - e.y) / pixelsPerUnit);
    int l, s;
    if (! cellAt (e.getPosition(), l, s) || l != dragLane) s = dragStep;
    auto st = bl.getStep (pat, dragLane, s);
    st.on = true;
    setValue (st, owner.editMode, v);
    bl.setStep (pat, dragLane, s, st);
    if (s != dragStep)
    {
        auto first = bl.getStep (pat, dragLane, dragStep);
        setValue (first, owner.editMode, v);
        bl.setStep (pat, dragLane, dragStep, first);
    }
    repaint();
}

void StepGrid::mouseDoubleClick (const juce::MouseEvent& e)
{
    int l, s;
    if (owner.editMode == modeTrig || ! cellAt (e.getPosition(), l, s)) return;
    auto& bl = owner.bl;
    auto st = bl.getStep (bl.getPatternEditing(), l, s);
    const BeatLab::Step def;
    setValue (st, owner.editMode, valueOf (def, owner.editMode));
    bl.setStep (bl.getPatternEditing(), l, s, st);
    repaint();
}

void StepGrid::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    int l, s;
    if (! cellAt (e.getPosition(), l, s)) return;
    auto& bl = owner.bl;
    const int mode = owner.editMode == modeTrig ? modeVelocity : owner.editMode;
    auto st = bl.getStep (bl.getPatternEditing(), l, s);
    if (! st.on) return;
    const int step = mode == modeVelocity || mode == modeProb ? 5 : 1;
    setValue (st, mode, valueOf (st, mode) + (w.deltaY > 0 ? step : -step));
    bl.setStep (bl.getPatternEditing(), l, s, st);
    repaint();
}

void StepGrid::stepMenu (int lane, int step)
{
    auto& bl = owner.bl;
    const int pat = bl.getPatternEditing();
    const auto st = bl.getStep (pat, lane, step);
    juce::PopupMenu m, roll, shape, chance, pitch, vel, nudge;
    m.addSectionHeader ("Step " + juce::String (step + 1));
    m.addItem (1, st.on ? "Turn Off" : "Turn On");
    for (int r : { 1, 2, 3, 4, 6, 8, 12, 16 }) roll.addItem (100 + r, r == 1 ? juce::String ("No roll") : juce::String (r) + " hits", true, st.ratchet == r);
    m.addSubMenu ("Roll (ratchet)", roll);
    const char* shapes[] = { "Even", "Pitch rising", "Pitch falling", "Fading out" };
    for (int i = 0; i < 4; ++i) shape.addItem (200 + i, shapes[i], true, st.ratchetShape == i);
    m.addSubMenu ("Roll shape", shape);
    for (int c : { 100, 90, 75, 50, 33, 25, 10 }) chance.addItem (300 + c, juce::String (c) + "%", true, st.probability == c);
    m.addSubMenu ("Chance", chance);
    for (int p : { 12, 7, 5, 3, 0, -5, -7, -12, -24 }) pitch.addItem (500 + p, (p > 0 ? "+" : "") + juce::String (p) + " st", true, st.pitch == p);
    m.addSubMenu ("Pitch", pitch);
    vel.addItem (601, "Accent", true, st.velocity >= 120);
    vel.addItem (602, "Normal", true, st.velocity >= 80 && st.velocity < 120);
    vel.addItem (603, "Ghost", true, st.velocity < 80);
    m.addSubMenu ("Velocity", vel);
    nudge.addItem (704, "Early");
    nudge.addItem (708, "On the grid");
    nudge.addItem (712, "Late (laid back)");
    m.addSubMenu ("Timing", nudge);
    m.addItem (2, "Reverse", true, st.reverse);
    m.addSeparator();
    m.addItem (3, "Fill this lane: every step from here like this one");
    juce::Component::SafePointer<StepGrid> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options(), [safe, lane, step, pat] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& b = safe->owner.bl;
        auto s = b.getStep (pat, lane, step);
        if (r == 1) s.on = ! s.on;
        else if (r == 2) { s.reverse = ! s.reverse; s.on = true; }
        else if (r == 3)
        {
            const int len = (int) b.param (BeatLab::laneParam (lane, "steps").toRawUTF8());
            s.on = true;
            for (int i = step; i < len; ++i) b.setStep (pat, lane, i, s);
        }
        else if (r >= 100 && r < 200) { s.ratchet = r - 100; s.on = true; }
        else if (r >= 200 && r < 300) { s.ratchetShape = r - 200; s.on = true; if (s.ratchet == 1) s.ratchet = 4; }
        else if (r >= 300 && r < 450) { s.probability = r - 300; s.on = true; }
        else if (r >= 450 && r < 600) { s.pitch = r - 500; s.on = true; }
        else if (r > 600 && r < 700) { s.velocity = r == 601 ? 127 : r == 602 ? 100 : 50; s.on = true; }
        else if (r >= 700) { s.nudge = r - 708; s.on = true; }
        if (r != 3) b.setStep (pat, lane, step, s);
        safe->repaint();
    });
}

void installBeatLabEditor()
{
    BeatLab::editorFactory = [] (BeatLab& b) -> juce::AudioProcessorEditor* { return new BeatLabEditor (b); };
}

} // namespace wis::daw
