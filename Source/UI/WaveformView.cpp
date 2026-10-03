#include "WaveformView.h"
#include "LookAndFeel.h"
#include "Separation/AudioFileLoader.h"

namespace wis
{

static juce::String formatTime (double s, bool tenths = false)
{
    if (s < 0) s = 0;
    const int m = (int) (s / 60.0);
    const double sec = s - m * 60.0;
    return tenths ? juce::String (m) + ":" + juce::String (sec, 1).paddedLeft ('0', 4)
                  : juce::String (m) + ":" + juce::String ((int) sec).paddedLeft ('0', 2);
}

WaveformView::WaveformView (StemPlayer& p) : player (p)
{
    stemAudible.fill (true);
    setOpaque (true);
}

void WaveformView::setSong (PlayableSong::Ptr s)
{
    song = s;
    cacheDirty = true;
    repaint();
}

void WaveformView::setLanes (std::vector<Lane> l, int ruler, int mix)
{
    lanes = std::move (l);
    rulerH = ruler;
    mixH = mix;
    cacheDirty = true;
    repaint();
}

void WaveformView::resized() { cacheDirty = true; }

double WaveformView::xToSeconds (float x) const
{
    const double len = player.getLengthSeconds();
    return juce::jlimit (0.0, len, (double) x / juce::jmax (1, getWidth()) * len);
}

float WaveformView::secondsToX (double s) const
{
    const double len = player.getLengthSeconds();
    return len > 0 ? (float) (s / len * getWidth()) : 0.0f;
}

void WaveformView::rebuildCache()
{
    cacheDirty = false;
    cachedAudible = stemAudible;

    const auto scale = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr
                         ? (float) juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->scale : 1.0f;
    const int w = juce::jmax (1, getWidth()), h = juce::jmax (1, getHeight());
    cache = juce::Image (juce::Image::ARGB, (int) (w * scale), (int) (h * scale), true);
    juce::Graphics g (cache);
    g.addTransform (juce::AffineTransform::scale (scale));

    g.fillAll (theme::panel);

    if (song == nullptr || song->length == 0)
        return;

    auto drawOverview = [&] (const std::vector<float>& ov, juce::Rectangle<float> r, juce::Colour c)
    {
        if (ov.empty()) return;
        const int buckets = (int) ov.size();
        const float mid = r.getCentreY();
        const float halfH = r.getHeight() * 0.48f;

        juce::Path p;
        p.startNewSubPath (r.getX(), mid);
        const int px = juce::jmax (1, (int) r.getWidth());
        std::vector<float> col ((size_t) px);
        for (int x = 0; x < px; ++x)
        {
            const int b0 = (int) ((double) x / px * buckets);
            const int b1 = juce::jmax (b0 + 1, (int) ((double) (x + 1) / px * buckets));
            float pk = 0;
            for (int b = b0; b < juce::jmin (b1, buckets); ++b) pk = juce::jmax (pk, ov[(size_t) b]);
            col[(size_t) x] = std::pow (juce::jlimit (0.0f, 1.0f, pk), 0.7f);   // gentle "loudness" curve so quiet parts are visible
        }
        for (int x = 0; x < px; ++x)  p.lineTo (r.getX() + (float) x, mid - col[(size_t) x] * halfH);
        for (int x = px; --x >= 0;)   p.lineTo (r.getX() + (float) x, mid + col[(size_t) x] * halfH);
        p.closeSubPath();

        g.setColour (c.withMultipliedAlpha (0.85f));
        g.fillPath (p);
    };

    // mix lane
    auto mixR = juce::Rectangle<float> (0, (float) rulerH, (float) w, (float) mixH).reduced (0, 3);
    g.setColour (theme::bg);
    g.fillRect (mixR);
    drawOverview (song->mixOverview, mixR, theme::text.withAlpha (0.55f));

    // stem lanes
    for (auto& lane : lanes)
    {
        auto r = juce::Rectangle<float> (0, (float) lane.y, (float) w, (float) lane.height);
        g.setColour (theme::bg.brighter (0.02f));
        g.fillRect (r.reduced (0, 1));
        auto id = (StemId) lane.stem;
        if (song->present[(size_t) lane.stem])
        {
            const bool audible = stemAudible[(size_t) lane.stem];
            drawOverview (song->overview[(size_t) lane.stem], r.reduced (0, 3),
                          audible ? stemColour (id) : theme::textFaint.withAlpha (0.45f));
        }
        else
        {
            g.setColour (theme::textFaint);
            g.setFont (uiFont (12.0f));
            g.drawText ("not detected in this song", r.reduced (12, 0), juce::Justification::centredLeft);
        }
    }

    // ruler ticks
    const double len = (double) song->length / song->sampleRate;
    double step = 1.0;
    for (double s : { 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0 })
    {
        step = s;
        if (w / (len / s) > 70.0) break;
    }
    g.setFont (uiFont (11.0f));
    for (double t = 0; t <= len; t += step)
    {
        const float x = (float) (t / len * w);
        g.setColour (theme::outline);
        g.drawVerticalLine ((int) x, (float) rulerH - 6.0f, (float) rulerH);
        g.drawVerticalLine ((int) x, (float) rulerH, (float) h);
        g.setColour (theme::textDim);
        g.drawText (formatTime (t), (int) x + 3, 2, 60, rulerH - 6, juce::Justification::centredLeft);
    }
    g.setColour (theme::outline);
    g.drawHorizontalLine (rulerH - 1, 0.0f, (float) w);
}

void WaveformView::refresh()
{
    if (stemAudible != cachedAudible)
        cacheDirty = true;

    const double pos = player.getPositionSeconds();
    if (cacheDirty || std::abs (pos - lastPos) > 1.0e-4)
    {
        lastPos = pos;
        repaint();
    }
}

void WaveformView::paint (juce::Graphics& g)
{
    if (cacheDirty)
        rebuildCache();

    g.drawImage (cache, getLocalBounds().toFloat());

    const int w = getWidth(), h = getHeight();

    if (song == nullptr || song->length == 0)
    {
        auto r = getLocalBounds().reduced (24).toFloat();
        g.setColour (dragHover ? theme::accent.withAlpha (0.12f) : theme::panelRaised.withAlpha (0.5f));
        g.fillRoundedRectangle (r, 14.0f);
        juce::Path border;
        border.addRoundedRectangle (r, 14.0f);
        juce::Path dashed;
        const float dashes[] = { 8.0f, 6.0f };
        juce::PathStrokeType (1.5f).createDashedStroke (dashed, border, dashes, 2);
        g.setColour (dragHover ? theme::accent : theme::outline.brighter (0.3f));
        g.fillPath (dashed);

        g.setColour (theme::text);
        g.setFont (uiFont (24.0f, true));
        g.drawText ("Drop a song here", r.withTrimmedBottom (r.getHeight() * 0.45f), juce::Justification::centredBottom);
        g.setColour (theme::textDim);
        g.setFont (uiFont (15.0f));
        g.drawText ("MP3, FLAC, WAV, AIFF or OGG  -  or click to browse", r.withTrimmedTop (r.getHeight() * 0.58f).withHeight (24.0f), juce::Justification::centredTop);
        g.setFont (uiFont (13.0f));
        g.setColour (theme::textFaint);
        g.drawText ("It will be split into drums, bass, guitar, keys, lead vocals, backing vocals and everything else.",
                    r.withTrimmedTop (r.getHeight() * 0.58f + 28.0f).withHeight (22.0f), juce::Justification::centredTop);
        return;
    }

    // loop region
    if (player.getLoopEndSeconds() > player.getLoopStartSeconds())
    {
        const float x0 = secondsToX (player.getLoopStartSeconds());
        const float x1 = secondsToX (player.getLoopEndSeconds());
        const bool on = player.isLooping();
        g.setColour ((on ? theme::accent2 : theme::textFaint).withAlpha (on ? 0.16f : 0.08f));
        g.fillRect (x0, (float) rulerH, x1 - x0, (float) (h - rulerH));
        g.setColour (on ? theme::accent2 : theme::textFaint);
        g.fillRect (x0, 0.0f, x1 - x0, 5.0f);
        g.drawVerticalLine ((int) x0, 0.0f, (float) h);
        g.drawVerticalLine ((int) x1, 0.0f, (float) h);
    }

    // hover time
    if (hoverX >= 0.0f)
    {
        g.setColour (theme::text.withAlpha (0.25f));
        g.drawVerticalLine ((int) hoverX, (float) rulerH, (float) h);
        g.setColour (theme::panelRaised);
        auto tr = juce::Rectangle<float> (hoverX + 4.0f, (float) rulerH + 2.0f, 46.0f, 16.0f);
        if (tr.getRight() > w) tr.setX (hoverX - 50.0f);
        g.fillRoundedRectangle (tr, 3.0f);
        g.setColour (theme::text);
        g.setFont (uiFont (11.0f));
        g.drawText (formatTime (xToSeconds (hoverX), true), tr, juce::Justification::centred);
    }

    // playhead
    const float px = secondsToX (player.getPositionSeconds());
    g.setColour (theme::accent);
    g.fillRect (px - 1.0f, 0.0f, 2.0f, (float) h);
    juce::Path tri;
    tri.addTriangle (px - 6.0f, 0.0f, px + 6.0f, 0.0f, px, 8.0f);
    g.fillPath (tri);
}

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    if (song == nullptr)
    {
        if (onOpenClicked) onOpenClicked();
        return;
    }

    dragging = true;
    draggingLoop = false;
    dragStartX = (float) e.x;

    if (e.mods.isShiftDown() && player.getLoopEndSeconds() > 0)
    {
        // shift-click: move the nearest loop edge here
        const double t = xToSeconds ((float) e.x);
        double a = player.getLoopStartSeconds(), b = player.getLoopEndSeconds();
        if (std::abs (t - a) < std::abs (t - b)) a = t; else b = t;
        if (onLoopChanged) onLoopChanged (true, juce::jmin (a, b), juce::jmax (a, b));
        dragging = false;
    }
}

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging || song == nullptr) return;

    if (std::abs ((float) e.x - dragStartX) > 5.0f)
        draggingLoop = true;

    if (draggingLoop && onLoopChanged)
    {
        const double a = xToSeconds (dragStartX), b = xToSeconds ((float) e.x);
        onLoopChanged (true, juce::jmin (a, b), juce::jmax (a, b));
    }
    hoverX = (float) e.x;
    repaint();
}

void WaveformView::mouseUp (const juce::MouseEvent& e)
{
    if (dragging && ! draggingLoop && song != nullptr && onSeek)
        onSeek (xToSeconds ((float) e.x));
    if (draggingLoop && onSeek)
        onSeek (juce::jmin (xToSeconds (dragStartX), xToSeconds ((float) e.x)));   // start playing from the loop start
    dragging = draggingLoop = false;
}

void WaveformView::mouseDoubleClick (const juce::MouseEvent&)
{
    if (song != nullptr && onLoopChanged)
        onLoopChanged (false, 0.0, 0.0);
}

void WaveformView::mouseMove (const juce::MouseEvent& e)
{
    hoverX = song != nullptr ? (float) e.x : -1.0f;
    setMouseCursor (song != nullptr ? juce::MouseCursor::IBeamCursor : juce::MouseCursor::PointingHandCursor);
    repaint();
}

void WaveformView::mouseExit (const juce::MouseEvent&)
{
    hoverX = -1.0f;
    repaint();
}

bool WaveformView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension (supportedAudioWildcard().replace ("*", "").replace (";", ";")))
            return true;
    return false;
}

void WaveformView::filesDropped (const juce::StringArray& files, int, int)
{
    dragHover = false;
    repaint();
    if (! files.isEmpty() && onFileDropped)
        onFileDropped (juce::File (files[0]));
}

} // namespace wis
