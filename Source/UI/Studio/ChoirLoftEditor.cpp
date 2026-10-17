#include "EditorParts.h"
#include "Daw/Instruments/ChoirLoft.h"
#include "Daw/Instruments/RoomIr.h"

namespace wis::daw
{

/*  Choir Loft's window: an evening chapel. A portrait of the selected singer (drawn from their anatomy, mouth
    moving with the vowel), the stage with every singer on it and the microphones in front, the vocal tract,
    the vowel chart, and the cast / room / expression controls. Everything is laid out in a fixed 1160 x 690
    design space scaled to the window. */

namespace
{
    namespace cc
    {
        const juce::Colour night     { 0xff16131d };
        const juce::Colour panel     { 0xff211c2a };
        const juce::Colour panelHi   { 0xff2a2435 };
        const juce::Colour edge      { 0xff3a3249 };
        const juce::Colour parchment { 0xffeee4d1 };
        const juce::Colour dim       { 0xffa79db4 };
        const juce::Colour faint     { 0xff6d6479 };
        const juce::Colour candle    { 0xfff2b65e };
        const juce::Colour candleDim { 0xff7a5a2e };
        const juce::Colour rose      { 0xffe58a86 };
        const juce::Colour sage      { 0xff8cc7a1 };
        const juce::Colour sky       { 0xff86b3e3 };
    }

    constexpr float designW = 1160.0f, designH = 690.0f;
    constexpr int headerH = 36;
    juce::Rectangle<float> rf (float x, float y, float w, float h) { return { x, y, w, h }; }
    juce::Font smallCaps (float h, bool bold = true) { return uiFont (h, bold).withExtraKerningFactor (0.1f); }

    const juce::Colour skinTones[] = { juce::Colour (0xfff3d6bd), juce::Colour (0xffe6c09d), juce::Colour (0xffcf9e76),
                                       juce::Colour (0xffa9714e), juce::Colour (0xff7d4e33), juce::Colour (0xff533222) };
    const juce::Colour hairColours[] = { juce::Colour (0xff1c1713), juce::Colour (0xff3c2718), juce::Colour (0xff6c4527),
                                         juce::Colour (0xff8d3d1e), juce::Colour (0xffd5b46f), juce::Colour (0xffc8c5bf) };
    const juce::Colour robeColours[] = { juce::Colour (0xff6e2a3a), juce::Colour (0xff2b3b66), juce::Colour (0xff2f5b45), juce::Colour (0xff5b3566) };

    juce::Colour kindColour (const juce::String& kind)
    {
        if (kind == "Soprano" || kind == "Treble") return cc::rose;
        if (kind == "Mezzo" || kind == "Alto") return cc::candle;
        if (kind == "Tenor" || kind == "Countertenor") return cc::sage;
        return cc::sky;
    }

    // ---- look and feel: thin bars with the label above, gold fill ---------------------------------------
    class LoftLnF : public juce::LookAndFeel_V4
    {
    public:
        LoftLnF()
        {
            setColour (juce::ComboBox::backgroundColourId, cc::panelHi);
            setColour (juce::ComboBox::textColourId, cc::parchment);
            setColour (juce::ComboBox::outlineColourId, cc::edge);
            setColour (juce::ComboBox::arrowColourId, cc::candle);
            setColour (juce::PopupMenu::backgroundColourId, cc::panel);
            setColour (juce::PopupMenu::textColourId, cc::parchment);
            setColour (juce::PopupMenu::highlightedBackgroundColourId, cc::candleDim);
            setColour (juce::PopupMenu::highlightedTextColourId, cc::parchment);
            setColour (juce::PopupMenu::headerTextColourId, cc::candle);
            setColour (juce::BubbleComponent::backgroundColourId, cc::panel);
            setColour (juce::TooltipWindow::backgroundColourId, cc::panel);
            setColour (juce::TooltipWindow::textColourId, cc::parchment);
            setColour (juce::TooltipWindow::outlineColourId, cc::edge);
        }

        void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle, juce::Slider& s) override
        {
            const auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
            const float fh = juce::jlimit (8.0f, 16.0f, (float) h * 0.46f);
            const bool hot = s.isMouseOverOrDragging();
            g.setFont (uiFont (fh));
            g.setColour (hot ? cc::parchment : cc::dim);
            const auto top = r.withHeight (fh + 2.0f);
            g.drawText (s.getProperties()["label"].toString(), top, juce::Justification::centredLeft, true);
            g.setColour (hot ? cc::candle : cc::parchment);
            g.drawText (s.getTextFromValue (s.getValue()), top, juce::Justification::centredRight, false);
            const float ty = r.getBottom() - (float) h * 0.24f;
            const float th = juce::jmax (2.0f, (float) h * 0.09f);
            g.setColour (cc::edge);
            g.fillRoundedRectangle (r.getX(), ty - th * 0.5f, r.getWidth(), th, th * 0.5f);
            const bool bipolar = (bool) s.getProperties()["bipolar"];
            const float zero = bipolar ? r.getCentreX() : r.getX();
            g.setColour (s.isEnabled() ? cc::candle : cc::faint);
            g.fillRoundedRectangle (juce::jmin (zero, pos), ty - th * 0.5f, std::abs (pos - zero), th, th * 0.5f);
            const float kr = th * (hot ? 2.6f : 2.2f);
            g.setColour (cc::parchment);
            g.fillEllipse (pos - kr, ty - kr, kr * 2.0f, kr * 2.0f);
            g.setColour (cc::night.withAlpha (0.6f));
            g.drawEllipse (pos - kr, ty - kr, kr * 2.0f, kr * 2.0f, 1.0f);
        }

        juce::Slider::SliderLayout getSliderLayout (juce::Slider& s) override
        {
            juce::Slider::SliderLayout l;
            l.sliderBounds = s.getLocalBounds().reduced (6, 0);
            return l;
        }

        void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
        {
            const auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
            g.setColour (cc::panelHi);
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (box.isMouseOver() ? cc::candleDim : cc::edge);
            g.drawRoundedRectangle (r, 4.0f, 1.0f);
            juce::Path a;
            const float ax = (float) w - (float) h * 0.55f, ay = (float) h * 0.5f, as = (float) h * 0.14f;
            a.addTriangle (ax - as, ay - as * 0.5f, ax + as, ay - as * 0.5f, ax, ay + as * 0.7f);
            g.setColour (cc::candle);
            g.fillPath (a);
        }
        juce::Font getComboBoxFont (juce::ComboBox& b) override { return uiFont (juce::jlimit (9.0f, 16.0f, (float) b.getHeight() * 0.5f)); }
        void positionComboBoxText (juce::ComboBox& b, juce::Label& l) override
        {
            l.setBounds (6, 1, b.getWidth() - b.getHeight() - 4, b.getHeight() - 2);
            l.setFont (getComboBoxFont (b));
        }
        juce::Font getPopupMenuFont() override { return uiFont (14.0f); }

        void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
        {
            const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
            const bool on = b.getToggleState();
            g.setColour (on ? cc::candle : (down ? cc::edge : cc::panelHi));
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (on ? cc::candle.brighter (0.2f) : (over ? cc::candleDim : cc::edge));
            g.drawRoundedRectangle (r, 4.0f, 1.0f);
        }
        void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
        {
            g.setFont (smallCaps (juce::jlimit (8.0f, 15.0f, (float) b.getHeight() * 0.42f)));
            g.setColour (b.getToggleState() ? cc::night : cc::parchment);
            g.drawText (b.getButtonText(), b.getLocalBounds().reduced (2, 0), juce::Justification::centred, true);
        }

        void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool) override
        {
            const float h = (float) b.getHeight();
            const auto box = juce::Rectangle<float> (2.0f, h * 0.22f, h * 0.56f, h * 0.56f);
            g.setColour (b.getToggleState() ? cc::candle : cc::panelHi);
            g.fillRoundedRectangle (box, 3.0f);
            g.setColour (over ? cc::candle : cc::edge);
            g.drawRoundedRectangle (box, 3.0f, 1.0f);
            if (b.getToggleState())
            {
                juce::Path tick;
                tick.startNewSubPath (box.getX() + box.getWidth() * 0.22f, box.getCentreY());
                tick.lineTo (box.getX() + box.getWidth() * 0.43f, box.getBottom() - box.getHeight() * 0.25f);
                tick.lineTo (box.getRight() - box.getWidth() * 0.2f, box.getY() + box.getHeight() * 0.25f);
                g.setColour (cc::night);
                g.strokePath (tick, juce::PathStrokeType (1.8f));
            }
            g.setColour (cc::parchment);
            g.setFont (uiFont (juce::jlimit (9.0f, 15.0f, h * 0.5f)));
            g.drawText (b.getButtonText(), juce::Rectangle<float> (box.getRight() + 6.0f, 0.0f, (float) b.getWidth() - box.getRight() - 6.0f, h),
                        juce::Justification::centredLeft, true);
        }
    };

    // ---- drawing people ---------------------------------------------------------------------------------------
    struct Look
    {
        juce::Colour skin, hair, robe;
        bool child, male;
    };

    Look lookOf (const ChoirLoft::Singer& s)
    {
        Look l;
        l.skin = skinTones[juce::jlimit (0, 5, s.skin)];
        l.hair = hairColours[juce::jlimit (0, 5, s.hairColour)];
        if (s.hairColour == 5 && s.age > 72.0f) l.hair = juce::Colour (0xffe9e7e2);
        l.robe = robeColours[juce::jlimit (0, 3, s.robe)];
        l.child = s.age < 13.0f;
        l.male = s.sex >= 0.5f;
        return l;
    }

    /** A head-and-shoulders portrait. `open` 0..1 opens the mouth, `round` 0..1 rounds the lips, `hum` closes them. */
    void drawPortrait (juce::Graphics& g, juce::Rectangle<float> area, const ChoirLoft::Singer& s, float open, float round, float hum, double t, bool blink)
    {
        const auto lk = lookOf (s);
        const float k = area.getHeight() / 190.0f;
        const float cx = area.getCentreX();
        const float breath = (float) std::sin (t * 1.6) * 1.2f * k;
        const float sway = (float) std::sin (t * 0.7 + s.name) * 1.5f * k;
        const float youth = lk.child ? 0.86f : 1.0f;
        const float headW = (58.0f + 16.0f * (s.head - 0.5f) + 5.0f * s.sex) * k * youth;
        const float faceL = (84.0f + 22.0f * (s.faceLength - 0.5f) + 6.0f * s.sex) * k * (lk.child ? 0.84f : 1.0f);
        const float jawW = headW * (0.66f + 0.22f * (s.jaw - 0.5f) + 0.08f * s.sex) * (lk.child ? 0.92f : 1.0f);
        const float chinW = jawW * (0.38f + 0.3f * (s.chin - 0.5f));
        const float cheekW = headW * (1.0f + 0.09f * (s.cheeks - 0.5f));
        const float neckLen = (22.0f + 20.0f * (s.neck - 0.5f)) * k;
        const float neckW = headW * (0.48f + 0.16f * s.sex) * (0.9f + 0.2f * s.chest);
        const float shoulderY = area.getBottom() - 38.0f * k + breath;
        const float chinY = shoulderY - neckLen;
        const float top = chinY - faceL;
        const float hx = cx + sway;

        // shoulders and robe
        {
            const float sw = (92.0f + 30.0f * s.chest + 14.0f * s.sex) * k * youth;
            juce::Path robe;
            robe.startNewSubPath (cx - sw, area.getBottom());
            robe.quadraticTo (cx - sw, shoulderY + 6.0f * k, cx - neckW * 1.2f, shoulderY);
            robe.lineTo (cx + neckW * 1.2f, shoulderY);
            robe.quadraticTo (cx + sw, shoulderY + 6.0f * k, cx + sw, area.getBottom());
            robe.closeSubPath();
            g.setGradientFill (juce::ColourGradient (lk.robe.brighter (0.15f), cx, shoulderY, lk.robe.darker (0.5f), cx, area.getBottom(), false));
            g.fillPath (robe);
            // a cream collar
            juce::Path collar;
            collar.startNewSubPath (cx - neckW * 1.25f, shoulderY + 1.0f);
            collar.quadraticTo (cx, shoulderY + 26.0f * k, cx + neckW * 1.25f, shoulderY + 1.0f);
            collar.quadraticTo (cx, shoulderY + 14.0f * k, cx - neckW * 1.25f, shoulderY + 1.0f);
            g.setColour (cc::parchment.withAlpha (0.9f));
            g.fillPath (collar);
        }
        // neck
        g.setColour (lk.skin.darker (0.18f));
        g.fillRoundedRectangle (cx - neckW * 0.5f, chinY - 10.0f * k, neckW, neckLen + 14.0f * k, neckW * 0.3f);

        // long hair falls behind the head
        if (s.hair == 1 || s.hair == 4)
        {
            g.setColour (lk.hair.darker (0.2f));
            // long: down over the shoulders, narrowing; bob: to the jaw, rounded under
            juce::Path back;
            const float y0 = top + faceL * 0.1f, y1 = top + (s.hair == 1 ? faceL * 1.55f : faceL * 0.86f);
            const float wTop = headW * 1.1f, wBot = s.hair == 1 ? headW * 1.25f : headW * 1.02f;
            back.startNewSubPath (hx - wTop, y0 + faceL * 0.2f);
            back.quadraticTo (hx - wTop, y0 - faceL * 0.05f, hx, y0 - faceL * 0.08f);
            back.quadraticTo (hx + wTop, y0 - faceL * 0.05f, hx + wTop, y0 + faceL * 0.2f);
            back.quadraticTo (hx + wBot * 1.05f, (y0 + y1) * 0.5f, hx + wBot, y1);
            back.quadraticTo (hx, y1 + faceL * (s.hair == 1 ? 0.04f : 0.1f), hx - wBot, y1);
            back.quadraticTo (hx - wBot * 1.05f, (y0 + y1) * 0.5f, hx - wTop, y0 + faceL * 0.2f);
            back.closeSubPath();
            g.fillPath (back);
        }

        // ears, just showing beside the face
        for (int side : { -1, 1 })
        {
            const float ex = hx + side * cheekW * 0.97f, ey = top + faceL * 0.44f, ew = headW * 0.13f, eh = faceL * 0.2f;
            juce::Path ear;
            ear.startNewSubPath (ex, ey);
            ear.cubicTo (ex + side * ew * 1.3f, ey - eh * 0.15f, ex + side * ew * 1.2f, ey + eh * 0.9f, ex, ey + eh);
            ear.closeSubPath();
            g.setColour (lk.skin.darker (0.05f));
            g.fillPath (ear);
            g.setColour (lk.skin.darker (0.4f).withAlpha (0.6f));
            g.drawLine (ex + side * ew * 0.35f, ey + eh * 0.25f, ex + side * ew * 0.45f, ey + eh * 0.65f, 0.9f * k);
        }

        // the face
        juce::Path face;
        face.startNewSubPath (hx, top);
        face.cubicTo (hx + headW * 0.75f, top, hx + headW, top + faceL * 0.16f, hx + headW, top + faceL * 0.36f);
        face.quadraticTo (hx + cheekW, top + faceL * 0.56f, hx + jawW, top + faceL * 0.8f);
        face.quadraticTo (hx + chinW * 0.9f, top + faceL * 0.97f, hx, top + faceL);
        face.quadraticTo (hx - chinW * 0.9f, top + faceL * 0.97f, hx - jawW, top + faceL * 0.8f);
        face.quadraticTo (hx - cheekW, top + faceL * 0.56f, hx - headW, top + faceL * 0.36f);
        face.cubicTo (hx - headW, top + faceL * 0.16f, hx - headW * 0.75f, top, hx, top);
        face.closeSubPath();
        g.setGradientFill (juce::ColourGradient (lk.skin.brighter (0.08f), hx - headW * 0.3f, top + faceL * 0.3f, lk.skin.darker (0.12f), hx + headW, top + faceL, true));
        g.fillPath (face);

        // ageing
        if (s.age > 50.0f)
        {
            const float a = juce::jlimit (0.0f, 0.35f, (s.age - 50.0f) / 80.0f);
            g.setColour (lk.skin.darker (0.5f).withAlpha (a));
            for (int i = 0; i < 2; ++i)
                g.drawLine (hx - headW * 0.35f, top + faceL * (0.15f + 0.05f * i), hx + headW * 0.35f, top + faceL * (0.15f + 0.05f * i), 0.8f * k);
            juce::Path lines;
            for (int side : { -1, 1 })
            {
                lines.startNewSubPath (hx + side * headW * 0.2f, top + faceL * 0.64f);
                lines.quadraticTo (hx + side * headW * 0.36f, top + faceL * 0.74f, hx + side * headW * 0.32f, top + faceL * 0.86f);
            }
            g.strokePath (lines, juce::PathStrokeType (0.9f * k));
        }

        // beard
        if (s.beard)
        {
            juce::Path beard;
            beard.startNewSubPath (hx - jawW * 1.02f, top + faceL * 0.62f);
            beard.quadraticTo (hx - jawW, top + faceL * 0.9f, hx, top + faceL * 1.06f);
            beard.quadraticTo (hx + jawW, top + faceL * 0.9f, hx + jawW * 1.02f, top + faceL * 0.62f);
            beard.quadraticTo (hx + headW * 0.3f, top + faceL * 0.86f, hx, top + faceL * 0.88f);
            beard.quadraticTo (hx - headW * 0.3f, top + faceL * 0.86f, hx - jawW * 1.02f, top + faceL * 0.62f);
            g.setColour (lk.hair.withAlpha (0.85f));
            g.fillPath (beard);
        }

        // hair on top
        {
            g.setColour (lk.hair);
            juce::Path cap;
            const float hairTop = top - (s.hair == 3 ? 10.0f : 4.0f) * k;
            if (s.hair == 5)
            {
                // thinning on top, hair round the sides above the ears
                for (int side : { -1, 1 })
                {
                    const float sd = (float) side;
                    juce::Path p;
                    p.startNewSubPath (hx + sd * headW * 0.99f, top + faceL * 0.2f);
                    p.quadraticTo (hx + sd * headW * 1.08f, top + faceL * 0.32f, hx + sd * headW * 1.0f, top + faceL * 0.45f);
                    p.lineTo (hx + sd * headW * 0.93f, top + faceL * 0.42f);
                    p.quadraticTo (hx + sd * headW * 0.97f, top + faceL * 0.31f, hx + sd * headW * 0.9f, top + faceL * 0.22f);
                    p.closeSubPath();
                    g.fillPath (p);
                }
                juce::Path thin;
                thin.addEllipse (hx - headW * 0.85f, top - faceL * 0.02f, headW * 1.7f, faceL * 0.22f);
                g.setColour (lk.hair.withAlpha (0.22f));
                g.fillPath (thin);
            }
            else
            {
                cap.startNewSubPath (hx - headW * 1.02f, top + faceL * 0.27f);
                cap.cubicTo (hx - headW * 1.12f, hairTop - faceL * 0.05f, hx + headW * 1.12f, hairTop - faceL * 0.05f, hx + headW * 1.02f, top + faceL * 0.27f);
                cap.quadraticTo (hx + headW * 0.98f, top + faceL * 0.15f, hx + headW * 0.2f, top + faceL * 0.15f);
                cap.quadraticTo (hx - headW * 0.4f, top + faceL * 0.11f, hx - headW * 0.99f, top + faceL * 0.22f);
                cap.closeSubPath();
                g.fillPath (cap);
                if (s.hair == 3)
                    for (int i = 0; i < 9; ++i)
                    {
                        const float a = juce::MathConstants<float>::pi * (1.1f + 0.8f * (float) i / 8.0f);   // over the crown, clear of the temples
                        g.fillEllipse (hx + std::cos (a) * headW * 1.0f - headW * 0.22f, top + faceL * 0.3f + std::sin (a) * faceL * 0.42f - headW * 0.22f, headW * 0.44f, headW * 0.44f);
                    }
                if (s.hair == 2)
                    g.fillEllipse (hx - headW * 0.32f, hairTop - headW * 0.5f, headW * 0.64f, headW * 0.56f);
            }
        }

        // eyes and brows
        const float eyeY = top + faceL * 0.46f;
        const float eyeDx = headW * 0.4f;
        const float eyeW = headW * 0.26f * (lk.child ? 1.15f : 1.0f), eyeH = eyeW * 0.5f;
        for (int side : { -1, 1 })
        {
            const float ex = hx + side * eyeDx;
            if (blink)
            {
                g.setColour (lk.skin.darker (0.5f));
                g.drawLine (ex - eyeW * 0.5f, eyeY, ex + eyeW * 0.5f, eyeY, 1.4f * k);
            }
            else
            {
                juce::Path eye;
                eye.startNewSubPath (ex - eyeW * 0.5f, eyeY);
                eye.quadraticTo (ex, eyeY - eyeH, ex + eyeW * 0.5f, eyeY);
                eye.quadraticTo (ex, eyeY + eyeH * 0.8f, ex - eyeW * 0.5f, eyeY);
                g.setColour (juce::Colour (0xfff7f2ea));
                g.fillPath (eye);
                g.setColour (s.skin <= 1 && s.hairColour >= 4 ? juce::Colour (0xff4f7aa0) : juce::Colour (0xff4a3020));
                g.fillEllipse (ex - eyeH * 0.55f, eyeY - eyeH * 0.55f, eyeH * 1.1f, eyeH * 1.1f);
                g.setColour (juce::Colours::black);
                g.fillEllipse (ex - eyeH * 0.24f, eyeY - eyeH * 0.24f, eyeH * 0.48f, eyeH * 0.48f);
                g.setColour (lk.skin.darker (0.45f));
                g.strokePath (eye, juce::PathStrokeType (0.8f * k));
            }
            juce::Path brow;
            const float by = eyeY - eyeH * (1.5f + 0.6f * open);
            brow.startNewSubPath (ex - eyeW * 0.6f, by + 1.5f * k);
            brow.quadraticTo (ex, by - 2.5f * k, ex + eyeW * 0.6f, by + 1.0f * k);
            g.setColour (s.age > 65.0f ? lk.hair.withAlpha (0.8f) : lk.hair.darker (0.2f));
            g.strokePath (brow, juce::PathStrokeType ((1.6f + 1.2f * s.sex) * k, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        if (s.glasses)
        {
            g.setColour (juce::Colour (0xff2a2330));
            for (int side : { -1, 1 })
                g.drawRoundedRectangle (hx + side * eyeDx - eyeW * 0.68f, eyeY - eyeH * 1.3f, eyeW * 1.36f, eyeH * 2.5f, eyeH * 0.6f, 1.4f * k);
            g.drawLine (hx - eyeDx + eyeW * 0.68f, eyeY - eyeH * 0.4f, hx + eyeDx - eyeW * 0.68f, eyeY - eyeH * 0.4f, 1.4f * k);
        }

        // nose
        {
            const float nw = headW * (0.14f + 0.12f * s.nose);
            const float ny0 = top + faceL * 0.5f, ny1 = top + faceL * (0.66f + 0.04f * s.nose);
            juce::Path n;
            n.startNewSubPath (hx - nw * 0.1f, ny0);
            n.quadraticTo (hx - nw * 0.25f, ny1 - nw * 0.4f, hx - nw * 0.9f, ny1);
            n.quadraticTo (hx, ny1 + nw * 0.35f, hx + nw * 0.9f, ny1);
            g.setColour (lk.skin.darker (0.3f));
            g.strokePath (n, juce::PathStrokeType (1.1f * k, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // cheeks
        g.setColour (juce::Colour (0xffe07a6a).withAlpha (0.12f));
        for (int side : { -1, 1 })
            g.fillEllipse (hx + side * headW * 0.55f - headW * 0.2f, top + faceL * 0.6f, headW * 0.4f, faceL * 0.12f);

        // mouth: width from the mouth and the rounding, opening from the vowel and the jaw
        {
            const float my = top + faceL * 0.8f;
            const float closedBy = juce::jlimit (0.0f, 1.0f, hum);
            const float mw = headW * (0.40f + 0.2f * (s.mouth - 0.5f)) * (1.0f - 0.38f * round) * (1.0f + 0.1f * open);
            const float lipT = (2.6f + 4.2f * s.lips) * k * (1.0f + 0.25f * round);
            const float gap = juce::jmax (0.0f, open * (1.0f - closedBy)) * faceL * (0.12f + 0.06f * s.jaw) * (1.0f + 0.4f * round);
            const juce::Colour lip = lk.skin.interpolatedWith (juce::Colour (0xffb84b52), 0.55f).darker (s.skin >= 4 ? 0.25f : 0.0f);
            if (gap > 0.6f)
            {
                juce::Path inner;
                inner.addEllipse (hx - mw * 0.5f, my - gap * 0.5f, mw, gap);
                g.setColour (juce::Colour (0xff3a1418));
                g.fillPath (inner);
                if (round < 0.5f)
                {
                    g.setColour (juce::Colour (0xfff2ece4).withAlpha (0.9f));
                    g.fillRect (hx - mw * 0.3f, my - gap * 0.5f, mw * 0.6f, juce::jmin (gap * 0.22f, 3.0f * k));
                }
            }
            juce::Path upper, lower;
            upper.startNewSubPath (hx - mw * 0.5f, my - gap * 0.45f);
            upper.quadraticTo (hx - mw * 0.25f, my - gap * 0.5f - lipT * 1.2f, hx, my - gap * 0.5f - lipT * 0.7f);
            upper.quadraticTo (hx + mw * 0.25f, my - gap * 0.5f - lipT * 1.2f, hx + mw * 0.5f, my - gap * 0.45f);
            upper.quadraticTo (hx, my - gap * 0.5f + lipT * 0.15f, hx - mw * 0.5f, my - gap * 0.45f);
            lower.startNewSubPath (hx - mw * 0.5f, my + gap * 0.45f);
            lower.quadraticTo (hx, my + gap * 0.5f + lipT * 1.4f, hx + mw * 0.5f, my + gap * 0.45f);
            lower.quadraticTo (hx, my + gap * 0.5f - lipT * 0.1f, hx - mw * 0.5f, my + gap * 0.45f);
            g.setColour (lip);
            g.fillPath (upper);
            g.setColour (lip.brighter (0.08f));
            g.fillPath (lower);
        }
    }

    /** A small singer for the stage. h = figure height in pixels. */
    void drawFigure (juce::Graphics& g, juce::Point<float> feet, float h, const ChoirLoft::Singer& s, float open, float level, bool selected, juce::Colour tint)
    {
        const auto lk = lookOf (s);
        const float hh = h * (lk.child ? 0.78f : (0.94f + 0.08f * s.sex + 0.04f * (s.chest - 0.5f)));
        const float bw = hh * 0.36f * (0.9f + 0.2f * s.chest);
        if (level > 0.001f)
        {
            const float glow = juce::jlimit (0.0f, 1.0f, level * 6.0f);
            g.setColour (tint.withAlpha (0.35f * glow));
            g.fillEllipse (feet.x - bw * 0.9f, feet.y - hh * 0.06f, bw * 1.8f, hh * 0.12f);
        }
        juce::Path robe;
        robe.startNewSubPath (feet.x - bw * 0.55f, feet.y);
        robe.lineTo (feet.x - bw * 0.42f, feet.y - hh * 0.72f);
        robe.quadraticTo (feet.x, feet.y - hh * 0.8f, feet.x + bw * 0.42f, feet.y - hh * 0.72f);
        robe.lineTo (feet.x + bw * 0.55f, feet.y);
        robe.closeSubPath();
        g.setGradientFill (juce::ColourGradient (lk.robe.brighter (0.2f), feet.x - bw * 0.4f, feet.y - hh * 0.7f, lk.robe.darker (0.4f), feet.x + bw * 0.5f, feet.y, false));
        g.fillPath (robe);
        g.setColour (cc::parchment.withAlpha (0.85f));
        g.fillEllipse (feet.x - bw * 0.3f, feet.y - hh * 0.77f, bw * 0.6f, hh * 0.08f);
        const float hr = hh * 0.13f;
        const auto hc = juce::Point<float> (feet.x, feet.y - hh * 0.86f);
        if (s.hair == 1 || s.hair == 4) { g.setColour (lk.hair.darker (0.2f)); g.fillRoundedRectangle (hc.x - hr * 1.1f, hc.y - hr * 0.6f, hr * 2.2f, hr * (s.hair == 1 ? 2.6f : 1.8f), hr * 0.6f); }
        g.setColour (lk.skin);
        g.fillEllipse (hc.x - hr * 0.86f, hc.y - hr, hr * 1.72f, hr * 2.0f);
        if (s.hair != 5)
        {
            g.setColour (lk.hair);
            juce::Path cap;
            cap.addPieSegment (hc.x - hr * (s.hair == 3 ? 1.08f : 0.94f), hc.y - hr * (s.hair == 3 ? 1.2f : 1.08f), hr * (s.hair == 3 ? 2.16f : 1.88f), hr * 1.5f,
                               -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, 0.0f);
            g.fillPath (cap);
            if (s.hair == 2) g.fillEllipse (hc.x - hr * 0.35f, hc.y - hr * 1.5f, hr * 0.7f, hr * 0.6f);
        }
        if (s.beard) { g.setColour (lk.hair.withAlpha (0.8f)); g.fillEllipse (hc.x - hr * 0.6f, hc.y + hr * 0.25f, hr * 1.2f, hr * 0.75f); }
        const float mo = juce::jlimit (0.0f, 1.0f, open);
        g.setColour (juce::Colour (0xff3a1418));
        g.fillEllipse (hc.x - hr * 0.22f, hc.y + hr * 0.45f - hr * 0.1f * mo, hr * 0.44f, juce::jmax (hr * 0.08f, hr * 0.42f * mo));
        if (selected)
        {
            g.setColour (cc::candle);
            g.drawEllipse (feet.x - bw * 0.95f, feet.y - hh * 0.07f, bw * 1.9f, hh * 0.14f, 1.6f);
        }
    }

    // ---- views -------------------------------------------------------------------------------------------------
    struct ViewBase : public juce::Component
    {
        explicit ViewBase (ChoirLoft& c) : choir (c) {}
        ChoirLoft& choir;
        float s = 1.0f;   // editor scale
        int selected() const { return juce::jlimit (0, choir.numSingers() - 1, juce::roundToInt (choir.param ("singer")) - 1); }
        void select (int i) { choir.setParam ("singer", (float) (i + 1)); if (onSelect) onSelect(); }
        std::function<void()> onSelect;
    };

    class PortraitView : public ViewBase
    {
    public:
        using ViewBase::ViewBase;
        double t = 0.0;
        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat();
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a2d3c), r.getCentreX(), r.getY() + r.getHeight() * 0.35f,
                                                     cc::night, r.getCentreX(), r.getBottom() + r.getHeight() * 0.3f, true));
            g.fillRoundedRectangle (r, 6.0f * s);
            // a lit window behind, softly
            g.setColour (cc::candle.withAlpha (0.05f));
            g.fillEllipse (r.getCentreX() - r.getWidth() * 0.35f, r.getY() - r.getHeight() * 0.1f, r.getWidth() * 0.7f, r.getHeight() * 0.8f);
            const int i = selected();
            const auto singer = choir.getSinger (i);
            const auto live = choir.getLive (i);
            const bool blink = std::fmod (t + 2.0 + (double) i * 1.37, 4.3) < 0.13;
            {
                juce::Graphics::ScopedSaveState ss (g);
                g.reduceClipRegion (r.toNearestInt());
                drawPortrait (g, r.reduced (0.0f, 4.0f * s), singer, live.open, juce::jlimit (0.0f, 1.0f, (live.vowelX - 0.4f) * 1.6f + choir.param ("round") * 0.5f), live.hum, t, blink);
            }
            g.setColour (cc::edge);
            g.drawRoundedRectangle (r.reduced (0.5f), 6.0f * s, 1.0f);
            if (live.note >= 0)
            {
                g.setColour (cc::candle);
                g.setFont (smallCaps (11.0f * s));
                g.drawText ("SINGING " + ChoirLoft::noteName ((float) live.note), r.reduced (10.0f * s, 8.0f * s), juce::Justification::topRight);
            }
        }
    };

    class StageView : public ViewBase
    {
    public:
        using ViewBase::ViewBase;

        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat();
            const int space = (int) choir.param ("space");
            choir.getCast (cast, places);
            {
                float maxX = 2.0f;
                for (auto& p : places) maxX = juce::jmax (maxX, std::abs (p.x) + 0.6f);
                ppm = juce::jmin ((float) getWidth() * 0.46f / maxX, 80.0f * s);
            }
            juce::Colour wall (0xff2b2433), floorC (0xff3a2f2a);
            if (space == roomir::church || space == roomir::cathedral || space == roomir::concreteRoom || space == roomir::stairwell) { wall = juce::Colour (0xff35323a); floorC = juce::Colour (0xff3e3a38); }
            if (space == roomir::cave) { wall = juce::Colour (0xff221f1e); floorC = juce::Colour (0xff2d2925); }
            if (space == roomir::forest || space == roomir::canyon) { wall = juce::Colour (0xff17231e); floorC = juce::Colour (0xff2a2b1f); }
            if (space == roomir::woodenStudio || space == roomir::livingRoom || space == roomir::barClub) { wall = juce::Colour (0xff3a2a20); floorC = juce::Colour (0xff4a3526); }
            g.setColour (cc::night);
            g.fillRoundedRectangle (r, 6.0f * s);
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (r.reduced (1.0f).toNearestInt());
            horizon = r.getY() + r.getHeight() * 0.36f;
            g.setGradientFill (juce::ColourGradient (wall.brighter (0.1f), r.getCentreX(), r.getY(), wall.darker (0.4f), r.getCentreX(), horizon, false));
            g.fillRect (r.withBottom (horizon));
            // arched windows (or trees, or nothing)
            if (space == roomir::forest || space == roomir::canyon)
            {
                g.setColour (juce::Colour (0xff0f1714));
                for (int i = 0; i < 9; ++i)
                {
                    const float x = r.getX() + r.getWidth() * (0.04f + 0.115f * (float) i), w = r.getWidth() * 0.05f;
                    juce::Path tree;
                    tree.addTriangle (x, horizon, x + w, horizon, x + w * 0.5f, r.getY() + r.getHeight() * (0.04f + 0.03f * (float) (i % 3)));
                    g.fillPath (tree);
                }
            }
            else if (space != roomir::cave && space != roomir::dry)
            {
                const int n = space == roomir::cathedral ? 5 : 3;
                for (int i = 0; i < n; ++i)
                {
                    const float w = r.getWidth() * (space == roomir::cathedral ? 0.06f : 0.08f);
                    const float x = r.getX() + r.getWidth() * (float) (i + 1) / (float) (n + 1) - w * 0.5f;
                    const float y0 = r.getY() + r.getHeight() * 0.07f, y1 = horizon - r.getHeight() * 0.05f;
                    juce::Path win;
                    win.addRoundedRectangle (x, y0, w, y1 - y0, w * 0.5f, w * 0.5f, true, true, false, false);
                    g.setGradientFill (juce::ColourGradient (cc::candle.withAlpha (0.4f), x, y0, juce::Colour (0xff6b4e7a).withAlpha (0.35f), x, y1, false));
                    g.fillPath (win);
                    g.setColour (wall.darker (0.6f));
                    g.drawLine (x + w * 0.5f, y0 + w * 0.3f, x + w * 0.5f, y1, 1.2f);
                    g.strokePath (win, juce::PathStrokeType (1.2f));
                }
            }
            g.setGradientFill (juce::ColourGradient (floorC.darker (0.3f), r.getCentreX(), horizon, floorC.brighter (0.05f), r.getCentreX(), r.getBottom(), false));
            g.fillRect (r.withTop (horizon));
            g.setColour (juce::Colours::black.withAlpha (0.18f));
            for (int i = -10; i <= 10; ++i)
            {
                const auto a = toScreen ({ (float) i * 0.8f, 9.0f }), b = toScreen ({ (float) i * 0.8f * 3.0f, -4.0f });
                g.drawLine (a.x, a.y, b.x, b.y, 1.0f);
            }

            // singers, back to front
            const int n = (int) cast.size();
            std::vector<std::pair<float, int>> order;
            for (int i = 0; i < n; ++i) order.push_back ({ -places[(size_t) i].y, i });
            std::sort (order.begin(), order.end());
            const int sel = selected();
            figures.clear();
            for (auto& [negZ, i] : order)
            {
                const auto p = places[(size_t) i];
                const auto& singer = cast[(size_t) i];
                const auto live = choir.getLive (i);
                const auto feet = toScreen (p);
                const float h = figureHeight (p.y);
                drawFigure (g, feet, h, singer, live.open, live.level, i == sel, kindColour (ChoirLoft::describe (singer).kind));
                figures.push_back ({ i, juce::Rectangle<float> (feet.x - h * 0.22f, feet.y - h, h * 0.44f, h) });
            }
            // the selected singer's name
            for (auto& f : figures)
                if (f.index == sel)
                {
                    const auto& singer = cast[(size_t) juce::jlimit (0, (int) cast.size() - 1, sel)];
                    g.setFont (smallCaps (10.5f * s));
                    const juce::String tag = ChoirLoft::personName (singer);
                    const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), tag) + 12.0f * s;
                    auto box = juce::Rectangle<float> (w, 16.0f * s).withCentre ({ f.bounds.getCentreX(), f.bounds.getY() - 10.0f * s });
                    g.setColour (cc::night.withAlpha (0.85f));
                    g.fillRoundedRectangle (box, 3.0f * s);
                    g.setColour (cc::candle);
                    g.drawText (tag, box, juce::Justification::centred);
                }

            // the microphones
            const auto mic = juce::Point<float> (r.getCentreX(), r.getBottom() - 10.0f * s);
            g.setColour (juce::Colour (0xff111014));
            g.drawLine (mic.x, mic.y, mic.x, mic.y - 26.0f * s, 2.0f * s);
            const int mics = (int) choir.param ("mics");
            const float spread = mics == ChoirLoft::micSpaced ? 16.0f : (mics == ChoirLoft::micNearCoincident ? 5.0f : 0.0f);
            for (int side : { -1, 1 })
            {
                if (mics == ChoirLoft::micMono && side > 0) break;
                const float ang = mics == ChoirLoft::micSpaced || mics == ChoirLoft::micMono ? 0.0f : (float) side * (mics == ChoirLoft::micNearCoincident ? 0.95f : 0.78f);
                const auto c = juce::Point<float> (mic.x + side * spread * s * (mics == ChoirLoft::micMono ? 0.0f : 1.0f), mic.y - 28.0f * s);
                juce::Path cap;
                cap.addRoundedRectangle (-3.0f * s, -11.0f * s, 6.0f * s, 14.0f * s, 3.0f * s);
                g.setColour (juce::Colour (0xffb7b2bd));
                g.fillPath (cap, juce::AffineTransform::rotation (ang).translated (c));
            }
            if (spread > 0.0f) g.drawLine (mic.x - spread * s, mic.y - 26.0f * s, mic.x + spread * s, mic.y - 26.0f * s, 1.5f * s);
            g.setColour (cc::dim);
            g.setFont (uiFont (11.0f * s));
            g.drawText (juce::String (choir.param ("distance"), 1) + " m", juce::Rectangle<float> (mic.x + 22.0f * s, mic.y - 30.0f * s, 60.0f * s, 16.0f * s), juce::Justification::centredLeft);
            g.drawText (ChoirLoft::arrangementNames()[(int) choir.param ("arrangement")] + "  /  " + roomir::spaceNames()[space],
                        juce::Rectangle<float> (r.getX() + 10.0f * s, r.getBottom() - 22.0f * s, r.getWidth() * 0.6f, 16.0f * s), juce::Justification::centredLeft);
            g.setColour (cc::faint);
            g.drawText ("click a singer to pick them, drag to move them", r.reduced (10.0f * s, 6.0f * s).withHeight (16.0f * s), juce::Justification::centredRight);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            dragging = -1;
            for (auto it = figures.rbegin(); it != figures.rend(); ++it)
                if (it->bounds.expanded (3.0f).contains (e.position)) { dragging = it->index; select (it->index); break; }
            repaint();
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (dragging < 0 || e.getDistanceFromDragStart() < 3) return;
            choir.setPosition (dragging, fromScreen (e.position));
            repaint();
        }
        void mouseUp (const juce::MouseEvent&) override { dragging = -1; }

    private:
        struct Fig { int index; juce::Rectangle<float> bounds; };
        std::vector<Fig> figures;
        int dragging = -1;
        float horizon = 0.0f;

        // a gentle perspective: depth z (metres behind the front row) shrinks things toward the horizon
        float depthScale (float z) const { return 1.0f / (1.0f + z * 0.22f); }
        std::vector<ChoirLoft::Singer> cast;
        std::vector<juce::Point<float>> places;
        float ppm = 60.0f;
        float pxPerMetre() const { return ppm; }
        float frontY() const { return (float) getHeight() - 52.0f * s; }
        juce::Point<float> toScreen (juce::Point<float> p) const
        {
            const float d = depthScale (p.y);
            return { (float) getWidth() * 0.5f + p.x * pxPerMetre() * d, horizon + (frontY() - horizon) * d };
        }
        juce::Point<float> fromScreen (juce::Point<float> q) const
        {
            const float d = juce::jlimit (0.12f, 1.4f, (q.y - horizon) / juce::jmax (1.0f, frontY() - horizon));
            const float z = juce::jlimit (0.0f, 10.0f, (1.0f / d - 1.0f) / 0.22f);
            return { (q.x - (float) getWidth() * 0.5f) / (pxPerMetre() * depthScale (z)), z };
        }
        float figureHeight (float z) const { return (frontY() - horizon) * 0.62f * depthScale (z) + 14.0f * s * depthScale (z); }
    };

    class TractView : public ViewBase
    {
    public:
        using ViewBase::ViewBase;
        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat();
            const int i = selected();
            const auto singer = choir.getSinger (i);
            const auto vi = ChoirLoft::describe (singer);
            const auto live = choir.getLive (i);
            const bool singing = live.level > 0.0005f;
            const float x = singing ? live.vowelX : choir.param ("vowelX");
            const float y = singing ? live.vowelY : choir.param ("vowelY");
            const float hum = singing ? live.hum : choir.param ("hum");
            const float round = juce::jlimit (0.0f, 1.0f, (x - 0.4f) * 1.4f + choir.param ("round") * 0.6f);

            auto area = r.reduced (14.0f * s, 8.0f * s);
            area.removeFromBottom (34.0f * s);
            const float len = (area.getWidth() - 40.0f * s) * juce::jlimit (0.55f, 1.0f, vi.tractCm / 18.0f);
            const float x0 = area.getX(), cy = area.getCentreY() + 6.0f * s, maxR = area.getHeight() * 0.36f;
            // radius along the tract (glottis -> lips): a throat that widens for front vowels, a tongue
            // constriction that moves back for back vowels and tightens for close ones, lips that round or shut
            auto radius = [&] (float u)
            {
                float rr = 0.45f;
                const float pharynx = 1.0f + 0.55f * (1.0f - x) - 0.5f * y;
                rr += 0.45f * pharynx * std::exp (-std::pow ((u - 0.3f) / 0.16f, 2.0f));
                const float place = 0.8f - 0.26f * x;
                const float tight = 0.25f + 0.75f * y;
                rr *= 1.0f - (1.0f - tight) * 0.85f * std::exp (-std::pow ((u - place) / 0.09f, 2.0f));
                rr += 0.35f * y * std::exp (-std::pow ((u - 0.88f) / 0.07f, 2.0f));
                const float lips = (0.25f + 0.75f * y) * (1.0f - 0.45f * round) * (1.0f - hum);
                const float lw = smoothstepF (0.9f, 1.0f, u);
                rr = rr * (1.0f - lw) + juce::jmax (0.02f, lips * 0.8f) * lw;
                return juce::jlimit (0.03f, 1.2f, rr);
            };
            juce::Path top, tube;
            const int steps = 60;
            for (int k = 0; k <= steps; ++k)
            {
                const float u = (float) k / steps;
                const float px = x0 + u * len, py = cy - radius (u) * maxR;
                if (k == 0) top.startNewSubPath (px, py); else top.lineTo (px, py);
            }
            tube = top;
            for (int k = steps; k >= 0; --k)
            {
                const float u = (float) k / steps;
                tube.lineTo (x0 + u * len, cy + radius (u) * maxR * 0.85f);
            }
            tube.closeSubPath();
            // the nose: a branch going up from the soft palate, as open as the nasal coupling
            const float nasal = juce::jlimit (0.0f, 1.0f, vi.nasal + hum * 0.9f);
            {
                const float vx = x0 + len * 0.62f;
                juce::Path nose;
                nose.startNewSubPath (vx, cy - radius (0.62f) * maxR);
                nose.quadraticTo (vx + len * 0.08f, cy - maxR * 1.6f, vx + len * 0.32f, cy - maxR * 1.55f);
                g.setColour (cc::candle.withAlpha (0.15f + 0.6f * nasal));
                g.strokePath (nose, juce::PathStrokeType ((2.0f + 9.0f * nasal) * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            g.setGradientFill (juce::ColourGradient (cc::rose.withAlpha (0.55f), x0, cy, cc::candle.withAlpha (0.45f), x0 + len, cy, false));
            g.fillPath (tube);
            g.setColour (cc::parchment.withAlpha (0.8f));
            g.strokePath (tube, juce::PathStrokeType (1.2f * s));
            g.setFont (smallCaps (9.0f * s));
            g.setColour (cc::faint);
            g.drawText ("GLOTTIS", juce::Rectangle<float> (x0, cy - maxR * 1.6f, 70.0f * s, 12.0f * s), juce::Justification::centredLeft);
            g.drawText ("LIPS", juce::Rectangle<float> (x0 + len + 4.0f * s, cy - 6.0f * s, 40.0f * s, 12.0f * s), juce::Justification::centredLeft);
            g.drawText ("NOSE", juce::Rectangle<float> (x0 + len * 0.86f, cy - maxR * 1.85f, 50.0f * s, 12.0f * s), juce::Justification::centredLeft);
            g.setColour (cc::parchment);
            g.setFont (uiFont (12.0f * s, true));
            g.drawText (juce::String (vi.tractCm, 1) + " cm", juce::Rectangle<float> (area.getRight() - 80.0f * s, area.getY(), 80.0f * s, 14.0f * s), juce::Justification::centredRight);

            // facts
            auto f = ChoirLoft::referenceFormants (x, y);
            const float f1 = f[0] * vi.f1Mul * vi.formantScale, f2 = f[1] * vi.f2Mul * vi.formantScale, f3 = f[2] * vi.f3Mul * vi.formantScale;
            g.setFont (uiFont (11.0f * s));
            g.setColour (cc::dim);
            const juce::String line1 = vi.kind + "   range " + ChoirLoft::noteName (vi.low) + "-" + ChoirLoft::noteName (vi.high) + "   break " + ChoirLoft::noteName (vi.breakNote)
                                     + "   vibrato " + juce::String (vi.vibRate, 1) + " Hz";
            const juce::String line2 = "F1 " + juce::String (juce::roundToInt (f1)) + "   F2 " + juce::String (juce::roundToInt (f2)) + "   F3 " + juce::String (juce::roundToInt (f3))
                                     + " Hz   nasal " + juce::String (juce::roundToInt (nasal * 100.0f)) + "%   folds Rd " + juce::String (vi.rd, 2);
            auto txt = r.reduced (14.0f * s, 6.0f * s).removeFromBottom (32.0f * s);
            g.drawText (line1, txt.removeFromTop (16.0f * s), juce::Justification::centredLeft);
            g.drawText (line2, txt, juce::Justification::centredLeft);
        }
    private:
        static float smoothstepF (float a, float b, float v) { const float t = juce::jlimit (0.0f, 1.0f, (v - a) / (b - a)); return t * t * (3.0f - 2.0f * t); }
    };

    class VowelPad : public ViewBase
    {
    public:
        using ViewBase::ViewBase;
        void paint (juce::Graphics& g) override
        {
            juce::Path p;
            p.startNewSubPath (map (0, 0)); p.lineTo (map (1, 0)); p.lineTo (map (1, 1)); p.lineTo (map (0, 1)); p.closeSubPath();
            g.setColour (cc::panelHi);
            g.fillPath (p);
            g.setColour (cc::edge);
            g.strokePath (p, juce::PathStrokeType (1.0f));
            for (float t : { 1.0f / 3.0f, 2.0f / 3.0f }) { const auto a = map (0, t), b = map (1, t); g.drawLine (a.x, a.y, b.x, b.y, 0.7f); }
            { const auto a = map (0.5f, 0), b = map (0.5f, 1); g.drawLine (a.x, a.y, b.x, b.y, 0.7f); }
            g.setFont (uiFont (13.0f * s, true));
            for (auto& v : ChoirLoft::vowelChart())
            {
                const auto c = map (v.x, v.y);
                g.setColour (cc::dim);
                g.drawText (juce::String::fromUTF8 (v.symbol), juce::Rectangle<float> (22.0f * s, 16.0f * s).withCentre (c.translated ((v.x < 0.5f ? 9.0f : v.x > 0.5f ? -9.0f : 0.0f) * s, (v.y < 0.5f ? 8.0f : -8.0f) * s)), juce::Justification::centred);
            }
            // where the active singers are right now
            for (int i = 0; i < choir.numSingers(); ++i)
            {
                const auto l = choir.getLive (i);
                if (l.level < 0.0005f) continue;
                const auto c = map (l.vowelX, l.vowelY);
                g.setColour (cc::rose.withAlpha (0.5f));
                g.fillEllipse (c.x - 2.5f * s, c.y - 2.5f * s, 5.0f * s, 5.0f * s);
            }
            // B (where motion goes) and A (the vowel)
            const int motion = (int) choir.param ("motion");
            const auto a = map (choir.param ("vowelX"), choir.param ("vowelY"));
            if (motion == ChoirLoft::wave || motion == ChoirLoft::envelope || motion == ChoirLoft::steps)
            {
                const auto b = map (choir.param ("vowel2X"), choir.param ("vowel2Y"));
                g.setColour (cc::sky.withAlpha (0.6f));
                juce::Path dash;
                dash.startNewSubPath (a); dash.lineTo (b);
                const float pattern[] = { 4.0f * s, 3.0f * s };
                juce::PathStrokeType (1.2f * s).createDashedStroke (dash, dash, pattern, 2);
                g.fillPath (dash);
                g.setColour (cc::sky);
                g.drawEllipse (b.x - 6.0f * s, b.y - 6.0f * s, 12.0f * s, 12.0f * s, 2.0f * s);
                g.setFont (smallCaps (9.0f * s));
                g.drawText ("B", juce::Rectangle<float> (14.0f * s, 12.0f * s).withCentre (b.translated (11.0f * s, -9.0f * s)), juce::Justification::centred);
            }
            else if (motion == ChoirLoft::randomPerNote || motion == ChoirLoft::drift)
            {
                const float depth = choir.param ("motionDepth");
                g.setColour (cc::sky.withAlpha (0.25f));
                const float rad = (motion == ChoirLoft::drift ? 0.5f : 1.0f) * depth * (chartW() * 0.5f);
                g.fillEllipse (a.x - rad, a.y - rad * 0.8f, rad * 2.0f, rad * 1.6f);
            }
            g.setColour (cc::candle);
            g.fillEllipse (a.x - 7.0f * s, a.y - 7.0f * s, 14.0f * s, 14.0f * s);
            g.setColour (cc::night);
            g.setFont (smallCaps (9.0f * s));
            g.drawText ("A", juce::Rectangle<float> (14.0f * s, 14.0f * s).withCentre (a), juce::Justification::centred);
            g.setColour (cc::faint);
            g.setFont (uiFont (9.5f * s));
            g.drawText ("front", juce::Rectangle<float> (0, 0, 60.0f * s, 12.0f * s), juce::Justification::topLeft);
            g.drawText ("back", juce::Rectangle<float> ((float) getWidth() - 60.0f * s, 0, 60.0f * s, 12.0f * s), juce::Justification::topRight);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            const auto b = map (choir.param ("vowel2X"), choir.param ("vowel2Y"));
            const auto a = map (choir.param ("vowelX"), choir.param ("vowelY"));
            const int motion = (int) choir.param ("motion");
            const bool bShown = motion == ChoirLoft::wave || motion == ChoirLoft::envelope || motion == ChoirLoft::steps;
            dragB = e.mods.isRightButtonDown() || e.mods.isShiftDown() || (bShown && e.position.getDistanceFrom (b) < e.position.getDistanceFrom (a) && e.position.getDistanceFrom (b) < 14.0f * s);
            setFrom (e.position);
        }
        void mouseDrag (const juce::MouseEvent& e) override { setFrom (e.position); }

    private:
        bool dragB = false;
        float chartW() const { return (float) getWidth() - 24.0f * s; }
        juce::Point<float> map (float x, float y) const
        {
            const float l = 12.0f * s, t = 16.0f * s, w = chartW(), h = (float) getHeight() - 28.0f * s;
            const float left = l + y * w * 0.32f;   // the front edge slopes, like the vowel chart
            return { left + x * (l + w - left), t + y * h };
        }
        void setFrom (juce::Point<float> p)
        {
            const float l = 12.0f * s, t = 16.0f * s, w = chartW(), h = (float) getHeight() - 28.0f * s;
            const float y = juce::jlimit (0.0f, 1.0f, (p.y - t) / h);
            const float left = l + y * w * 0.32f;
            const float x = juce::jlimit (0.0f, 1.0f, (p.x - left) / juce::jmax (1.0f, l + w - left));
            choir.setParam (dragB ? "vowel2X" : "vowelX", x);
            choir.setParam (dragB ? "vowel2Y" : "vowelY", y);
            repaint();
        }
    };

    // ---- the editor ----------------------------------------------------------------------------------------
    class ChoirLoftEditor : public juce::AudioProcessorEditor, private juce::Timer
    {
    public:
        explicit ChoirLoftEditor (ChoirLoft& c) : AudioProcessorEditor (c), choir (c), header (c, "Choir Loft"),
                                                  portrait (c), stage (c), tract (c), vowels (c)
        {
            setLookAndFeel (&lnf);
            addAndMakeVisible (header);
            header.onPresetChanged = [this] { refreshSinger(); repaint(); };
            for (auto* v : std::initializer_list<ViewBase*> { &portrait, &stage, &tract, &vowels })
            {
                addAndMakeVisible (v);
                v->onSelect = [this] { refreshSinger(); repaintViews(); };
            }

            // singer
            for (auto* b : { &prevB, &nextB, &lockB, &rerollB, &bodyTab, &voiceTab, &rearrangeB, &castDice })
            {
                addAndMakeVisible (b);
                b->setWantsKeyboardFocus (false);
            }
            prevB.onClick = [this] { step (-1); };
            nextB.onClick = [this] { step (1); };
            lockB.setClickingTogglesState (false);
            lockB.onClick = [this] { const int i = sel(); choir.setLocked (i, ! choir.getSinger (i).locked); refreshSinger(); };
            lockB.setTooltip ("Locked singers keep who they are when you re-roll the cast or change its balance (editing a singer locks them)");
            rerollB.onClick = [this] { choir.rerollSinger (sel()); refreshSinger(); repaintViews(); };
            rerollB.setTooltip ("A new person in this place (same part)");
            bodyTab.setClickingTogglesState (false);
            voiceTab.setClickingTogglesState (false);
            bodyTab.onClick = [this] { showVoice = false; layoutFields(); };
            voiceTab.onClick = [this] { showVoice = true; layoutFields(); };
            for (int f = 0; f < ChoirLoft::numFields; ++f)
            {
                auto* s = fields.add (new juce::Slider (juce::Slider::LinearHorizontal, juce::Slider::NoTextBox));
                s->getProperties().set ("label", ChoirLoft::fieldName (f));
                s->setTooltip (ChoirLoft::fieldTip (f));
                s->setRange (ChoirLoft::fieldMin (f), ChoirLoft::fieldMax (f), f == ChoirLoft::fAge ? 1.0 : 0.0);
                s->setWantsKeyboardFocus (false);
                s->textFromValueFunction = [f] (double v)
                {
                    if (f == ChoirLoft::fAge) return juce::String (juce::roundToInt (v)) + " y";
                    if (f == ChoirLoft::fSex) return v < 0.35 ? juce::String ("Woman") : v > 0.65 ? juce::String ("Man") : juce::String (juce::roundToInt (v * 100.0)) + "%";
                    return juce::String (juce::roundToInt (v * 100.0)) + "%";
                };
                s->onValueChange = [this, f, s]
                {
                    if (updating) return;
                    auto person = choir.getSinger (sel());
                    ChoirLoft::field (person, f) = (float) s->getValue();
                    choir.setSinger (sel(), person);
                    refreshInfo();
                };
                addAndMakeVisible (s);
            }

            // cast
            const char* modes[] = { "Sections", "Octaves", "Round Robin", "Solo" };
            const char* modeTips[] = { "The whole choir shares the notes you hold: each singer takes the note that suits their voice",
                                       "Like Sections, but each singer sings it in the octave of their own range (one key, the whole choir)",
                                       "Each new note goes to the next singer (or few singers: Singers per Note)",
                                       "Only the selected singer, legato: overlap the notes and they glide" };
            for (int i = 0; i < 4; ++i)
            {
                auto* b = modeButtons.add (new juce::TextButton (modes[i]));
                b->setTooltip (modeTips[i]);
                b->setWantsKeyboardFocus (false);
                b->onClick = [this, i] { choir.setParam ("mode", (float) i); refreshButtons(); };
                addAndMakeVisible (b);
            }
            castDice.onClick = [this] { choir.rerollCast(); refreshSinger(); repaintViews(); };
            castDice.setTooltip ("A new cast: new people (locked singers stay)");
            rearrangeB.onClick = [this] { choir.rearrange(); repaintViews(); };
            rearrangeB.setTooltip ("Next arrangement (rows, arc, sections, line, cluster); forgets singers you dragged");

            for (auto id : { "singers", "women", "age", "ageSpread", "diversity", "perNote",
                             "distance", "width", "roomSize", "roomLevel",
                             "dynamics", "breath", "vibrato", "looseness", "rasp", "breaths", "attack", "release", "glide", "tune", "volume",
                             "motionRate", "motionDepth", "round", "hum" })
                addBar (id);
            for (auto id : { "style", "space", "mics", "motion", "onset" }) addCombo (id);
            poly.setButtonText ("each singer alone");
            poly.setTooltip ("Each singer moves through the vowels in their own time (off: the choir moves together)");
            polyAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (choir.state, "motionPoly", poly);
            poly.setWantsKeyboardFocus (false);
            addAndMakeVisible (poly);

            setResizable (true, false);
            setResizeLimits (760, headerH + 452, 1900, headerH + 1130);
            setSize ((int) designW, headerH + (int) designH);
            refreshSinger();
            refreshButtons();
            startTimerHz (30);
        }

        ~ChoirLoftEditor() override
        {
            stopTimer();
            setLookAndFeel (nullptr);
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (cc::night);
            juce::Graphics::ScopedSaveState ss (g);
            g.addTransform (toScreen);
            auto panel = [&] (juce::Rectangle<float> r, const juce::String& title)
            {
                g.setColour (cc::panel);
                g.fillRoundedRectangle (r, 8.0f);
                g.setColour (cc::edge);
                g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
                g.setColour (cc::candle);
                g.setFont (smallCaps (11.0f));
                g.drawText (title, r.reduced (12.0f, 6.0f).withHeight (16.0f), juce::Justification::centredLeft);
            };
            panel (rf (10, 10, 362, 670), "SINGER");
            panel (rf (382, 10, 396, 330), "STAGE");
            panel (rf (382, 350, 396, 140), "VOCAL TRACT");
            panel (rf (382, 500, 396, 180), "VOWEL");
            panel (rf (788, 10, 362, 262), "CAST");
            panel (rf (788, 282, 362, 160), "ROOM & MICROPHONES");
            panel (rf (788, 452, 362, 228), "EXPRESSION");

            // singer line
            const int i = sel();
            const auto person = choir.getSinger (i);
            const auto vi = ChoirLoft::describe (person);
            g.setColour (cc::parchment);
            g.setFont (uiFont (15.0f, true));
            g.drawText (ChoirLoft::personName (person), rf (60, 264, 200, 18), juce::Justification::centredLeft);
            g.setColour (cc::dim);
            g.setFont (uiFont (11.5f));
            g.drawText ("Singer " + juce::String (i + 1) + " of " + juce::String (choir.numSingers()) + "   " + ChoirLoft::personLabel (person),
                        rf (60, 282, 210, 16), juce::Justification::centredLeft);
            g.setColour (kindColour (vi.kind));
            g.drawText (vi.kind + "  " + ChoirLoft::noteName (vi.low) + "-" + ChoirLoft::noteName (vi.high), rf (60, 298, 210, 16), juce::Justification::centredLeft);

            // singing now
            g.setColour (cc::dim);
            g.setFont (uiFont (11.5f));
            g.drawText (juce::String (choir.getActiveSingers()) + " / " + juce::String (choir.numSingers()) + " singing", rf (1000, 14, 140, 16), juce::Justification::centredRight);
            g.setColour (cc::faint);
            g.setFont (uiFont (10.5f));
            g.drawText (ChoirLoft::styleDescription ((int) choir.param ("style")), rf (800, 240, 340, 26), juce::Justification::topLeft, true);
            g.drawText ("Mod wheel deepens the vibrato, CC11 is expression, MPE bends each note", rf (800, 660, 340, 16), juce::Justification::centredLeft);
            g.drawText ("drag A; drag B (or shift-drag) for vowel motion", rf (394, 664, 220, 14), juce::Justification::centredLeft);
        }

        void resized() override
        {
            header.setBounds (getLocalBounds().removeFromTop (headerH).reduced (10, 0));
            auto area = getLocalBounds().withTrimmedTop (headerH).toFloat();
            scale = juce::jmax (0.05f, juce::jmin (area.getWidth() / designW, area.getHeight() / designH));
            const float ox = area.getX() + (area.getWidth() - designW * scale) * 0.5f;
            const float oy = area.getY() + (area.getHeight() - designH * scale) * 0.5f;
            toScreen = juce::AffineTransform::scale (scale).translated (ox, oy);
            for (auto* v : std::initializer_list<ViewBase*> { &portrait, &stage, &tract, &vowels }) v->s = scale;

            portrait.setBounds (screen (rf (22, 34, 338, 222)));
            prevB.setBounds (screen (rf (22, 268, 30, 40)));
            nextB.setBounds (screen (rf (330, 268, 30, 40)));
            lockB.setBounds (screen (rf (262, 268, 64, 19)));
            rerollB.setBounds (screen (rf (262, 290, 64, 19)));
            bodyTab.setBounds (screen (rf (22, 320, 166, 24)));
            voiceTab.setBounds (screen (rf (194, 320, 166, 24)));
            layoutFields();

            stage.setBounds (screen (rf (394, 32, 372, 298)));
            rearrangeB.setBounds (screen (rf (680, 13, 86, 18)));
            tract.setBounds (screen (rf (394, 370, 372, 116)));
            vowels.setBounds (screen (rf (394, 524, 220, 140)));
            place ("motion", rf (624, 520, 144, 22));
            place ("motionRate", rf (624, 548, 144, 28));
            place ("motionDepth", rf (624, 580, 144, 28));
            poly.setBounds (screen (rf (624, 610, 144, 20)));
            place ("round", rf (624, 632, 144, 28));

            for (int k = 0; k < 4; ++k) modeButtons[k]->setBounds (screen (rf (800.0f + (float) k * 86.5f, 34, 82, 24)));
            place ("singers", rf (800, 66, 164, 28)); place ("women", rf (976, 66, 164, 28));
            place ("age", rf (800, 98, 164, 28));     place ("ageSpread", rf (976, 98, 164, 28));
            place ("diversity", rf (800, 130, 164, 28)); place ("perNote", rf (976, 130, 164, 28));
            place ("style", rf (800, 168, 244, 24));
            castDice.setBounds (screen (rf (1052, 168, 88, 24)));
            place ("onset", rf (800, 204, 340, 24));

            place ("space", rf (800, 306, 200, 24)); place ("mics", rf (1008, 306, 132, 24));
            place ("roomSize", rf (800, 338, 164, 28)); place ("roomLevel", rf (976, 338, 164, 28));
            place ("distance", rf (800, 370, 164, 28)); place ("width", rf (976, 370, 164, 28));

            const char* expr[] = { "dynamics", "breath", "vibrato", "looseness", "rasp", "breaths", "attack", "release", "glide", "tune", "volume", "hum" };
            for (int k = 0; k < 12; ++k)
                place (expr[k], rf (800.0f + (float) (k % 2) * 176.0f, 478.0f + (float) (k / 2) * 30.0f, 164, 28));
            repaint();
        }

    private:
        ChoirLoft& choir;
        LoftLnF lnf;
        EditorHeader header;
        PortraitView portrait;
        StageView stage;
        TractView tract;
        VowelPad vowels;
        juce::TextButton prevB { "<" }, nextB { ">" }, lockB { "LOCK" }, rerollB { "NEW FACE" }, bodyTab { "BODY" }, voiceTab { "VOICE" },
                         rearrangeB { "Re-arrange" }, castDice { "New Cast" };
        juce::OwnedArray<juce::Slider> fields;
        juce::OwnedArray<juce::TextButton> modeButtons;
        std::map<juce::String, std::unique_ptr<juce::Slider>> bars;
        std::map<juce::String, std::unique_ptr<juce::ComboBox>> combos;
        std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> barAttach;
        std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> comboAttach;
        juce::ToggleButton poly;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> polyAttach;
        juce::AffineTransform toScreen;
        float scale = 1.0f;
        bool showVoice = false, updating = false;
        int lastSel = -1, lastMode = -1, tick = 0;
        double t0 = juce::Time::getMillisecondCounterHiRes();

        int sel() const { return juce::jlimit (0, choir.numSingers() - 1, juce::roundToInt (choir.param ("singer")) - 1); }
        juce::Rectangle<int> screen (juce::Rectangle<float> r) const { return r.transformedBy (toScreen).getSmallestIntegerContainer(); }

        void addBar (const juce::String& id)
        {
            auto s = std::make_unique<juce::Slider> (juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
            auto* p = choir.state.getParameter (id);
            s->getProperties().set ("label", p != nullptr ? p->getName (40) : id);
            s->getProperties().set ("bipolar", id == "round" || id == "tune");
            s->setWantsKeyboardFocus (false);
            addAndMakeVisible (*s);
            barAttach.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (choir.state, id, *s));
            if (p != nullptr) s->setDoubleClickReturnValue (true, (double) p->convertFrom0to1 (p->getDefaultValue()));
            s->onValueChange = [this] { repaintViews(); };
            bars[id] = std::move (s);
        }

        void addCombo (const juce::String& id)
        {
            auto c = std::make_unique<juce::ComboBox>();
            if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (choir.state.getParameter (id)))
                c->addItemList (p->choices, 1);
            c->setWantsKeyboardFocus (false);
            c->setTooltip (choir.state.getParameter (id)->getName (40));
            addAndMakeVisible (*c);
            comboAttach.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (choir.state, id, *c));
            c->onChange = [this] { repaint(); repaintViews(); };
            combos[id] = std::move (c);
        }

        void place (const juce::String& id, juce::Rectangle<float> r)
        {
            if (r.getWidth() <= 0.0f) return;
            if (auto it = bars.find (id); it != bars.end()) it->second->setBounds (screen (r));
            if (auto it = combos.find (id); it != combos.end()) it->second->setBounds (screen (r));
        }

        void layoutFields()
        {
            bodyTab.setToggleState (! showVoice, juce::dontSendNotification);
            voiceTab.setToggleState (showVoice, juce::dontSendNotification);
            int row = 0;
            for (int f = 0; f < ChoirLoft::numFields; ++f)
            {
                const bool voiceField = f >= ChoirLoft::fPitch;
                const bool visible = voiceField == showVoice || f == ChoirLoft::fSex || f == ChoirLoft::fAge;
                fields[f]->setVisible (visible);
                if (! visible) continue;
                fields[f]->setBounds (screen (rf (22, 354.0f + (float) row * 25.6f, 338, 25)));
                ++row;
            }
        }

        void refreshSinger()
        {
            const auto person = choir.getSinger (sel());
            updating = true;
            for (int f = 0; f < ChoirLoft::numFields; ++f)
            {
                auto copy = person;
                if (! fields[f]->isMouseButtonDown()) fields[f]->setValue (ChoirLoft::field (copy, f), juce::dontSendNotification);
            }
            updating = false;
            lockB.setToggleState (person.locked, juce::dontSendNotification);
            lastSel = sel();
            refreshInfo();
        }

        void refreshInfo() { repaint (screen (rf (10, 256, 362, 64))); repaintViews(); }

        void refreshButtons()
        {
            const int m = (int) choir.param ("mode");
            for (int k = 0; k < 4; ++k) modeButtons[k]->setToggleState (k == m, juce::dontSendNotification);
            if (auto it = bars.find ("perNote"); it != bars.end()) it->second->setEnabled (m == ChoirLoft::roundRobin);
            if (auto it = bars.find ("glide"); it != bars.end()) it->second->setEnabled (m == ChoirLoft::solo);
            lastMode = m;
        }

        void step (int d)
        {
            const int n = choir.numSingers();
            choir.setParam ("singer", (float) ((sel() + d + n) % n + 1));
            refreshSinger();
        }

        void repaintViews()
        {
            for (auto* v : std::initializer_list<juce::Component*> { &portrait, &stage, &tract, &vowels }) v->repaint();
        }

        void timerCallback() override
        {
            portrait.t = (juce::Time::getMillisecondCounterHiRes() - t0) * 0.001;
            if (sel() != lastSel) refreshSinger();
            if ((int) choir.param ("mode") != lastMode) refreshButtons();
            // keep the sliders in step with cast changes (balance, ages) unless one is being dragged
            if (++tick % 6 == 0)
            {
                bool dragging = false;
                for (auto* f : fields) dragging = dragging || f->isMouseButtonDown();
                if (! dragging) refreshSinger();
                repaint (screen (rf (1000, 12, 150, 20)));
            }
            repaintViews();
        }
    };
}

juce::Image choirPortraitSheet()
{
    // 4 x 3 portraits of a mixed cast, for checking the drawing (UI self-test snapshot)
    ChoirLoft c;
    c.setParam ("singers", 12.0f);
    c.setParam ("diversity", 1.0f);
    c.setParam ("ageSpread", 30.0f);
    c.setParam ("age", 40.0f);
    juce::Image img (juce::Image::ARGB, 4 * 240, 3 * 200, true);
    juce::Graphics g (img);
    g.fillAll (cc::night);
    for (int i = 0; i < 12; ++i)
    {
        auto s = c.getSinger (i);
        s.hair = i % 6;
        if (i == 4) s.age = 9.0f;
        const auto r = juce::Rectangle<float> ((float) (i % 4) * 240.0f, (float) (i / 4) * 200.0f, 240.0f, 200.0f).reduced (4.0f);
        g.setColour (cc::panel);
        g.fillRect (r);
        drawPortrait (g, r, s, (float) (i % 3) * 0.4f, (float) (i % 2), 0.0f, 0.0, false);
        g.setColour (cc::parchment);
        g.setFont (uiFont (11.0f));
        g.drawText (ChoirLoft::personName (s) + ", hair " + juce::String (s.hair), r.reduced (4.0f), juce::Justification::topLeft);
    }
    return img;
}

void installChoirLoftEditor()
{
    ChoirLoft::editorFactory = [] (ChoirLoft& c) -> juce::AudioProcessorEditor* { return new ChoirLoftEditor (c); };
}

} // namespace wis::daw
