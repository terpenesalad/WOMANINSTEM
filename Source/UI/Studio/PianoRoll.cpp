#include "PianoRoll.h"
#include "Daw/Model/TempoDetect.h"
#include "Daw/Instruments/InstrumentRefs.h"

namespace wis::daw
{

static constexpr int keysWidthNormal = 64;
static constexpr int keysWidthDrums = 128;
static constexpr int velocityHeight = 70;
static constexpr int toolbarHeight = 34;
static constexpr int headerHeight = 18;

static bool isBlackKey (int p) { const int n = p % 12; return n == 1 || n == 3 || n == 6 || n == 8 || n == 10; }

// =====================================================================================================
//  Grid
// =====================================================================================================
class PianoRoll::Grid : public juce::Component
{
public:
    explicit Grid (PianoRoll& o) : roll (o) { setWantsKeyboardFocus (true); }

    void paint (juce::Graphics& g) override
    {
        auto& r = roll;
        g.fillAll (theme::bg);
        if (! r.clip.isValid()) return;
        Clip c (r.clip);
        const double len = c.midiLength();

        // rows
        for (int p = 0; p < 128; ++p)
        {
            const float y = r.pitchToY (p) + headerHeight;
            if (y > getHeight() || y + r.rowH < headerHeight) continue;
            g.setColour (isBlackKey (p) && ! r.drumMode ? juce::Colour (0xff0b0d10) : juce::Colour (0xff12151b));
            g.fillRect (0.0f, y, (float) getWidth(), r.rowH);
            if (p % 12 == 0 && ! r.drumMode)
            {
                g.setColour (theme::outline);
                g.drawHorizontalLine ((int) (y + r.rowH), 0.0f, (float) getWidth());
            }
        }

        // outside the clip
        const float endX = r.beatToX (len);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRect (endX, (float) headerHeight, (float) getWidth() - endX, (float) getHeight());
        const float startX = r.beatToX (0.0);
        if (startX > 0) g.fillRect (0.0f, (float) headerHeight, startX, (float) getHeight());

        // beat grid
        const double bpb = r.ctx.project.beatsPerBar();
        const double first = std::floor (r.scrollBeats / r.grid) * r.grid;
        for (double b = first; ; b += r.grid)
        {
            const float x = r.beatToX (b);
            if (x > getWidth()) break;
            const double absB = c.start() + b;
            const bool bar = std::abs (std::fmod (absB, bpb)) < 1e-6;
            const bool beat = std::abs (absB - std::round (absB)) < 1e-6;
            g.setColour (bar ? theme::outline.brighter (0.5f) : beat ? theme::outline : theme::outline.withAlpha (0.4f));
            g.drawVerticalLine ((int) x, (float) headerHeight, (float) getHeight());
        }

        // header with bar numbers + clip end handle
        g.setColour (theme::panel);
        g.fillRect (0, 0, getWidth(), headerHeight);
        g.setFont (uiFont (10.5f, true));
        for (double b = std::floor ((c.start() + r.scrollBeats) / bpb) * bpb; ; b += bpb)
        {
            const float x = r.beatToX (b - c.start());
            if (x > getWidth()) break;
            g.setColour (theme::textDim);
            g.drawText (juce::String ((int) (b / bpb) + 1), (int) x + 3, 1, 40, headerHeight - 2, juce::Justification::centredLeft);
        }
        g.setColour (theme::accent);
        g.fillRect (endX - 2.0f, 0.0f, 4.0f, (float) headerHeight);

        // notes
        const auto trackCol = r.ctx.project.trackForClip (r.clip).colour();
        for (auto n : r.clip)
        {
            if (! n.hasType (ids::NOTE)) continue;
            auto rect = noteRect (n);
            if (rect.getRight() < 0 || rect.getX() > getWidth()) continue;
            const bool sel = r.selected.contains (n);
            const float v = (float) (int) n[ids::v] / 127.0f;
            auto col = trackCol.withBrightness (0.45f + 0.55f * v).withSaturation (0.5f + 0.4f * v);
            g.setColour (sel ? col.brighter (0.5f) : col);
            g.fillRoundedRectangle (rect, 2.5f);
            g.setColour (sel ? juce::Colours::white : juce::Colours::black.withAlpha (0.5f));
            g.drawRoundedRectangle (rect, 2.5f, sel ? 1.5f : 1.0f);
            if (rect.getWidth() > 26 && r.rowH >= 10)
            {
                g.setColour (juce::Colours::black.withAlpha (0.7f));
                g.setFont (uiFont (juce::jmin (10.5f, r.rowH - 1.0f)));
                g.drawText (juce::MidiMessage::getMidiNoteName ((int) n[ids::p], true, true, 4), rect.reduced (3, 0).toNearestInt(), juce::Justification::centredLeft, false);
            }
        }

        if (! marquee.isEmpty())
        {
            g.setColour (theme::accent.withAlpha (0.12f));
            g.fillRect (marquee);
            g.setColour (theme::accent);
            g.drawRect (marquee);
        }

        // playhead
        const double ph = r.ctx.engine.getPositionBeats() - c.start();
        const float px = r.beatToX (ph);
        g.setColour (theme::accent);
        g.fillRect (px, 0.0f, 1.5f, (float) getHeight());
    }

    juce::Rectangle<float> noteRect (const juce::ValueTree& n) const
    {
        const double s = n[ids::s], l = n[ids::l];
        const float x = roll.beatToX (s);
        const float w = juce::jmax (3.0f, (float) (l * roll.ppb) - 1.0f);
        const float y = roll.pitchToY ((int) n[ids::p]) + headerHeight;
        return { x, y + 0.5f, w, roll.rowH - 1.0f };
    }

    juce::ValueTree noteAt (juce::Point<float> p) const
    {
        for (int i = roll.clip.getNumChildren(); --i >= 0;)
        {
            auto n = roll.clip.getChild (i);
            if (n.hasType (ids::NOTE) && noteRect (n).contains (p)) return n;
        }
        return {};
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        auto& r = roll;
        if (! r.clip.isValid()) return;
        auto& p = r.ctx.project;
        mode = Mode::none;
        dragNotes.clear();
        starts.clear();

        if (e.y < headerHeight)
        {
            // clip-length handle or set the playhead
            if (std::abs (e.x - r.beatToX (Clip (r.clip).midiLength())) < 8) { r.ctx.beginEdit ("Clip length"); mode = Mode::clipEnd; }
            else r.ctx.engine.setPositionBeats (Clip (r.clip).start() + juce::jmax (0.0, r.xToBeat ((float) e.x)));
            return;
        }

        auto hit = noteAt (e.position);
        if (e.mods.isPopupMenu())
        {
            if (hit.isValid() && ! r.selected.contains (hit)) { r.selected.clear(); r.selected.add (hit); }
            menu();
            return;
        }

        if (! hit.isValid())
        {
            if (r.drawMode || e.getNumberOfClicks() > 1)
            {
                // add a note
                r.ctx.beginEdit ("Add note");
                const double b = juce::jmax (0.0, std::floor (r.xToBeat ((float) e.x) / r.grid) * r.grid);
                const int pitch = r.yToPitch ((float) e.y - headerHeight);
                juce::ValueTree n (ids::NOTE);
                n.setProperty (ids::p, pitch, nullptr);
                n.setProperty (ids::s, b, nullptr);
                n.setProperty (ids::l, r.drumMode ? r.grid : r.lastLength, nullptr);
                n.setProperty (ids::v, r.lastVelocity, nullptr);
                r.clip.appendChild (n, p.um());
                r.selected.clear();
                r.selected.add (n);
                r.previewNote (pitch, r.lastVelocity);
                dragNotes.add (n);
                starts.add (n.createCopy());
                anchor = n;
                downBeat = r.xToBeat ((float) e.x);
                downPitch = pitch;
                mode = Mode::resize;
                return;
            }
            if (! e.mods.isShiftDown()) r.selected.clear();
            mode = Mode::marquee;
            marqueeStart = e.getPosition();
            r.repaintAll();
            return;
        }

        if (e.getNumberOfClicks() > 1 && ! r.drawMode)
        {
            r.ctx.beginEdit ("Delete note");
            r.selected.removeFirstMatchingValue (hit);
            r.clip.removeChild (hit, p.um());
            return;
        }

        if (! r.selected.contains (hit))
        {
            if (! e.mods.isShiftDown()) r.selected.clear();
            r.selected.add (hit);
        }
        r.previewNote ((int) hit[ids::p], (int) hit[ids::v]);
        r.lastLength = (double) hit[ids::l];
        r.lastVelocity = (int) hit[ids::v];

        r.ctx.beginEdit ("Edit notes");
        anchor = hit;
        downBeat = r.xToBeat ((float) e.x);
        downPitch = r.yToPitch ((float) e.y - headerHeight);
        for (auto& n : r.selected) { dragNotes.add (n); starts.add (n.createCopy()); }
        const auto rect = noteRect (hit);
        mode = e.x > rect.getRight() - juce::jmin (8.0f, rect.getWidth() / 3.0f) ? Mode::resize : Mode::move;
        copied = false;
        r.repaintAll();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto& r = roll;
        if (! r.clip.isValid()) return;
        auto& p = r.ctx.project;
        const bool free = e.mods.isShiftDown();

        if (mode == Mode::clipEnd)
        {
            const double len = juce::jmax (r.grid, r.snapBeat (r.xToBeat ((float) e.x), free));
            r.clip.setProperty (ids::length, len, p.um());
            return;
        }
        if (mode == Mode::marquee)
        {
            marquee = juce::Rectangle<int> (marqueeStart, e.getPosition());
            if (! e.mods.isShiftDown()) r.selected.clear();
            for (auto n : r.clip)
                if (n.hasType (ids::NOTE) && noteRect (n).toNearestInt().intersects (marquee) && ! r.selected.contains (n))
                    r.selected.add (n);
            r.repaintAll();
            return;
        }
        if (mode == Mode::none || ! anchor.isValid()) return;

        const double beatNow = r.xToBeat ((float) e.x);
        if (mode == Mode::move)
        {
            if (e.mods.isAltDown() && ! copied)
            {
                for (auto& s : starts) r.clip.appendChild (s.createCopy(), p.um());
                copied = true;
            }
            const double anchorStart = (double) starts[dragNotes.indexOf (anchor)][ids::s];
            const double newStart = r.snapBeat (anchorStart + (beatNow - downBeat), free);
            const double delta = newStart - anchorStart;
            const int dp = r.drumMode && false ? 0 : r.yToPitch ((float) e.y - headerHeight) - downPitch;
            for (int i = 0; i < dragNotes.size(); ++i)
            {
                auto n = dragNotes[i];
                const int newPitch = juce::jlimit (0, 127, (int) starts[i][ids::p] + dp);
                if ((int) n[ids::p] != newPitch && n == anchor) r.previewNote (newPitch, (int) n[ids::v]);
                n.setProperty (ids::s, (double) starts[i][ids::s] + delta, p.um());
                n.setProperty (ids::p, newPitch, p.um());
            }
        }
        else if (mode == Mode::resize)
        {
            const double anchorEnd = (double) starts[dragNotes.indexOf (anchor)][ids::s] + (double) starts[dragNotes.indexOf (anchor)][ids::l];
            const double newEnd = r.snapBeat (anchorEnd + (beatNow - downBeat), free);
            const double delta = newEnd - anchorEnd;
            for (int i = 0; i < dragNotes.size(); ++i)
            {
                const double l = juce::jmax (1.0 / 64.0, (double) starts[i][ids::l] + delta);
                dragNotes[i].setProperty (ids::l, l, p.um());
                if (dragNotes[i] == anchor) r.lastLength = l;
            }
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        mode = Mode::none;
        marquee = {};
        anchor = {};
        roll.repaintAll();
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        if (e.y < headerHeight)
        {
            setMouseCursor (std::abs (e.x - roll.beatToX (Clip (roll.clip).midiLength())) < 8 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
            return;
        }
        auto hit = noteAt (e.position);
        if (hit.isValid())
        {
            const auto rect = noteRect (hit);
            setMouseCursor (e.x > rect.getRight() - juce::jmin (8.0f, rect.getWidth() / 3.0f) ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor);
        }
        else setMouseCursor (roll.drawMode ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::NormalCursor);
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        auto& r = roll;
        if (e.mods.isCommandDown() || e.mods.isCtrlDown())
        {
            const double anchorBeat = r.xToBeat ((float) e.x);
            r.ppb = juce::jlimit (10.0, 1200.0, r.ppb * std::pow (1.0025, w.deltaY * 400.0));
            r.scrollBeats = anchorBeat - e.x / r.ppb;
        }
        else if (e.mods.isAltDown())
        {
            r.rowH = juce::jlimit (6.0f, 30.0f, r.rowH + w.deltaY * 8.0f);
        }
        else if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
        {
            const float d = std::abs (w.deltaX) > 0.0f ? w.deltaX : w.deltaY;
            r.scrollBeats -= d * 200.0 / r.ppb;
        }
        else
        {
            r.scrollY = juce::jlimit (0.0f, juce::jmax (0.0f, 128 * r.rowH - (getHeight() - headerHeight)), r.scrollY - w.deltaY * 240.0f);
        }
        r.scrollBeats = juce::jmax (-1.0, r.scrollBeats);
        r.resized();
        r.repaintAll();
    }

    void menu()
    {
        auto& r = roll;
        juce::PopupMenu m;
        addMenuItem (m, 1, "Delete\tDel", ! r.selected.isEmpty());
        addMenuItem (m, 2, "Quantize\tQ");
        addMenuItem (m, 3, "Select All\tCtrl+A");
        m.addItem (4, "Transpose +1 octave");
        m.addItem (5, "Transpose -1 octave");
        juce::PopupMenu vel;
        for (int v : { 127, 110, 96, 80, 64, 40 }) vel.addItem (100 + v, juce::String (v));
        m.addSubMenu ("Set Velocity", vel, ! r.selected.isEmpty());
        m.addItem (6, "Legato (fill gaps)", ! r.selected.isEmpty());
        m.showMenuAsync (juce::PopupMenu::Options(), [this] (int res)
        {
            auto& rr = roll;
            auto& p = rr.ctx.project;
            if (res == 1) rr.deleteSelected();
            if (res == 2) rr.quantizeSelected();
            if (res == 3) rr.selectAll();
            if (res == 4) rr.transposeSelected (12);
            if (res == 5) rr.transposeSelected (-12);
            if (res >= 100) { rr.ctx.beginEdit ("Velocity"); for (auto& n : rr.selected) n.setProperty (ids::v, res - 100, p.um()); }
            if (res == 6)
            {
                rr.ctx.beginEdit ("Legato");
                auto sorted = rr.selected;
                std::sort (sorted.begin(), sorted.end(), [] (const juce::ValueTree& a, const juce::ValueTree& b) { return (double) a[ids::s] < (double) b[ids::s]; });
                for (int i = 0; i + 1 < sorted.size(); ++i)
                {
                    const double nextStart = sorted[i + 1][ids::s];
                    if (nextStart > (double) sorted[i][ids::s]) sorted[i].setProperty (ids::l, nextStart - (double) sorted[i][ids::s], p.um());
                }
            }
            rr.repaintAll();
        });
    }

private:
    enum class Mode { none, move, resize, marquee, clipEnd };
    PianoRoll& roll;
    Mode mode = Mode::none;
    juce::ValueTree anchor;
    juce::Array<juce::ValueTree> dragNotes, starts;
    double downBeat = 0;
    int downPitch = 0;
    bool copied = false;
    juce::Point<int> marqueeStart;
    juce::Rectangle<int> marquee;
};

// =====================================================================================================
//  Keys
// =====================================================================================================
class PianoRoll::Keys : public juce::Component
{
public:
    explicit Keys (PianoRoll& o) : roll (o) {}

    void paint (juce::Graphics& g) override
    {
        auto& r = roll;
        g.fillAll (theme::panel);
        g.setFont (uiFont (juce::jmin (11.0f, r.rowH - 1.0f)));
        for (int p = 0; p < 128; ++p)
        {
            const float y = r.pitchToY (p) + headerHeight;
            if (y > getHeight() || y + r.rowH < headerHeight) continue;
            if (r.drumMode)
            {
                const auto name = gmDrumName (p);
                g.setColour (name.isNotEmpty() ? juce::Colour (0xff1d212a) : juce::Colour (0xff15181e));
                g.fillRect (0.0f, y, (float) getWidth(), r.rowH - 0.5f);
                g.setColour (name.isNotEmpty() ? theme::text : theme::textFaint);
                g.drawText (name.isNotEmpty() ? name : juce::MidiMessage::getMidiNoteName (p, true, true, 4), juce::Rectangle<float> (4.0f, y, (float) getWidth() - 6, r.rowH), juce::Justification::centredLeft);
            }
            else
            {
                const bool black = isBlackKey (p);
                g.setColour (p == pressed ? theme::accent : black ? juce::Colour (0xff1a1a1a) : juce::Colour (0xffe8e8e8));
                g.fillRect (black ? 0.0f : 0.0f, y, black ? getWidth() * 0.62f : (float) getWidth(), r.rowH - 0.5f);
                if (p % 12 == 0)
                {
                    g.setColour (juce::Colour (0xff333333));
                    g.drawText ("C" + juce::String (p / 12 - 1), juce::Rectangle<float> (0, y, (float) getWidth() - 4, r.rowH), juce::Justification::centredRight);
                }
            }
        }
        g.setColour (theme::panel);
        g.fillRect (0, 0, getWidth(), headerHeight);
    }

    void mouseDown (const juce::MouseEvent& e) override { play (e); }
    void mouseDrag (const juce::MouseEvent& e) override { if (roll.yToPitch ((float) e.y - headerHeight) != pressed) play (e); }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (pressed >= 0) roll.ctx.engine.keyboardState.noteOff (1, pressed, 0.0f);
        pressed = -1;
        repaint();
    }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        roll.scrollY = juce::jlimit (0.0f, juce::jmax (0.0f, 128 * roll.rowH - (getHeight() - headerHeight)), roll.scrollY - w.deltaY * 240.0f);
        juce::ignoreUnused (e);
        roll.resized();
        roll.repaintAll();
    }

private:
    void play (const juce::MouseEvent& e)
    {
        if (pressed >= 0) roll.ctx.engine.keyboardState.noteOff (1, pressed, 0.0f);
        pressed = roll.yToPitch ((float) e.y - headerHeight);
        roll.ctx.engine.keyboardState.noteOn (1, pressed, 0.8f);
        repaint();
    }
    PianoRoll& roll;
    int pressed = -1;
};

// =====================================================================================================
//  Velocity lane
// =====================================================================================================
class PianoRoll::Velocity : public juce::Component
{
public:
    explicit Velocity (PianoRoll& o) : roll (o) {}

    void paint (juce::Graphics& g) override
    {
        auto& r = roll;
        g.fillAll (theme::panel);
        g.setColour (theme::outline);
        g.drawHorizontalLine (0, 0.0f, (float) getWidth());
        if (! r.clip.isValid()) return;
        const auto col = r.ctx.project.trackForClip (r.clip).colour();
        for (auto n : r.clip)
        {
            if (! n.hasType (ids::NOTE)) continue;
            const float x = r.beatToX ((double) n[ids::s]);
            if (x < -4 || x > getWidth()) continue;
            const float v = (float) (int) n[ids::v] / 127.0f;
            const float h = (getHeight() - 8) * v;
            const bool sel = r.selected.contains (n);
            g.setColour (sel ? col.brighter (0.6f) : col);
            g.fillRect (x, getHeight() - h, 3.0f, h);
            g.fillEllipse (x - 2.0f, getHeight() - h - 3.0f, 7.0f, 7.0f);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override { roll.ctx.beginEdit ("Velocity"); edit (e); }
    void mouseDrag (const juce::MouseEvent& e) override { edit (e); }

private:
    void edit (const juce::MouseEvent& e)
    {
        auto& r = roll;
        if (! r.clip.isValid()) return;
        const int vel = juce::jlimit (1, 127, (int) std::round ((1.0f - (float) e.y / (float) (getHeight() - 8)) * 127.0f + 6.0f));
        // notes under the mouse (within 4 px); if any are selected, only those
        for (auto n : r.clip)
        {
            if (! n.hasType (ids::NOTE)) continue;
            if (! r.selected.isEmpty() && ! r.selected.contains (n)) continue;
            if (std::abs (r.beatToX ((double) n[ids::s]) - e.x) <= (r.selected.size() > 1 ? 4000 : 5))
                n.setProperty (ids::v, vel, r.ctx.project.um());
        }
        r.lastVelocity = vel;
        r.repaintAll();
    }
    PianoRoll& roll;
};

// =====================================================================================================
//  PianoRoll
// =====================================================================================================
PianoRoll::PianoRoll (StudioContext& c) : ctx (c)
{
    gridView = std::make_unique<Grid> (*this);
    keys = std::make_unique<Keys> (*this);
    velocity = std::make_unique<Velocity> (*this);
    addAndMakeVisible (*gridView);
    addAndMakeVisible (*keys);
    addAndMakeVisible (*velocity);
    addAndMakeVisible (hScroll);
    addAndMakeVisible (vScroll);
    hScroll.setAutoHide (false);
    vScroll.setAutoHide (false);
    hScroll.addListener (this);
    vScroll.addListener (this);

    title.setFont (uiFont (13.5f, true));
    addAndMakeVisible (title);

    drawButton.setClickingTogglesState (true);
    drawButton.setTooltip ("Draw mode: click to add notes (or double-click anywhere in normal mode)");
    drawButton.onClick = [this] { drawMode = drawButton.getToggleState(); };
    quantizeButton.setTooltip ("Snap note starts to the grid (Q)");
    quantizeButton.onClick = [this] { quantizeSelected(); };
    octDown.onClick = [this] { transposeSelected (-12); };
    octUp.onClick = [this] { transposeSelected (12); };
    duplicateButton.setTooltip ("Duplicate the selected notes right after themselves (Ctrl+D)");
    duplicateButton.onClick = [this]
    {
        if (selected.isEmpty()) selectAll();
        double lo = 1e9, hi = -1e9;
        for (auto& n : selected) { lo = juce::jmin (lo, (double) n[ids::s]); hi = juce::jmax (hi, (double) n[ids::s] + (double) n[ids::l]); }
        const double shift = std::ceil ((hi - lo) / grid - 1e-9) * grid;
        ctx.beginEdit ("Duplicate notes");
        juce::Array<juce::ValueTree> copies;
        for (auto& n : selected)
        {
            auto copy = n.createCopy();
            copy.setProperty (ids::s, (double) n[ids::s] + shift, nullptr);
            clip.appendChild (copy, ctx.project.um());
            copies.add (copy);
        }
        selected = copies;
        const double needed = hi + shift;
        if (needed > (double) clip[ids::length]) clip.setProperty (ids::length, std::ceil (needed / ctx.project.beatsPerBar()) * ctx.project.beatsPerBar(), ctx.project.um());
        repaintAll();
    };
    for (auto* b : { &drawButton, &quantizeButton, &octDown, &octUp, &duplicateButton }) addAndMakeVisible (b);
    drawButton.setColour (juce::TextButton::buttonOnColourId, theme::accent);

    gridBox.addItemList ({ "1/4", "1/8", "1/16", "1/32", "1/8T", "1/16T" }, 1);
    gridBox.setSelectedItemIndex (2, juce::dontSendNotification);
    gridBox.setTooltip ("Note grid");
    gridBox.onChange = [this]
    {
        const double v[] = { 1.0, 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0 };
        grid = v[juce::jlimit (0, 5, gridBox.getSelectedItemIndex())];
        lastLength = grid;
        repaintAll();
    };
    addAndMakeVisible (gridBox);

    velLabel.setText ("Velocity", juce::dontSendNotification);
    velLabel.setFont (uiFont (12.0f));
    velLabel.setColour (juce::Label::textColourId, theme::textDim);
    addAndMakeVisible (velLabel);
    velSlider.setRange (1, 127, 1);
    velSlider.setValue (100, juce::dontSendNotification);
    velSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 34, 20);
    velSlider.setTooltip ("Velocity of new notes / the selected notes");
    velSlider.onDragStart = [this] { ctx.beginEdit ("Velocity"); };
    velSlider.onValueChange = [this]
    {
        lastVelocity = (int) velSlider.getValue();
        for (auto& n : selected) n.setProperty (ids::v, lastVelocity, ctx.project.um());
        repaintAll();
    };
    addAndMakeVisible (velSlider);

    setWantsKeyboardFocus (true);
    startTimerHz (30);
}

PianoRoll::~PianoRoll()
{
    if (clip.isValid()) clip.removeListener (this);
    if (previewPitch >= 0) ctx.engine.keyboardState.noteOff (1, previewPitch, 0.0f);
}

void PianoRoll::setClip (int clipId)
{
    if (clip.isValid()) clip.removeListener (this);
    clip = ctx.project.clipById (clipId).v;
    selected.clear();
    if (clip.isValid())
    {
        clip.addListener (this);
        auto t = ctx.project.trackForClip (clip);
        auto inst = t.instrument();
        drumMode = false;
        if (inst.isValid() && inst[ids::uid].toString() == "soundfont" && inst[ids::name].toString().containsIgnoreCase ("drum"))
            drumMode = true;
        if (t.name().containsIgnoreCase ("drum")) drumMode = true;
        title.setText (t.name() + "  -  " + clip[ids::name].toString(), juce::dontSendNotification);
        scrollBeats = -0.25;
        ppb = juce::jlimit (20.0, 400.0, (getWidth() - 200) / juce::jmax (4.0, (double) clip[ids::length] + 0.5));
        if (t.isValid()) ctx.selectTrack (t.id());   // so the on-screen keys play this instrument
    }
    resized();
    if (clip.isValid()) centreOnNotes();
    repaintAll();
}

void PianoRoll::centreOnNotes()
{
    int lo = 127, hi = 0, count = 0;
    for (auto n : clip)
        if (n.hasType (ids::NOTE)) { lo = juce::jmin (lo, (int) n[ids::p]); hi = juce::jmax (hi, (int) n[ids::p]); ++count; }
    const int centre = count > 0 ? (lo + hi) / 2 : (drumMode ? 42 : 60);
    const float visible = (float) juce::jmax (60, gridView->getHeight() - headerHeight);
    float y = (127 - centre) * rowH - visible * 0.5f;
    if (count > 0 && (hi - lo + 3) * rowH > visible)
        y = (127 - lo + 2) * rowH - visible;   // doesn't fit: keep the lowest notes (bass line / kick) in view
    scrollY = juce::jlimit (0.0f, juce::jmax (0.0f, 128 * rowH - visible), y);
    vScroll.setCurrentRangeStart (scrollY, juce::dontSendNotification);
}

void PianoRoll::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& child, int)
{
    selected.removeFirstMatchingValue (child);
    repaintAll();
}

void PianoRoll::repaintAll()
{
    gridView->repaint();
    keys->repaint();
    velocity->repaint();
    if (clip.isValid())
    {
        const double len = clip[ids::length];
        hScroll.setRangeLimits (-1.0, juce::jmax (len + 8.0, scrollBeats + gridView->getWidth() / ppb));
        hScroll.setCurrentRange (scrollBeats, gridView->getWidth() / ppb, juce::dontSendNotification);
        vScroll.setRangeLimits (0.0, 128.0 * rowH);
        vScroll.setCurrentRange (scrollY, gridView->getHeight() - headerHeight, juce::dontSendNotification);
    }
}

void PianoRoll::previewNote (int pitch, int velocity)
{
    if (ctx.engine.isPlaying()) return;
    if (previewPitch >= 0) ctx.engine.keyboardState.noteOff (1, previewPitch, 0.0f);
    previewPitch = pitch;
    previewCountdown = 8;
    ctx.engine.keyboardState.noteOn (1, pitch, (float) velocity / 127.0f);
}

void PianoRoll::timerCallback()
{
    if (previewPitch >= 0 && --previewCountdown <= 0)
    {
        ctx.engine.keyboardState.noteOff (1, previewPitch, 0.0f);
        previewPitch = -1;
    }
    if (ctx.engine.isPlaying()) gridView->repaint();
    if (! selected.isEmpty() && ! velSlider.isMouseButtonDown())
        velSlider.setValue ((int) selected.getFirst()[ids::v], juce::dontSendNotification);
}

void PianoRoll::deleteSelected()
{
    if (selected.isEmpty() || ! clip.isValid()) return;
    ctx.beginEdit ("Delete notes");
    for (auto& n : selected) clip.removeChild (n, ctx.project.um());
    selected.clear();
}

void PianoRoll::transposeSelected (int semis)
{
    if (selected.isEmpty()) selectAll();
    ctx.beginEdit ("Transpose");
    for (auto& n : selected) n.setProperty (ids::p, juce::jlimit (0, 127, (int) n[ids::p] + semis), ctx.project.um());
}

void PianoRoll::nudgeSelected (double beats)
{
    ctx.beginEdit ("Nudge");
    for (auto& n : selected) n.setProperty (ids::s, (double) n[ids::s] + beats, ctx.project.um());
}

void PianoRoll::quantizeSelected()
{
    if (! clip.isValid()) return;
    ctx.beginEdit ("Quantize");
    const double clipStart = clip[ids::start];
    for (auto n : clip)
    {
        if (! n.hasType (ids::NOTE)) continue;
        if (! selected.isEmpty() && ! selected.contains (n)) continue;
        const double abs = clipStart + (double) n[ids::s];
        n.setProperty (ids::s, std::round (abs / grid) * grid - clipStart, ctx.project.um());
    }
    ctx.setStatus ("Quantized to " + gridBox.getText());
}

void PianoRoll::selectAll()
{
    selected.clear();
    for (auto n : clip) if (n.hasType (ids::NOTE)) selected.add (n);
    repaintAll();
}

bool PianoRoll::keyPressed (const juce::KeyPress& k)
{
    if (! clip.isValid()) return false;
    const bool shift = k.getModifiers().isShiftDown();
    if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) { deleteSelected(); return true; }
    if (k == juce::KeyPress::upKey)    { transposeSelected (shift ? 12 : 1); return true; }
    if (k == juce::KeyPress::downKey)  { transposeSelected (shift ? -12 : -1); return true; }
    if (k == juce::KeyPress::leftKey)  { nudgeSelected (-grid); return true; }
    if (k == juce::KeyPress::rightKey) { nudgeSelected (grid); return true; }
    if (k == juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0)) { selectAll(); return true; }
    if (k == juce::KeyPress ('d', juce::ModifierKeys::commandModifier, 0)) { duplicateButton.triggerClick(); return true; }
    if (k.getTextCharacter() == 'q' || k.getTextCharacter() == 'Q') { quantizeSelected(); return true; }
    if (k.getTextCharacter() == 'b' || k.getTextCharacter() == 'B') { drawButton.triggerClick(); return true; }
    return false;
}

void PianoRoll::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    if (! clip.isValid())
    {
        g.setColour (theme::textDim);
        g.setFont (uiFont (14.0f));
        g.drawText ("Double-click a MIDI clip (or an empty spot on an instrument track) to edit its notes here",
                    getLocalBounds(), juce::Justification::centred);
    }
}

void PianoRoll::resized()
{
    auto r = getLocalBounds();
    auto tb = r.removeFromTop (toolbarHeight).reduced (8, 5);
    title.setBounds (tb.removeFromLeft (juce::jmin (260, tb.getWidth() / 4)));
    drawButton.setBounds (tb.removeFromLeft (60)); tb.removeFromLeft (6);
    gridBox.setBounds (tb.removeFromLeft (76)); tb.removeFromLeft (6);
    quantizeButton.setBounds (tb.removeFromLeft (80)); tb.removeFromLeft (6);
    duplicateButton.setBounds (tb.removeFromLeft (84)); tb.removeFromLeft (6);
    octDown.setBounds (tb.removeFromLeft (56)); tb.removeFromLeft (4);
    octUp.setBounds (tb.removeFromLeft (56)); tb.removeFromLeft (12);
    velLabel.setBounds (tb.removeFromLeft (58));
    velSlider.setBounds (tb.removeFromLeft (180));

    const bool hasClip = clip.isValid();
    for (juce::Component* c : { (juce::Component*) gridView.get(), (juce::Component*) keys.get(), (juce::Component*) velocity.get(),
                                (juce::Component*) &hScroll, (juce::Component*) &vScroll, (juce::Component*) &drawButton,
                                (juce::Component*) &gridBox, (juce::Component*) &quantizeButton, (juce::Component*) &duplicateButton,
                                (juce::Component*) &octDown, (juce::Component*) &octUp, (juce::Component*) &velSlider, (juce::Component*) &velLabel })
        c->setVisible (hasClip);

    const int kw = drumMode ? keysWidthDrums : keysWidthNormal;
    hScroll.setBounds (r.removeFromBottom (12).withTrimmedLeft (kw).withTrimmedRight (12));
    auto vel = r.removeFromBottom (velocityHeight);
    vScroll.setBounds (r.removeFromRight (12));
    vel.removeFromRight (12);
    keys->setBounds (r.removeFromLeft (kw));
    vel.removeFromLeft (kw);
    gridView->setBounds (r);
    velocity->setBounds (vel);
    repaintAll();
}

// =====================================================================================================
//  AudioClipEditor
// =====================================================================================================
AudioClipEditor::AudioClipEditor (StudioContext& c) : ctx (c)
{
    ctx.engine.getCache().addChangeListener (this);
    title.setFont (uiFont (14.0f, true));
    info.setFont (uiFont (12.0f));
    info.setColour (juce::Label::textColourId, theme::textDim);
    addAndMakeVisible (title);
    addAndMakeVisible (info);

    auto setup = [this] (juce::Slider& s, juce::Label& l, const juce::String& name, double lo, double hi, const juce::String& suffix)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
        s.setRange (lo, hi, 0.01);
        s.setTextValueSuffix (suffix);
        s.onDragStart = [this] { ctx.beginEdit ("Clip"); };
        l.setText (name, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centred);
        l.setFont (uiFont (11.5f, true));
        l.setColour (juce::Label::textColourId, theme::textDim);
        addAndMakeVisible (s);
        addAndMakeVisible (l);
    };
    setup (gain, gainL, "CLIP GAIN", -24.0, 18.0, " dB");
    setup (fadeIn, fadeInL, "FADE IN", 0.0, 10.0, " s");
    setup (fadeOut, fadeOutL, "FADE OUT", 0.0, 10.0, " s");
    setup (speed, speedL, "SPEED", 25.0, 400.0, " %");
    setup (pitch, pitchL, "PITCH", -12.0, 12.0, " st");
    speed.setSkewFactorFromMidPoint (100.0);
    speed.setDoubleClickReturnValue (true, 100.0);
    pitch.setDoubleClickReturnValue (true, 0.0);
    speed.setTooltip ("Time-stretch: change the speed without changing the pitch");
    pitch.setTooltip ("Transpose the audio without changing its speed");
    speed.onValueChange = [this]
    {
        if (! clip.isValid()) return;
        Clip c (clip);
        // keep tempo-following: the speed knob scales on top of it
        clip.setProperty (ids::stretch, 100.0 / juce::jmax (1.0, speed.getValue()) / (c.follows() ? c.srcTempo() / ctx.project.tempo() : 1.0), ctx.project.um());
    };
    pitch.onValueChange = [this] { if (clip.isValid()) clip.setProperty (ids::pitch, std::round (pitch.getValue() * 10.0) / 10.0, ctx.project.um()); };
    follow.onClick = [this]
    {
        if (! clip.isValid()) return;
        Clip c (clip);
        double bpm = c.srcTempo();
        if (follow.getToggleState() && bpm <= 0.0)
        {
            auto data = ctx.engine.getCache().getBlocking (ctx.project.resolve (c.file()), ctx.engine.getSampleRate());
            if (data != nullptr) bpm = estimateTempo (data->buffer, data->sampleRate).bpm;
            if (bpm <= 0.0) { ctx.setStatus ("Couldn't find a steady tempo in this clip."); follow.setToggleState (false, juce::dontSendNotification); return; }
            ctx.setStatus ("Detected " + juce::String (bpm, 1) + " BPM: the clip now follows the song's tempo.");
        }
        ctx.beginEdit ("Follow tempo");
        ctx.project.setClipFollowTempo (c, bpm, follow.getToggleState());
    };
    reverse.onClick = [this] { if (clip.isValid()) { ctx.beginEdit ("Reverse"); clip.setProperty (ids::reverse, reverse.getToggleState(), ctx.project.um()); } };
    normalize.onClick = [this]
    {
        if (! clip.isValid()) return;
        Clip c (clip);
        auto data = ctx.engine.getCache().getBlocking (ctx.project.resolve (c.file()), ctx.engine.getSampleRate());
        if (data == nullptr) return;
        const int a = juce::jlimit (0, data->buffer.getNumSamples(), (int) (c.offsetSeconds() * data->sampleRate));
        const int b = juce::jlimit (a, data->buffer.getNumSamples(), (int) ((c.offsetSeconds() + c.lengthSeconds()) * data->sampleRate));
        float peak = 0.0f;
        for (int ch = 0; ch < data->buffer.getNumChannels(); ++ch) peak = juce::jmax (peak, data->buffer.getMagnitude (ch, a, b - a));
        if (peak <= 1.0e-6f) return;
        ctx.beginEdit ("Normalize");
        clip.setProperty (ids::gain, -0.3 - juce::Decibels::gainToDecibels (peak), ctx.project.um());
    };
    for (auto* b : std::initializer_list<juce::Component*> { &follow, &reverse, &normalize }) addAndMakeVisible (b);
    fadeIn.setSkewFactorFromMidPoint (1.0);
    fadeOut.setSkewFactorFromMidPoint (1.0);
    gain.setDoubleClickReturnValue (true, 0.0);
    gain.onValueChange = [this] { if (clip.isValid()) clip.setProperty (ids::gain, gain.getValue(), ctx.project.um()); };
    fadeIn.onValueChange = [this] { if (clip.isValid()) clip.setProperty (ids::fadeIn, fadeIn.getValue(), ctx.project.um()); };
    fadeOut.onValueChange = [this] { if (clip.isValid()) clip.setProperty (ids::fadeOut, fadeOut.getValue(), ctx.project.um()); };

    takeBox.onChange = [this]
    {
        if (! clip.isValid() || takeBox.getSelectedItemIndex() < 0) return;
        ctx.beginEdit ("Choose take");
        ctx.project.setTake (Clip (clip), takeBox.getSelectedItemIndex());
    };
    addAndMakeVisible (takeBox);
}

AudioClipEditor::~AudioClipEditor()
{
    ctx.engine.getCache().removeChangeListener (this);
    if (clip.isValid()) clip.removeListener (this);
}

void AudioClipEditor::setClip (int id)
{
    if (clip.isValid()) clip.removeListener (this);
    clip = ctx.project.clipById (id).v;
    if (clip.isValid()) clip.addListener (this);
    sync();
}

void AudioClipEditor::sync()
{
    if (! clip.isValid()) return;
    Clip c (clip);
    title.setText (ctx.project.trackForClip (clip).name() + "  -  " + c.name(), juce::dontSendNotification);
    info.setText (ctx.project.resolve (c.file()).getFileName() + "   |   " + formatSeconds (c.lengthSeconds()) + " long, starts "
                  + juce::String (c.offsetSeconds(), 2) + " s into the file", juce::dontSendNotification);
    gain.setValue (c.gainDb(), juce::dontSendNotification);
    fadeIn.setValue (c.fadeIn(), juce::dontSendNotification);
    fadeOut.setValue (c.fadeOut(), juce::dontSendNotification);
    fadeIn.setRange (0.0, juce::jmax (0.1, c.lengthSeconds() * 0.9), 0.01);
    fadeOut.setRange (0.0, juce::jmax (0.1, c.lengthSeconds() * 0.9), 0.01);
    speed.setValue (100.0 / c.stretchRatio (ctx.project.tempo()), juce::dontSendNotification);
    pitch.setValue (c.pitch(), juce::dontSendNotification);
    follow.setToggleState (c.follows(), juce::dontSendNotification);
    reverse.setToggleState (c.reversed(), juce::dontSendNotification);
    if (c.follows()) follow.setButtonText ("Follow song tempo (" + juce::String (c.srcTempo(), 1) + " BPM)");
    else follow.setButtonText ("Follow song tempo");

    takeBox.clear (juce::dontSendNotification);
    int n = 0;
    for (auto t : clip) if (t.hasType (ids::TAKE)) { ++n; takeBox.addItem ("Take " + juce::String (n), n); }
    takeBox.setVisible (n > 0);
    if (n > 0) takeBox.setSelectedItemIndex ((int) clip[ids::take], juce::dontSendNotification);
    repaint();
}

void AudioClipEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    if (! clip.isValid()) return;
    auto r = getLocalBounds().reduced (16).withTrimmedTop (50).withTrimmedRight (530);
    Clip c (clip);
    auto data = ctx.engine.getCache().get (ctx.project.resolve (c.file()), ctx.engine.getSampleRate());
    g.setColour (theme::bg);
    g.fillRoundedRectangle (r.toFloat(), 6.0f);
    if (data == nullptr || data->peaks.empty()) return;
    const auto col = ctx.project.trackForClip (clip).colour();
    const double total = data->lengthSeconds();
    const double pps = data->sampleRate / AudioData::peakStep;
    // whole file dimmed, the clip's region bright
    for (int x = 0; x < r.getWidth(); ++x)
    {
        const double t0 = total * x / r.getWidth(), t1 = total * (x + 1) / r.getWidth();
        float pk = 0;
        for (int i = (int) (t0 * pps); i < juce::jmin ((int) data->peaks.size(), (int) (t1 * pps) + 1); ++i) pk = juce::jmax (pk, data->peaks[(size_t) i]);
        const bool inClip = t0 >= c.offsetSeconds() && t0 <= c.offsetSeconds() + c.lengthSeconds();
        g.setColour (inClip ? col.brighter (0.3f) : theme::textFaint.withAlpha (0.4f));
        const float h = juce::jmin (1.0f, pk) * r.getHeight() * 0.48f;
        g.drawVerticalLine (r.getX() + x, r.getCentreY() - h, r.getCentreY() + h);
    }
}

void AudioClipEditor::resized()
{
    auto r = getLocalBounds().reduced (16, 10);
    auto top = r.removeFromTop (40);
    title.setBounds (top.removeFromTop (20));
    info.setBounds (top);
    auto right = r.removeFromRight (520);
    auto knobs = right.removeFromTop (110);
    for (auto [s, l] : { std::pair<juce::Slider*, juce::Label*> { &gain, &gainL }, { &fadeIn, &fadeInL }, { &fadeOut, &fadeOutL }, { &speed, &speedL }, { &pitch, &pitchL } })
    {
        auto col = knobs.removeFromLeft (100);
        l->setBounds (col.removeFromTop (16));
        s->setBounds (col);
    }
    auto row = right.removeFromTop (28);
    follow.setBounds (row.removeFromLeft (230));
    reverse.setBounds (row.removeFromLeft (100));
    normalize.setBounds (row.removeFromLeft (100).reduced (2, 1));
    right.removeFromTop (4);
    takeBox.setBounds (right.removeFromTop (28).withWidth (200).reduced (0, 2));
}

} // namespace wis::daw
