#include "StemMixer.h"

namespace wis
{

StemStrip::StemStrip (StemPlayer& p, StemId stem) : player (p), id (stem)
{
    auto& info = stemInfo (id);

    name.setText (info.displayName, juce::dontSendNotification);
    name.setFont (uiFont (13.5f, true));
    name.setColour (juce::Label::textColourId, theme::text);
    name.setTooltip (info.hint);
    name.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (name);

    for (auto* b : { &mute, &solo })
    {
        b->setClickingTogglesState (true);
        addAndMakeVisible (b);
    }
    mute.setColour (juce::TextButton::buttonOnColourId, theme::warn.darker (0.1f));
    mute.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    solo.setColour (juce::TextButton::buttonOnColourId, theme::good.darker (0.1f));
    solo.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    mute.setTooltip ("Mute " + juce::String (info.displayName) + " - so you can play this part yourself");
    solo.setTooltip ("Solo - hear only this stem (and other soloed stems)");

    mute.onClick = [this] { player.control (id).mute = mute.getToggleState(); if (onChange) onChange(); };
    solo.onClick = [this] { player.control (id).solo = solo.getToggleState(); if (onChange) onChange(); };

    gain.setRange (-60.0, 6.0, 0.1);
    gain.setSkewFactorFromMidPoint (-12.0);
    gain.setValue (0.0, juce::dontSendNotification);
    gain.setDoubleClickReturnValue (true, 0.0);
    gain.setColour (juce::Slider::trackColourId, stemColour (id));
    gain.setPopupDisplayEnabled (true, true, nullptr);
    gain.setTextValueSuffix (" dB");
    gain.setTooltip ("Volume (double-click = 0 dB)");
    gain.onValueChange = [this] { player.control (id).gainDb = (float) gain.getValue(); };
    addAndMakeVisible (gain);

    balance.setRange (-1.0, 1.0, 0.01);
    balance.setValue (0.0, juce::dontSendNotification);
    balance.setDoubleClickReturnValue (true, 0.0);
    balance.setColour (juce::Slider::rotarySliderFillColourId, stemColour (id));
    balance.setTooltip ("Left / right balance (double-click = centre)");
    balance.onValueChange = [this] { player.control (id).balance = (float) balance.getValue(); };
    addAndMakeVisible (balance);

    meter.colour = stemColour (id);
    addAndMakeVisible (meter);
}

void StemStrip::syncFromPlayer()
{
    auto& c = player.control (id);
    mute.setToggleState (c.mute.load(), juce::dontSendNotification);
    solo.setToggleState (c.solo.load(), juce::dontSendNotification);
    gain.setValue (c.gainDb.load(), juce::dontSendNotification);
    balance.setValue (c.balance.load(), juce::dontSendNotification);
}

void StemStrip::setPresent (bool p)
{
    present = p;
    for (juce::Component* c : { (juce::Component*) &mute, (juce::Component*) &solo, (juce::Component*) &gain, (juce::Component*) &balance })
        c->setEnabled (p);
    name.setColour (juce::Label::textColourId, p ? theme::text : theme::textFaint);
    repaint();
}

void StemStrip::refreshMeter()
{
    meter.setLevel (player.readMeter (id));
}

void StemStrip::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0, 1);
    g.setColour (theme::panelRaised.withAlpha (present ? 1.0f : 0.5f));
    g.fillRect (r);
    g.setColour (present ? stemColour (id) : theme::ledOff);
    g.fillRect (r.withWidth (4.0f));
}

void StemStrip::resized()
{
    auto r = getLocalBounds().reduced (10, 3).withTrimmedLeft (2);
    const bool compact = getHeight() < 40;

    auto top = compact ? r : r.removeFromTop (r.getHeight() / 2);
    solo.setBounds (top.removeFromRight (24).withSizeKeepingCentre (24, juce::jmin (22, top.getHeight())));
    top.removeFromRight (4);
    mute.setBounds (top.removeFromRight (24).withSizeKeepingCentre (24, juce::jmin (22, top.getHeight())));
    top.removeFromRight (6);
    balance.setBounds (top.removeFromRight (juce::jmin (26, top.getHeight() + 4)));
    top.removeFromRight (4);

    if (compact)
    {
        name.setBounds (top.removeFromLeft (110));
        auto meterR = top.removeFromBottom (3);
        gain.setBounds (top);
        meter.setBounds (meterR);
    }
    else
    {
        name.setBounds (top);
        auto bottom = r;
        meter.setBounds (bottom.removeFromBottom (3).withTrimmedRight (4));
        gain.setBounds (bottom);
    }
}

// ---- StemMixer ------------------------------------------------------------------------------------

StemMixer::StemMixer (StemPlayer& p) : player (p)
{
    for (auto& s : allStems())
    {
        auto* strip = strips.add (new StemStrip (player, s.id));
        strip->onChange = [this] { if (onMixChanged) onMixChanged(); };
        addAndMakeVisible (strip);
    }

    mixLabel.setText ("FULL MIX", juce::dontSendNotification);
    mixLabel.setFont (uiFont (11.5f, true));
    mixLabel.setColour (juce::Label::textColourId, theme::textDim);
    addAndMakeVisible (mixLabel);
}

void StemMixer::setSong (PlayableSong::Ptr song)
{
    for (auto* s : strips)
        s->setPresent (song != nullptr && song->present[(size_t) s->getId()]);
}

void StemMixer::refresh()
{
    for (auto* s : strips)
        s->refreshMeter();
}

void StemMixer::layoutRows (int ruler, int mix, int top, int rowHeight)
{
    rulerH = ruler;
    mixH = mix;
    mixLabel.setBounds (12, ruler, getWidth() - 12, mix);
    int y = top;
    for (auto* s : strips)
    {
        s->setBounds (0, y, getWidth(), rowHeight);
        y += rowHeight;
    }
    repaint();
}

void StemMixer::soloOut (StemId id, bool mutePart)
{
    player.control (id).mute = mutePart;
    for (auto* s : strips) s->syncFromPlayer();
    if (onMixChanged) onMixChanged();
}

void StemMixer::sync()
{
    for (auto* s : strips) s->syncFromPlayer();
    if (onMixChanged) onMixChanged();
}

void StemMixer::resetAll()
{
    for (auto& s : allStems())
    {
        auto& c = player.control (s.id);
        c.mute = false; c.solo = false; c.gainDb = 0.0f; c.balance = 0.0f;
    }
    for (auto* s : strips) s->syncFromPlayer();
    if (onMixChanged) onMixChanged();
}

std::array<bool, numStemIds> StemMixer::audibleStems() const
{
    std::array<bool, numStemIds> a {};
    bool anySolo = false;
    for (auto& s : allStems()) anySolo = anySolo || player.control (s.id).solo.load();
    for (auto& s : allStems())
    {
        auto& c = player.control (s.id);
        a[(size_t) s.id] = ! c.mute.load() && (! anySolo || c.solo.load()) && c.gainDb.load() > -59.0f;
    }
    return a;
}

void StemMixer::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    g.setColour (theme::outline);
    g.drawHorizontalLine (rulerH - 1, 0.0f, (float) getWidth());
    g.setColour (theme::textFaint);
    g.setFont (uiFont (11.0f, true));
    g.drawText ("STEMS", 12, 0, 100, rulerH, juce::Justification::centredLeft);
}

} // namespace wis
