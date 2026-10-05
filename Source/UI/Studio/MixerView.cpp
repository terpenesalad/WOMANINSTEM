#include "MixerView.h"
#include "PluginMenus.h"

namespace wis::daw
{

static constexpr int stripWidth = 104;

/** One insert / instrument slot button. */
class SlotButton : public juce::Component
{
public:
    SlotButton (StudioContext& c, juce::ValueTree parentNode, juce::ValueTree plugin, bool instrument)
        : ctx (c), parent (parentNode), node (plugin), isInstrument (instrument) {}

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const bool empty = ! node.isValid();
        const bool bypassed = ! empty && (bool) node[ids::bypass];
        const bool failed = ! empty && ctx.engine.getSlotError (node[ids::id].toString()).isNotEmpty();
        g.setColour (empty ? theme::bg : isInstrument ? theme::accent2.withAlpha (0.35f) : theme::panelRaised.brighter (0.1f));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (failed ? theme::bad : theme::outline);
        g.drawRoundedRectangle (r, 3.0f, 1.0f);

        if (! empty && ! isInstrument)
        {
            g.setColour (bypassed ? theme::ledOff : theme::good);
            g.fillEllipse (r.getX() + 4, r.getCentreY() - 3, 6, 6);
        }
        g.setColour (empty ? theme::textFaint : bypassed ? theme::textFaint : theme::text);
        g.setFont (uiFont (11.0f, ! empty));
        g.drawText (empty ? (isInstrument ? juce::String ("Instrument") : juce::String ("+")) : node[ids::name].toString(),
                    r.withTrimmedLeft (isInstrument || empty ? 4.0f : 13.0f).reduced (2, 0), empty ? juce::Justification::centred : juce::Justification::centredLeft, true);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! node.isValid() || e.mods.isPopupMenu())
        {
            showMenu();
            return;
        }
        if (! isInstrument && e.x < 14)
        {
            node.setProperty (ids::bypass, ! (bool) node[ids::bypass], nullptr);
            repaint();
            return;
        }
        if (ctx.openPluginWindow) ctx.openPluginWindow (node);
    }

    void showMenu()
    {
        auto pm = isInstrument ? PluginMenu::instruments (ctx.host) : PluginMenu::effects (ctx.host);
        juce::PopupMenu m;
        if (node.isValid())
        {
            m.addItem (1, "Open Editor");
            if (! isInstrument) m.addItem (2, (bool) node[ids::bypass] ? "Turn On" : "Bypass");
            m.addItem (3, "Move Up", ! isInstrument && parent.indexOf (node) > 0);
            m.addItem (4, "Move Down", ! isInstrument && parent.indexOf (node) < parent.getNumChildren() - 1);
            m.addItem (5, "Remove");
            m.addSeparator();
            m.addSubMenu (isInstrument ? "Change Instrument" : "Replace With", pm.menu);
        }
        else
        {
            m = pm.menu;
        }
        auto refs = pm.refs;
        juce::Component::SafePointer<SlotButton> safe (this);
        auto& c = ctx;
        auto par = parent;
        auto n = node;
        const bool inst = isInstrument;
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe, refs, &c, par, n, inst] (int r) mutable
        {
            auto& p = c.project;
            if (r == 1 && c.openPluginWindow) c.openPluginWindow (n);
            else if (r == 2) n.setProperty (ids::bypass, ! (bool) n[ids::bypass], nullptr);
            else if (r == 3) { c.beginEdit ("Move plugin"); par.moveChild (par.indexOf (n), par.indexOf (n) - 1, p.um()); }
            else if (r == 4) { c.beginEdit ("Move plugin"); par.moveChild (par.indexOf (n), par.indexOf (n) + 1, p.um()); }
            else if (r == 5) { c.engine.flushPluginState (n); c.beginEdit ("Remove plugin"); par.removeChild (n, p.um()); }
            else if (auto it = refs.find (r); it != refs.end())
            {
                c.beginEdit (inst ? "Change instrument" : "Add plugin");
                if (inst)
                {
                    auto track = Track (par.getParent());
                    p.setInstrument (track, it->second);
                }
                else
                {
                    p.setPlugin (par, n.isValid() ? par.indexOf (n) : -1, it->second);
                    // open the new plugin straight away
                    auto added = n.isValid() ? par.getChild (par.indexOf (n) < 0 ? par.getNumChildren() - 1 : par.indexOf (n)) : par.getChild (par.getNumChildren() - 1);
                    juce::ignoreUnused (added);
                }
            }
            juce::ignoreUnused (safe);
        });
    }

private:
    StudioContext& ctx;
    juce::ValueTree parent, node;
    bool isInstrument;
};

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
            for (auto* b : { &mute, &solo, &arm }) addAndMakeVisible (b);

            pan.setRange (-1.0, 1.0, 0.01);
            pan.setDoubleClickReturnValue (true, 0.0);
            pan.onDragStart = [this] { ctx.beginEdit ("Pan"); };
            pan.onValueChange = [this] { tree.setProperty (ids::pan, pan.getValue(), ctx.project.um()); };
            pan.setTooltip ("Pan");
            addAndMakeVisible (pan);
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

        rebuildSlots();
        sync();
    }

    ~ChannelStrip() override { tree.removeListener (this); }

    void rebuildSlots()
    {
        slots.clear();
        if (! isMaster && Track (tree).isInstrument())
        {
            auto instParent = tree.getChildWithName (ids::INSTRUMENT);
            if (! instParent.isValid())
            {
                instParent = juce::ValueTree (ids::INSTRUMENT);
                tree.appendChild (instParent, nullptr);
            }
            slots.add (new SlotButton (ctx, instParent, instParent.getChild (0), true));
        }
        auto inserts = isMaster ? ctx.project.masterInserts() : Track (tree).inserts();
        for (auto p : inserts) slots.add (new SlotButton (ctx, inserts, p, false));
        slots.add (new SlotButton (ctx, inserts, {}, false));
        for (auto* s : slots) addAndMakeVisible (s);
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
        g.setColour (sel ? theme::panelRaised.brighter (0.06f) : theme::panelRaised);
        g.fillRoundedRectangle (r.toFloat(), 5.0f);
        g.setColour (isMaster ? theme::accent : Track (tree).colour());
        g.fillRoundedRectangle (r.removeFromTop (4).toFloat(), 2.0f);
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
        const int slotH = 20;
        for (auto* s : slots) s->setBounds (r.removeFromTop (slotH).reduced (0, 1));
        r.removeFromTop (4);
        if (! isMaster)
        {
            auto buttons = r.removeFromBottom (22);
            const int bw = buttons.getWidth() / 3;
            mute.setBounds (buttons.removeFromLeft (bw).reduced (1));
            solo.setBounds (buttons.removeFromLeft (bw).reduced (1));
            arm.setBounds (buttons.reduced (1));
            r.removeFromBottom (4);
            pan.setBounds (r.removeFromTop (34).withSizeKeepingCentre (34, 34));
        }
        auto meters = r.removeFromRight (14);
        meterL.setBounds (meters.removeFromLeft (6).withTrimmedBottom (18));
        meters.removeFromLeft (2);
        meterR.setBounds (meters.removeFromLeft (6).withTrimmedBottom (18));
        fader.setBounds (r);
    }

private:
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier& p) override
    {
        if (p == ids::name || p == ids::bypass) { for (auto* s : slots) s->repaint(); }
        sync();
    }
    void valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree&) override       { if (! parent.hasType (ids::CLIP) && ! parent.hasType (ids::CLIPS) && ! parent.hasType (ids::LANE)) juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<ChannelStrip> (this)] { if (sp) sp->rebuildSlots(); }); }
    void valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree&, int) override { valueTreeChildAdded (parent, parent); }
    void valueTreeChildOrderChanged (juce::ValueTree& parent, int, int) override         { valueTreeChildAdded (parent, parent); }

    StudioContext& ctx;
    juce::ValueTree tree;
    bool isMaster;
    juce::Label name;
    juce::OwnedArray<SlotButton> slots;
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

void MixerView::valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) {}

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
