#include "TunerView.h"
#include "LookAndFeel.h"

namespace wis
{

void TunerView::setReading (const Tuner::Reading& r)
{
    if (r.valid)
    {
        reading = r;
        framesSinceValid = 0;
        needle += 0.45f * (r.cents - needle);
    }
    else
    {
        ++framesSinceValid;
    }
    repaint();
}

void TunerView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (theme::bg);
    g.fillRoundedRectangle (r, 6.0f);

    const bool show = framesSinceValid < 12;   // hold the last reading briefly
    const float cents = needle;
    const bool inTune = show && std::abs (cents) < 3.0f;
    const auto col = ! show ? theme::textFaint : inTune ? theme::good : std::abs (cents) < 15.0f ? theme::warn : theme::bad;

    auto top = r.removeFromTop (r.getHeight() * 0.62f);

    // note name
    g.setColour (col);
    g.setFont (uiFont (top.getHeight() * 0.78f, true));
    g.drawText (show ? reading.noteName : juce::String ("--"), top.removeFromLeft (top.getWidth() * 0.55f), juce::Justification::centredRight);
    g.setFont (uiFont (top.getHeight() * 0.32f));
    g.setColour (theme::textDim);
    auto info = top.reduced (6, 4);
    g.drawText (show ? juce::String (reading.octave) : juce::String(), info.removeFromTop (info.getHeight() * 0.5f), juce::Justification::bottomLeft);
    g.setFont (uiFont (11.0f));
    g.drawText (show ? juce::String (reading.frequency, 1) + " Hz" : juce::String ("play a note"), info, juce::Justification::topLeft);

    // cents scale
    auto scale = r.reduced (10, 6);
    const float cx = scale.getCentreX();
    g.setColour (theme::outline);
    for (int c = -50; c <= 50; c += 10)
    {
        const float x = cx + (float) c / 50.0f * scale.getWidth() * 0.5f;
        const float th = c == 0 ? scale.getHeight() : scale.getHeight() * 0.45f;
        g.fillRect (x - 0.5f, scale.getCentreY() - th * 0.5f, 1.0f, th);
    }
    g.setColour (theme::good.withAlpha (0.25f));
    g.fillRect (cx - scale.getWidth() * 0.03f, scale.getY(), scale.getWidth() * 0.06f, scale.getHeight());

    if (show)
    {
        const float nx = cx + juce::jlimit (-50.0f, 50.0f, cents) / 50.0f * scale.getWidth() * 0.5f;
        g.setColour (col);
        g.fillRoundedRectangle (nx - 2.5f, scale.getY() - 1.0f, 5.0f, scale.getHeight() + 2.0f, 2.0f);
    }
}

} // namespace wis
