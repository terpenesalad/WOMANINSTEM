#include "SeparationOverlay.h"
#include "LookAndFeel.h"

namespace wis
{

SeparationOverlay::SeparationOverlay()
{
    cancel.onClick = [this] { if (onCancel) onCancel(); };
    addAndMakeVisible (cancel);
    setInterceptsMouseClicks (true, true);
}

void SeparationOverlay::start (const juce::String& songTitle, const juce::String& h)
{
    title = songTitle;
    heading = h;
    stage = "Starting...";
    detail.clear();
    progress = 0.0f;
    lastProgress = 0.0f;
    rateEstimate = 0.0f;
    startTime = lastTime = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    repaint();
}

void SeparationOverlay::update (float p, const juce::String& s)
{
    const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    if (p > lastProgress && now - lastTime > 0.5)
    {
        const float rate = (p - lastProgress) / (float) (now - lastTime);
        rateEstimate = rateEstimate <= 0.0f ? rate : rateEstimate * 0.9f + rate * 0.1f;
        lastProgress = p;
        lastTime = now;
    }
    progress = p;
    stage = s;
    repaint();
}

static juce::String formatDuration (double s)
{
    const int total = (int) std::round (s);
    if (total < 60) return juce::String (total) + " s";
    return juce::String (total / 60) + " min " + juce::String (total % 60).paddedLeft ('0', 2) + " s";
}

void SeparationOverlay::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg.withAlpha (0.86f));

    g.setColour (theme::panel);
    g.fillRoundedRectangle (card, 16.0f);
    g.setColour (theme::outline);
    g.drawRoundedRectangle (card, 16.0f, 1.0f);

    auto r = card.reduced (32.0f, 26.0f);

    g.setColour (theme::accent);
    g.setFont (uiFont (12.0f, true));
    g.drawText (heading, r.removeFromTop (18.0f), juce::Justification::centredLeft);

    g.setColour (theme::text);
    g.setFont (uiFont (22.0f, true));
    g.drawFittedText (title, r.removeFromTop (34.0f).toNearestInt(), juce::Justification::centredLeft, 1);
    r.removeFromTop (10.0f);

    // progress bar
    auto bar = r.removeFromTop (12.0f);
    g.setColour (theme::panelRaised);
    g.fillRoundedRectangle (bar, 6.0f);
    auto fill = bar.withWidth (juce::jmax (12.0f, bar.getWidth() * juce::jlimit (0.0f, 1.0f, progress)));
    juce::ColourGradient grad (theme::accent2, fill.getX(), 0, theme::accent, fill.getRight(), 0, false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (fill, 6.0f);
    r.removeFromTop (10.0f);

    auto line = r.removeFromTop (20.0f);
    g.setColour (theme::text);
    g.setFont (uiFont (14.0f));
    g.drawText (stage, line, juce::Justification::centredLeft);

    const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    juce::String timing = juce::String ((int) std::round (progress * 100.0f)) + "%   " + formatDuration (now - startTime) + " elapsed";
    if (rateEstimate > 0.0f && progress > 0.03f && progress < 0.99f)
        timing << "   about " << formatDuration ((1.0f - progress) / rateEstimate) << " left";
    g.setColour (theme::textDim);
    g.drawText (timing, line, juce::Justification::centredRight);
    r.removeFromTop (14.0f);

    g.setColour (theme::textDim);
    g.setFont (uiFont (13.0f));
    const juce::String info = detail.isNotEmpty() ? detail
        : juce::String ("The AI model runs on your computer's processor, so this takes a few minutes per song. ")
          + "Finished songs are saved to your Library and open instantly next time. "
          + "Tip: tune up while you wait - the rig is live.";
    g.drawFittedText (info, r.removeFromTop (54.0f).toNearestInt(), juce::Justification::topLeft, 3);
}

void SeparationOverlay::resized()
{
    card = getLocalBounds().toFloat().withSizeKeepingCentre (juce::jmin (620.0f, (float) getWidth() - 40.0f), 280.0f);
    cancel.setBounds (card.reduced (32.0f, 24.0f).removeFromBottom (32.0f).removeFromRight (110.0f).toNearestInt());
}

} // namespace wis
