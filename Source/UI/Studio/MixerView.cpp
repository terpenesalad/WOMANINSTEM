#include "MixerView.h"
#include "PluginMenus.h"

namespace wis::daw
{

static constexpr int stripWidth = 112;
static const juce::Colour busColour { 0xff38bdf8 };

/** Makes a new aux bus (optionally with an effect on it) and returns it. */
static Track makeAuxBus (StudioContext& ctx, const juce::String& name, const char* effectId)
{
    auto bus = ctx.project.addBus (name);
    if (effectId != nullptr)
    {
        auto ref = builtinRef (effectId);
        if (auto proto = createBuiltin (effectId))   // effects on a return bus are 100% wet
        {
            if (proto->state.getParameter ("mix") != nullptr) proto->setParam ("mix", 1.0f);
            ref.state = encodeState (*proto);
        }
        ctx.project.setPlugin (bus.inserts(), -1, ref);
    }
    return bus;
}

/** Menu of places a track can send to / output to. Item ids: 1000 + bus id; 1 = master; 2.. new buses. */
static void addBusChoices (StudioContext& ctx, juce::PopupMenu& m, const Track& from, bool forOutput)
{
    if (forOutput) m.addItem (1, "Master", true, from.output() == 0);
    for (auto& b : ctx.project.buses())
    {
        if (b.id() == from.id()) continue;
        const bool loop = ctx.project.wouldCreateLoop (from.id(), b.id());
        const bool current = forOutput ? from.output() == b.id() : from.sends().getChildWithProperty (ids::bus, b.id()).isValid();
        m.addItem (1000 + b.id(), b.name() + (loop ? "  (would loop back)" : juce::String()), ! loop, current);
    }
    m.addSeparator();
    if (forOutput) m.addItem (2, "New Bus (group tracks into one fader)");
    else
    {
        m.addItem (3, "New Reverb Bus");
        m.addItem (4, "New Delay Bus");
        m.addItem (5, "New Shimmer Bus");
        m.addItem (6, "New Empty Bus");
    }
}

static int resolveBusChoice (StudioContext& ctx, int r)
{
    switch (r)
    {
        case 2: return makeAuxBus (ctx, "Group Bus", nullptr).id();
        case 3: return makeAuxBus (ctx, "Reverb Bus", "reverb").id();
        case 4: return makeAuxBus (ctx, "Delay Bus", "delay").id();
        case 5: return makeAuxBus (ctx, "Shimmer Bus", "shimmer").id();
        case 6: return makeAuxBus (ctx, "Aux Bus", nullptr).id();
        default: return r >= 1000 ? r - 1000 : 0;
    }
}

// =====================================================================================================
/** One insert / instrument / MIDI effect slot button. */
class SlotButton : public juce::Component
{
public:
    enum Kind { instrumentSlot, effectSlot, midiSlot };
    SlotButton (StudioContext& c, juce::ValueTree parentNode, juce::ValueTree plugin, Kind k)
        : ctx (c), parent (parentNode), node (plugin), kind (k) {}

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const bool empty = ! node.isValid();
        const bool bypassed = ! empty && (bool) node[ids::bypass];
        const bool failed = ! empty && ctx.engine.getSlotError (node[ids::id].toString()).isNotEmpty();
        const auto fill = empty ? theme::bg
                        : kind == instrumentSlot ? theme::accent2.withAlpha (0.35f)
                        : kind == midiSlot ? theme::warn.withAlpha (0.25f)
                        : theme::panelRaised.brighter (0.1f);
        g.setColour (fill);
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (failed ? theme::bad : theme::outline);
        g.drawRoundedRectangle (r, 3.0f, 1.0f);

        const bool hasPower = ! empty && kind != instrumentSlot;
        if (hasPower)
        {
            g.setColour (bypassed ? theme::ledOff : kind == midiSlot ? theme::warn : theme::good);
            g.fillEllipse (r.getX() + 4, r.getCentreY() - 3, 6, 6);
        }
        auto textArea = r.withTrimmedLeft (hasPower ? 13.0f : 4.0f).reduced (2, 0);
        if (! empty && (int) node.getProperty (ids::sidechain, 0) != 0)
        {
            auto key = textArea.removeFromRight (16.0f);
            g.setColour (theme::accent);
            g.setFont (uiFont (8.5f, true));
            g.drawText ("SC", key, juce::Justification::centred);
        }
        g.setColour (empty || bypassed ? theme::textFaint : theme::text);
        g.setFont (uiFont (11.0f, ! empty));
        const juce::String emptyText = kind == instrumentSlot ? "Instrument" : kind == midiSlot ? "+ MIDI FX" : "+";
        g.drawText (empty ? emptyText : node[ids::name].toString(), textArea,
                    empty ? juce::Justification::centred : juce::Justification::centredLeft, true);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! node.isValid() || e.mods.isPopupMenu())
        {
            showMenu();
            return;
        }
        if (kind != instrumentSlot && e.x < 14)
        {
            node.setProperty (ids::bypass, ! (bool) node[ids::bypass], nullptr);
            repaint();
            return;
        }
        if (ctx.openPluginWindow) ctx.openPluginWindow (node);
    }

    bool acceptsSidechain() const
    {
        if (kind != effectSlot || ! node.isValid()) return false;
        auto* proc = ctx.engine.getProcessor (node[ids::id].toString());
        if (auto* bp = dynamic_cast<BuiltinProcessor*> (proc)) return bp->wantsSidechain();
        return proc != nullptr && proc->getTotalNumInputChannels() >= 4;
    }

    void showMenu()
    {
        auto pm = kind == instrumentSlot ? PluginMenu::instruments (ctx.host) : kind == midiSlot ? PluginMenu::midiEffects (ctx.host) : PluginMenu::effects (ctx.host);
        juce::PopupMenu m;
        auto track = Track (parent.getParent());
        if (node.isValid())
        {
            m.addItem (1, "Open Editor");
            if (kind != instrumentSlot) m.addItem (2, (bool) node[ids::bypass] ? "Turn On" : "Bypass");
            m.addItem (3, "Move Up", kind != instrumentSlot && parent.indexOf (node) > 0);
            m.addItem (4, "Move Down", kind != instrumentSlot && parent.indexOf (node) < parent.getNumChildren() - 1);
            m.addItem (5, "Remove");
            if (acceptsSidechain())
            {
                juce::PopupMenu sc;
                const int current = (int) node.getProperty (ids::sidechain, 0);
                sc.addItem (100, "None", true, current == 0);
                for (auto tv : ctx.project.tracks())
                {
                    Track t (tv);
                    if (t.id() == track.id()) continue;
                    sc.addItem (100 + t.id(), t.name(), true, current == t.id());
                }
                m.addSubMenu ("Side-chain Key Input", sc);
            }
            m.addSeparator();
            m.addSubMenu (kind == instrumentSlot ? "Change Instrument" : "Replace With", pm.menu);
        }
        else
        {
            m = pm.menu;
        }
        auto refs = pm.refs;
        auto& c = ctx;
        auto par = parent;
        auto n = node;
        const auto k = kind;
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [refs, &c, par, n, k] (int r) mutable
        {
            auto& p = c.project;
            if (r == 1 && c.openPluginWindow) c.openPluginWindow (n);
            else if (r == 2) n.setProperty (ids::bypass, ! (bool) n[ids::bypass], nullptr);
            else if (r == 3) { c.beginEdit ("Move plugin"); par.moveChild (par.indexOf (n), par.indexOf (n) - 1, p.um()); }
            else if (r == 4) { c.beginEdit ("Move plugin"); par.moveChild (par.indexOf (n), par.indexOf (n) + 1, p.um()); }
            else if (r == 5) { c.engine.flushPluginState (n); c.beginEdit ("Remove plugin"); par.removeChild (n, p.um()); }
            else if (r >= 100 && r < 10000)
            {
                c.beginEdit ("Side-chain");
                n.setProperty (ids::sidechain, r - 100, p.um());
                c.setStatus (r == 100 ? juce::String ("Side-chain off.")
                                      : "Side-chain from \"" + p.trackById (r - 100).name() + "\": it now drives " + n[ids::name].toString()
                                        + " (mute that track if you only want it as a trigger).");
            }
            else if (auto it = refs.find (r); it != refs.end())
            {
                const auto ref = PluginMenu::resolve (it->second);
                c.beginEdit (k == instrumentSlot ? "Change instrument" : "Add plugin");
                if (k == instrumentSlot)
                    p.setInstrument (Track (par.getParent()), ref);
                else
                {
                    const int index = n.isValid() ? par.indexOf (n) : -1;
                    p.setPlugin (par, index, ref);
                    c.engine.rebuildNow();
                    auto added = par.getChild (index >= 0 ? index : par.getNumChildren() - 1);
                    if (added.isValid() && c.openPluginWindow) c.openPluginWindow (added);
                }
            }
        });
    }

private:
    StudioContext& ctx;
    juce::ValueTree parent, node;
    Kind kind;
};

// =====================================================================================================
/** A send: bus name with a level bar you drag (double-click = 0 dB). Right-click for pre/post and remove. */
class SendRow : public juce::Component
{
public:
    SendRow (StudioContext& c, Track t, juce::ValueTree s) : ctx (c), track (std::move (t)), send (std::move (s)) {}

    static float toPos (float db) { return db <= -60.0f ? 0.0f : std::pow ((db + 60.0f) / 66.0f, 2.0f); }
    static float toDb (float pos) { return pos <= 0.001f ? -96.0f : juce::jlimit (-60.0f, 6.0f, std::sqrt (pos) * 66.0f - 60.0f); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const auto bus = ctx.project.trackById ((int) send[ids::bus]);
        const float db = (float) (double) send.getProperty (ids::level, 0.0);
        const bool pre = (bool) send[ids::pre];
        g.setColour (theme::bg);
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (busColour.withAlpha (0.45f));
        g.fillRoundedRectangle (r.withWidth (r.getWidth() * toPos (db)), 3.0f);
        g.setColour (theme::outline);
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
        g.setColour (theme::text);
        g.setFont (uiFont (10.5f, true));
        auto t = r.reduced (4, 0);
        g.drawText (db <= -60.0f ? juce::String ("-inf") : juce::String (db, 1), t.removeFromRight (30), juce::Justification::centredRight);
        g.drawText ((pre ? "PRE " : "") + (bus.isValid() ? bus.name() : juce::String ("?")), t, juce::Justification::centredLeft, true);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu m;
            m.addItem (1, "Pre-fader (ignores the track's fader)", true, (bool) send[ids::pre]);
            m.addItem (2, "Remove Send");
            auto& c = ctx;
            auto tr = track;
            auto s = send;
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [&c, tr, s] (int r) mutable
            {
                if (r == 1) { c.beginEdit ("Send"); s.setProperty (ids::pre, ! (bool) s[ids::pre], c.project.um()); }
                if (r == 2) { c.beginEdit ("Remove send"); c.project.removeSend (tr, (int) s[ids::bus]); }
            });
            return;
        }
        ctx.beginEdit ("Send level");
        startDb = (float) (double) send.getProperty (ids::level, 0.0);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) return;
        const float pos = juce::jlimit (0.0f, 1.0f, toPos (startDb) + e.getDistanceFromDragStartX() / (float) juce::jmax (40, getWidth()));
        send.setProperty (ids::level, toDb (pos), ctx.project.um());
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        ctx.beginEdit ("Send level");
        send.setProperty (ids::level, 0.0, ctx.project.um());
        repaint();
    }

private:
    StudioContext& ctx;
    Track track;
    juce::ValueTree send;
    float startDb = 0.0f;
};

/** "+ Send" button. */
class AddSendButton : public juce::Component
{
public:
    AddSendButton (StudioContext& c, Track t) : ctx (c), track (std::move (t)) {}
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (theme::bg);
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (theme::outline);
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
        g.setColour (theme::textFaint);
        g.setFont (uiFont (10.5f));
        g.drawText ("+ Send", r, juce::Justification::centred);
    }
    void mouseDown (const juce::MouseEvent&) override
    {
        juce::PopupMenu m;
        m.addSectionHeader ("Send to bus");
        addBusChoices (ctx, m, track, false);
        auto& c = ctx;
        auto tr = track;
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [&c, tr] (int r)
        {
            if (r == 0) return;
            c.beginEdit ("Add send");
            const int bus = resolveBusChoice (c, r);
            if (bus > 0 && ! c.project.wouldCreateLoop (tr.id(), bus))
            {
                c.project.setSend (tr, bus, -10.0f);
                c.setStatus ("Sending \"" + tr.name() + "\" to \"" + c.project.trackById (bus).name() + "\". Drag the send to set how much.");
            }
        });
    }
private:
    StudioContext& ctx;
    Track track;
};

/** Output routing button at the bottom of a strip. */
class OutputButton : public juce::Component
{
public:
    OutputButton (StudioContext& c, Track t) : ctx (c), track (std::move (t)) {}
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const int out = track.output();
        const auto bus = ctx.project.trackById (out);
        g.setColour (out != 0 ? busColour.withAlpha (0.25f) : theme::bg);
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (theme::outline);
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
        g.setColour (theme::textDim);
        g.setFont (uiFont (10.5f));
        g.drawText ("Out: " + (out != 0 && bus.isValid() ? bus.name() : juce::String ("Master")), r.reduced (4, 0), juce::Justification::centred, true);
    }
    void mouseDown (const juce::MouseEvent&) override
    {
        juce::PopupMenu m;
        m.addSectionHeader ("Output");
        addBusChoices (ctx, m, track, true);
        auto& c = ctx;
        auto tr = track;
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [&c, tr] (int r)
        {
            if (r == 0) return;
            c.beginEdit ("Output");
            const int bus = r == 1 ? 0 : resolveBusChoice (c, r);
            if (bus == 0 || ! c.project.wouldCreateLoop (tr.id(), bus)) c.project.setOutput (tr, bus);
        });
    }
private:
    StudioContext& ctx;
    Track track;
};

// =====================================================================================================
/** A full channel strip. */
class ChannelStrip : public juce::Component, private juce::ValueTree::Listener
{
public:
    ChannelStrip (StudioContext& c, juce::ValueTree trackOrRoot, bool master)
        : ctx (c), tree (trackOrRoot), isMaster (master)
    {
        tree.addListener (this);
        if (! isMaster)
        {
            mute.onClick = [this] { ctx.beginEdit ("Mute"); tree.setProperty (ids::mute, mute.getToggleState(), ctx.project.um()); };
            solo.onClick = [this] { ctx.beginEdit ("Solo"); tree.setProperty (ids::solo, solo.getToggleState(), ctx.project.um()); };
            arm.onClick = [this] { tree.setProperty (ids::arm, arm.getToggleState(), nullptr); };
            for (auto* b : { &mute, &solo }) addAndMakeVisible (b);
            if (! Track (tree).isBus()) addAndMakeVisible (arm);

            pan.setRange (-1.0, 1.0, 0.01);
            pan.setDoubleClickReturnValue (true, 0.0);
            pan.onDragStart = [this] { ctx.beginEdit ("Pan"); };
            pan.onValueChange = [this] { tree.setProperty (ids::pan, pan.getValue(), ctx.project.um()); };
            pan.setTooltip ("Pan");
            addAndMakeVisible (pan);

            output = std::make_unique<OutputButton> (ctx, Track (tree));
            addAndMakeVisible (*output);
        }

        fader.setSliderStyle (juce::Slider::LinearVertical);
        fader.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 16);
        fader.setRange (-60.0, 6.0, 0.1);
        fader.setSkewFactorFromMidPoint (-12.0);
        fader.setDoubleClickReturnValue (true, 0.0);
        fader.setTextValueSuffix (" dB");
        fader.onDragStart = [this] { ctx.beginEdit ("Volume"); };
        fader.onValueChange = [this] { tree.setProperty (isMaster ? ids::masterVolume : ids::volume, fader.getValue(), ctx.project.um()); };
        addAndMakeVisible (fader);

        meterL.colour = meterR.colour = theme::good;
        addAndMakeVisible (meterL);
        addAndMakeVisible (meterR);

        name.setJustificationType (juce::Justification::centred);
        name.setFont (uiFont (12.0f, true));
        addAndMakeVisible (name);

        slotView.setViewedComponent (&slotHolder, false);
        slotView.setScrollBarsShown (true, false);
        slotView.setScrollBarThickness (6);
        addAndMakeVisible (slotView);

        rebuildSlots();
        sync();
    }

    ~ChannelStrip() override { tree.removeListener (this); }

    void rebuildSlots()
    {
        slots.clear();
        sendRows.clear();
        addSend.reset();
        Track t (tree);
        if (! isMaster && t.isInstrument())
        {
            auto mfx = t.midiFx();
            if (mfx.isValid())
            {
                for (auto p : mfx) slots.add (new SlotButton (ctx, mfx, p, SlotButton::midiSlot));
                slots.add (new SlotButton (ctx, mfx, {}, SlotButton::midiSlot));
            }
            auto instParent = tree.getChildWithName (ids::INSTRUMENT);
            if (! instParent.isValid())
            {
                instParent = juce::ValueTree (ids::INSTRUMENT);
                tree.appendChild (instParent, nullptr);
            }
            slots.add (new SlotButton (ctx, instParent, instParent.getChild (0), SlotButton::instrumentSlot));
        }
        auto inserts = isMaster ? ctx.project.masterInserts() : t.inserts();
        for (auto p : inserts) slots.add (new SlotButton (ctx, inserts, p, SlotButton::effectSlot));
        slots.add (new SlotButton (ctx, inserts, {}, SlotButton::effectSlot));
        for (auto* s : slots) slotHolder.addAndMakeVisible (s);

        if (! isMaster && t.sends().isValid())
        {
            for (auto s : t.sends()) slotHolder.addAndMakeVisible (sendRows.add (new SendRow (ctx, t, s)));
            addSend = std::make_unique<AddSendButton> (ctx, t);
            slotHolder.addAndMakeVisible (*addSend);
        }
        resized();
    }

    void sync()
    {
        if (isMaster)
        {
            name.setText ("MASTER", juce::dontSendNotification);
            if (! fader.isMouseButtonDown()) fader.setValue ((double) tree[ids::masterVolume], juce::dontSendNotification);
            return;
        }
        name.setText (tree[ids::name].toString(), juce::dontSendNotification);
        mute.setToggleState ((bool) tree[ids::mute], juce::dontSendNotification);
        solo.setToggleState ((bool) tree[ids::solo], juce::dontSendNotification);
        arm.setToggleState ((bool) tree[ids::arm], juce::dontSendNotification);
        if (! fader.isMouseButtonDown()) fader.setValue ((double) tree[ids::volume], juce::dontSendNotification);
        if (! pan.isMouseButtonDown()) pan.setValue ((double) tree[ids::pan], juce::dontSendNotification);
        for (auto* s : sendRows) s->repaint();
        if (output != nullptr) output->repaint();
        repaint();
    }

    void refreshMeter()
    {
        auto [l, r] = isMaster ? ctx.engine.readMasterMeter() : ctx.engine.readTrackMeter ((int) tree[ids::id]);
        meterL.setLevel (l);
        meterR.setLevel (r);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().reduced (2);
        const bool sel = ! isMaster && ctx.selectedTrack == (int) tree[ids::id];
        const bool bus = ! isMaster && Track (tree).isBus();
        g.setColour (sel ? theme::panelRaised.brighter (0.06f) : bus ? theme::panelRaised.interpolatedWith (busColour, 0.06f) : theme::panelRaised);
        g.fillRoundedRectangle (r.toFloat(), 5.0f);
        g.setColour (isMaster ? theme::accent : Track (tree).colour());
        g.fillRoundedRectangle (r.removeFromTop (4).toFloat(), 2.0f);
        if (bus)
        {
            g.setColour (busColour);
            g.setFont (uiFont (9.0f, true));
            g.drawText ("BUS", getLocalBounds().reduced (6, 6).removeFromTop (12), juce::Justification::topLeft);
        }
        if (sel) { g.setColour (theme::accent); g.drawRoundedRectangle (getLocalBounds().reduced (2).toFloat(), 5.0f, 1.0f); }
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        if (! isMaster) ctx.selectTrack ((int) tree[ids::id]);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6, 8);
        name.setBounds (r.removeFromTop (20));
        r.removeFromTop (2);
        if (! isMaster)
        {
            if (output != nullptr) output->setBounds (r.removeFromBottom (20).reduced (0, 1));
            auto buttons = r.removeFromBottom (22);
            const bool hasArm = arm.isVisible();
            const int bw = buttons.getWidth() / (hasArm ? 3 : 2);
            mute.setBounds (buttons.removeFromLeft (bw).reduced (1));
            solo.setBounds ((hasArm ? buttons.removeFromLeft (bw) : buttons).reduced (1));
            if (hasArm) arm.setBounds (buttons.reduced (1));
            r.removeFromBottom (4);
        }

        // plugin slots + sends scroll inside a box; the fader always keeps room to move
        const int slotH = 20, sendH = 18, faderMin = 96;
        int contentH = slots.size() * slotH;
        if (! sendRows.isEmpty() || addSend != nullptr) contentH += 6 + (sendRows.size() + 1) * sendH;
        const int boxH = juce::jmin (contentH, juce::jmax (2 * slotH, r.getHeight() - faderMin - 4));
        auto box = r.removeFromTop (boxH);
        slotView.setBounds (box);
        const int w = box.getWidth() - (contentH > boxH ? 7 : 0);
        slotHolder.setSize (w, contentH);
        int y = 0;
        for (auto* s : slots) { s->setBounds (0, y + 1, w, slotH - 2); y += slotH; }
        if (! sendRows.isEmpty() || addSend != nullptr)
        {
            y += 6;
            for (auto* s : sendRows) { s->setBounds (0, y + 1, w, sendH - 2); y += sendH; }
            if (addSend != nullptr) addSend->setBounds (0, y + 1, w, sendH - 2);
        }
        r.removeFromTop (4);

        if (! isMaster) pan.setBounds (r.removeFromLeft (30).removeFromTop (30));
        auto meters = r.removeFromRight (14);
        meterL.setBounds (meters.removeFromLeft (6).withTrimmedBottom (18));
        meters.removeFromLeft (2);
        meterR.setBounds (meters.removeFromLeft (6).withTrimmedBottom (18));
        fader.setBounds (r);
    }

private:
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier& p) override
    {
        if (p == ids::name || p == ids::bypass || p == ids::sidechain) { for (auto* s : slots) s->repaint(); }
        sync();
    }
    void valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree&) override
    {
        if (! parent.hasType (ids::CLIP) && ! parent.hasType (ids::CLIPS) && ! parent.hasType (ids::LANE) && ! parent.hasType (ids::AUTOMATION))
            juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<ChannelStrip> (this)] { if (sp) sp->rebuildSlots(); });
    }
    void valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree&, int) override { valueTreeChildAdded (parent, parent); }
    void valueTreeChildOrderChanged (juce::ValueTree& parent, int, int) override         { valueTreeChildAdded (parent, parent); }

    StudioContext& ctx;
    juce::ValueTree tree;
    bool isMaster;
    juce::Label name;
    juce::Viewport slotView;
    juce::Component slotHolder;
    juce::OwnedArray<SlotButton> slots;
    juce::OwnedArray<SendRow> sendRows;
    std::unique_ptr<AddSendButton> addSend;
    std::unique_ptr<OutputButton> output;
    HeaderToggle mute { "M", theme::warn }, solo { "S", theme::good }, arm { "R", theme::bad };
    juce::Slider pan { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::Slider fader;
    LevelMeter meterL { true }, meterR { true };
    friend class MixerView;
};

// =====================================================================================================
MixerView::MixerView (StudioContext& c) : ctx (c)
{
    viewport.setViewedComponent (&strips, false);
    viewport.setScrollBarsShown (false, true);
    addAndMakeVisible (viewport);
    masterStrip = std::make_unique<ChannelStrip> (ctx, ctx.project.tree(), true);
    addAndMakeVisible (*masterStrip);
    ctx.project.tree().addListener (this);
    startTimerHz (30);
}

MixerView::~MixerView()
{
    ctx.project.tree().removeListener (this);
}

bool MixerView::affects (const juce::ValueTree& parent)
{
    return parent.hasType (ids::TRACKS) || parent.hasType (ids::PROJECT);
}

void MixerView::valueTreePropertyChanged (juce::ValueTree& t, const juce::Identifier& p)
{
    // a bus was renamed / a track re-routed: sends and output buttons show bus names
    if (t.hasType (ids::TRACK) && (p == ids::name || p == ids::output))
        for (auto* s : channelStrips) s->sync();
}

void MixerView::rebuild()
{
    needsRebuild = false;
    channelStrips.clear();
    for (auto t : ctx.project.tracks())
    {
        auto* s = channelStrips.add (new ChannelStrip (ctx, t, false));
        strips.addAndMakeVisible (s);
    }
    masterStrip = std::make_unique<ChannelStrip> (ctx, ctx.project.tree(), true);
    addAndMakeVisible (*masterStrip);
    resized();
}

void MixerView::timerCallback()
{
    if (needsRebuild) rebuild();
    if (! isShowing()) return;
    for (auto* s : channelStrips) s->refreshMeter();
    masterStrip->refreshMeter();
}

void MixerView::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    if (channelStrips.isEmpty())
    {
        g.setColour (theme::textFaint);
        g.setFont (uiFont (13.0f));
        g.drawText ("No tracks yet", getLocalBounds().withTrimmedRight (stripWidth + 10), juce::Justification::centred);
    }
}

void MixerView::resized()
{
    auto r = getLocalBounds().reduced (4);
    masterStrip->setBounds (r.removeFromRight (stripWidth));
    r.removeFromRight (8);
    viewport.setBounds (r);
    const int h = r.getHeight() - (channelStrips.size() * stripWidth > r.getWidth() ? viewport.getScrollBarThickness() : 0);
    strips.setSize (juce::jmax (r.getWidth(), channelStrips.size() * stripWidth), h);
    for (int i = 0; i < channelStrips.size(); ++i)
        channelStrips[i]->setBounds (i * stripWidth, 0, stripWidth, h);
}

} // namespace wis::daw
