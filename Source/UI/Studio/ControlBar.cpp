#include "ControlBar.h"

namespace wis::daw
{

void TransportButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (1.5f);
    const bool on = getToggleState() || lit;
    juce::Colour onCol = theme::accent;
    switch (kind)
    {
        case Kind::record:    onCol = theme::bad; break;
        case Kind::play:      onCol = theme::good; break;
        case Kind::cycle:     onCol = juce::Colour (0xfff2b84b); break;
        case Kind::metronome: case Kind::countIn: onCol = juce::Colour (0xff60a5fa); break;
        case Kind::typing:    onCol = theme::accent2; break;
        default: break;
    }
    g.setColour (on ? onCol.withAlpha (0.9f) : theme::panelRaised.brighter (over ? 0.15f : 0.0f).darker (down ? 0.2f : 0.0f));
    g.fillRoundedRectangle (r, 6.0f);
    if (! on) { g.setColour (theme::outline.brighter (0.2f)); g.drawRoundedRectangle (r, 6.0f, 1.0f); }

    const auto ic = on ? (kind == Kind::record || kind == Kind::play ? juce::Colours::white : juce::Colours::black) : theme::text;
    auto c = r.getCentre();
    const float s = juce::jmin (r.getWidth(), r.getHeight()) * 0.28f;
    juce::Path p;
    g.setColour (kind == Kind::record && ! on ? theme::bad : ic);
    switch (kind)
    {
        case Kind::toStart:
            g.fillRect (c.x - s, c.y - s, s * 0.3f, s * 2);
            p.addTriangle (c.x + s, c.y - s, c.x + s, c.y + s, c.x - s * 0.6f, c.y);
            g.fillPath (p);
            break;
        case Kind::rewind:
            p.addTriangle (c.x, c.y - s, c.x, c.y + s, c.x - s, c.y);
            p.addTriangle (c.x + s, c.y - s, c.x + s, c.y + s, c.x, c.y);
            g.fillPath (p);
            break;
        case Kind::forward:
            p.addTriangle (c.x - s, c.y - s, c.x - s, c.y + s, c.x, c.y);
            p.addTriangle (c.x, c.y - s, c.x, c.y + s, c.x + s, c.y);
            g.fillPath (p);
            break;
        case Kind::stop:    g.fillRect (c.x - s * 0.8f, c.y - s * 0.8f, s * 1.6f, s * 1.6f); break;
        case Kind::play:    p.addTriangle (c.x - s * 0.7f, c.y - s, c.x - s * 0.7f, c.y + s, c.x + s, c.y); g.fillPath (p); break;
        case Kind::record:  g.fillEllipse (c.x - s, c.y - s, s * 2, s * 2); break;
        case Kind::cycle:
        {
            juce::Path arc;
            arc.addCentredArc (c.x, c.y, s, s * 0.8f, 0.0f, 0.4f, juce::MathConstants<float>::twoPi - 0.4f, true);
            g.strokePath (arc, juce::PathStrokeType (2.0f));
            p.addTriangle (c.x + s * 0.2f, c.y - s * 1.2f, c.x + s * 0.2f, c.y - s * 0.4f, c.x + s * 0.8f, c.y - s * 0.8f);
            g.fillPath (p);
            break;
        }
        case Kind::metronome:
            p.addTriangle (c.x - s * 0.8f, c.y + s, c.x + s * 0.8f, c.y + s, c.x, c.y - s * 1.1f);
            g.strokePath (p, juce::PathStrokeType (1.8f));
            g.drawLine (c.x, c.y + s * 0.6f, c.x + s * 0.7f, c.y - s * 0.9f, 1.8f);
            break;
        case Kind::countIn:
            g.setFont (uiFont (11.0f, true));
            g.drawText ("1234", r, juce::Justification::centred);
            break;
        case Kind::typing:
            g.drawRoundedRectangle (c.x - s * 1.3f, c.y - s * 0.8f, s * 2.6f, s * 1.6f, 2.0f, 1.6f);
            for (int i = 0; i < 4; ++i) g.fillRect (c.x - s * 0.95f + i * s * 0.55f, c.y - s * 0.4f, s * 0.3f, s * 0.3f);
            g.fillRect (c.x - s * 0.7f, c.y + s * 0.2f, s * 1.4f, s * 0.25f);
            break;
    }
}

// =====================================================================================================
Lcd::Lcd (StudioContext& c) : ctx (c)
{
    tap.setTooltip ("Tap the tempo (4+ taps)");
    tap.onClick = [this]
    {
        const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
        if (! taps.isEmpty() && now - taps.getLast() > 2.0) taps.clear();
        taps.add (now);
        if (taps.size() > 8) taps.remove (0);
        if (taps.size() >= 4)
        {
            const double avg = (taps.getLast() - taps.getFirst()) / (taps.size() - 1);
            ctx.beginEdit ("Tempo");
            ctx.project.setTempo (std::round (60.0 / avg * 10.0) / 10.0);
        }
    };
    addAndMakeVisible (tap);
    startTimerHz (20);
}

juce::Rectangle<int> Lcd::keyArea() const   { return { getWidth() - 264, 0, 62, getHeight() }; }
juce::Rectangle<int> Lcd::tempoArea() const { return { getWidth() - 200, 0, 90, getHeight() }; }
juce::Rectangle<int> Lcd::sigArea() const   { return { getWidth() - 106, 0, 50, getHeight() }; }

void Lcd::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff0a0c10));
    g.fillRoundedRectangle (r, 7.0f);
    g.setColour (theme::outline);
    g.drawRoundedRectangle (r.reduced (0.5f), 7.0f, 1.0f);

    auto& p = ctx.project;
    const double beats = ctx.engine.getPositionBeats();
    const bool rec = ctx.engine.isRecording(), counting = ctx.engine.isCountingIn();

    auto pos = getLocalBounds().withWidth (getWidth() - 268).reduced (12, 3);
    g.setColour (rec ? theme::bad : counting ? theme::warn : theme::text);
    g.setFont (juce::Font (juce::FontOptions (24.0f, juce::Font::bold)).withExtraKerningFactor (0.04f));
    g.drawText (formatBarsBeats (beats, p.beatsPerBar(), p.tsDen()), pos.removeFromLeft (pos.getWidth() / 2 + 20), juce::Justification::centredLeft);
    g.setFont (uiFont (13.0f));
    g.setColour (theme::textDim);
    g.drawText (formatSeconds (p.beatsToSeconds (beats)), pos, juce::Justification::centredLeft);

    // tempo
    auto t = tempoArea();
    g.setColour (theme::text);
    g.setFont (uiFont (19.0f, true));
    g.drawText (juce::String (p.tempo(), p.tempo() == std::floor (p.tempo()) ? 0 : 1), t.withTrimmedBottom (14), juce::Justification::centredBottom);
    g.setFont (uiFont (9.5f, true));
    g.setColour (theme::textFaint);
    g.drawText ("TEMPO", t.withTrimmedTop (getHeight() - 16), juce::Justification::centredTop);

    auto s = sigArea();
    g.setColour (theme::text);
    g.setFont (uiFont (17.0f, true));
    g.drawText (juce::String (p.tsNum()) + "/" + juce::String (p.tsDen()), s.withTrimmedBottom (14), juce::Justification::centredBottom);
    g.setFont (uiFont (9.5f, true));
    g.setColour (theme::textFaint);
    g.drawText ("TIME", s.withTrimmedTop (getHeight() - 16), juce::Justification::centredTop);

    auto k = keyArea();
    g.setColour (theme::text);
    g.setFont (uiFont (18.0f, true));
    g.drawText (Project::keyName (p.key(), p.scale()).replace (" major", "").replace (" minor", "m"), k.withTrimmedBottom (14), juce::Justification::centredBottom);
    g.setFont (uiFont (9.5f, true));
    g.setColour (theme::textFaint);
    g.drawText ("KEY", k.withTrimmedTop (getHeight() - 16), juce::Justification::centredTop);

    g.setColour (theme::outline);
    g.drawVerticalLine (getWidth() - 268, 6.0f, (float) getHeight() - 6.0f);
}

void Lcd::resized()
{
    tap.setBounds (getWidth() - 52, 8, 44, getHeight() - 16);
}

void Lcd::mouseDown (const juce::MouseEvent& e)
{
    draggingTempo = tempoArea().contains (e.getPosition());
    if (draggingTempo) { dragStartTempo = ctx.project.tempo(); ctx.beginEdit ("Tempo"); }
    if (keyArea().contains (e.getPosition()))
    {
        juce::PopupMenu m, major, minor;
        for (int k = 0; k < 12; ++k)
        {
            major.addItem (1 + k, Project::keyName (k, 0), true, ctx.project.key() == k && ctx.project.scale() == 0);
            minor.addItem (13 + k, Project::keyName (k, 1), true, ctx.project.key() == k && ctx.project.scale() == 1);
        }
        m.addSectionHeader ("Song key (Scale Lock, Vocal Tune and Loops follow it)");
        m.addSubMenu ("Major", major);
        m.addSubMenu ("Minor", minor);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this] (int r)
        {
            if (r > 0) { ctx.beginEdit ("Key"); ctx.project.setKey ((r - 1) % 12, r > 12 ? 1 : 0); }
        });
        return;
    }
    if (sigArea().contains (e.getPosition()))
    {
        juce::PopupMenu m;
        int id = 1;
        for (auto [n, d] : { std::pair<int, int> { 4, 4 }, { 3, 4 }, { 2, 4 }, { 5, 4 }, { 6, 8 }, { 7, 8 }, { 9, 8 }, { 12, 8 } })
            m.addItem (id++, juce::String (n) + "/" + juce::String (d), true, ctx.project.tsNum() == n && ctx.project.tsDen() == d);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this] (int r)
        {
            const std::pair<int, int> sigs[] = { { 4, 4 }, { 3, 4 }, { 2, 4 }, { 5, 4 }, { 6, 8 }, { 7, 8 }, { 9, 8 }, { 12, 8 } };
            if (r > 0) { ctx.beginEdit ("Time signature"); ctx.project.setTimeSignature (sigs[r - 1].first, sigs[r - 1].second); }
        });
    }
}

void Lcd::mouseDrag (const juce::MouseEvent& e)
{
    if (! draggingTempo) return;
    const double t = std::round (dragStartTempo - e.getDistanceFromDragStartY() * 0.25);
    ctx.project.setTempo (t);
}

void Lcd::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! tempoArea().contains (e.getPosition())) return;
    auto* w = new juce::AlertWindow ("Tempo", "Beats per minute:", juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("bpm", juce::String (ctx.project.tempo(), 1));
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int r)
    {
        const double v = w->getTextEditorContents ("bpm").getDoubleValue();
        if (r == 1 && v >= 20 && v <= 300) { ctx.beginEdit ("Tempo"); ctx.project.setTempo (v); }
    }), true);
}

// =====================================================================================================
ControlBar::ControlBar (StudioContext& c) : ctx (c), lcd (c)
{
    projectButton.setTooltip ("New, open, save, import, export, plugins");
    projectButton.onClick = [this] { if (onProjectMenu) onProjectMenu(); };
    addAndMakeVisible (projectButton);

    toStart.onClick = [this] { ctx.engine.setPositionBeats ((bool) ctx.project.tree()[ids::cycleOn] ? (double) ctx.project.tree()[ids::cycleStart] : 0.0); };
    rewind.onClick  = [this] { const double bpb = ctx.project.beatsPerBar(); ctx.engine.setPositionBeats (juce::jmax (0.0, std::ceil (ctx.engine.getPositionBeats() / bpb - 1.0001) * bpb)); };
    forward.onClick = [this] { const double bpb = ctx.project.beatsPerBar(); ctx.engine.setPositionBeats (std::floor (ctx.engine.getPositionBeats() / bpb + 1.0001) * bpb); };
    stopB.onClick   = [this] { ctx.engine.stop(); };
    playB.onClick   = [this] { ctx.engine.togglePlay(); };
    recB.onClick    = [this] { ctx.engine.record(); };
    cycleB.onClick  = [this] { auto& t = ctx.project.tree(); t.setProperty (ids::cycleOn, ! (bool) t[ids::cycleOn], nullptr); };
    metroB.onClick  = [this] { auto& t = ctx.project.tree(); t.setProperty (ids::metronome, ! (bool) t[ids::metronome], nullptr); };
    countB.onClick  = [this] { auto& t = ctx.project.tree(); t.setProperty (ids::countIn, ! (bool) t[ids::countIn], nullptr); };
    typingB.onClick = [this] { if (onToggleTyping) onToggleTyping(); };
    for (auto* b : { &toStart, &rewind, &forward, &stopB, &playB, &recB, &cycleB, &metroB, &countB, &typingB })
        addAndMakeVisible (b);
    addAndMakeVisible (lcd);

    for (auto [b, cb] : { std::pair<juce::TextButton*, std::function<void()>*> { &browserB, &onToggleBrowser }, { &editorB, &onToggleEditor },
                          { &mixerB, &onToggleMixer }, { &keysB, &onToggleKeys }, { &pluginsB, &onTogglePlugins } })
    {
        b->setClickingTogglesState (false);
        b->setColour (juce::TextButton::buttonOnColourId, theme::accent.withAlpha (0.8f));
        auto* callback = cb;
        b->onClick = [callback] { if (*callback) (*callback)(); };
        addAndMakeVisible (b);
    }
    browserB.setTooltip ("Show / hide the Library (B)");
    editorB.setTooltip ("Show / hide the editor (E)");
    mixerB.setTooltip ("Show / hide the mixer (X)");
    keysB.setTooltip ("Show / hide the on-screen keyboard");
    pluginsB.setTooltip ("Show / hide the open instruments and effects (P)");

    master.setRange (-40.0, 6.0, 0.1);
    master.setSkewFactorFromMidPoint (-10.0);
    master.setDoubleClickReturnValue (true, 0.0);
    master.setPopupDisplayEnabled (true, true, nullptr);
    master.setTextValueSuffix (" dB");
    master.setTooltip ("Master volume");
    master.onValueChange = [this] { ctx.project.tree().setProperty (ids::masterVolume, master.getValue(), nullptr); };
    addAndMakeVisible (master);
    addAndMakeVisible (meterL);
    addAndMakeVisible (meterR);
    cpu.setFont (uiFont (10.5f));
    cpu.setColour (juce::Label::textColourId, theme::textFaint);
    cpu.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (cpu);

    ctx.project.tree().addListener (this);
    syncToggles();
    startTimerHz (20);
}

ControlBar::~ControlBar() { ctx.project.tree().removeListener (this); }

void ControlBar::syncToggles()
{
    auto& t = ctx.project.tree();
    cycleB.lit = (bool) t[ids::cycleOn];
    metroB.lit = (bool) t[ids::metronome];
    countB.lit = (bool) t[ids::countIn];
    if (! master.isMouseButtonDown()) master.setValue ((double) t[ids::masterVolume], juce::dontSendNotification);
    for (auto* b : { &cycleB, &metroB, &countB }) b->repaint();
}

void ControlBar::setPanelStates (bool browser, bool editor, bool mixer, bool keys, bool plugins, bool typing)
{
    browserB.setToggleState (browser, juce::dontSendNotification);
    editorB.setToggleState (editor, juce::dontSendNotification);
    mixerB.setToggleState (mixer, juce::dontSendNotification);
    keysB.setToggleState (keys, juce::dontSendNotification);
    pluginsB.setToggleState (plugins, juce::dontSendNotification);
    typingB.lit = typing;
    typingB.repaint();
}

void ControlBar::timerCallback()
{
    const bool playing = ctx.engine.isPlaying() || ctx.engine.isCountingIn();
    if (playB.lit != playing) { playB.lit = playing; playB.repaint(); }
    const bool rec = ctx.engine.isRecording() || (ctx.engine.isCountingIn());
    if (recB.lit != rec) { recB.lit = rec; recB.repaint(); }
    auto [l, r] = ctx.engine.readMasterMeter();
    meterL.setLevel (l);
    meterR.setLevel (r);
    if (++tick % 10 == 0 && cpuUsage)
        cpu.setText ("CPU " + juce::String ((int) std::round (cpuUsage() * 100.0)) + "%", juce::dontSendNotification);
}

void ControlBar::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    g.setColour (theme::outline);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
}

void ControlBar::resized()
{
    auto r = getLocalBounds().reduced (10, 7);
    browserB.setBounds (r.removeFromLeft (70)); r.removeFromLeft (6);
    projectButton.setBounds (r.removeFromLeft (80)); r.removeFromLeft (14);

    auto right = r.removeFromRight (374);
    pluginsB.setBounds (right.removeFromRight (60)); right.removeFromRight (4);
    keysB.setBounds (right.removeFromRight (52)); right.removeFromRight (4);
    mixerB.setBounds (right.removeFromRight (58)); right.removeFromRight (4);
    editorB.setBounds (right.removeFromRight (60)); right.removeFromRight (10);
    auto vol = right;
    master.setBounds (vol.removeFromTop (vol.getHeight() / 2 + 2));
    meterL.setBounds (vol.removeFromTop (5).reduced (6, 0));
    vol.removeFromTop (2);
    meterR.setBounds (vol.removeFromTop (5).reduced (6, 0));

    const int bw = 38;
    for (auto* b : { &toStart, &rewind, &forward, &stopB, &playB, &recB })
    {
        b->setBounds (r.removeFromLeft (bw));
        r.removeFromLeft (3);
    }
    r.removeFromLeft (10);
    auto lcdArea = r.removeFromLeft (juce::jmin (540, r.getWidth() - 4 * (bw + 3) - 10));
    lcd.setBounds (lcdArea);
    r.removeFromLeft (10);
    for (auto* b : { &cycleB, &metroB, &countB, &typingB })
    {
        b->setBounds (r.removeFromLeft (bw));
        r.removeFromLeft (3);
    }
    cpu.setBounds (r);
}

} // namespace wis::daw
