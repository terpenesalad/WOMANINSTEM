#include "AppShell.h"

namespace wis
{

static constexpr int modeBarHeight = 40;

class AppShell::ModeTab : public juce::Button
{
public:
    ModeTab (const juce::String& name, const juce::String& sub) : juce::Button (name), subtitle (sub) {}

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f, 5.0f);
        const bool on = getToggleState();
        if (on)
        {
            juce::ColourGradient grad (theme::accent, r.getX(), 0, theme::accent2, r.getRight(), 0, false);
            g.setGradientFill (grad);
            g.fillRoundedRectangle (r, 6.0f);
        }
        else
        {
            g.setColour (over ? theme::panelRaised.brighter (0.08f) : theme::panelRaised);
            g.fillRoundedRectangle (r, 6.0f);
        }
        g.setColour (on ? juce::Colours::white : (over ? theme::text : theme::textDim));
        g.setFont (uiFont (13.0f, true));
        g.drawText (getName(), r, juce::Justification::centred);
    }

    juce::String subtitle;
};

AppShell::AppShell (juce::PropertiesFile& s) : settings (s)
{
    setOpaque (true);
    setWantsKeyboardFocus (false);

    playAlong.setEmbedded (true);
    addAndMakeVisible (playAlong);

    studio = std::make_unique<daw::StudioPage> (project, dawEngine, host, settings);
    studio->setDeviceManager (&playAlong.getDeviceManager());
    addChildComponent (*studio);

    playTab = std::make_unique<ModeTab> ("PLAY ALONG", "Split songs and jam");
    studioTab = std::make_unique<ModeTab> ("STUDIO", "Record and produce");
    playTab->setTooltip ("Play Along: split any song into stems, mute your part and jam with your amp rig");
    studioTab->setTooltip ("Studio: a full multitrack recording studio - instruments, drummer, audio recording, piano roll, mixer, effects, VST3");
    playTab->onClick = [this] { setMode (0); };
    studioTab->onClick = [this] { setMode (1); };
    addAndMakeVisible (*playTab);
    addAndMakeVisible (*studioTab);

    audioButton.setTooltip ("Audio interface, inputs, sample rate, buffer size and MIDI keyboards");
    audioButton.onClick = [this] { playAlong.showAudioSettings(); };
    helpButton.setTooltip ("Quick start and shortcuts");
    helpButton.onClick = [this] { if (mode == 0) playAlong.showHelp(); else studio->showShortcuts(); };
    addAndMakeVisible (audioButton);
    addAndMakeVisible (helpButton);

    playAlong.getEngine().scope = &scopeFeed;
    dawEngine.scope = &scopeFeed;
    scopeButton.setTooltip ("Oscilloscope: a glowing picture drawn by your instrument, the song or the whole mix. "
                            "Put it on a projector or second screen and go full screen (Ctrl+Shift+O)");
    scopeButton.onClick = [this] { toggleScope(); };
    addAndMakeVisible (scopeButton);

    playAlong.onOpenInStudio = [this] (const SongInfo& info, const std::array<bool, numStemIds>& muted, const juce::ValueTree& rig, const juce::String& part)
    {
        setMode (1);
        studio->openSongInStudio (info, muted, rig, part);
    };

    setMode (settings.getIntValue ("appMode", 0) == 1 ? 1 : 0);
    setSize (1440, 900);
}

AppShell::~AppShell()
{
    scopeWindow.reset();
    playAlong.getEngine().scope = nullptr;
    dawEngine.scope = nullptr;
    dawEngine.detach();
    studio.reset();
}

void AppShell::setMode (int m)
{
    mode = m;
    playTab->setToggleState (m == 0, juce::dontSendNotification);
    studioTab->setToggleState (m == 1, juce::dontSendNotification);

    auto& dm = playAlong.getDeviceManager();
    if (m == 1)
    {
        playAlong.setActive (false);
        playAlong.setVisible (false);
        dawEngine.attach (dm);
        studio->setVisible (true);
        studio->setActive (true);
        studio->grabKeyboardFocus();
    }
    else
    {
        studio->setActive (false);
        studio->setVisible (false);
        dawEngine.detach();
        playAlong.setActive (true);
        playAlong.setVisible (true);
        playAlong.grabKeyboardFocus();
    }
    settings.setValue ("appMode", m);
    if (scopeWindow != nullptr) scopeWindow->setAppMode (m);
    repaint();
}

void AppShell::toggleScope()
{
    if (scopeWindow == nullptr)
    {
        scopeWindow = std::make_unique<ScopeWindow> (scopeFeed, settings);
        scopeWindow->setAppMode (mode);
    }
    if (scopeWindow->isVisible() && scopeWindow->isActiveWindow())
        scopeWindow->closeButtonPressed();
    else
        scopeWindow->show();
}

void AppShell::openFile (const juce::File& f)
{
    if (f.hasFileExtension (daw::Project::fileExtension()))
    {
        setMode (1);
        studio->confirmDiscardThen ([this, f] { studio->openProjectFile (f); });
    }
    else
    {
        setMode (0);
        playAlong.openFile (f);
    }
}

void AppShell::filesDropped (const juce::StringArray& files, int, int)
{
    if (! files.isEmpty()) openFile (juce::File (files[0]));
}

void AppShell::requestQuit (std::function<void()> quit)
{
    if (mode == 1) dawEngine.stop();
    studio->confirmDiscardThen (std::move (quit));
}

void AppShell::saveState()
{
    playAlong.saveState();
    studio->saveSettings();
    if (scopeWindow != nullptr) scopeWindow->saveBounds();
    settings.setValue ("appMode", mode);
    settings.saveIfNeeded();
}

bool AppShell::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress ('1', juce::ModifierKeys::commandModifier, 0)) { setMode (0); return true; }
    if (k == juce::KeyPress ('2', juce::ModifierKeys::commandModifier, 0)) { setMode (1); return true; }
    if (k == juce::KeyPress ('o', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)) { toggleScope(); return true; }
    return false;
}

void AppShell::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg);
    auto bar = getLocalBounds().removeFromTop (modeBarHeight);
    g.setColour (theme::panel.darker (0.25f));
    g.fillRect (bar);
    g.setColour (theme::outline);
    g.drawHorizontalLine (bar.getBottom() - 1, 0.0f, (float) getWidth());

    auto logo = bar.reduced (16, 0).removeFromLeft (200).toFloat();
    g.setFont (uiFont (19.0f, true));
    juce::ColourGradient grad (theme::accent, logo.getX(), 0, theme::accent2, logo.getX() + 150.0f, 0, false);
    g.setGradientFill (grad);
    g.drawText ("WOMANINSTEM", logo, juce::Justification::centredLeft);

    g.setColour (theme::textFaint);
    g.setFont (uiFont (11.5f));
    const auto tag = mode == 0 ? juce::String ("split it. mute it. play it.") : juce::String ("write it. record it. mix it.");
    g.drawText (tag, getLocalBounds().removeFromTop (modeBarHeight).withTrimmedLeft (getWidth() / 2 + 150).withTrimmedRight (290), juce::Justification::centredLeft);
}

void AppShell::resized()
{
    auto r = getLocalBounds();
    auto bar = r.removeFromTop (modeBarHeight);

    const int tabW = 138;
    auto tabs = bar.withSizeKeepingCentre (tabW * 2 + 8, modeBarHeight);
    playTab->setBounds (tabs.removeFromLeft (tabW));
    tabs.removeFromLeft (8);
    studioTab->setBounds (tabs.removeFromLeft (tabW));

    auto right = bar.reduced (14, 6);
    helpButton.setBounds (right.removeFromRight (32));
    right.removeFromRight (8);
    audioButton.setBounds (right.removeFromRight (124));
    right.removeFromRight (8);
    scopeButton.setBounds (right.removeFromRight (78));

    playAlong.setBounds (r);
    studio->setBounds (r);
}

} // namespace wis
