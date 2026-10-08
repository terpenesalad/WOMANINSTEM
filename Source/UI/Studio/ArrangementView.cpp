#include "ArrangementView.h"
#include "Daw/Model/TempoDetect.h"
#include "Daw/Instruments/Sampler.h"

namespace wis::daw
{

static constexpr int rulerHeight = 42;
static constexpr int cycleStripH = 18;   // the yellow cycle (repeat) strip at the top of the ruler
static constexpr int rulerTextY = cycleStripH + 2;
static constexpr int bottomBar = 18;
static constexpr int laneExtra = 72;

// automation value <-> 0..1  (lanes: "volume", "pan", "send:<bus id>" in dB, "plug:<slot>:<param index>" 0..1)
static float autoNorm (const juce::String& param, float v)
{
    if (param == "pan") return (v + 1.0f) * 0.5f;
    if (param.startsWith ("plug:")) return juce::jlimit (0.0f, 1.0f, v);
    return juce::jlimit (0.0f, 1.0f, std::pow ((juce::jlimit (-60.0f, 6.0f, v) + 60.0f) / 66.0f, 2.0f));
}
static float autoValue (const juce::String& param, float n)
{
    n = juce::jlimit (0.0f, 1.0f, n);
    if (param == "pan") return n * 2.0f - 1.0f;
    if (param.startsWith ("plug:")) return n;
    return std::sqrt (n) * 66.0f - 60.0f;
}
static juce::String plugSlot (const juce::String& param)  { return param.fromFirstOccurrenceOf (":", false, false).upToFirstOccurrenceOf (":", false, false); }
static int plugIndex (const juce::String& param)          { return param.fromLastOccurrenceOf (":", false, false).getIntValue(); }

// =====================================================================================================
//  Ruler: bar numbers, playhead positioning, cycle region (top strip)
// =====================================================================================================
class ArrangementView::Ruler : public juce::Component
{
public:
    explicit Ruler (ArrangementView& o) : owner (o) {}

    void paint (juce::Graphics& g) override
    {
        auto& ctx = owner.ctx;
        auto& p = ctx.project;
        g.fillAll (theme::panel);

        // cycle strip
        const bool cycleOn = p.tree()[ids::cycleOn];
        const float cx0 = owner.beatToX ((double) p.tree()[ids::cycleStart]);
        const float cx1 = owner.beatToX ((double) p.tree()[ids::cycleEnd]);
        g.setColour (theme::bg);
        g.fillRect (0, 0, getWidth(), cycleStripH);
        {
            const auto col = cycleOn ? juce::Colour (0xfff2b84b) : theme::textFaint.withAlpha (0.45f);
            juce::Rectangle<float> region (cx0, 2.0f, juce::jmax (4.0f, cx1 - cx0), (float) cycleStripH - 4.0f);
            g.setColour (col.withAlpha (cycleOn ? (hoverPart == 3 ? 1.0f : 0.85f) : 0.5f));
            g.fillRoundedRectangle (region, 3.0f);
            // edge grips: drag them to change where the repeat starts / ends
            for (int edge = 0; edge < 2; ++edge)
            {
                const float ex = edge == 0 ? region.getX() : region.getRight() - 7.0f;
                g.setColour (juce::Colours::black.withAlpha (hoverPart == edge + 1 ? 0.55f : 0.3f));
                g.fillRoundedRectangle (ex, region.getY(), 7.0f, region.getHeight(), 3.0f);
                g.setColour (juce::Colours::white.withAlpha (0.7f));
                g.drawVerticalLine ((int) ex + 2, region.getY() + 4, region.getBottom() - 4);
                g.drawVerticalLine ((int) ex + 4, region.getY() + 4, region.getBottom() - 4);
            }
            if (region.getWidth() > 90)
            {
                g.setColour (juce::Colours::black.withAlpha (0.75f));
                g.setFont (uiFont (10.0f, true));
                auto& pr = owner.ctx.project;
                g.drawText (juce::String (cycleOn ? "REPEAT  " : "REPEAT OFF  ")
                            + formatBarsBeats ((double) pr.tree()[ids::cycleStart], pr.beatsPerBar(), pr.tsDen(), false) + " - "
                            + formatBarsBeats ((double) pr.tree()[ids::cycleEnd], pr.beatsPerBar(), pr.tsDen(), false),
                            region.reduced (10, 0), juce::Justification::centred, true);
            }
        }

        // bars / beats
        const double ppb = ctx.pixelsPerBeat;
        const double bpb = p.beatsPerBar();
        int barStep = 1;
        while (barStep * bpb * ppb < 46.0) barStep *= 2;
        const double firstBar = std::floor (ctx.scrollBeats / bpb);
        g.setFont (uiFont (11.0f, true));
        for (double bar = firstBar; ; bar += 1.0)
        {
            const float x = owner.beatToX (bar * bpb);
            if (x > getWidth()) break;
            const bool labelled = ((int) bar % barStep) == 0;
            g.setColour (labelled ? theme::textDim : theme::outline);
            g.drawVerticalLine ((int) x, labelled ? (float) rulerTextY : rulerTextY + 11.0f, (float) getHeight());
            if (labelled)
                g.drawText (juce::String ((int) bar + 1), (int) x + 4, rulerTextY, 40, 16, juce::Justification::centredLeft);
            if (bpb * ppb > 60)
                for (int b = 1; b < (int) bpb; ++b)
                {
                    const float bx = owner.beatToX (bar * bpb + b);
                    g.setColour (theme::outline);
                    g.drawVerticalLine ((int) bx, rulerTextY + 14.0f, (float) getHeight());
                }
        }

        // song markers (Verse, Chorus...)
        g.setFont (uiFont (10.5f, true));
        for (auto m : p.markers())
        {
            const float mx = owner.beatToX ((double) m[ids::b]);
            if (mx < -120 || mx > getWidth()) continue;
            const auto name = m[ids::name].toString();
            const float w = juce::jmin (140.0f, (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), name) + 12.0f);
            juce::Rectangle<float> tag (mx, (float) rulerTextY, w, 14.0f);
            g.setColour (juce::Colour (0xff3b82f6));
            g.fillRoundedRectangle (tag, 3.0f);
            g.setColour (juce::Colours::white);
            g.drawText (name, tag.reduced (5, 0), juce::Justification::centredLeft, true);
        }

        // playhead marker
        const float px = owner.beatToX (ctx.engine.getPositionBeats());
        juce::Path tri;
        tri.addTriangle (px - 6, (float) rulerTextY - 1, px + 6, (float) rulerTextY - 1, px, rulerTextY + 9.0f);
        g.setColour (theme::accent);
        g.fillPath (tri);
        g.fillRect (px - 0.5f, rulerTextY + 7.0f, 1.0f, (float) getHeight() - rulerTextY - 7.0f);

        g.setColour (theme::outline);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
    }

    juce::ValueTree markerAt (juce::Point<int> pos) const
    {
        if (pos.y < cycleStripH || pos.y > rulerTextY + 16) return {};
        juce::ValueTree best;
        for (auto m : owner.ctx.project.markers())
        {
            const float mx = owner.beatToX ((double) m[ids::b]);
            if (pos.x >= mx - 2 && pos.x <= mx + 90) best = m;   // later markers win (drawn on top)
        }
        return best;
    }

    void renameMarker (juce::ValueTree m)
    {
        auto* w = new juce::AlertWindow ("Marker", "Name:", juce::MessageBoxIconType::NoIcon, this);
        w->addTextEditor ("name", m[ids::name].toString());
        w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        auto& ctx = owner.ctx;
        w->enterModalState (true, juce::ModalCallbackFunction::create ([&ctx, w, m] (int r) mutable
        {
            if (r == 1 && w->getTextEditorContents ("name").trim().isNotEmpty())
            { ctx.beginEdit ("Rename marker"); m.setProperty (ids::name, w->getTextEditorContents ("name").trim(), ctx.project.um()); }
        }), true);
    }

    void markerMenu (const juce::MouseEvent& e)
    {
        auto& ctx = owner.ctx;
        auto m = markerAt (e.getPosition());
        const double beat = ctx.snap (owner.xToBeat ((float) e.x), ctx.pixelsPerBeat);
        juce::PopupMenu menu;
        juce::PopupMenu names;
        int id = 10;
        static const juce::StringArray presets { "Intro", "Verse", "Pre-Chorus", "Chorus", "Bridge", "Solo", "Breakdown", "Drop", "Outro" };
        for (auto& n : presets) names.addItem (id++, n);
        menu.addSubMenu ("Add Marker Here", names);
        menu.addItem (2, "Add Marker at Playhead");
        if (m.isValid())
        {
            menu.addSeparator();
            menu.addItem (3, "Rename \"" + m[ids::name].toString() + "\"...");
            menu.addItem (4, "Delete Marker");
        }
        menu.showMenuAsync (juce::PopupMenu::Options(), [this, m, beat] (int r) mutable
        {
            auto& c = owner.ctx;
            auto& p = c.project;
            if (r >= 10) { c.beginEdit ("Add marker"); p.addMarker (beat, presets[r - 10]); }
            else if (r == 2) { c.beginEdit ("Add marker"); renameMarker (p.addMarker (c.engine.getPositionBeats(), "Marker " + juce::String (p.markers().getNumChildren() + 1))); }
            else if (r == 3) renameMarker (m);
            else if (r == 4) { c.beginEdit ("Delete marker"); p.markers().removeChild (m, p.um()); }
            owner.repaintAll();
        });
    }

    // ---- cycle (repeat) strip -----------------------------------------------------------------------
    enum CycleMode { cycleNone, cycleCreate, cycleStart, cycleEnd, cycleMove };

    /** 0 none, 1 start edge, 2 end edge, 3 inside the region. */
    int cyclePartAt (juce::Point<int> pos) const
    {
        if (pos.y >= cycleStripH) return 0;
        auto& t = owner.ctx.project.tree();
        const float x0 = owner.beatToX ((double) t[ids::cycleStart]), x1 = owner.beatToX ((double) t[ids::cycleEnd]);
        const float grab = juce::jmin (9.0f, juce::jmax (4.0f, (x1 - x0) / 3.0f));
        if (std::abs (pos.x - x0) <= grab) return 1;
        if (std::abs (pos.x - x1) <= grab) return 2;
        if (pos.x > x0 && pos.x < x1) return 3;
        return 0;
    }

    /** The cycle snaps to the grid, but never coarser than a bar (so it can always be set precisely). Shift = no snap. */
    double cycleSnap (double beat, bool free) const
    {
        auto& ctx = owner.ctx;
        if (free) return juce::jmax (0.0, beat);
        double g = ctx.gridBeats (ctx.pixelsPerBeat);
        if (g <= 0.0) g = 0.25;
        g = juce::jmin (g, ctx.project.beatsPerBar());
        return juce::jmax (0.0, std::round (beat / g) * g);
    }
    double cycleMinLength() const
    {
        auto& ctx = owner.ctx;
        const double g = ctx.gridBeats (ctx.pixelsPerBeat);
        return juce::jlimit (0.25, ctx.project.beatsPerBar(), g > 0.0 ? g : 0.25);
    }

    void setCycle (double start, double end, bool on = true)
    {
        auto& tree = owner.ctx.project.tree();
        if (end < start) std::swap (start, end);
        tree.setProperty (ids::cycleStart, juce::jmax (0.0, start), nullptr);
        tree.setProperty (ids::cycleEnd, juce::jmax (start + 0.0625, end), nullptr);
        tree.setProperty (ids::cycleOn, on, nullptr);
        // already past the new end while playing: jump back into the repeat
        auto& eng = owner.ctx.engine;
        if (on && eng.isPlaying() && eng.getPositionBeats() >= juce::jmax (start + 0.0625, end))
            eng.setPositionBeats (juce::jmax (0.0, start));
        owner.repaintAll();
    }

    void cycleMenu (const juce::MouseEvent& e)
    {
        auto& ctx = owner.ctx;
        auto& p = ctx.project;
        const double bpb = p.beatsPerBar();
        const double cs = p.tree()[ids::cycleStart], ce = p.tree()[ids::cycleEnd];
        const double clickBar = std::floor (owner.xToBeat ((float) e.x) / bpb) * bpb;
        // the section between the markers around the click
        double secStart = 0.0, secEnd = -1.0;
        for (auto m : p.markers())
        {
            const double b = m[ids::b];
            if (b <= owner.xToBeat ((float) e.x)) secStart = juce::jmax (secStart, b);
            else secEnd = secEnd < 0 ? b : juce::jmin (secEnd, b);
        }
        juce::PopupMenu m;
        m.addItem (1, (bool) p.tree()[ids::cycleOn] ? "Turn Repeat Off" : "Turn Repeat On");
        m.addSeparator();
        m.addItem (2, "Repeat This Bar");
        m.addItem (3, "Repeat 4 Bars From Here");
        m.addItem (4, "Repeat Selected Clips", ! ctx.selectedClips.isEmpty());
        m.addItem (5, "Repeat This Section (between markers)", secEnd > secStart);
        m.addSeparator();
        m.addItem (6, "Double Length");
        m.addItem (7, "Halve Length", ce - cs > cycleMinLength() * 1.5);
        m.addItem (8, "Move Forward (next section of the same length)");
        m.addItem (9, "Move Back", cs > 0.0);
        m.addSeparator();
        m.addItem (-1, "Drag the edges to resize, the middle to move, or drag empty strip to draw. Shift = no snap.", false);
        m.showMenuAsync (juce::PopupMenu::Options(), [this, clickBar, bpb, cs, ce, secStart, secEnd] (int r)
        {
            auto& c = owner.ctx;
            auto& pr = c.project;
            const double len = ce - cs;
            switch (r)
            {
                case 1: pr.tree().setProperty (ids::cycleOn, ! (bool) pr.tree()[ids::cycleOn], nullptr); owner.repaintAll(); break;
                case 2: setCycle (clickBar, clickBar + bpb); break;
                case 3: setCycle (clickBar, clickBar + 4 * bpb); break;
                case 4:
                {
                    double a = 1.0e9, b = 0.0;
                    for (int id : c.selectedClips)
                        if (auto cl = pr.clipById (id); cl.isValid()) { a = juce::jmin (a, cl.start()); b = juce::jmax (b, cl.endBeats (pr.tempo())); }
                    if (b > a) setCycle (a, b);
                    break;
                }
                case 5: setCycle (secStart, secEnd); break;
                case 6: setCycle (cs, cs + len * 2.0); break;
                case 7: setCycle (cs, cs + len * 0.5); break;
                case 8: setCycle (ce, ce + len); break;
                case 9: setCycle (juce::jmax (0.0, cs - len), juce::jmax (0.0, cs - len) + len); break;
                default: break;
            }
        });
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int part = cyclePartAt (e.getPosition());
        if (part != hoverPart) { hoverPart = part; repaint(); }
        setMouseCursor (part == 1 || part == 2 ? juce::MouseCursor::LeftRightResizeCursor
                      : part == 3 ? juce::MouseCursor::DraggingHandCursor
                      : e.y < cycleStripH ? juce::MouseCursor::IBeamCursor : juce::MouseCursor::NormalCursor);
    }
    void mouseExit (const juce::MouseEvent&) override { if (hoverPart != 0) { hoverPart = 0; repaint(); } }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (e.y < cycleStripH)
        {
            // double-click the strip: repeat that bar
            const double bpb = owner.ctx.project.beatsPerBar();
            const double bar = std::floor (owner.xToBeat ((float) e.x) / bpb) * bpb;
            setCycle (bar, bar + bpb);
            return;
        }
        if (auto m = markerAt (e.getPosition()); m.isValid()) renameMarker (m);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        auto& p = owner.ctx.project;
        cycleMode = cycleNone;
        movedCycle = false;
        ignoreDrag = false;
        if (e.y < cycleStripH)
        {
            if (e.mods.isPopupMenu()) { cycleMenu (e); ignoreDrag = true; return; }
            cs0 = p.tree()[ids::cycleStart];
            ce0 = p.tree()[ids::cycleEnd];
            downBeat = owner.xToBeat ((float) e.x);
            const int part = cyclePartAt (e.getPosition());
            cycleMode = part == 1 ? cycleStart : part == 2 ? cycleEnd : part == 3 ? cycleMove : cycleCreate;
            return;
        }
        if (e.mods.isPopupMenu()) { markerMenu (e); ignoreDrag = true; return; }
        if (auto m = markerAt (e.getPosition()); m.isValid())
        {
            owner.ctx.engine.setPositionBeats ((double) m[ids::b]);
            ignoreDrag = true;
            owner.repaintAll();
            return;
        }
        owner.ctx.engine.setPositionBeats (owner.ctx.snap (owner.xToBeat ((float) e.x), owner.ctx.pixelsPerBeat, e.mods.isShiftDown()));
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (ignoreDrag) return;
        auto& ctx = owner.ctx;
        const bool free = e.mods.isShiftDown();
        if (cycleMode != cycleNone)
        {
            if (! movedCycle && std::abs (e.getDistanceFromDragStartX()) < 3) return;
            movedCycle = true;
            const double now = owner.xToBeat ((float) e.x);
            const double minLen = free ? 0.0625 : cycleMinLength();
            switch (cycleMode)
            {
                case cycleStart: setCycle (juce::jmin (cycleSnap (now, free), ce0 - minLen), ce0, true); break;
                case cycleEnd:   setCycle (cs0, juce::jmax (cycleSnap (now, free), cs0 + minLen), true); break;
                case cycleMove:
                {
                    const double start = cycleSnap (cs0 + (now - downBeat), free);
                    setCycle (start, start + (ce0 - cs0), true);
                    break;
                }
                case cycleCreate:
                {
                    double a = cycleSnap (downBeat, free), b = cycleSnap (now, free);
                    if (std::abs (b - a) < minLen) b = a + (now >= downBeat ? minLen : -minLen);
                    setCycle (juce::jmin (a, b), juce::jmax (a, b), true);
                    break;
                }
                default: break;
            }
            ctx.setStatus ("Repeat: bar " + formatBarsBeats ((double) ctx.project.tree()[ids::cycleStart], ctx.project.beatsPerBar(), ctx.project.tsDen(), false)
                           + " to " + formatBarsBeats ((double) ctx.project.tree()[ids::cycleEnd], ctx.project.beatsPerBar(), ctx.project.tsDen(), false)
                           + (free ? "" : "   (hold Shift to move freely)"));
            return;
        }
        ctx.engine.setPositionBeats (ctx.snap (owner.xToBeat ((float) e.x), ctx.pixelsPerBeat, free));
        owner.repaintAll();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (ignoreDrag) return;
        // a click (no drag) on the region switches repeat on / off
        if (cycleMode == cycleMove && ! movedCycle)
        {
            auto& tree = owner.ctx.project.tree();
            tree.setProperty (ids::cycleOn, ! (bool) tree[ids::cycleOn], nullptr);
            owner.repaintAll();
        }
        cycleMode = cycleNone;
    }

    ArrangementView& owner;
    CycleMode cycleMode = cycleNone;
    double cs0 = 0, ce0 = 0, downBeat = 0;
    int hoverPart = 0;
    bool movedCycle = false, ignoreDrag = false;
};

// =====================================================================================================
//  Canvas: clips, grid, automation, recording, playhead
// =====================================================================================================
class ArrangementView::Canvas : public juce::Component
{
public:
    explicit Canvas (ArrangementView& o) : owner (o), ctx (o.ctx) {}

    enum class Zone { none, body, trimStart, trimEnd, fadeIn, fadeOut };
    struct Hit { juce::ValueTree clip; Zone zone = Zone::none; };

    // ---- painting --------------------------------------------------------------------------------
    void paint (juce::Graphics& g) override
    {
        auto& p = ctx.project;
        const double tempo = p.tempo();
        g.fillAll (theme::bg);

        // bar shading + grid
        const double bpb = p.beatsPerBar();
        const double ppb = ctx.pixelsPerBeat;
        const double firstBar = std::floor (ctx.scrollBeats / bpb);
        for (double bar = firstBar; ; bar += 1.0)
        {
            const float x = owner.beatToX (bar * bpb);
            if (x > getWidth()) break;
            if (((int) bar & 1) == 1)
            {
                g.setColour (juce::Colours::white.withAlpha (0.012f));
                g.fillRect (x, 0.0f, (float) (bpb * ppb), (float) getHeight());
            }
            g.setColour (theme::outline.withAlpha (0.8f));
            g.drawVerticalLine ((int) x, 0.0f, (float) getHeight());
            if (ppb >= 18)
            {
                g.setColour (theme::outline.withAlpha (0.35f));
                for (int b = 1; b < (int) bpb; ++b)
                    g.drawVerticalLine ((int) owner.beatToX (bar * bpb + b), 0.0f, (float) getHeight());
            }
        }

        // cycle region
        if ((bool) p.tree()[ids::cycleOn])
        {
            const float cx0 = owner.beatToX ((double) p.tree()[ids::cycleStart]);
            const float cx1 = owner.beatToX ((double) p.tree()[ids::cycleEnd]);
            g.setColour (juce::Colour (0xfff2b84b).withAlpha (0.05f));
            g.fillRect (cx0, 0.0f, cx1 - cx0, (float) getHeight());
        }

        auto live = ctx.engine.getLiveRecordings();

        for (auto& row : owner.rows())
        {
            const int y = row.y - owner.scrollY;
            if (y > getHeight() || y + row.h + row.laneH < 0) continue;
            Track t (row.track);

            if (ctx.selectedTrack == t.id())
            {
                g.setColour (juce::Colours::white.withAlpha (0.025f));
                g.fillRect (0, y, getWidth(), row.h + row.laneH);
            }
            g.setColour (theme::outline);
            g.drawHorizontalLine (y + row.h + row.laneH - 1, 0.0f, (float) getWidth());

            for (auto cv : t.clips())
                paintClip (g, Clip (cv), t, y, row.h, tempo);

            for (auto& lr : live)
                if (lr.trackId == t.id())
                    paintLiveRecording (g, lr, y, row.h);

            if (row.laneH > 0)
                paintAutomation (g, t, y + row.h, row.laneH);
        }

        // marquee
        if (! marquee.isEmpty())
        {
            g.setColour (theme::accent.withAlpha (0.12f));
            g.fillRect (marquee);
            g.setColour (theme::accent.withAlpha (0.6f));
            g.drawRect (marquee, 1);
        }

        if (! owner.dropHighlight.isEmpty())
        {
            g.setColour (theme::accent.withAlpha (0.18f));
            g.fillRect (owner.dropHighlight);
            g.setColour (theme::accent);
            g.drawRect (owner.dropHighlight, 2);
        }

        // playhead
        const float px = owner.beatToX (ctx.engine.getPositionBeats());
        g.setColour (ctx.engine.isRecording() ? theme::bad : theme::accent);
        g.fillRect (px - 0.5f, 0.0f, 1.5f, (float) getHeight());

        if (owner.rows().empty())
        {
            g.setColour (theme::textDim);
            g.setFont (uiFont (17.0f, true));
            g.drawText ("Add a track to start making music", getLocalBounds().withTrimmedBottom (40), juce::Justification::centred);
            g.setFont (uiFont (13.0f));
            g.setColour (theme::textFaint);
            g.drawText ("Click \"+ Track\", double-click a sound in the Library, or drop audio / MIDI files here",
                        getLocalBounds().withTrimmedTop (20), juce::Justification::centred);
        }
    }

    juce::Rectangle<float> clipRect (const Clip& c, int y, int h, double tempo) const
    {
        const float x0 = owner.beatToX (c.start());
        const float x1 = owner.beatToX (c.endBeats (tempo));
        return { x0, (float) y + 2.0f, juce::jmax (3.0f, x1 - x0), (float) h - 4.0f };
    }

    void paintClip (juce::Graphics& g, const Clip& c, const Track& t, int y, int h, double tempo)
    {
        auto r = clipRect (c, y, h, tempo);
        if (r.getRight() < 0 || r.getX() > getWidth()) return;

        const bool sel = ctx.isClipSelected (c.id());
        const bool muted = c.v[ids::mute];
        auto col = muted ? theme::textFaint : t.colour();

        g.setColour (col.withAlpha (sel ? 0.42f : 0.28f));
        g.fillRoundedRectangle (r, 4.0f);
        auto header = r.withHeight (juce::jmin (16.0f, r.getHeight()));
        g.setColour (col.withAlpha (sel ? 0.95f : 0.7f));
        g.fillRoundedRectangle (header, 4.0f);
        g.fillRect (header.withTrimmedTop (8.0f));

        g.setColour (sel ? juce::Colours::white : col.brighter (0.2f));
        g.drawRoundedRectangle (r, 4.0f, sel ? 1.6f : 1.0f);

        g.setColour (juce::Colours::black.withAlpha (0.85f));
        g.setFont (uiFont (11.0f, true));
        juce::String name = c.name();
        if (c.v.getChildWithName (ids::TAKE).isValid())
            name << "  [take " << ((int) c.v[ids::take] + 1) << "]";
        g.drawText (name, header.reduced (5.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft, true);

        auto body = r.withTrimmedTop (header.getHeight()).reduced (1.0f, 2.0f);
        if (body.getHeight() < 4.0f) return;
        g.saveState();
        g.reduceClipRegion (body.toNearestInt());

        if (c.isAudio())
            paintWaveform (g, c, body, col);
        else
            paintNotes (g, c, body, col);

        g.restoreState();

        if (c.isAudio())
        {
            // fades
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            const float fi = (float) (c.fadeIn() * tempo / 60.0 * ctx.pixelsPerBeat);
            const float fo = (float) (c.fadeOut() * tempo / 60.0 * ctx.pixelsPerBeat);
            if (fi > 1)
            {
                juce::Path p; p.addTriangle (body.getX(), body.getY(), body.getX() + fi, body.getY(), body.getX(), body.getBottom());
                g.fillPath (p);
            }
            if (fo > 1)
            {
                juce::Path p; p.addTriangle (body.getRight(), body.getY(), body.getRight() - fo, body.getY(), body.getRight(), body.getBottom());
                g.fillPath (p);
            }
        }
    }

    void paintWaveform (juce::Graphics& g, const Clip& c, juce::Rectangle<float> body, juce::Colour col)
    {
        auto data = ctx.engine.getCache().get (ctx.project.resolve (c.file()), ctx.engine.getSampleRate());
        if (data == nullptr)
        {
            g.setColour (juce::Colours::white.withAlpha (0.4f));
            g.setFont (uiFont (11.0f));
            const bool missing = ! ctx.project.resolve (c.file()).existsAsFile();
            g.drawText (missing ? "File missing: " + c.file() : juce::String ("Loading..."), body.toNearestInt(), juce::Justification::centredLeft);
            return;
        }

        const double tempo = ctx.project.tempo();
        const double ratio = c.stretchRatio (tempo);
        const double secsPerPixel = 60.0 / tempo / ctx.pixelsPerBeat / ratio;   // source seconds per pixel
        const bool rev = c.reversed();
        const double gain = juce::Decibels::decibelsToGain (c.gainDb());
        const float mid = body.getCentreY(), half = body.getHeight() * 0.48f;
        const float x0 = juce::jmax (body.getX(), 0.0f), x1 = juce::jmin (body.getRight(), (float) getWidth());
        const auto& peaks = data->peaks;
        const double peaksPerSecond = data->sampleRate / AudioData::peakStep;

        juce::Path path;
        std::vector<float> vals;
        for (float x = x0; x < x1; x += 1.0f)
        {
            const double u = (x - body.getX()) * secsPerPixel;
            const double tA = rev ? c.offsetSeconds() + c.lengthSeconds() - u - secsPerPixel : c.offsetSeconds() + u;
            const double tB = tA + secsPerPixel;
            const int pA = juce::jmax (0, (int) (tA * peaksPerSecond));
            const int pB = juce::jmin ((int) peaks.size(), juce::jmax (pA + 1, (int) (tB * peaksPerSecond)));
            float pk = 0;
            for (int i = pA; i < pB; ++i) pk = juce::jmax (pk, peaks[(size_t) i]);
            vals.push_back (juce::jmin (1.0f, (float) (pk * gain)));
        }
        if (vals.empty()) return;
        path.startNewSubPath (x0, mid);
        for (size_t i = 0; i < vals.size(); ++i) path.lineTo (x0 + (float) i, mid - vals[i] * half);
        for (size_t i = vals.size(); i-- > 0;) path.lineTo (x0 + (float) i, mid + vals[i] * half);
        path.closeSubPath();
        g.setColour (col.brighter (0.4f).withAlpha (0.9f));
        g.fillPath (path);

        // time & pitch badge
        juce::StringArray tags;
        if (c.follows()) tags.add ("follows tempo");
        else if (std::abs (ratio - 1.0) > 1.0e-3) tags.add (juce::String (100.0 / ratio, 0) + "% speed");
        if (std::abs (c.pitch()) > 0.01) tags.add ((c.pitch() > 0 ? "+" : "") + juce::String (c.pitch(), 0) + " st");
        if (rev) tags.add ("reversed");
        if (! tags.isEmpty() && body.getWidth() > 60)
        {
            g.setFont (uiFont (9.5f, true));
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.drawText (tags.joinIntoString ("  "), body.reduced (4, 2).toNearestInt(), juce::Justification::bottomRight, true);
        }
    }

    void paintNotes (juce::Graphics& g, const Clip& c, juce::Rectangle<float> body, juce::Colour col)
    {
        int lo = 127, hi = 0, count = 0;
        for (auto n : c.v)
            if (n.hasType (ids::NOTE)) { lo = juce::jmin (lo, (int) n[ids::p]); hi = juce::jmax (hi, (int) n[ids::p]); ++count; }
        if (count == 0) return;
        lo -= 2; hi += 2;
        const float rowH = body.getHeight() / (float) juce::jmax (12, hi - lo + 1);
        const double len = c.midiLength();
        g.setColour (col.brighter (0.6f));
        for (auto n : c.v)
        {
            if (! n.hasType (ids::NOTE)) continue;
            const double s = n[ids::s], l = n[ids::l];
            if (s + l <= 0 || s >= len) continue;
            const float x = body.getX() + (float) (juce::jmax (0.0, s) * ctx.pixelsPerBeat);
            const float w = juce::jmax (2.0f, (float) ((juce::jmin (len, s + l) - juce::jmax (0.0, s)) * ctx.pixelsPerBeat) - 1.0f);
            const float yy = body.getBottom() - ((int) n[ids::p] - lo + 1) * rowH;
            g.fillRect (x, yy, w, juce::jmax (1.5f, rowH - 0.5f));
        }
    }

    void paintLiveRecording (juce::Graphics& g, const DawEngine::LiveRecording& lr, int y, int h)
    {
        const float x0 = owner.beatToX (lr.startBeat), x1 = owner.beatToX (lr.endBeat);
        auto r = juce::Rectangle<float> (x0, (float) y + 2, juce::jmax (2.0f, x1 - x0), (float) h - 4);
        g.setColour (theme::bad.withAlpha (0.25f));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (theme::bad);
        g.drawRoundedRectangle (r, 4.0f, 1.2f);
        auto body = r.reduced (1, 4);
        if (lr.audio && ! lr.peaks.empty())
        {
            const double secsPerPixel = 60.0 / ctx.project.tempo() / ctx.pixelsPerBeat;
            g.setColour (theme::bad.brighter (0.5f));
            for (float x = 0; x < body.getWidth(); x += 1.0f)
            {
                const int i = (int) (x * secsPerPixel / lr.peakSeconds);
                if (i >= (int) lr.peaks.size()) break;
                const float v = juce::jmin (1.0f, lr.peaks[(size_t) i]) * body.getHeight() * 0.48f;
                g.drawVerticalLine ((int) (body.getX() + x), body.getCentreY() - v, body.getCentreY() + v);
            }
        }
        else
        {
            const double spb = ctx.engine.getSampleRate() * 60.0 / ctx.project.tempo();
            const double now = ctx.engine.getPositionBeats();
            g.setColour (juce::Colours::white);
            for (auto& n : lr.notes)
            {
                if (n.pitch < 0) continue;
                const double on = n.on / spb, off = n.off < 0 ? now : n.off / spb;
                const float nx = owner.beatToX (on), nw = juce::jmax (2.0f, (float) ((off - on) * ctx.pixelsPerBeat));
                const float ny = body.getBottom() - (n.pitch - 24) / 84.0f * body.getHeight();
                g.fillRect (nx, ny, nw, 2.5f);
            }
        }
    }

    juce::String laneName (const Track& t, const juce::String& param) const
    {
        if (param == "pan") return "PAN";
        if (param == "volume") return "VOLUME";
        if (param.startsWith ("send:"))
        {
            auto bus = ctx.project.trackById (param.fromFirstOccurrenceOf (":", false, false).getIntValue());
            return "SEND TO " + (bus.isValid() ? bus.name().toUpperCase() : juce::String ("?"));
        }
        if (param.startsWith ("plug:"))
            if (auto* proc = ctx.engine.getProcessor (plugSlot (param)))
                if (auto* prm = proc->getParameters()[plugIndex (param)])
                    return (proc->getName() + ": " + prm->getName (40)).toUpperCase();
        juce::ignoreUnused (t);
        return "PLUGIN PARAMETER";
    }

    float laneFallback (const Track& t, const juce::String& param) const
    {
        if (param == "pan") return (float) (double) t.v[ids::pan];
        if (param.startsWith ("send:"))
            return (float) (double) t.sends().getChildWithProperty (ids::bus, param.fromFirstOccurrenceOf (":", false, false).getIntValue()).getProperty (ids::level, 0.0);
        if (param.startsWith ("plug:"))
        {
            if (auto* proc = ctx.engine.getProcessor (plugSlot (param)))
                if (auto* prm = proc->getParameters()[plugIndex (param)])
                    return prm->getValue();
            return 0.5f;
        }
        return (float) (double) t.v[ids::volume];
    }

    void paintAutomation (juce::Graphics& g, const Track& t, int y, int h)
    {
        const auto param = t.v.getProperty ("autoParam", "volume").toString();
        auto area = juce::Rectangle<float> (0.0f, (float) y, (float) getWidth(), (float) h - 1);
        g.setColour (theme::panel.withAlpha (0.7f));
        g.fillRect (area);
        g.setColour (theme::textFaint);
        g.setFont (uiFont (10.5f, true));
        g.drawText (laneName (t, param) + "   (right-click to choose what to automate)", area.reduced (6, 2).toNearestInt(), juce::Justification::topLeft);

        auto lane = t.lane (param);
        const float fallback = laneFallback (t, param);
        auto toY = [&] (float v) { return area.getBottom() - 6.0f - autoNorm (param, v) * (area.getHeight() - 12.0f); };

        juce::Path path;
        if (! lane.isValid() || lane.getNumChildren() == 0)
        {
            g.setColour (t.colour().withAlpha (0.5f));
            g.drawHorizontalLine ((int) toY (fallback), 0.0f, (float) getWidth());
            return;
        }
        bool first = true;
        juce::Array<juce::Point<float>> pts;
        for (auto pt : lane)
        {
            juce::Point<float> p (owner.beatToX ((double) pt[ids::b]), toY ((float) (double) pt[ids::v]));
            pts.add (p);
            if (first) { path.startNewSubPath (0.0f, p.y); path.lineTo (p); first = false; }
            else path.lineTo (p);
        }
        path.lineTo ((float) getWidth(), pts.getLast().y);
        g.setColour (t.colour());
        g.strokePath (path, juce::PathStrokeType (1.8f));
        for (auto& p : pts)
        {
            g.setColour (juce::Colours::white);
            g.fillEllipse (p.x - 3.5f, p.y - 3.5f, 7.0f, 7.0f);
        }
    }

    // ---- hit testing ---------------------------------------------------------------------------
    const ArrangementView::Row* rowAt (int y) const
    {
        static ArrangementView::Row found;
        for (auto& r : owner.rows())
            if (y + owner.scrollY >= r.y && y + owner.scrollY < r.y + r.h + r.laneH)
            {
                found = r;
                return &found;
            }
        return nullptr;
    }

    Hit hitTest2 (juce::Point<int> pos) const
    {
        Hit hit;
        auto* row = rowAt (pos.y);
        if (row == nullptr) return hit;
        const int y = row->y - owner.scrollY;
        if (pos.y >= y + row->h) return hit;   // automation lane
        const double tempo = ctx.project.tempo();
        Track t (row->track);
        for (int i = t.clips().getNumChildren(); --i >= 0;)   // topmost (last drawn) first
        {
            Clip c (t.clips().getChild (i));
            auto r = clipRect (c, y, row->h, tempo);
            if (! r.contains (pos.toFloat())) continue;
            hit.clip = c.v;
            const float edge = juce::jmin (7.0f, r.getWidth() / 4.0f);
            if (c.isAudio() && pos.y < r.getY() + 12)
            {
                if (pos.x < r.getX() + 14) { hit.zone = Zone::fadeIn; return hit; }
                if (pos.x > r.getRight() - 14) { hit.zone = Zone::fadeOut; return hit; }
            }
            if (pos.x < r.getX() + edge)          hit.zone = Zone::trimStart;
            else if (pos.x > r.getRight() - edge) hit.zone = Zone::trimEnd;
            else                                  hit.zone = Zone::body;
            return hit;
        }
        return hit;
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        auto hit = hitTest2 (e.getPosition());
        switch (hit.zone)
        {
            case Zone::trimStart: setMouseCursor (juce::MouseCursor::LeftRightResizeCursor); break;
            case Zone::trimEnd:   setMouseCursor (e.mods.isCommandDown() && Clip (hit.clip).isAudio() ? juce::MouseCursor::UpDownLeftRightResizeCursor
                                                                                                    : juce::MouseCursor::LeftRightResizeCursor); break;
            case Zone::fadeIn: case Zone::fadeOut:   setMouseCursor (juce::MouseCursor::CrosshairCursor); break;
            case Zone::body:                         setMouseCursor (juce::MouseCursor::DraggingHandCursor); break;
            case Zone::none: default:                setMouseCursor (juce::MouseCursor::NormalCursor); break;
        }
    }

    // ---- editing --------------------------------------------------------------------------------
    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        auto& p = ctx.project;
        dragMode = DragMode::none;
        dragClips.clear();
        startStates.clear();
        startTracks.clear();
        dragStarted = false;
        copied = false;

        auto* row = rowAt (e.y);
        if (row != nullptr) ctx.selectTrack (Track (row->track).id());

        // automation lane
        if (row != nullptr && e.y >= row->y - owner.scrollY + row->h)
        {
            startAutomationEdit (e, *row);
            return;
        }

        auto hit = hitTest2 (e.getPosition());
        if (! hit.clip.isValid())
        {
            if (e.mods.isPopupMenu()) { emptyMenu (e, row); return; }
            if (! e.mods.isShiftDown() && ! e.mods.isCommandDown()) ctx.clearClipSelection();
            dragMode = DragMode::marquee;
            marqueeStart = e.getPosition();
            if (! e.mods.isShiftDown())
                ctx.engine.setPositionBeats (ctx.snap (owner.xToBeat ((float) e.x), ctx.pixelsPerBeat));
            return;
        }

        Clip clip (hit.clip);
        if (! ctx.isClipSelected (clip.id()))
            ctx.selectClip (clip.id(), e.mods.isShiftDown() || e.mods.isCommandDown());
        if (e.mods.isPopupMenu()) { clipMenu (e, clip); return; }

        ctx.beginEdit ("Edit clip");
        anchorClip = clip.v;
        mouseDownBeat = owner.xToBeat ((float) e.x);
        for (int id : ctx.selectedClips)
        {
            auto c = p.clipById (id);
            if (! c.isValid()) continue;
            dragClips.add (c.v);
            startStates.add (c.v.createCopy());
            startTracks.set (id, p.tracks().indexOf (p.trackForClip (c.v).v));
        }
        switch (hit.zone)
        {
            case Zone::trimStart: dragMode = DragMode::trimStart; break;
            case Zone::trimEnd:   dragMode = e.mods.isCommandDown() && clip.isAudio() ? DragMode::stretch : DragMode::trimEnd; break;
            case Zone::fadeIn:    dragMode = DragMode::fadeIn; break;
            case Zone::fadeOut:   dragMode = DragMode::fadeOut; break;
            default:              dragMode = DragMode::move; break;
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto& p = ctx.project;
        const double tempo = p.tempo();
        const bool noSnap = e.mods.isShiftDown();

        if (dragMode == DragMode::marquee)
        {
            marquee = juce::Rectangle<int> (marqueeStart, e.getPosition());
            selectInMarquee (e.mods.isShiftDown());
            repaint();
            return;
        }
        if (dragMode == DragMode::automation) { dragAutomation (e); return; }
        if (dragMode == DragMode::none || ! anchorClip.isValid()) return;
        if (e.getDistanceFromDragStart() < 3 && ! dragStarted) return;
        dragStarted = true;

        Clip anchor (anchorClip);
        const double beatNow = owner.xToBeat ((float) e.x);

        if (dragMode == DragMode::move)
        {
            if (e.mods.isAltDown() && ! copied)
            {
                // alt-drag: leave copies behind
                for (int i = 0; i < dragClips.size(); ++i)
                {
                    Clip c (dragClips.getReference (i));
                    const int ti = startTracks[c.id()];
                    p.duplicateClip (c, (double) startStates.getReference (i)[ids::start],
                                     juce::isPositiveAndBelow (ti, p.numTracks()) ? p.track (ti) : Track());
                }
                copied = true;
            }
            const double anchorStart = (double) startStateFor (anchor.id())[ids::start];
            const double newAnchorStart = ctx.snap (anchorStart + (beatNow - mouseDownBeat), ctx.pixelsPerBeat, noSnap);
            const double delta = newAnchorStart - anchorStart;

            // vertical: move by whole tracks of the same kind
            int trackDelta = 0;
            if (auto* row = rowAt (e.y))
            {
                const int targetIndex = p.tracks().indexOf (row->track);
                trackDelta = targetIndex - startTracks[anchor.id()];
            }

            for (int i = 0; i < dragClips.size(); ++i)
            {
                Clip c (dragClips.getReference (i));
                const double s0 = startStates.getReference (i)[ids::start];
                Track dest;
                const int ti = startTracks[c.id()] + trackDelta;
                if (trackDelta != 0 && juce::isPositiveAndBelow (ti, p.numTracks()))
                {
                    auto cand = p.track (ti);
                    if (cand.isInstrument() == c.isMidi() && ! cand.isBus()) dest = cand;
                }
                p.moveClip (c, dest.isValid() ? dest : p.trackForClip (c.v), juce::jmax (0.0, s0 + delta));
            }
        }
        else if (dragMode == DragMode::trimStart || dragMode == DragMode::trimEnd)
        {
            for (int i = 0; i < dragClips.size(); ++i)
            {
                Clip c (dragClips.getReference (i));
                restore (c.v, startStates.getReference (i));
                Clip orig (startStates.getReference (i));
                const double delta = beatNow - mouseDownBeat;
                if (dragMode == DragMode::trimStart)
                    p.trimClipStart (c, ctx.snap (orig.start() + delta, ctx.pixelsPerBeat, noSnap));
                else
                    p.trimClipEnd (c, ctx.snap (orig.endBeats (tempo) + delta, ctx.pixelsPerBeat, noSnap));
            }
        }
        else if (dragMode == DragMode::stretch)
        {
            for (int i = 0; i < dragClips.size(); ++i)
            {
                Clip c (dragClips.getReference (i));
                if (! c.isAudio()) continue;
                restore (c.v, startStates.getReference (i));
                Clip orig (startStates.getReference (i));
                p.stretchClipEnd (c, juce::jmax (orig.start() + 0.05, ctx.snap (orig.endBeats (tempo) + (beatNow - mouseDownBeat), ctx.pixelsPerBeat, noSnap)));
            }
            ctx.setStatus ("Time-stretching: " + juce::String (100.0 / anchor.stretchRatio (tempo), 0) + "% speed (the pitch stays the same).");
        }
        else if (dragMode == DragMode::fadeIn || dragMode == DragMode::fadeOut)
        {
            const double secs = (beatNow - (dragMode == DragMode::fadeIn ? anchor.start() : anchor.endBeats (tempo))) * 60.0 / tempo;
            const double len = anchor.lengthSeconds();
            if (dragMode == DragMode::fadeIn) anchorClip.setProperty (ids::fadeIn, juce::jlimit (0.0, len * 0.9, secs), p.um());
            else anchorClip.setProperty (ids::fadeOut, juce::jlimit (0.0, len * 0.9, -secs), p.um());
        }
        repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (dragMode == DragMode::marquee && marquee.getWidth() < 3)
            ; // simple click on empty space
        marquee = {};
        dragMode = DragMode::none;
        dragStarted = false;
        anchorClip = {};
        autoPoint = {};
        juce::ignoreUnused (e);
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        auto& p = ctx.project;
        auto* row = rowAt (e.y);
        if (row == nullptr) { if (owner.onAddTrack) owner.onAddTrack(); return; }
        if (e.y >= row->y - owner.scrollY + row->h) return;   // lane handled in mouseDown

        auto hit = hitTest2 (e.getPosition());
        if (hit.clip.isValid())
        {
            if (ctx.openEditor) ctx.openEditor (Clip (hit.clip).id());
            return;
        }
        Track t (row->track);
        if (t.isInstrument())
        {
            const double bpb = p.beatsPerBar();
            const double start = std::floor (owner.xToBeat ((float) e.x) / bpb) * bpb;
            ctx.beginEdit ("New MIDI clip");
            auto c = p.addMidiClip (t, start, bpb, t.name());
            ctx.selectClip (c.id(), false);
            if (ctx.openEditor) ctx.openEditor (c.id());
        }
    }

    // ---- automation editing -------------------------------------------------------------------
    void startAutomationEdit (const juce::MouseEvent& e, const ArrangementView::Row& row)
    {
        auto& p = ctx.project;
        Track t (row.track);
        const auto param = t.v.getProperty ("autoParam", "volume").toString();
        autoTrack = t.v;
        autoParam = param;
        autoArea = { 0, row.y - owner.scrollY + row.h, getWidth(), row.laneH - 1 };

        ctx.beginEdit ("Automation");
        auto lane = p.getOrCreateLane (t, param);
        // existing point?
        for (auto pt : lane)
        {
            const float px = owner.beatToX ((double) pt[ids::b]);
            const float py = valueToY ((float) (double) pt[ids::v]);
            if (std::abs (px - e.x) < 6 && std::abs (py - e.y) < 6)
            {
                if (e.mods.isPopupMenu() || e.getNumberOfClicks() > 1) { lane.removeChild (pt, p.um()); repaint(); return; }
                autoPoint = pt;
                dragMode = DragMode::automation;
                return;
            }
        }
        if (e.mods.isPopupMenu())
        {
            auto hasPoints = [&t] (const juce::String& prm) { auto l = t.lane (prm); return l.isValid() && l.getNumChildren() > 0; };
            juce::PopupMenu m;
            juce::StringArray choices;
            auto addChoice = [&] (juce::PopupMenu& menu, const juce::String& prm, const juce::String& label)
            {
                choices.add (prm);
                menu.addItem (1000 + choices.size() - 1, label + (hasPoints (prm) ? "  *" : ""), true, param == prm);
            };
            addChoice (m, "volume", "Volume");
            addChoice (m, "pan", "Pan");
            if (t.sends().getNumChildren() > 0)
            {
                juce::PopupMenu sends;
                for (auto s : t.sends())
                {
                    auto bus = p.trackById ((int) s[ids::bus]);
                    addChoice (sends, "send:" + juce::String ((int) s[ids::bus]), "Send to " + (bus.isValid() ? bus.name() : juce::String ("?")));
                }
                m.addSubMenu ("Sends", sends);
            }
            juce::Array<juce::ValueTree> plugins;
            for (auto n : t.midiFx()) plugins.add (n);
            if (t.instrument().isValid()) plugins.add (t.instrument());
            for (auto n : t.inserts()) plugins.add (n);
            for (auto& n : plugins)
            {
                auto* proc = ctx.engine.getProcessor (n[ids::id].toString());
                if (proc == nullptr) continue;
                juce::PopupMenu pm;
                auto params = proc->getParameters();
                for (int i = 0; i < juce::jmin (128, params.size()); ++i)
                    if (params[i]->isAutomatable())
                        addChoice (pm, "plug:" + n[ids::id].toString() + ":" + juce::String (i), params[i]->getName (40));
                if (pm.getNumItems() > 0) m.addSubMenu (n[ids::name].toString(), pm);
            }
            m.addSeparator();
            m.addItem (3, "Clear this automation");
            m.showMenuAsync (juce::PopupMenu::Options(), [this, t, param, choices] (int r)
            {
                if (r >= 1000 && r - 1000 < choices.size()) t.v.setProperty ("autoParam", choices[r - 1000], nullptr);
                if (r == 3) { ctx.beginEdit ("Clear automation"); auto l = t.lane (param); if (l.isValid()) l.removeAllChildren (ctx.project.um()); }
                owner.repaintAll();
            });
            return;
        }
        // new point: the first point also pins the current value at the start of the song
        if (lane.getNumChildren() == 0)
        {
            juce::ValueTree first (ids::POINT);
            first.setProperty (ids::b, 0.0, nullptr);
            first.setProperty (ids::v, (double) laneFallback (t, param), nullptr);
            lane.appendChild (first, p.um());
        }
        juce::ValueTree pt (ids::POINT);
        pt.setProperty (ids::b, ctx.snap (owner.xToBeat ((float) e.x), ctx.pixelsPerBeat, true), nullptr);
        pt.setProperty (ids::v, (double) yToValue ((float) e.y), nullptr);
        insertSorted (lane, pt);
        autoPoint = pt;
        dragMode = DragMode::automation;
        repaint();
    }

    void dragAutomation (const juce::MouseEvent& e)
    {
        if (! autoPoint.isValid()) return;
        auto lane = autoPoint.getParent();
        autoPoint.setProperty (ids::b, juce::jmax (0.0, owner.xToBeat ((float) e.x)), ctx.project.um());
        autoPoint.setProperty (ids::v, (double) yToValue ((float) e.y), ctx.project.um());
        // keep points sorted
        lane.sort (*this, ctx.project.um(), true);
        repaint();
    }

    int compareElements (const juce::ValueTree& a, const juce::ValueTree& b) const
    {
        const double x = a[ids::b], y = b[ids::b];
        return x < y ? -1 : (x > y ? 1 : 0);
    }

    void insertSorted (juce::ValueTree lane, juce::ValueTree pt)
    {
        int idx = 0;
        while (idx < lane.getNumChildren() && (double) lane.getChild (idx)[ids::b] <= (double) pt[ids::b]) ++idx;
        lane.addChild (pt, idx, ctx.project.um());
    }

    float valueToY (float v) const { return autoArea.getBottom() - 6.0f - autoNorm (autoParam, v) * (autoArea.getHeight() - 12.0f); }
    float yToValue (float y) const { return autoValue (autoParam, (autoArea.getBottom() - 6.0f - y) / (autoArea.getHeight() - 12.0f)); }

    // ---- menus -----------------------------------------------------------------------------------
    void clipMenu (const juce::MouseEvent& e, const Clip& clip)
    {
        juce::PopupMenu m;
        m.addItem (1, "Open in Editor");
        addMenuItem (m, 2, "Split at Playhead\tCtrl+T");
        addMenuItem (m, 3, "Duplicate\tCtrl+D");
        m.addItem (4, "Repeat 4 times");
        m.addItem (5, "Rename...");
        m.addItem (6, clip.v[ids::mute] ? "Unmute Clip" : "Mute Clip");
        if (clip.isMidi())
        {
            juce::PopupMenu q;
            q.addItem (20, "1/4");  q.addItem (21, "1/8");  q.addItem (22, "1/16"); q.addItem (23, "1/8 triplet"); q.addItem (24, "1/32");
            q.addSeparator();
            q.addItem (25, "1/16 with light swing");
            q.addItem (26, "1/16 with heavy swing");
            q.addItem (27, "1/8 with swing (shuffle)");
            m.addSubMenu ("Quantize", q);
            int midiSelected = 0;
            for (int sid : ctx.selectedClips) if (auto sc = ctx.project.clipById (sid); sc.isValid() && sc.isMidi()) ++midiSelected;
            m.addItem (33, "Join Selected Clips", midiSelected > 1);
        }
        if (clip.isAudio())
        {
            const double tempo = ctx.project.tempo();
            juce::PopupMenu tp;
            tp.addItem (43, "Follow Song Tempo (detects the clip's tempo)", true, clip.follows());
            tp.addSeparator();
            tp.addItem (40, "Half Speed");
            tp.addItem (44, "75% Speed");
            tp.addItem (45, "150% Speed");
            tp.addItem (41, "Double Speed");
            tp.addItem (42, "Original Speed", std::abs (clip.stretchRatio (tempo) - 1.0) > 1.0e-3 || clip.follows());
            tp.addSeparator();
            juce::PopupMenu pitch;
            for (int st = 12; st >= -12; --st)
                if (st % 12 == 0 || std::abs (st) <= 7)
                    pitch.addItem (62 + st, (st > 0 ? "+" : "") + juce::String (st) + (std::abs (st) == 12 ? " (octave)" : " semitones"), true, std::abs (clip.pitch() - st) < 0.01);
            tp.addSubMenu ("Transpose (keeps the speed)", pitch);
            m.addSubMenu ("Time & Pitch", tp);
            m.addItem (30, "Reverse", true, clip.reversed());
            m.addItem (31, "Normalize");
            m.addItem (32, "Convert to Sampler Track (slices)");
            m.addSeparator();
            m.addItem (-1, "Tip: Ctrl+drag a clip's right edge to time-stretch it", false);
        }
        int takes = 0;
        for (auto t : clip.v) takes += t.hasType (ids::TAKE) ? 1 : 0;
        if (takes > 0)
        {
            juce::PopupMenu tm;
            for (int i = 0; i < takes; ++i) tm.addItem (100 + i, "Take " + juce::String (i + 1), true, (int) clip.v[ids::take] == i);
            m.addSubMenu ("Takes", tm);
        }
        if (clip.isAudio())
        {
            juce::PopupMenu gm;
            for (int db : { 6, 3, 0, -3, -6, -12 }) gm.addItem (200 + db + 20, (db > 0 ? "+" : "") + juce::String (db) + " dB", true, std::abs (clip.gainDb() - db) < 0.01f);
            m.addSubMenu ("Clip Gain", gm);
        }
        m.addSeparator();
        addMenuItem (m, 9, "Delete\tDel");

        const int id = clip.id();
        m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ e.getScreenX(), e.getScreenY(), 1, 1 }),
                         [this, id] (int r) { clipMenuResult (id, r); });
    }

    void clipMenuResult (int id, int r)
    {
        auto& p = ctx.project;
        auto c = p.clipById (id);
        if (! c.isValid() || r == 0) return;
        const double tempo = p.tempo();
        switch (r)
        {
            case 1: if (ctx.openEditor) ctx.openEditor (id); break;
            case 2: ctx.beginEdit ("Split"); p.splitClip (c, ctx.engine.getPositionBeats()); break;
            case 3: ctx.beginEdit ("Duplicate"); ctx.selectClip (p.duplicateClip (c, c.endBeats (tempo)).id(), false); break;
            case 4:
            {
                ctx.beginEdit ("Repeat");
                const double len = c.lengthBeats (tempo);
                for (int i = 1; i <= 4; ++i) p.duplicateClip (c, c.start() + len * i);
                break;
            }
            case 5:
            {
                auto* w = new juce::AlertWindow ("Rename clip", {}, juce::MessageBoxIconType::NoIcon, this);
                w->addTextEditor ("name", c.name());
                w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
                w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
                auto v = c.v;
                w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, v] (int res) mutable
                {
                    if (res == 1) { ctx.beginEdit ("Rename clip"); v.setProperty (ids::name, w->getTextEditorContents ("name"), ctx.project.um()); }
                }), true);
                break;
            }
            case 6: ctx.beginEdit ("Mute clip"); c.v.setProperty (ids::mute, ! (bool) c.v[ids::mute], p.um()); break;
            case 9: ctx.beginEdit ("Delete clip"); p.deleteClip (c); ctx.clearClipSelection(); break;
            case 20: case 21: case 22: case 23: case 24:
            {
                const double grids[] = { 1.0, 0.5, 0.25, 1.0 / 3.0, 0.125 };
                ctx.beginEdit ("Quantize");
                p.quantize (c, grids[r - 20]);
                break;
            }
            case 25: ctx.beginEdit ("Swing quantize"); p.quantize (c, 0.25, 1.0f, false, {}, 0.25f); break;
            case 26: ctx.beginEdit ("Swing quantize"); p.quantize (c, 0.25, 1.0f, false, {}, 0.5f); break;
            case 27: ctx.beginEdit ("Swing quantize"); p.quantize (c, 0.5, 1.0f, false, {}, 0.5f); break;
            case 33:
            {
                juce::Array<Clip> list;
                for (int sid : ctx.selectedClips) if (auto sc = p.clipById (sid); sc.isValid() && sc.isMidi()) list.add (sc);
                ctx.beginEdit ("Join clips");
                auto joined = p.joinMidiClips (list);
                if (joined.isValid()) ctx.selectClip (joined.id(), false);
                break;
            }
            case 30: ctx.beginEdit ("Reverse"); c.v.setProperty (ids::reverse, ! c.reversed(), p.um()); break;
            case 31: normalizeClip (c); break;
            case 32: convertToSampler (c); break;
            case 40: case 41: case 42: case 44: case 45:
            {
                ctx.beginEdit ("Time stretch");
                const double factor = r == 40 ? 2.0 : r == 41 ? 0.5 : r == 44 ? 4.0 / 3.0 : r == 45 ? 2.0 / 3.0 : 0.0;
                if (r == 42) { c.v.setProperty (ids::follow, false, p.um()); c.v.setProperty (ids::stretch, 1.0, p.um()); }
                else c.v.setProperty (ids::stretch, c.stretch() * factor, p.um());
                break;
            }
            case 43:
            {
                if (c.follows()) { ctx.beginEdit ("Follow tempo"); p.setClipFollowTempo (c, c.srcTempo(), false); break; }
                double bpm = c.srcTempo();
                if (bpm <= 0.0)
                {
                    auto data = ctx.engine.getCache().getBlocking (p.resolve (c.file()), ctx.engine.getSampleRate());
                    if (data == nullptr) { ctx.setStatus ("Can't read the clip's audio."); break; }
                    bpm = estimateTempo (data->buffer, data->sampleRate).bpm;
                }
                if (bpm <= 0.0) { ctx.setStatus ("Couldn't find a steady tempo in this clip."); break; }
                ctx.beginEdit ("Follow tempo");
                p.setClipFollowTempo (c, bpm, true);
                ctx.setStatus ("The clip was played at " + juce::String (bpm, 1) + " BPM; it now follows the song's tempo.");
                break;
            }
            default:
                if (r >= 100 && r < 200) { ctx.beginEdit ("Choose take"); p.setTake (c, r - 100); }
                else if (r >= 200 && r < 260) { ctx.beginEdit ("Clip gain"); c.v.setProperty (ids::gain, (double) (r - 220), p.um()); }
                else if (r >= 50 && r <= 74) { ctx.beginEdit ("Transpose audio"); c.v.setProperty (ids::pitch, (double) (r - 62), p.um()); }
                break;
        }
        owner.repaintAll();
    }

    void normalizeClip (const Clip& c)
    {
        auto data = ctx.engine.getCache().getBlocking (ctx.project.resolve (c.file()), ctx.engine.getSampleRate());
        if (data == nullptr) return;
        const int a = juce::jlimit (0, data->buffer.getNumSamples(), (int) (c.offsetSeconds() * data->sampleRate));
        const int b = juce::jlimit (a, data->buffer.getNumSamples(), (int) ((c.offsetSeconds() + c.lengthSeconds()) * data->sampleRate));
        float peak = 0.0f;
        for (int ch = 0; ch < data->buffer.getNumChannels(); ++ch) peak = juce::jmax (peak, data->buffer.getMagnitude (ch, a, b - a));
        if (peak <= 1.0e-6f) { ctx.setStatus ("The clip is silent."); return; }
        ctx.beginEdit ("Normalize");
        const double gain = -0.3 - juce::Decibels::gainToDecibels (peak);
        c.v.setProperty (ids::gain, gain, ctx.project.um());
        ctx.setStatus ("Clip gain set to " + juce::String (gain, 1) + " dB (peak -0.3 dB).");
    }

    void convertToSampler (const Clip& c)
    {
        auto& p = ctx.project;
        Sampler proto;
        const auto file = p.resolve (c.file());
        auto err = proto.loadFile (file);
        auto* sample = proto.getSample();
        if (err.isNotEmpty() || sample == nullptr) { ctx.setStatus (err.isNotEmpty() ? err : juce::String ("Can't read the clip's audio.")); return; }
        const double len = sample->length() / sample->sampleRate;
        proto.setParam ("start", (float) juce::jlimit (0.0, 1.0, c.offsetSeconds() / len));
        proto.setParam ("end", (float) juce::jlimit (0.0, 1.0, (c.offsetSeconds() + c.lengthSeconds()) / len));
        proto.setParam ("mode", (float) Sampler::slice);
        proto.setParam ("slices", 0.0f);
        proto.setParam ("release", 30.0f);
        const auto slices = proto.currentSlices();
        if (slices.size() < 2) return;

        ctx.beginEdit ("Convert to Sampler");
        auto src = p.trackForClip (c.v);
        auto t = p.addTrack (kindInstrument, c.name() + " Sampler", p.tracks().indexOf (src.v) + 1);
        auto ref = builtinRef ("sampler");
        ref.name = "Sampler: " + c.name();
        ref.state = encodeState (proto);
        p.setInstrument (t, ref);
        const double tempo = p.tempo();
        auto mc = p.addMidiClip (t, c.start(), c.lengthBeats (tempo), c.name() + " (slices)");
        const double startSample = slices.front();
        for (size_t i = 0; i + 1 < slices.size() && Sampler::firstSliceNote + (int) i < 128; ++i)
        {
            const double s0 = (slices[i] - startSample) / sample->sampleRate, s1 = (slices[i + 1] - startSample) / sample->sampleRate;
            p.addNote (mc, Sampler::firstSliceNote + (int) i, p.secondsToBeats (s0), juce::jmax (0.05, p.secondsToBeats (s1 - s0)), 100);
        }
        c.v.setProperty (ids::mute, true, p.um());
        ctx.selectTrack (t.id());
        ctx.setStatus ("Sliced into " + juce::String ((int) slices.size() - 1) + " pieces on a Sampler track: each slice is a key from C2 up. "
                       "Rearrange the notes to remix it. (The original clip is muted.)");
    }

    void emptyMenu (const juce::MouseEvent& e, const ArrangementView::Row* row)
    {
        juce::PopupMenu m;
        const double beat = ctx.snap (owner.xToBeat ((float) e.x), ctx.pixelsPerBeat);
        Track t (row != nullptr ? row->track : juce::ValueTree());
        if (t.isValid() && t.isInstrument()) m.addItem (1, "New MIDI Clip Here");
        addMenuItem (m, 2, "Paste\tCtrl+V", pasteAvailable && pasteAvailable());
        m.addItem (3, "Set Playhead Here");
        m.addItem (4, "Add Track...");
        m.showMenuAsync (juce::PopupMenu::Options(), [this, beat, t] (int r) mutable
        {
            auto& p = ctx.project;
            if (r == 1)
            {
                const double bpb = p.beatsPerBar();
                ctx.beginEdit ("New MIDI clip");
                auto c = p.addMidiClip (t, std::floor (beat / bpb) * bpb, bpb, t.name());
                if (ctx.openEditor) ctx.openEditor (c.id());
            }
            if (r == 2) { ctx.engine.setPositionBeats (beat); if (paste) paste(); }
            if (r == 3) ctx.engine.setPositionBeats (beat);
            if (r == 4 && owner.onAddTrack) owner.onAddTrack();
        });
    }

    void selectInMarquee (bool add)
    {
        if (! add) ctx.selectedClips.clearQuick();
        const double tempo = ctx.project.tempo();
        for (auto& row : owner.rows())
        {
            const int y = row.y - owner.scrollY;
            for (auto cv : Track (row.track).clips())
            {
                Clip c (cv);
                if (clipRect (c, y, row.h, tempo).toNearestInt().intersects (marquee) && ! ctx.selectedClips.contains (c.id()))
                    ctx.selectedClips.add (c.id());
            }
        }
        ctx.sendChangeMessage();
    }

    void restore (juce::ValueTree target, const juce::ValueTree& from)
    {
        for (int i = 0; i < from.getNumProperties(); ++i)
        {
            auto name = from.getPropertyName (i);
            if (target[name] != from[name]) target.setProperty (name, from[name], ctx.project.um());
        }
        if (Clip (target).isMidi())
        {
            // notes may have been shifted by a start-trim: restore their positions
            for (int i = 0; i < juce::jmin (target.getNumChildren(), from.getNumChildren()); ++i)
            {
                auto t = target.getChild (i), f = from.getChild (i);
                for (auto id : { ids::s, ids::b, ids::l })
                    if (f.hasProperty (id) && t[id] != f[id]) t.setProperty (id, f[id], ctx.project.um());
            }
        }
        else
        {
            for (int i = 0; i < juce::jmin (target.getNumChildren(), from.getNumChildren()); ++i)
                if (target.getChild (i)[ids::offset] != from.getChild (i)[ids::offset])
                    target.getChild (i).setProperty (ids::offset, from.getChild (i)[ids::offset], ctx.project.um());
        }
    }

    juce::ValueTree startStateFor (int id) const
    {
        for (auto& s : startStates) if ((int) s[ids::id] == id) return s;
        return {};
    }

    std::function<bool()> pasteAvailable;
    std::function<void()> paste;

private:
    enum class DragMode { none, move, trimStart, trimEnd, stretch, fadeIn, fadeOut, marquee, automation };
    ArrangementView& owner;
    StudioContext& ctx;
    DragMode dragMode = DragMode::none;
    juce::ValueTree anchorClip;
    juce::Array<juce::ValueTree> dragClips, startStates;
    juce::HashMap<int, int> startTracks;
    double mouseDownBeat = 0;
    bool copied = false, dragStarted = false;
    juce::Point<int> marqueeStart;
    juce::Rectangle<int> marquee;
    juce::ValueTree autoTrack, autoPoint;
    juce::String autoParam;
    juce::Rectangle<int> autoArea;
};

// =====================================================================================================
//  ArrangementView
// =====================================================================================================
ArrangementView::ArrangementView (StudioContext& c) : ctx (c)
{
    canvas = std::make_unique<Canvas> (*this);
    ruler = std::make_unique<Ruler> (*this);
    addAndMakeVisible (*canvas);
    addAndMakeVisible (*ruler);
    addAndMakeVisible (headerHolder);
    addAndMakeVisible (vScroll);
    addAndMakeVisible (hScroll);
    vScroll.addListener (this);
    hScroll.addListener (this);
    vScroll.setAutoHide (false);
    hScroll.setAutoHide (false);

    addTrack.setColour (juce::TextButton::buttonColourId, theme::accent);
    addTrack.setTooltip ("Add a track: instrument, audio (mic / guitar / bass) or drummer");
    addTrack.onClick = [this] { if (onAddTrack) onAddTrack(); };
    addAndMakeVisible (addTrack);

    snapBox.addItemList ({ "Snap: Auto", "Snap: Bar", "Snap: Beat", "Snap: 1/8", "Snap: 1/16", "Snap: Off" }, 1);
    snapBox.setSelectedItemIndex (ctx.snapMode, juce::dontSendNotification);
    snapBox.setTooltip ("Grid that clips and the playhead snap to (hold Shift to move freely)");
    snapBox.onChange = [this] { ctx.snapMode = snapBox.getSelectedItemIndex(); };
    addAndMakeVisible (snapBox);

    zoomSlider.setRange (std::log (2.0), std::log (400.0), 0.0);
    zoomSlider.setValue (std::log (ctx.pixelsPerBeat), juce::dontSendNotification);
    zoomSlider.setTooltip ("Zoom (Ctrl + mouse wheel)");
    zoomSlider.onValueChange = [this]
    {
        const double centre = xToBeat (canvas->getWidth() * 0.5f);
        setZoom (std::exp (zoomSlider.getValue()), centre, canvas->getWidth() / 2);
    };
    addAndMakeVisible (zoomSlider);

    canvas->pasteAvailable = [this] { return false; };

    ctx.project.tree().addListener (this);
    ctx.addChangeListener (this);
    ctx.engine.getCache().addChangeListener (this);   // waveforms appear as files finish loading
    startTimerHz (30);
}

ArrangementView::~ArrangementView()
{
    ctx.project.tree().removeListener (this);
    ctx.removeChangeListener (this);
    ctx.engine.getCache().removeChangeListener (this);
}

void ArrangementView::valueTreePropertyChanged (juce::ValueTree& t, const juce::Identifier& prop)
{
    if (t.hasType (ids::TRACK) && (prop == ids::height || prop == ids::showAutomation))
        layoutHeaders();
    repaintAll();
}

void ArrangementView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    for (auto* h : headers) h->repaint();
    repaintAll();
}

void ArrangementView::repaintAll()
{
    canvas->repaint();
    ruler->repaint();
}

std::vector<ArrangementView::Row> ArrangementView::rows() const
{
    std::vector<Row> r;
    int y = 0;
    for (auto t : ctx.project.tracks())
    {
        Row row;
        row.track = t;
        row.y = y;
        row.h = juce::jlimit (36, 240, (int) t.getProperty (ids::height, 72));
        row.laneH = (bool) t[ids::showAutomation] ? laneExtra : 0;
        y += row.h + row.laneH;
        r.push_back (row);
    }
    return r;
}

int ArrangementView::totalHeight() const
{
    int h = 0;
    for (auto& r : rows()) h += r.h + r.laneH;
    return h;
}

double ArrangementView::xToBeat (float x) const   { return ctx.scrollBeats + x / ctx.pixelsPerBeat; }
float ArrangementView::beatToX (double beat) const { return (float) ((beat - ctx.scrollBeats) * ctx.pixelsPerBeat); }

void ArrangementView::rebuildHeaders()
{
    needsRebuild = false;
    headers.clear();
    for (auto t : ctx.project.tracks())
    {
        auto* h = headers.add (new TrackHeader (ctx, t));
        h->onReorder = [this] (int from, int to) { ctx.beginEdit ("Move track"); ctx.project.moveTrack (from, to); };
        headerHolder.addAndMakeVisible (h);
    }
    layoutHeaders();
}

void ArrangementView::layoutHeaders()
{
    updateScrollBars();   // clamps scrollY first, so headers and lanes always agree
    auto r = rows();
    for (size_t i = 0; i < r.size() && i < (size_t) headers.size(); ++i)
        headers[(int) i]->setBounds (0, r[i].y - scrollY, headerHolder.getWidth(), r[i].h + r[i].laneH);
}

void ArrangementView::updateScrollBars()
{
    const int visible = canvas->getHeight();
    const int total = totalHeight() + 120;
    scrollY = juce::jlimit (0, juce::jmax (0, total - visible), scrollY);
    vScroll.setRangeLimits (0.0, (double) juce::jmax (total, visible));
    vScroll.setCurrentRange ((double) scrollY, (double) visible, juce::dontSendNotification);

    const double songEnd = juce::jmax (ctx.project.contentEndBeats(), (double) ctx.project.tree()[ids::cycleEnd], ctx.engine.getPositionBeats()) + 64.0;
    const double visibleBeats = canvas->getWidth() / ctx.pixelsPerBeat;
    hScroll.setRangeLimits (0.0, juce::jmax (songEnd, ctx.scrollBeats + visibleBeats));
    hScroll.setCurrentRange (ctx.scrollBeats, visibleBeats, juce::dontSendNotification);
}

void ArrangementView::scrollBarMoved (juce::ScrollBar* sb, double start)
{
    if (sb == &vScroll) { scrollY = (int) start; layoutHeaders(); }
    else ctx.scrollBeats = juce::jmax (0.0, start);
    repaintAll();
}

void ArrangementView::setZoom (double ppb, double anchorBeat, int anchorX)
{
    ctx.pixelsPerBeat = juce::jlimit (2.0, 400.0, ppb);
    ctx.scrollBeats = juce::jmax (0.0, anchorBeat - anchorX / ctx.pixelsPerBeat);
    zoomSlider.setValue (std::log (ctx.pixelsPerBeat), juce::dontSendNotification);
    updateScrollBars();
    repaintAll();
}

void ArrangementView::zoomToFit()
{
    const double end = juce::jmax (ctx.project.beatsPerBar() * 8, ctx.project.contentEndBeats() + ctx.project.beatsPerBar());
    setZoom (canvas->getWidth() / end, 0.0, 0);
}

void ArrangementView::scrollToShowBeat (double beat)
{
    const double visible = canvas->getWidth() / ctx.pixelsPerBeat;
    if (beat < ctx.scrollBeats || beat > ctx.scrollBeats + visible * 0.92)
    {
        ctx.scrollBeats = juce::jmax (0.0, beat - visible * 0.05);
        updateScrollBars();
        repaintAll();
    }
}

void ArrangementView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    auto local = e.getEventRelativeTo (canvas.get()).getPosition();
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        const double anchor = xToBeat ((float) local.x);
        setZoom (ctx.pixelsPerBeat * std::pow (1.0025, w.deltaY * 400.0), anchor, local.x);
    }
    else if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
    {
        const float d = std::abs (w.deltaX) > 0.0f ? w.deltaX : w.deltaY;
        ctx.scrollBeats = juce::jmax (0.0, ctx.scrollBeats - d * 300.0 / ctx.pixelsPerBeat);
        updateScrollBars();
    }
    else
    {
        scrollY = juce::jmax (0, scrollY - (int) (w.deltaY * 300.0f));
        layoutHeaders();
    }
    repaintAll();
}

void ArrangementView::mouseMagnify (const juce::MouseEvent& e, float scale)
{
    auto local = e.getEventRelativeTo (canvas.get()).getPosition();
    setZoom (ctx.pixelsPerBeat * scale, xToBeat ((float) local.x), local.x);
}

void ArrangementView::timerCallback()
{
    if (needsRebuild) rebuildHeaders();
    for (auto* h : headers) h->refreshMeter();

    const double pos = ctx.engine.getPositionBeats();
    if (ctx.engine.isPlaying() && ! canvas->isMouseButtonDown())
    {
        // follow playback, page by page
        const double visible = canvas->getWidth() / ctx.pixelsPerBeat;
        if (pos > ctx.scrollBeats + visible * 0.95 || pos < ctx.scrollBeats)
        {
            ctx.scrollBeats = juce::jmax (0.0, pos - visible * 0.05);
            updateScrollBars();
        }
    }
    if (pos != lastPlayhead || ctx.engine.isRecording())
    {
        lastPlayhead = pos;
        repaintAll();
    }
}

juce::Point<int> ArrangementView::canvasPoint (int x, int y) const
{
    return { x - canvas->getX(), y - canvas->getY() };
}

void ArrangementView::updateDropHighlight (int x, int y)
{
    auto p = canvasPoint (x, y);
    dropHighlight = {};
    for (auto& r : rows())
        if (p.y + scrollY >= r.y && p.y + scrollY < r.y + r.h)
            dropHighlight = { 0, r.y - scrollY, canvas->getWidth(), r.h };
    if (dropHighlight.isEmpty())
        dropHighlight = { 0, totalHeight() - scrollY, canvas->getWidth(), 60 };
    repaintAll();
}

static int trackIdAt (const ArrangementView& v, int canvasY, int scrollY)
{
    for (auto& r : v.rows())
        if (canvasY + scrollY >= r.y && canvasY + scrollY < r.y + r.h + r.laneH)
            return (int) r.track[ids::id];
    return 0;
}

void ArrangementView::itemDragMove (const SourceDetails& d) { updateDropHighlight (d.localPosition.x, d.localPosition.y); }

void ArrangementView::itemDropped (const SourceDetails& d)
{
    dropHighlight = {};
    auto p = canvasPoint (d.localPosition.x, d.localPosition.y);
    const double beat = ctx.snap (xToBeat ((float) juce::jmax (0, p.x)), ctx.pixelsPerBeat);
    if (onBrowserDrop) onBrowserDrop (d.description.toString(), trackIdAt (*this, p.y, scrollY), beat);
    repaintAll();
}

void ArrangementView::fileDragMove (const juce::StringArray&, int x, int y) { updateDropHighlight (x, y); }

void ArrangementView::filesDropped (const juce::StringArray& files, int x, int y)
{
    dropHighlight = {};
    auto p = canvasPoint (x, y);
    const double beat = ctx.snap (xToBeat ((float) juce::jmax (0, p.x)), ctx.pixelsPerBeat);
    if (onFilesDrop) onFilesDrop (files, trackIdAt (*this, p.y, scrollY), beat);
    repaintAll();
}

void ArrangementView::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
}

void ArrangementView::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (rulerHeight);
    auto bottom = r.removeFromBottom (bottomBar);
    const int hw = headerWidth();

    addTrack.setBounds (top.removeFromLeft (hw).reduced (8, 6));
    vScroll.setBounds (r.removeFromRight (12));
    top.removeFromRight (12);
    ruler->setBounds (top);

    auto bl = bottom.removeFromLeft (hw);
    snapBox.setBounds (bl.removeFromLeft (110).reduced (2, 0));
    zoomSlider.setBounds (bl.reduced (6, 0));
    bottom.removeFromRight (12);
    hScroll.setBounds (bottom);

    headerHolder.setBounds (r.removeFromLeft (hw));
    canvas->setBounds (r);
    layoutHeaders();
}

} // namespace wis::daw
