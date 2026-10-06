#include "TrackHeader.h"

namespace wis::daw
{

void HeaderToggle::paintButton (juce::Graphics& g, bool over, bool)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool on = getToggleState();
    g.setColour (on ? onColour : theme::panelRaised.brighter (over ? 0.15f : 0.05f));
    g.fillRoundedRectangle (r, 4.0f);
    if (! on)
    {
        g.setColour (theme::outline.brighter (0.2f));
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
    }
    g.setColour (on ? juce::Colours::black : theme::textDim);
    g.setFont (uiFont (11.0f, true));
    g.drawText (getButtonText(), r, juce::Justification::centred);
}

TrackHeader::TrackHeader (StudioContext& c, juce::ValueTree t) : ctx (c), track (t)
{
    track.addListener (this);
    const bool inst = Track (track).isInstrument();

    mute.setTooltip ("Mute (M)");
    solo.setTooltip ("Solo (S)");
    arm.setTooltip ("Record-enable this track");
    monitor.setTooltip ("Input monitoring: hear your instrument / mic through this track's effects");
    mute.onClick    = [this] { ctx.beginEdit ("Mute");  track.setProperty (ids::mute, mute.getToggleState(), ctx.project.um()); };
    solo.onClick    = [this] { ctx.beginEdit ("Solo");  track.setProperty (ids::solo, solo.getToggleState(), ctx.project.um()); };
    arm.onClick     = [this] { track.setProperty (ids::arm, arm.getToggleState(), nullptr); };
    monitor.onClick = [this] { track.setProperty (ids::monitor, monitor.getToggleState(), nullptr); };
    const bool bus = Track (track).isBus();
    for (auto* b : { &mute, &solo }) addAndMakeVisible (b);
    if (! bus) addAndMakeVisible (arm);
    if (! inst && ! bus) addAndMakeVisible (monitor);

    volume.setRange (-60.0, 6.0, 0.1);
    volume.setSkewFactorFromMidPoint (-12.0);
    volume.setDoubleClickReturnValue (true, 0.0);
    volume.setPopupDisplayEnabled (true, true, nullptr);
    volume.setTextValueSuffix (" dB");
    volume.setTooltip ("Volume (double-click = 0 dB)");
    volume.onDragStart = [this] { ctx.beginEdit ("Volume"); };
    volume.onValueChange = [this] { track.setProperty (ids::volume, volume.getValue(), ctx.project.um()); };
    addAndMakeVisible (volume);

    pan.setRange (-1.0, 1.0, 0.01);
    pan.setDoubleClickReturnValue (true, 0.0);
    pan.setTooltip ("Pan (double-click = centre)");
    pan.onDragStart = [this] { ctx.beginEdit ("Pan"); };
    pan.onValueChange = [this] { track.setProperty (ids::pan, pan.getValue(), ctx.project.um()); };
    addAndMakeVisible (pan);

    meterL.colour = meterR.colour = theme::good;
    addAndMakeVisible (meterL);
    addAndMakeVisible (meterR);

    instrumentButton.setTooltip (inst ? "Open the instrument" : bus ? "Bus: tracks send to it or output into it (see the Mixer)" : "Track effects");
    instrumentButton.onClick = [this]
    {
        Track tr (track);
        if (tr.isInstrument() && tr.instrument().isValid() && ctx.openPluginWindow)
            ctx.openPluginWindow (tr.instrument());
        else if (ctx.showTrackMenu)
            ctx.showTrackMenu (trackId(), instrumentButton.getScreenBounds().getBottomLeft());
    };
    instrumentButton.setColour (juce::TextButton::buttonColourId, theme::bg);
    addAndMakeVisible (instrumentButton);

    sync();
}

TrackHeader::~TrackHeader()
{
    track.removeListener (this);
}

void TrackHeader::valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&)
{
    sync();
}

void TrackHeader::sync()
{
    mute.setToggleState ((bool) track[ids::mute], juce::dontSendNotification);
    solo.setToggleState ((bool) track[ids::solo], juce::dontSendNotification);
    arm.setToggleState ((bool) track[ids::arm], juce::dontSendNotification);
    monitor.setToggleState ((bool) track[ids::monitor], juce::dontSendNotification);
    if (! volume.isMouseButtonDown()) volume.setValue ((double) track[ids::volume], juce::dontSendNotification);
    if (! pan.isMouseButtonDown()) pan.setValue ((double) track[ids::pan], juce::dontSendNotification);

    Track t (track);
    juce::String label;
    if (t.isInstrument())
    {
        auto inst = t.instrument();
        label = inst.isValid() ? inst[ids::name].toString() : juce::String ("No instrument");
    }
    else if (t.isBus())
    {
        int feeding = 0;
        for (auto tv : ctx.project.tracks())
        {
            Track o (tv);
            if (o.output() == t.id() || o.sends().getChildWithProperty (ids::bus, t.id()).isValid()) ++feeding;
        }
        label = "Bus  |  " + juce::String (feeding) + " track" + (feeding == 1 ? "" : "s") + " in";
        const int fx = t.inserts().getNumChildren();
        if (fx > 0) label << "  |  " << fx << " FX";
    }
    else
    {
        label = "Input " + juce::String ((int) track[ids::input] + 1) + ((bool) track[ids::inputStereo] ? "+" + juce::String ((int) track[ids::input] + 2) : juce::String());
        const int fx = t.inserts().getNumChildren();
        if (fx > 0) label << "  |  " << fx << " FX";
    }
    instrumentButton.setButtonText (label);
    repaint();
}

void TrackHeader::refreshMeter()
{
    auto [l, r] = ctx.engine.readTrackMeter (trackId());
    if ((bool) track[ids::arm] && Track (track).isAudio())
    {
        const float in = ctx.engine.readTrackInputMeter (trackId());
        l = juce::jmax (l, in);
        r = juce::jmax (r, in);
    }
    meterL.setLevel (l);
    meterR.setLevel (r);
}

void TrackHeader::paint (juce::Graphics& g)
{
    Track t (track);
    const bool selected = ctx.selectedTrack == t.id();
    auto r = getLocalBounds();

    g.setColour (selected ? theme::panelRaised.brighter (0.08f) : theme::panel);
    g.fillRect (r);
    g.setColour (t.colour());
    g.fillRect (r.removeFromLeft (5));
    g.setColour (theme::outline);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

    // icon
    auto icon = juce::Rectangle<float> (14.0f, 9.0f, 18.0f, 18.0f);
    g.setColour (t.colour().withAlpha (0.25f));
    g.fillRoundedRectangle (icon, 4.0f);
    g.setColour (t.colour());
    if (t.isBus())
    {
        // bus glyph: arrows merging into one
        juce::Path p;
        p.startNewSubPath (icon.getX() + 3, icon.getY() + 4);  p.lineTo (icon.getCentreX(), icon.getCentreY());
        p.startNewSubPath (icon.getX() + 3, icon.getBottom() - 4); p.lineTo (icon.getCentreX(), icon.getCentreY());
        p.lineTo (icon.getRight() - 3, icon.getCentreY());
        g.strokePath (p, juce::PathStrokeType (1.8f));
    }
    else if (t.isInstrument())
    {
        // piano keys glyph
        for (int k = 0; k < 4; ++k) g.fillRect (icon.getX() + 2 + k * 3.7f, icon.getY() + 3, 2.6f, 12.0f);
    }
    else
    {
        // waveform glyph
        juce::Path p;
        p.startNewSubPath (icon.getX() + 2, icon.getCentreY());
        for (int i = 0; i <= 14; ++i)
            p.lineTo (icon.getX() + 2 + i, icon.getCentreY() + std::sin (i * 1.3f) * (i % 3 == 0 ? 6.0f : 3.0f));
        g.strokePath (p, juce::PathStrokeType (1.5f));
    }

    g.setColour (selected ? theme::text : theme::text.withAlpha (0.85f));
    g.setFont (uiFont (13.5f, true));
    g.drawText (t.name(), 38, 8, getWidth() - 140, 20, juce::Justification::centredLeft);

    if (selected)
    {
        g.setColour (theme::accent);
        g.drawRect (getLocalBounds(), 1);
    }
}

void TrackHeader::resized()
{
    auto r = getLocalBounds().reduced (8, 6).withTrimmedLeft (4);
    auto top = r.removeFromTop (22);
    const bool inst = Track (track).isInstrument(), bus = Track (track).isBus();
    auto buttons = top.removeFromRight (bus ? 2 * 24 : inst ? 3 * 24 : 4 * 24);
    if (! inst && ! bus) { monitor.setBounds (buttons.removeFromLeft (24).reduced (1)); }
    mute.setBounds (buttons.removeFromLeft (24).reduced (1));
    solo.setBounds (buttons.removeFromLeft (24).reduced (1));
    if (! bus) arm.setBounds (buttons.removeFromLeft (24).reduced (1));

    r.removeFromTop (4);
    if (r.getHeight() >= 40)
    {
        instrumentButton.setBounds (r.removeFromTop (20).withTrimmedLeft (26));
        r.removeFromTop (4);
    }
    else instrumentButton.setBounds ({});

    auto row = r.removeFromTop (juce::jmin (20, r.getHeight()));
    pan.setBounds (row.removeFromRight (22));
    row.removeFromRight (4);
    auto meters = row.removeFromBottom (5);
    volume.setBounds (row.withTrimmedLeft (22));
    meterL.setBounds (meters.withTrimmedLeft (26).removeFromTop (2));
    meterR.setBounds (meters.withTrimmedLeft (26).withTrimmedTop (3));
}

void TrackHeader::mouseDown (const juce::MouseEvent& e)
{
    ctx.selectTrack (trackId());
    if (e.mods.isPopupMenu())
    {
        if (ctx.showTrackMenu) ctx.showTrackMenu (trackId(), e.getScreenPosition());
        return;
    }
    dragging = false;
    dragStartY = e.getScreenY();
}

void TrackHeader::mouseDrag (const juce::MouseEvent& e)
{
    if (std::abs (e.getScreenY() - dragStartY) > 8) dragging = true;
    if (dragging) setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

void TrackHeader::mouseUp (const juce::MouseEvent& e)
{
    setMouseCursor (juce::MouseCursor::NormalCursor);
    if (! dragging || onReorder == nullptr) return;
    const int from = ctx.project.tracks().indexOf (track);
    const int steps = (int) std::round ((float) (e.getScreenY() - dragStartY) / (float) juce::jmax (30, getHeight()));
    if (steps != 0) onReorder (from, juce::jlimit (0, ctx.project.numTracks() - 1, from + steps));
    dragging = false;
}

void TrackHeader::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.y < 30) rename();
}

void TrackHeader::rename()
{
    auto* w = new juce::AlertWindow ("Rename track", {}, juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", track[ids::name].toString());
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<TrackHeader> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int r)
    {
        if (safe == nullptr || r != 1) return;
        const auto name = w->getTextEditorContents ("name").trim();
        if (name.isEmpty()) return;
        safe->ctx.beginEdit ("Rename track");
        safe->track.setProperty (ids::name, name, safe->ctx.project.um());
    }), true);
}

} // namespace wis::daw
