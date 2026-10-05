#include "LookAndFeel.h"

namespace wis
{

juce::Font uiFont (float height, bool bold)
{
    auto opts = juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain);
   #if JUCE_WINDOWS
    opts = opts.withName ("Segoe UI");
   #endif
    return juce::Font (opts);
}

WisLookAndFeel::WisLookAndFeel()
{
    using namespace theme;
    setColour (juce::ResizableWindow::backgroundColourId, bg);
    setColour (juce::DocumentWindow::backgroundColourId, bg);
    setColour (juce::Label::textColourId, text);
    setColour (juce::TextButton::buttonColourId, panelRaised);
    setColour (juce::TextButton::buttonOnColourId, accent);
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setColour (juce::ComboBox::backgroundColourId, panelRaised);
    setColour (juce::ComboBox::outlineColourId, outline);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::arrowColourId, textDim);
    setColour (juce::PopupMenu::backgroundColourId, panelRaised);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.85f));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::PopupMenu::headerTextColourId, textDim);
    setColour (juce::Slider::textBoxTextColourId, textDim);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::rotarySliderFillColourId, accent);
    setColour (juce::Slider::trackColourId, accent);
    setColour (juce::TextEditor::backgroundColourId, panelRaised);
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::outlineColourId, outline);
    setColour (juce::TextEditor::focusedOutlineColourId, accent);
    setColour (juce::ListBox::backgroundColourId, panel);
    setColour (juce::ListBox::outlineColourId, outline);
    setColour (juce::TableHeaderComponent::backgroundColourId, panelRaised);
    setColour (juce::TableHeaderComponent::textColourId, text);
    setColour (juce::TableHeaderComponent::outlineColourId, outline);
    setColour (juce::TableHeaderComponent::highlightColourId, accent.withAlpha (0.25f));
    setColour (juce::ScrollBar::thumbColourId, outline);
    setColour (juce::ProgressBar::backgroundColourId, panelRaised);
    setColour (juce::ProgressBar::foregroundColourId, accent);
    setColour (juce::AlertWindow::backgroundColourId, panel);
    setColour (juce::AlertWindow::textColourId, text);
    setColour (juce::AlertWindow::outlineColourId, outline);
    setColour (juce::TooltipWindow::backgroundColourId, panelRaised);
    setColour (juce::TooltipWindow::textColourId, text);
    setColour (juce::TooltipWindow::outlineColourId, outline);
    setColour (juce::ToggleButton::textColourId, text);
    setColour (juce::ToggleButton::tickColourId, accent);
    setColour (juce::ToggleButton::tickDisabledColourId, textFaint);
    setColour (juce::TreeView::backgroundColourId, panel);

    getCurrentColourScheme().setUIColour (ColourScheme::UIColour::windowBackground, bg);
    getCurrentColourScheme().setUIColour (ColourScheme::UIColour::widgetBackground, panelRaised);
    getCurrentColourScheme().setUIColour (ColourScheme::UIColour::menuBackground, panelRaised);
    getCurrentColourScheme().setUIColour (ColourScheme::UIColour::outline, outline);
    getCurrentColourScheme().setUIColour (ColourScheme::UIColour::defaultText, text);
    getCurrentColourScheme().setUIColour (ColourScheme::UIColour::defaultFill, accent);
    getCurrentColourScheme().setUIColour (ColourScheme::UIColour::highlightedText, juce::Colours::white);
    getCurrentColourScheme().setUIColour (ColourScheme::UIColour::highlightedFill, accent);
    getCurrentColourScheme().setUIColour (ColourScheme::UIColour::menuText, text);
}

void WisLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                                       float startAngle, float endAngle, juce::Slider& slider)
{
    auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (juce::jmin (width, height) < 30 ? 1.0f : 3.0f);
    const float size = juce::jmin (bounds.getWidth(), bounds.getHeight());
    auto r = bounds.withSizeKeepingCentre (size, size);
    const float radius = size * 0.5f;
    const auto centre = r.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);
    const float track = radius < 14.0f ? 2.0f : juce::jmax (2.5f, radius * 0.14f);
    auto accentCol = slider.findColour (juce::Slider::rotarySliderFillColourId);
    if (! slider.isEnabled()) accentCol = theme::textFaint;

    // track
    juce::Path bg;
    bg.addCentredArc (centre.x, centre.y, radius - track, radius - track, 0.0f, startAngle, endAngle, true);
    g.setColour (theme::outline);
    g.strokePath (bg, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // value arc (from the centre for bipolar knobs)
    const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    const float zeroAngle = bipolar ? startAngle + (float) slider.valueToProportionOfLength (0.0) * (endAngle - startAngle) : startAngle;
    juce::Path val;
    val.addCentredArc (centre.x, centre.y, radius - track, radius - track, 0.0f, juce::jmin (zeroAngle, angle), juce::jmax (zeroAngle, angle), true);
    g.setColour (accentCol);
    g.strokePath (val, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // knob body
    const float bodyR = radius - track * 2.2f;
    juce::ColourGradient grad (theme::panelRaised.brighter (0.25f), centre.x, centre.y - bodyR,
                               theme::panel.darker (0.3f), centre.x, centre.y + bodyR, false);
    g.setGradientFill (grad);
    g.fillEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2, bodyR * 2);
    g.setColour (theme::outline.brighter (0.2f));
    g.drawEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2, bodyR * 2, 1.0f);

    // pointer
    juce::Path pointer;
    const float pw = juce::jmax (2.0f, bodyR * 0.16f);
    pointer.addRoundedRectangle (-pw * 0.5f, -bodyR * 0.92f, pw, bodyR * 0.5f, pw * 0.5f);
    g.setColour (slider.isEnabled() ? theme::text : theme::textFaint);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));
}

void WisLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                       float, float, juce::Slider::SliderStyle style, juce::Slider& slider)
{
    const bool horizontal = style == juce::Slider::LinearHorizontal || style == juce::Slider::LinearBar;
    auto accentCol = slider.findColour (juce::Slider::trackColourId);

    if (horizontal)
    {
        const float cy = (float) y + (float) height * 0.5f;
        const float th = 4.0f;
        g.setColour (theme::outline);
        g.fillRoundedRectangle ((float) x, cy - th * 0.5f, (float) width, th, th * 0.5f);

        const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0 && slider.getProperties().contains ("bipolar");
        const float zeroX = bipolar ? (float) x + (float) slider.valueToProportionOfLength (0.0) * (float) width : (float) x;
        g.setColour (accentCol.withAlpha (slider.isEnabled() ? 0.9f : 0.3f));
        g.fillRoundedRectangle (juce::jmin (zeroX, sliderPos), cy - th * 0.5f, std::abs (sliderPos - zeroX), th, th * 0.5f);

        const float tw = 10.0f, thh = juce::jmin (18.0f, (float) height - 2.0f);
        g.setColour (theme::text);
        g.fillRoundedRectangle (sliderPos - tw * 0.5f, cy - thh * 0.5f, tw, thh, 3.0f);
        g.setColour (theme::bg.withAlpha (0.6f));
        g.fillRect (sliderPos - 0.5f, cy - thh * 0.3f, 1.0f, thh * 0.6f);
    }
    else
    {
        const float cx = (float) x + (float) width * 0.5f;
        const float tw = 4.0f;
        g.setColour (theme::outline);
        g.fillRoundedRectangle (cx - tw * 0.5f, (float) y, tw, (float) height, tw * 0.5f);
        g.setColour (accentCol);
        g.fillRoundedRectangle (cx - tw * 0.5f, sliderPos, tw, (float) (y + height) - sliderPos, tw * 0.5f);
        g.setColour (theme::text);
        g.fillRoundedRectangle (cx - 9.0f, sliderPos - 5.0f, 18.0f, 10.0f, 3.0f);
    }
}

void WisLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bgColour,
                                           bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto c = b.getToggleState() ? b.findColour (juce::TextButton::buttonOnColourId) : bgColour;
    if (! b.isEnabled()) c = c.withMultipliedAlpha (0.4f);
    if (down) c = c.darker (0.2f);
    else if (over) c = c.brighter (0.12f);

    g.setColour (c);
    g.fillRoundedRectangle (r, 6.0f);
    if (! b.getToggleState())
    {
        g.setColour (theme::outline.brighter (over ? 0.3f : 0.0f));
        g.drawRoundedRectangle (r, 6.0f, 1.0f);
    }
}

juce::Font WisLookAndFeel::getTextButtonFont (juce::TextButton&, int h)
{
    return uiFont (juce::jmin (15.0f, (float) h * 0.48f), true);
}

void WisLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (getTextButtonFont (b, b.getHeight()));
    auto col = b.findColour (b.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId);
    g.setColour (col.withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.45f));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (6, 2), juce::Justification::centred, 1);
}

void WisLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool)
{
    auto r = b.getLocalBounds().toFloat();
    const float box = juce::jmin (16.0f, r.getHeight() - 4.0f);
    auto boxR = juce::Rectangle<float> (r.getX() + 2.0f, r.getCentreY() - box * 0.5f, box, box);

    g.setColour (b.getToggleState() ? theme::accent : theme::panelRaised);
    g.fillRoundedRectangle (boxR, 4.0f);
    g.setColour (b.getToggleState() ? theme::accent : theme::outline.brighter (over ? 0.3f : 0.0f));
    g.drawRoundedRectangle (boxR, 4.0f, 1.0f);
    if (b.getToggleState())
    {
        juce::Path tick;
        tick.startNewSubPath (boxR.getX() + box * 0.25f, boxR.getCentreY());
        tick.lineTo (boxR.getX() + box * 0.43f, boxR.getBottom() - box * 0.28f);
        tick.lineTo (boxR.getRight() - box * 0.22f, boxR.getY() + box * 0.27f);
        g.setColour (juce::Colours::white);
        g.strokePath (tick, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    g.setColour (b.findColour (juce::ToggleButton::textColourId).withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.5f));
    g.setFont (uiFont (13.5f));
    g.drawFittedText (b.getButtonText(), r.withTrimmedLeft (box + 8.0f).toNearestInt(), juce::Justification::centredLeft, 1);
}

void WisLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<int> (0, 0, width, height).toFloat().reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId).brighter (box.isMouseOver (true) ? 0.08f : 0.0f));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (box.hasKeyboardFocus (false) ? theme::accent : theme::outline);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);

    juce::Path arrow;
    const float ax = (float) width - 14.0f, ay = (float) height * 0.5f;
    arrow.startNewSubPath (ax - 4.0f, ay - 2.0f);
    arrow.lineTo (ax, ay + 2.5f);
    arrow.lineTo (ax + 4.0f, ay - 2.0f);
    g.setColour (theme::textDim);
    g.strokePath (arrow, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Font WisLookAndFeel::getComboBoxFont (juce::ComboBox& b) { return uiFont (juce::jmin (14.0f, (float) b.getHeight() * 0.52f)); }
juce::Font WisLookAndFeel::getPopupMenuFont() { return uiFont (14.5f); }

void WisLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (8, 1, box.getWidth() - 28, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

juce::Label* WisLookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (uiFont (12.0f));
    l->setColour (juce::Label::textColourId, theme::textDim);
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::textWhenEditingColourId, theme::text);
    l->setColour (juce::Label::outlineWhenEditingColourId, theme::accent);
    return l;
}

juce::Font WisLookAndFeel::getLabelFont (juce::Label& l)
{
    return l.getFont();
}

void WisLookAndFeel::drawProgressBar (juce::Graphics& g, juce::ProgressBar&, int width, int height, double progress, const juce::String& textToShow)
{
    auto r = juce::Rectangle<float> (0, 0, (float) width, (float) height);
    g.setColour (theme::panelRaised);
    g.fillRoundedRectangle (r, height * 0.5f);
    if (progress >= 0.0)
    {
        auto fill = r.withWidth (juce::jmax ((float) height, (float) (width * juce::jlimit (0.0, 1.0, progress))));
        juce::ColourGradient grad (theme::accent2, fill.getX(), 0, theme::accent, fill.getRight(), 0, false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (fill, height * 0.5f);
    }
    if (textToShow.isNotEmpty())
    {
        g.setColour (theme::text);
        g.setFont (uiFont ((float) height * 0.6f, true));
        g.drawText (textToShow, r, juce::Justification::centred);
    }
}

void WisLookAndFeel::drawAlertBox (juce::Graphics& g, juce::AlertWindow& alert, const juce::Rectangle<int>& textArea, juce::TextLayout& layout)
{
    auto bounds = alert.getLocalBounds().toFloat();
    g.setColour (theme::panel);
    g.fillRoundedRectangle (bounds, 10.0f);
    g.setColour (theme::outline);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 10.0f, 1.0f);

    juce::Colour accentCol = theme::accent;
    switch (alert.getAlertType())
    {
        case juce::MessageBoxIconType::WarningIcon:  accentCol = theme::warn;    break;
        case juce::MessageBoxIconType::QuestionIcon: accentCol = theme::accent2; break;
        case juce::MessageBoxIconType::InfoIcon:
        case juce::MessageBoxIconType::NoIcon:
        default: break;
    }

    g.setColour (accentCol);
    g.fillRoundedRectangle (bounds.withHeight (4.0f).reduced (14.0f, 0.0f), 2.0f);

    g.setColour (theme::text);
    layout.draw (g, textArea.toFloat());
}

// ---- PowerButton ------------------------------------------------------------------------------------

void PowerButton::paintButton (juce::Graphics& g, bool over, bool)
{
    auto r = getLocalBounds().toFloat();
    const float d = juce::jmin (r.getWidth(), r.getHeight()) - 2.0f;
    auto c = r.withSizeKeepingCentre (d, d);
    const bool on = getToggleState();

    if (on)
    {
        g.setColour (onColour.withAlpha (0.25f));
        g.fillEllipse (c.expanded (2.0f));
    }
    g.setColour (on ? onColour : theme::ledOff.brighter (over ? 0.3f : 0.0f));
    g.fillEllipse (c);
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.drawEllipse (c, 1.0f);

    // power glyph
    juce::Path p;
    const float gr = d * 0.26f;
    p.addCentredArc (c.getCentreX(), c.getCentreY(), gr, gr, 0.0f, 0.75f, juce::MathConstants<float>::twoPi - 0.75f, true);
    p.startNewSubPath (c.getCentreX(), c.getCentreY() - gr * 1.25f);
    p.lineTo (c.getCentreX(), c.getCentreY() - gr * 0.2f);
    g.setColour (on ? juce::Colours::white : theme::textDim);
    g.strokePath (p, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// ---- LevelMeter ---------------------------------------------------------------------------------------

void LevelMeter::setLevel (float peak)
{
    const float db = juce::Decibels::gainToDecibels (peak, -60.0f);
    const float norm = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
    level = norm > level ? norm : level * 0.86f + norm * 0.14f;
    if (norm >= hold) { hold = norm; holdFrames = 30; }
    else if (--holdFrames <= 0) hold = juce::jmax (0.0f, hold - 0.02f);
    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (theme::bg);
    g.fillRoundedRectangle (r, 2.0f);

    auto colourFor = [this] (float v)
    {
        if (v > 0.95f) return theme::bad;
        if (v > 0.8f)  return theme::warn;
        return colour;
    };

    if (isVertical)
    {
        auto f = r.withTop (r.getBottom() - r.getHeight() * level);
        g.setColour (colourFor (level));
        g.fillRoundedRectangle (f, 2.0f);
        const float hy = r.getBottom() - r.getHeight() * hold;
        g.setColour (colourFor (hold).withAlpha (0.9f));
        g.fillRect (r.getX(), hy, r.getWidth(), 1.5f);
    }
    else
    {
        auto f = r.withWidth (r.getWidth() * level);
        g.setColour (colourFor (level));
        g.fillRoundedRectangle (f, 2.0f);
        const float hx = r.getX() + r.getWidth() * hold;
        g.setColour (colourFor (hold).withAlpha (0.9f));
        g.fillRect (hx - 1.5f, r.getY(), 1.5f, r.getHeight());
    }
}

} // namespace wis
