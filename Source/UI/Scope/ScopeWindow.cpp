#include "ScopeWindow.h"
#include "UI/LookAndFeel.h"

namespace wis
{

namespace
{
    struct SourceChoice { const char* name; ScopeFeed::Tap tap; const char* hint; };

    const std::array<SourceChoice, 4> playSources { {
        { "My instrument",          ScopeFeed::instrument, "Your instrument through the amp rig" },
        { "The song",               ScopeFeed::song,       "The stems you're hearing (solo a stem in the mixer to watch just that one)" },
        { "Everything",             ScopeFeed::everything, "The whole mix: the song and you" },
        { "Me vs the song",         ScopeFeed::duet,       "You on one axis, the song on the other. Try the XY shape." },
    } };
    const std::array<SourceChoice, 3> studioSources { {
        { "Master",                 ScopeFeed::studioMaster, "Everything the Studio is playing" },
        { "Selected track",         ScopeFeed::studioTrack,  "The selected track after its instrument and effects (even while you play it live)" },
        { "Selected track vs master", ScopeFeed::studioDuet, "The selected track on one axis, the master on the other. Try the XY shape." },
    } };

    constexpr int stripHeight = 118;
    constexpr double hideControlsAfterMs = 2500.0;
}

// =====================================================================================================
//  Controls strip
// =====================================================================================================
class ScopeView::Controls : public juce::Component
{
public:
    struct MiniKnob : public juce::Component
    {
        MiniKnob (const juce::String& text, const juce::String& tip)
        {
            label.setText (text, juce::dontSendNotification);
            label.setJustificationType (juce::Justification::centred);
            label.setFont (uiFont (11.0f, true));
            label.setColour (juce::Label::textColourId, theme::textDim);
            label.setInterceptsMouseClicks (false, false);
            addAndMakeVisible (label);
            slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            slider.setMouseDragSensitivity (180);
            slider.setTooltip (tip + " (double-click to reset)");
            slider.setWantsKeyboardFocus (false);
            addAndMakeVisible (slider);
        }
        void resized() override
        {
            auto r = getLocalBounds();
            label.setBounds (r.removeFromBottom (15));
            slider.setBounds (r);
        }
        juce::Label label;
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    };

    explicit Controls (ScopeView& v) : view (v)
    {
        auto setupCombo = [this] (juce::ComboBox& c, const juce::String& tip)
        {
            c.setTooltip (tip);
            c.setWantsKeyboardFocus (false);
            addAndMakeVisible (c);
        };
        setupCombo (source, "What the beam draws");
        setupCombo (colour, "Colours (C cycles through them). Tame: lime and aqua, like the projections at a Tame Impala show");
        colour.addItemList (ScopeSettings::paletteNames(), 1);
        source.onChange = [this] { view.setSourceIndex (source.getSelectedItemIndex()); };
        colour.onChange = [this] { view.cfg.palette = juce::jmax (0, colour.getSelectedItemIndex()); view.saveSettings(); };

        const juce::StringArray shapeTips {
            "Swirl: loops and spirals that grow with how hard you play (a single note draws a circle, chords draw flowers)",
            "XY: classic oscilloscope Lissajous figures, left channel against right (or you against the song)",
            "Wave: the waveform itself, held still on each cycle" };
        for (int i = 0; i < ScopeSettings::numShapes; ++i)
        {
            auto* b = shapes.add (new juce::TextButton (ScopeSettings::shapeNames()[i]));
            b->setClickingTogglesState (true);
            b->setRadioGroupId (4711);
            b->setTooltip (shapeTips[i] + "  [" + juce::String (i + 1) + "]");
            b->setWantsKeyboardFocus (false);
            b->setConnectedEdges ((i > 0 ? juce::Button::ConnectedOnLeft : 0) | (i < ScopeSettings::numShapes - 1 ? juce::Button::ConnectedOnRight : 0));
            b->onClick = [this, i] { if (shapes[i]->getToggleState()) { view.cfg.shape = i; view.saveSettings(); refresh(); } };
            addAndMakeVisible (b);
        }

        for (auto* t : { &autoSize, &mirror })
        {
            t->setWantsKeyboardFocus (false);
            addAndMakeVisible (t);
        }
        autoSize.setTooltip ("Keeps the figure filling the screen whether you play softly or loudly");
        mirror.setTooltip ("Kaleidoscope: four mirrored copies of the beam  [M]");
        autoSize.onClick = [this] { view.cfg.autoSize = autoSize.getToggleState(); view.saveSettings(); };
        mirror.onClick = [this] { view.cfg.mirror = mirror.getToggleState(); view.saveSettings(); };

        fullScreen.setTooltip ("Full screen on the monitor or projector this window is on  [F]. Esc leaves.");
        fullScreen.setWantsKeyboardFocus (false);
        fullScreen.onClick = [this] { if (view.onToggleFullScreen) view.onToggleFullScreen(); };
        addAndMakeVisible (fullScreen);

        addKnob (size,   0.25, 4.0,  1.0,  "How big the figure is", [this] (double v) { view.cfg.size = (float) v; });
        addKnob (trail,  0.0,  1.0,  0.45, "How long the beam keeps glowing after it moves", [this] (double v) { view.cfg.trail = (float) v; });
        addKnob (glow,   0.0,  1.0,  0.6,  "Bloom around the beam", [this] (double v) { view.cfg.glow = (float) v; });
        addKnob (spin,  -1.0,  1.0,  0.0,  "Slowly rotates the picture (left = anticlockwise)", [this] (double v) { view.cfg.spin = (float) v; });
        addKnob (tangle, 0.0,  1.0,  0.35, "Swirl: clean circles (left) to knotted scribbles (right). Wave: how much time is shown", [this] (double v) { view.cfg.tangle = (float) v; });
        addKnob (beam,   0.0,  1.0,  0.5,  "Beam brightness", [this] (double v) { view.cfg.beam = (float) v; });
        size.slider.setSkewFactorFromMidPoint (1.0);

        refresh();
    }

    void addKnob (MiniKnob& k, double lo, double hi, double def, const juce::String& tip, std::function<void (double)> set)
    {
        k.slider.setRange (lo, hi, 0.0);
        k.slider.setDoubleClickReturnValue (true, def);
        k.slider.setTooltip (tip + " (double-click to reset)");
        k.slider.onValueChange = [this, &k, set] { set (k.slider.getValue()); view.saveSettings(); };
        addAndMakeVisible (k);
    }

    void refresh()
    {
        const auto& c = view.cfg;
        source.clear (juce::dontSendNotification);
        if (view.appMode == 0) for (auto& s : playSources)   source.addItem (s.name, source.getNumItems() + 1);
        else                   for (auto& s : studioSources) source.addItem (s.name, source.getNumItems() + 1);
        source.setSelectedItemIndex (view.appMode == 0 ? view.sourcePlay : view.sourceStudio, juce::dontSendNotification);
        const juce::String list = view.appMode == 0 ? juce::String (playSources[(size_t) view.sourcePlay].hint)
                                             : juce::String (studioSources[(size_t) view.sourceStudio].hint);
        source.setTooltip ("What the beam draws: " + list);

        colour.setSelectedItemIndex (c.palette, juce::dontSendNotification);
        for (int i = 0; i < shapes.size(); ++i) shapes[i]->setToggleState (c.shape == i, juce::dontSendNotification);
        autoSize.setToggleState (c.autoSize, juce::dontSendNotification);
        mirror.setToggleState (c.mirror, juce::dontSendNotification);
        size.slider.setValue (c.size, juce::dontSendNotification);
        trail.slider.setValue (c.trail, juce::dontSendNotification);
        glow.slider.setValue (c.glow, juce::dontSendNotification);
        spin.slider.setValue (c.spin, juce::dontSendNotification);
        tangle.slider.setValue (c.tangle, juce::dontSendNotification);
        beam.slider.setValue (c.beam, juce::dontSendNotification);
        tangle.label.setText (c.shape == ScopeSettings::wave ? "Time" : "Tangle", juce::dontSendNotification);
        fullScreen.setButtonText (view.fullScreen ? "Exit full screen" : "Full screen");
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (theme::panel.withAlpha (0.86f));
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (theme::outline.withAlpha (0.9f));
        g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 8);
        auto top = r.removeFromTop (30);
        r.removeFromTop (6);

        source.setBounds (top.removeFromLeft (juce::jmin (220, top.getWidth() / 4)));
        top.removeFromLeft (10);
        const int shapeW = 66;
        for (auto* b : shapes) b->setBounds (top.removeFromLeft (shapeW));
        top.removeFromLeft (10);
        fullScreen.setBounds (top.removeFromRight (128));
        top.removeFromRight (10);
        colour.setBounds (top.removeFromLeft (juce::jmin (180, juce::jmax (110, top.getWidth() / 3))));
        top.removeFromLeft (10);
        autoSize.setBounds (top.removeFromLeft (juce::jmin (90, top.getWidth() / 2)));
        mirror.setBounds (top.removeFromLeft (juce::jmin (80, top.getWidth())));

        const int knobW = juce::jlimit (48, 74, r.getWidth() / 6);
        auto row = r.withSizeKeepingCentre (knobW * 6, r.getHeight());
        for (auto* k : { &size, &trail, &glow, &spin, &tangle, &beam })
            k->setBounds (row.removeFromLeft (knobW));
    }

    ScopeView& view;
    juce::ComboBox source, colour;
    juce::OwnedArray<juce::TextButton> shapes;
    juce::ToggleButton autoSize { "Auto size" }, mirror { "Mirror" };
    juce::TextButton fullScreen { "Full screen" };
    MiniKnob size { "Size", "" }, trail { "Trail", "" }, glow { "Glow", "" }, spin { "Spin", "" }, tangle { "Tangle", "" }, beam { "Bright", "" };
};

// =====================================================================================================
//  ScopeView
// =====================================================================================================
ScopeView::ScopeView (ScopeFeed& f, juce::PropertiesFile& s) : feed (f), settings (s)
{
    setOpaque (true);
    setWantsKeyboardFocus (true);
    cfg.load (settings);
    sourcePlay = juce::jlimit (0, (int) playSources.size() - 1, settings.getIntValue ("scope.sourcePlay", 0));
    sourceStudio = juce::jlimit (0, (int) studioSources.size() - 1, settings.getIntValue ("scope.sourceStudio", 0));
    controls = std::make_unique<Controls> (*this);
    addAndMakeVisible (*controls);
    lastMoveMs = juce::Time::getMillisecondCounterHiRes();
}

ScopeView::~ScopeView()
{
    stopTimer();
    feed.tap = ScopeFeed::off;
}

void ScopeView::setAppMode (int mode)
{
    appMode = mode == 1 ? 1 : 0;
    controls->refresh();
    renderer.clear();
    applySource();
}

void ScopeView::setSourceIndex (int index)
{
    if (index < 0) return;
    if (appMode == 0) sourcePlay = juce::jlimit (0, (int) playSources.size() - 1, index);
    else              sourceStudio = juce::jlimit (0, (int) studioSources.size() - 1, index);
    saveSettings();
    controls->refresh();
    renderer.clear();
    applySource();
}

void ScopeView::applySource()
{
    if (! running) { feed.tap = ScopeFeed::off; return; }
    feed.tap = appMode == 0 ? playSources[(size_t) sourcePlay].tap : studioSources[(size_t) sourceStudio].tap;
    cursor = feed.getWritePosition();
    quietSinceMs = juce::Time::getMillisecondCounterHiRes();
}

juce::String ScopeView::sourceName() const
{
    return appMode == 0 ? playSources[(size_t) sourcePlay].name : studioSources[(size_t) sourceStudio].name;
}

void ScopeView::setRunning (bool shouldRun)
{
    running = shouldRun;
    applySource();
    if (running)
    {
        lastFrameMs = juce::Time::getMillisecondCounterHiRes();
        startTimerHz (60);
    }
    else
    {
        stopTimer();
    }
}

void ScopeView::saveSettings()
{
    cfg.save (settings);
    settings.setValue ("scope.sourcePlay", sourcePlay);
    settings.setValue ("scope.sourceStudio", sourceStudio);
}

void ScopeView::setFullScreenState (bool fs)
{
    fullScreen = fs;
    controlsShown = true;
    controls->setAlpha (1.0f);
    controls->setVisible (true);
    setMouseCursor (juce::MouseCursor::NormalCursor);
    lastMoveMs = juce::Time::getMillisecondCounterHiRes();
    controls->refresh();
    resized();
}

void ScopeView::updateRendererSize()
{
    // render at up to ~900 px wide and scale up: the glow hides the difference and it keeps 60 fps cheap
    const int W = juce::jmax (16, getWidth()), H = juce::jmax (16, getHeight());
    const float scale = juce::jmax (1.0f, juce::jmax ((float) W / 900.0f, (float) H / 560.0f));
    renderer.setSize (juce::roundToInt ((float) W / scale), juce::roundToInt ((float) H / scale));
}

void ScopeView::resized()
{
    updateRendererSize();
    auto r = getLocalBounds();
    controls->setBounds (r.removeFromBottom (stripHeight).reduced (12, 0).withTrimmedBottom (12).withWidth (juce::jmin (r.getWidth() - 24, 1100))
                          .withX (juce::jmax (12, (getWidth() - juce::jmin (r.getWidth() - 24, 1100)) / 2)));
}

void ScopeView::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double dt = (now - lastFrameMs) / 1000.0;
    lastFrameMs = now;

    const double sr = feed.getSampleRate();
    const int n = feed.read (cursor, bufA, bufB, (int) (sr * 0.25));
    float pk = 0.0f;
    for (int i = 0; i < n; ++i) pk = juce::jmax (pk, std::abs (bufA[(size_t) i]), std::abs (bufB[(size_t) i]));
    if (pk > 0.003f) quietSinceMs = now;

    renderer.beginFrame (dt, cfg);
    if (n > 0) renderer.addSamples (bufA.data(), bufB.data(), n, sr, cfg);
    else if (cfg.shape == ScopeSettings::wave) { float z = 0.0f; renderer.addSamples (&z, &z, 1, sr, cfg); }
    renderer.renderTo (frame, cfg);

    // full screen: the controls and the mouse pointer get out of the way
    const bool wantControls = ! fullScreen || now - lastMoveMs < hideControlsAfterMs;
    const float target = wantControls ? 1.0f : 0.0f;
    const float alpha = controls->getAlpha();
    if (std::abs (alpha - target) > 0.001f)
    {
        const float next = alpha + (target - alpha) * 0.15f;
        controls->setAlpha (std::abs (next - target) < 0.02f ? target : next);
        controls->setVisible (controls->getAlpha() > 0.0f);
    }
    if (wantControls != controlsShown)
    {
        controlsShown = wantControls;
        setMouseCursor (wantControls ? juce::MouseCursor::NormalCursor : juce::MouseCursor::NoCursor);
    }
    repaint();
}

void ScopeView::paint (juce::Graphics& g)
{
    if (frame.isValid())
    {
        g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
        g.drawImage (frame, getLocalBounds().toFloat(), juce::RectanglePlacement::stretchToFit);
    }
    else
    {
        g.fillAll (juce::Colours::black);
    }

    // a hint while nothing is coming in (never over a projected picture with the controls hidden)
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (running && controlsShown && now - quietSinceMs > 1500.0)
    {
        juce::String hint;
        if (appMode == 0)
            hint = sourcePlay == 0 || sourcePlay == 3 ? "Play something! Pick your input under Audio & MIDI if nothing shows up."
                                                      : "Press play on a song (or pick \"My instrument\").";
        else
            hint = sourceStudio == 0 ? "Press play, or play the selected track." : "Select a track with sound, then play it.";
        auto area = getLocalBounds().withTrimmedBottom (stripHeight).reduced (20).removeFromTop (60);
        g.setColour (theme::textDim.withAlpha (0.8f));
        g.setFont (uiFont (15.0f, true));
        g.drawText ("Waiting for sound from: " + sourceName(), area.removeFromTop (24), juce::Justification::centred);
        g.setFont (uiFont (13.0f));
        g.setColour (theme::textFaint);
        g.drawText (hint, area.removeFromTop (22), juce::Justification::centred);
    }
}

bool ScopeView::keyPressed (const juce::KeyPress& k)
{
    const auto c = juce::CharacterFunctions::toLowerCase (k.getTextCharacter());
    if (k == juce::KeyPress::F11Key || (c == 'f' && ! k.getModifiers().isAnyModifierKeyDown()))
    {
        if (onToggleFullScreen) onToggleFullScreen();
        return true;
    }
    if (k == juce::KeyPress::escapeKey && fullScreen)
    {
        if (onToggleFullScreen) onToggleFullScreen();
        return true;
    }
    if (k == juce::KeyPress ('o', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0))
    {
        if (onClose) onClose();
        return true;
    }
    if (k.getModifiers().isAnyModifierKeyDown()) return false;
    if (c >= '1' && c < '1' + ScopeSettings::numShapes) { cfg.shape = (int) (c - '1'); saveSettings(); controls->refresh(); return true; }
    if (c == 'c') { cfg.palette = (cfg.palette + 1) % ScopeSettings::numPalettes; saveSettings(); controls->refresh(); return true; }
    if (c == 'm') { cfg.mirror = ! cfg.mirror; saveSettings(); controls->refresh(); return true; }
    return false;
}

void ScopeView::mouseMove (const juce::MouseEvent&)
{
    lastMoveMs = juce::Time::getMillisecondCounterHiRes();
}

void ScopeView::mouseDown (const juce::MouseEvent&)
{
    lastMoveMs = juce::Time::getMillisecondCounterHiRes();
    grabKeyboardFocus();
}

void ScopeView::mouseDoubleClick (const juce::MouseEvent&)
{
    if (onToggleFullScreen) onToggleFullScreen();
}

juce::Image ScopeView::renderTestFrame (const ScopeSettings& s, int width, int height, double seconds)
{
    // a plucked bass note with a guitar chord ringing over it, slightly different left and right
    ScopeRenderer r;
    r.setSize (width, height);
    const double sr = 48000.0, fps = 60.0;
    const int perFrame = (int) (sr / fps);
    std::vector<float> a ((size_t) perFrame), b ((size_t) perFrame);
    juce::Image img;
    juce::int64 t = 0;
    const double twoPi = juce::MathConstants<double>::twoPi;
    for (int f = 0; f < (int) (seconds * fps); ++f)
    {
        for (int i = 0; i < perFrame; ++i, ++t)
        {
            const double x = (double) t / sr;
            const double env = 0.35 + 0.65 * std::exp (-std::fmod (x, 1.0) * 2.5);
            const double bass = 0.5 * std::sin (twoPi * 55.0 * x) + 0.22 * std::sin (twoPi * 110.0 * x + 0.4) + 0.09 * std::sin (twoPi * 165.0 * x + 1.1);
            const double chordL = 0.10 * std::sin (twoPi * 220.0 * x) + 0.08 * std::sin (twoPi * 277.2 * x) + 0.07 * std::sin (twoPi * 329.6 * x);
            const double chordR = 0.10 * std::sin (twoPi * 220.4 * x + 0.9) + 0.08 * std::sin (twoPi * 277.0 * x + 0.3) + 0.07 * std::sin (twoPi * 330.1 * x + 2.0);
            a[(size_t) i] = (float) (env * (bass + chordL));
            b[(size_t) i] = (float) (env * (bass * 0.8 + chordR));
        }
        r.beginFrame (1.0 / fps, s);
        r.addSamples (a.data(), b.data(), perFrame, sr, s);
    }
    r.renderTo (img, s);
    return img;
}

// =====================================================================================================
//  ScopeWindow
// =====================================================================================================
ScopeWindow::ScopeWindow (ScopeFeed& feed, juce::PropertiesFile& s)
    : DocumentWindow ("WOMANINSTEM Scope", juce::Colours::black, DocumentWindow::allButtons), settings (s)
{
    setUsingNativeTitleBar (true);
    view = new ScopeView (feed, settings);
    view->setSize (960, 640);
    view->onToggleFullScreen = [this] { toggleFullScreen(); };
    view->onClose = [this] { closeButtonPressed(); };
    setContentOwned (view, true);
    setResizable (true, false);
    setResizeLimits (520, 360, 10000, 10000);
    const auto saved = settings.getValue ("scope.window");
    if (saved.isNotEmpty()) restoreWindowStateFromString (saved);
    else centreWithSize (getWidth(), getHeight());
}

ScopeWindow::~ScopeWindow()
{
    if (isFullScreenNow())
        juce::Desktop::getInstance().setKioskModeComponent (nullptr, false);
    view->setRunning (false);
}

void ScopeWindow::show()
{
    setVisible (true);
    view->setRunning (true);
    toFront (true);
    view->grabKeyboardFocus();
}

void ScopeWindow::saveBounds()
{
    if (! isFullScreenNow())
        settings.setValue ("scope.window", getWindowStateAsString());
}

void ScopeWindow::closeButtonPressed()
{
    if (isFullScreenNow()) toggleFullScreen();
    saveBounds();
    view->setRunning (false);
    setVisible (false);
}

bool ScopeWindow::isFullScreenNow() const
{
    return juce::Desktop::getInstance().getKioskModeComponent() == this;
}

void ScopeWindow::toggleFullScreen()
{
    auto& desktop = juce::Desktop::getInstance();
    if (isFullScreenNow())
    {
        desktop.setKioskModeComponent (nullptr, false);
        view->setFullScreenState (false);
    }
    else
    {
        saveBounds();
        desktop.setKioskModeComponent (this, false);
        view->setFullScreenState (true);
    }
    view->grabKeyboardFocus();
}

} // namespace wis
