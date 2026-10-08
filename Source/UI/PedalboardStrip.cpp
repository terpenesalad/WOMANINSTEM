#include "PedalboardStrip.h"
#include "LookAndFeel.h"
#include "Daw/Plugins/BuiltinProcessor.h"

namespace wis
{

namespace
{
    constexpr int gap = 14;          // room for the arrow between pedals
    const juce::StringArray categoryOrder { "Pedals", "Distortion", "Dynamics", "EQ & Filter", "Modulation", "Pitch",
                                            "Space", "Lo-Fi", "Experimental", "Vocal", "Utility" };

    juce::String categoryTitle (const juce::String& c)
    {
        return c == "Pedals" ? juce::String ("Stomp boxes (drive, boost, wah, octaver, swell)") : c;
    }
}

juce::Colour PedalboardStrip::colourFor (const RigProcessor::BoardItem& it)
{
    if (! it.isPedal)
    {
        static const juce::uint32 core[] = { 0xff64748b, 0xff38bdf8, 0xff22c55e, 0xffff4d8d, 0xffa3e635, 0xffd97706, 0xff60a5fa, 0xffc084fc, 0xff2dd4bf };
        return juce::Colour (juce::isPositiveAndBelow (it.block, 9) ? core[it.block] : 0xff94a3b8);
    }
    const auto* info = daw::findBuiltin (it.builtinId);
    const juce::String cat = info != nullptr ? info->category : juce::String();
    if (cat == "Pedals")       return juce::Colour (0xfff97316);
    if (cat == "Distortion")   return juce::Colour (0xffef4444);
    if (cat == "Dynamics")     return juce::Colour (0xff38bdf8);
    if (cat == "EQ & Filter")  return juce::Colour (0xffa3e635);
    if (cat == "Modulation")   return juce::Colour (0xff22d3ee);
    if (cat == "Pitch")        return juce::Colour (0xffe879f9);
    if (cat == "Space")        return juce::Colour (0xff818cf8);
    if (cat == "Lo-Fi")        return juce::Colour (0xfffbbf24);
    if (cat == "Experimental") return juce::Colour (0xfff472b6);
    if (cat == "Vocal")        return juce::Colour (0xffec4899);
    return juce::Colour (0xff94a3b8);
}

PedalboardStrip::PedalboardStrip (RigProcessor& r) : rig (r)
{
    addButton.setTooltip ("Add a pedal: drive, boost, wah, octaver, swell, phaser, flanger, tremolo, pitch shifter, "
                          "shimmer, tape warble, bitcrusher, looper and more. It goes in front of the amp; drag it anywhere.");
    addButton.onClick = [this] { showAddMenu(); };
    addAndMakeVisible (addButton);
    rig.boardChanged.addChangeListener (this);
    refresh();
    startTimerHz (10);
}

PedalboardStrip::~PedalboardStrip()
{
    rig.boardChanged.removeChangeListener (this);
}

void PedalboardStrip::refresh()
{
    items = rig.getBoard();
    pressed = hover = -1;
    dragging = false;
    layoutChips();
    repaint();
}

void PedalboardStrip::timerCallback()
{
    bool changed = false;
    for (auto& it : items)
    {
        const bool on = rig.isBoardItemOn (it.key);
        if (on != it.on) { it.on = on; changed = true; }
    }
    if (changed) repaint();
}

void PedalboardStrip::resized()
{
    auto r = getLocalBounds();
    addButton.setBounds (r.removeFromRight (112).reduced (0, 4));
    r.removeFromRight (8);
    chainArea = r;
    layoutChips();
}

void PedalboardStrip::layoutChips()
{
    chips.clear();
    auto r = chainArea.reduced (4, 4);
    if (r.isEmpty()) return;
    inChip = r.removeFromLeft (30);
    r.removeFromLeft (gap);
    outChip = r.removeFromRight (36);
    r.removeFromRight (gap);

    float units = 0.0f;
    for (auto& it : items) units += (! it.isPedal && it.block == (int) RigBlock::ampCab) ? 1.5f : 1.0f;
    const int n = (int) items.size();
    const float avail = (float) (r.getWidth() - gap * juce::jmax (0, n - 1));
    const float unit = juce::jmin (112.0f, avail / juce::jmax (1.0f, units));
    int x = r.getX();
    for (auto& it : items)
    {
        const int w = juce::roundToInt (unit * ((! it.isPedal && it.block == (int) RigBlock::ampCab) ? 1.5f : 1.0f));
        chips.push_back ({ x, r.getY(), w, r.getHeight() });
        x += w + gap;
    }
}

juce::Rectangle<float> PedalboardStrip::ledArea (int i) const
{
    const auto c = chips[(size_t) i].toFloat();
    return { c.getX() + 5.0f, c.getCentreY() - 7.0f, 14.0f, 14.0f };
}

int PedalboardStrip::chipAt (juce::Point<int> p) const
{
    for (int i = 0; i < (int) chips.size(); ++i)
        if (chips[(size_t) i].contains (p)) return i;
    return -1;
}

int PedalboardStrip::insertIndexFor (int x) const
{
    int idx = 0;
    for (auto& c : chips)
        if (c.getCentreX() < x) ++idx;
    return idx;
}

void PedalboardStrip::paint (juce::Graphics& g)
{
    auto area = chainArea.toFloat();
    g.setColour (theme::bg.withAlpha (0.6f));
    g.fillRoundedRectangle (area, 7.0f);

    auto drawPill = [&] (juce::Rectangle<int> r, const juce::String& t)
    {
        g.setColour (theme::panelRaised);
        g.fillRoundedRectangle (r.toFloat(), 5.0f);
        g.setColour (theme::textFaint);
        g.setFont (uiFont (10.5f, true));
        g.drawText (t, r, juce::Justification::centred);
    };
    drawPill (inChip, "IN");
    drawPill (outChip, "OUT");

    auto arrow = [&] (int xLeft, int cy)
    {
        juce::Path p;
        const float x = (float) xLeft + gap * 0.5f;
        p.startNewSubPath (x - 3.0f, (float) cy - 4.0f);
        p.lineTo (x + 2.0f, (float) cy);
        p.lineTo (x - 3.0f, (float) cy + 4.0f);
        g.setColour (theme::textFaint);
        g.strokePath (p, juce::PathStrokeType (1.4f));
    };
    const int cy = chainArea.getCentreY();
    arrow (inChip.getRight(), cy);
    for (size_t i = 0; i < chips.size(); ++i)
        if (! (dragging && (int) i == pressed))
            arrow (chips[i].getRight(), cy);

    auto drawChip = [&] (const RigProcessor::BoardItem& it, juce::Rectangle<float> c, bool hot, float alpha)
    {
        const auto col = colourFor (it);
        const bool amp = ! it.isPedal && it.block == (int) RigBlock::ampCab;
        g.setColour ((it.on ? col.withAlpha (hot ? 0.34f : 0.22f) : theme::panelRaised.brighter (hot ? 0.08f : 0.0f)).withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (c, 6.0f);
        g.setColour ((it.on ? col : theme::outline.brighter (0.2f)).withMultipliedAlpha (alpha));
        g.drawRoundedRectangle (c.reduced (0.5f), 6.0f, amp ? 1.6f : 1.0f);
        if (it.isPedal)
        {
            // added pedals get a little "stomp switch" bar along the bottom
            g.setColour ((it.on ? col : theme::ledOff).withMultipliedAlpha (alpha * 0.8f));
            g.fillRoundedRectangle (c.withTop (c.getBottom() - 3.0f).reduced (8.0f, 0.0f), 1.5f);
        }
        const juce::Rectangle<float> led { c.getX() + 8.0f, c.getCentreY() - 4.0f, 8.0f, 8.0f };
        g.setColour ((it.on ? col.brighter (0.3f) : theme::ledOff).withMultipliedAlpha (alpha));
        g.fillEllipse (led);
        if (it.on)
        {
            g.setColour (col.withAlpha (0.35f * alpha));
            g.drawEllipse (led.expanded (2.0f), 1.5f);
        }
        g.setColour ((it.on ? theme::text : theme::textDim).withMultipliedAlpha (alpha));
        g.setFont (uiFont (amp ? 12.0f : 11.5f, true));
        g.drawFittedText (it.name, c.withTrimmedLeft (21.0f).withTrimmedRight (4.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
    };

    for (size_t i = 0; i < chips.size(); ++i)
    {
        if (dragging && (int) i == pressed) continue;
        drawChip (items[i], chips[i].toFloat(), (int) i == hover, 1.0f);
    }

    if (dragging && juce::isPositiveAndBelow (pressed, (int) items.size()))
    {
        // where it will land
        int x = dragInsert >= (int) chips.size() ? chips.back().getRight() + gap / 2 : chips[(size_t) dragInsert].getX() - gap / 2;
        if (dragInsert <= 0) x = chips.front().getX() - gap / 2;
        g.setColour (theme::accent);
        g.fillRoundedRectangle ((float) x - 1.5f, (float) chainArea.getY() + 3.0f, 3.0f, (float) chainArea.getHeight() - 6.0f, 1.5f);
        auto c = chips[(size_t) pressed].toFloat();
        drawChip (items[(size_t) pressed], c.withCentre ({ (float) dragPos.x, c.getCentreY() }).translated (0.0f, -2.0f), true, 0.9f);
    }
}

juce::String PedalboardStrip::getTooltip()
{
    if (! juce::isPositiveAndBelow (hover, (int) items.size()))
        return "Your signal chain, left to right. Drag pedals to re-order them; pedals before the amp go into its input, "
               "pedals after it sit in its effects loop.";
    const auto& it = items[(size_t) hover];
    if (it.isPedal)
    {
        const auto* info = daw::findBuiltin (it.builtinId);
        return it.name + (info != nullptr ? ": " + info->description : juce::String()) + "\nClick to open its knobs, click the light to switch it on / off, drag to move it, right-click to remove it.";
    }
    if (it.block == (int) RigBlock::ampCab)
        return "The amp and its cabinet. Pedals to the left go into the amp's input; pedals to the right are in its effects loop.\nDrag to move, click the light to switch on / off.";
    return it.name + ": click to show its knobs below, click the light to switch it on / off, drag to move it.";
}

void PedalboardStrip::mouseMove (const juce::MouseEvent& e)
{
    const int h = chipAt (e.getPosition());
    if (h != hover) { hover = h; repaint(); }
    setMouseCursor (h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
}

void PedalboardStrip::mouseExit (const juce::MouseEvent&)
{
    if (hover != -1) { hover = -1; repaint(); }
}

void PedalboardStrip::mouseDown (const juce::MouseEvent& e)
{
    pressed = chipAt (e.getPosition());
    dragging = false;
    if (e.mods.isPopupMenu())
    {
        if (pressed >= 0) showItemMenu (pressed);
        else showAddMenu (insertIndexFor (e.x));
        pressed = -1;
    }
}

void PedalboardStrip::mouseDrag (const juce::MouseEvent& e)
{
    if (pressed < 0 || e.mods.isPopupMenu()) return;
    if (! dragging && e.getDistanceFromDragStart() > 5) dragging = true;
    if (dragging)
    {
        dragPos = e.getPosition();
        dragInsert = insertIndexFor (e.x);
        repaint();
    }
}

void PedalboardStrip::mouseUp (const juce::MouseEvent& e)
{
    const int i = pressed;
    const bool wasDragging = dragging;
    pressed = -1;
    dragging = false;
    if (i < 0 || e.mods.isPopupMenu()) { repaint(); return; }

    if (wasDragging)
    {
        const int to = dragInsert > i ? dragInsert - 1 : dragInsert;
        repaint();
        if (to != i) rig.moveBoardItem (i, to);
        return;
    }

    const auto it = items[(size_t) i];
    if (ledArea (i).contains (e.position))
    {
        rig.setBoardItemOn (it.key, ! it.on);
        items[(size_t) i].on = ! it.on;
        repaint();
    }
    else if (it.isPedal)
    {
        if (onOpenPedal) onOpenPedal (it.key);
    }
    else if (onShowBlock)
    {
        onShowBlock (it.key);
    }
}

void PedalboardStrip::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int i = chipAt (e.getPosition());
    if (i >= 0 && items[(size_t) i].isPedal && ! ledArea (i).contains (e.position) && onOpenPedal)
        onOpenPedal (items[(size_t) i].key);
}

void PedalboardStrip::showAddMenu (int insertAt)
{
    const auto ids = RigProcessor::pedalIds();
    int pedals = 0;
    for (auto& it : items) if (it.isPedal) ++pedals;
    const bool full = pedals >= RigProcessor::maxPedals;

    juce::PopupMenu menu;
    if (full)
        menu.addSectionHeader ("The board is full (" + juce::String (RigProcessor::maxPedals) + " pedals): remove one first");

    juce::StringArray cats (categoryOrder);
    for (auto& id : ids)
        if (auto* info = daw::findBuiltin (id))
            if (! cats.contains (info->category)) cats.add (info->category);

    for (auto& cat : cats)
    {
        juce::PopupMenu sub;
        for (int k = 0; k < ids.size(); ++k)
            if (auto* info = daw::findBuiltin (ids[k]); info != nullptr && info->category == cat)
                sub.addItem (k + 1, info->name, ! full);
        if (sub.getNumItems() == 0) continue;
        if (cat == "Pedals")
        {
            menu.addSectionHeader (categoryTitle (cat));
            for (int k = 0; k < ids.size(); ++k)
                if (auto* info = daw::findBuiltin (ids[k]); info != nullptr && info->category == cat)
                    menu.addItem (k + 1, info->name, ! full);
            menu.addSeparator();
        }
        else
        {
            menu.addSubMenu (cat, sub);
        }
    }

    juce::Component::SafePointer<PedalboardStrip> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (insertAt < 0 ? (juce::Component*) &addButton : this)
                                                   .withMousePosition(),
        [safe, ids, insertAt] (int result)
        {
            if (safe == nullptr || result <= 0) return;
            const auto uid = safe->rig.addPedal (ids[result - 1], insertAt);
            if (uid.isNotEmpty() && safe->onOpenPedal) safe->onOpenPedal (uid);
        });
}

void PedalboardStrip::showItemMenu (int index)
{
    const auto it = items[(size_t) index];
    const int last = (int) items.size() - 1;
    juce::PopupMenu menu;
    menu.addSectionHeader (it.name);
    if (it.isPedal) menu.addItem (1, "Show knobs...");
    else            menu.addItem (1, "Show its knobs");
    menu.addItem (2, it.on ? "Switch off" : "Switch on");
    menu.addSeparator();
    menu.addItem (3, "Move to the start", index > 0);
    menu.addItem (4, "Move to the end", index < last);
    menu.addItem (5, "Add a pedal before this...");
    menu.addItem (6, "Add a pedal after this...");
    if (it.isPedal)
    {
        menu.addSeparator();
        menu.addItem (7, "Duplicate");
        menu.addItem (8, "Remove");
    }
    menu.addSeparator();
    menu.addItem (9, "Reset the board (standard order, no added pedals)");

    juce::Component::SafePointer<PedalboardStrip> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
        [safe, it, index, last] (int r)
        {
            if (safe == nullptr) return;
            auto& rg = safe->rig;
            switch (r)
            {
                case 1: if (it.isPedal) { if (safe->onOpenPedal) safe->onOpenPedal (it.key); } else if (safe->onShowBlock) safe->onShowBlock (it.key); break;
                case 2: rg.setBoardItemOn (it.key, ! it.on); break;
                case 3: rg.moveBoardItem (index, 0); break;
                case 4: rg.moveBoardItem (index, last); break;
                case 5: safe->showAddMenu (index); break;
                case 6: safe->showAddMenu (index + 1); break;
                case 7:
                    if (auto* p = rg.getPedalProcessor (it.key))
                        rg.addPedal (it.builtinId, index + 1, daw::encodeState (*p));
                    break;
                case 8: rg.removePedal (it.key); break;
                case 9:
                    if (juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Reset the pedalboard?",
                                                            "Puts the rig's own blocks back in the standard order and removes the pedals you added.",
                                                            "Reset", "Cancel", safe.getComponent()))
                        rg.resetBoard();
                    break;
                default: break;
            }
        });
}

} // namespace wis
