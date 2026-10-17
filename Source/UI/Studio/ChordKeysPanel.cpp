#include "ChordKeysPanel.h"
#include "UI/LookAndFeel.h"

namespace wis::daw
{

using namespace chordkeys;

namespace
{
    constexpr int sideW = 340;
    const float rowOffsets[] = { 0.0f, 0.5f, 0.78f, 1.28f };

    juce::Colour qualityColour (const Slot& s)
    {
        if (! s.used) return theme::textFaint;
        const juce::String suf = types()[(size_t) juce::jlimit (0, (int) types().size() - 1, s.type)].suffix;
        if (suf == "dim" || suf == "dim7" || suf == "m7b5" || suf == "aug" || suf == "7#5") return juce::Colour (0xffb48cff);
        if (suf.startsWith ("m") && ! suf.startsWith ("maj")) return juce::Colour (0xff6fa8ff);
        if (suf == "7" || suf == "9" || suf == "11" || suf == "13" || suf == "7b9" || suf == "7#9" || suf == "7sus4") return juce::Colour (0xffff9a4d);
        if (suf.startsWith ("sus") || suf == "5" || suf.startsWith ("(")) return juce::Colour (0xff5fd3a0);
        return juce::Colour (0xfff2c14e);
    }

    juce::String noteList (const std::vector<int>& notes)
    {
        juce::StringArray s;
        for (int n : notes) s.add (juce::MidiMessage::getMidiNoteName (n, false, true, 4));
        return s.joinIntoString (" ");
    }
}

ChordKeysPanel::ChordKeysPanel (ChordKeysState& s) : state (s)
{
    state.addChangeListener (this);

    auto setupButton = [this] (juce::Button& b)
    {
        b.setWantsKeyboardFocus (false);
        b.setMouseClickGrabsKeyboardFocus (false);
        addAndMakeVisible (b);
    };
    for (auto* b : std::initializer_list<juce::Button*> { &onButton, &bankButton, &octDown, &octUp, &trDown, &trUp, &followKey, &voiceLead, &bassNote, &latch,
                                                          &slotOctDown, &slotOctUp, &playB, &clearB, &copyB, &pasteB, &fillB })
        setupButton (*b);

    onButton.setClickingTogglesState (false);
    onButton.setColour (juce::TextButton::buttonOnColourId, theme::accent2);
    onButton.onClick = [this] { if (onToggleEnabled) onToggleEnabled(); };
    onButton.setTooltip ("Play chords from your computer keyboard (Ctrl+Shift+K). Musical Typing turns on with it.");

    bankBox.setWantsKeyboardFocus (false);
    bankBox.onChange = [this]
    {
        if (updating) return;
        const int i = bankBox.getSelectedItemIndex();
        if (juce::isPositiveAndBelow (i, (int) state.banks.size())) { state.current = i; state.changed(); }
    };
    addAndMakeVisible (bankBox);
    bankButton.onClick = [this] { bankMenu(); };

    for (auto* l : { &octLabel, &trLabel, &slotOctLabel })
    {
        l->setJustificationType (juce::Justification::centred);
        l->setFont (uiFont (13.0f, true));
        l->setColour (juce::Label::textColourId, theme::text);
        l->setInterceptsMouseClicks (false, false);
        addAndMakeVisible (l);
    }
    octDown.onClick = [this] { state.octave = juce::jmax (1, state.octave - 1); state.changed(); };
    octUp.onClick = [this] { state.octave = juce::jmin (7, state.octave + 1); state.changed(); };
    trDown.onClick = [this] { state.transpose = juce::jmax (-11, state.transpose - 1); state.changed(); };
    trUp.onClick = [this] { state.transpose = juce::jmin (11, state.transpose + 1); state.changed(); };
    octDown.setTooltip ("Chords an octave lower ( [ )");
    octUp.setTooltip ("Chords an octave higher ( ] )");
    trDown.setTooltip ("Transpose every chord down a semitone ( - )");
    trUp.setTooltip ("Transpose every chord up a semitone ( = )");
    followKey.onClick = [this] { state.followKey = followKey.getToggleState(); state.changed(); };
    followKey.setTooltip ("Each bank is written in a home key; this moves it to the song's key (set in the LCD / Project menu)");
    voiceLead.onClick = [this] { state.voiceLeading = voiceLead.getToggleState(); state.changed(); };
    voiceLead.setTooltip ("Each chord picks the inversion closest to the one before, so changes move smoothly like a keyboard player's hands");
    bassNote.onClick = [this] { state.bassNote = bassNote.getToggleState(); state.changed(); };
    bassNote.setTooltip ("Adds each chord's root an octave below (slash chords use their own bass note anyway)");
    latch.onClick = [this] { state.latch = latch.getToggleState(); state.changed(); };
    latch.setTooltip ("Chords keep sounding after you let go, until you press the next one");

    velocity.setRange (1.0, 127.0, 1.0);
    velocity.onValueChange = [this] { if (! updating) { state.velocity = (int) velocity.getValue(); state.save(); } };
    strum.setRange (0.0, 120.0, 1.0);
    strum.setTextValueSuffix (" ms");
    strum.onValueChange = [this] { if (! updating) { state.strumMs = (float) strum.getValue(); state.save(); } };
    strum.setTooltip ("Spreads the notes of each chord out like a strum or a rolled piano chord (0 = all together)");
    for (auto* sl : { &velocity, &strum })
    {
        sl->setWantsKeyboardFocus (false);
        sl->setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 20);
        sl->setDoubleClickReturnValue (true, sl == &velocity ? 100.0 : 0.0);
        addAndMakeVisible (sl);
    }
    strumDir.addItemList ({ "Strum up", "Strum down", "Up / down" }, 1);
    strumDir.setWantsKeyboardFocus (false);
    strumDir.onChange = [this] { if (! updating) { state.strumDir = strumDir.getSelectedItemIndex(); state.save(); } };
    addAndMakeVisible (strumDir);

    // the chord on the selected key
    for (int pc = 0; pc < 12; ++pc)
    {
        auto* b = rootButtons.add (new juce::TextButton (pitchName (pc)));
        b->setClickingTogglesState (false);
        b->setColour (juce::TextButton::buttonOnColourId, theme::accent);
        b->onClick = [this, pc] { editSlot ([pc] (Slot& s) { s.root = pc; }); };
        setupButton (*b);
    }
    const char* inv[] = { "Root", "1st inv", "2nd inv", "3rd inv" };
    for (int i = 0; i < 4; ++i)
    {
        auto* b = invButtons.add (new juce::TextButton (inv[i]));
        b->setClickingTogglesState (false);
        b->setColour (juce::TextButton::buttonOnColourId, theme::accent2);
        b->onClick = [this, i] { editSlot ([i] (Slot& s) { s.inversion = i; }); };
        setupButton (*b);
    }
    {
        juce::String group;
        for (int t = 0; t < (int) types().size(); ++t)
        {
            if (group != types()[(size_t) t].group) { group = types()[(size_t) t].group; typeBox.addSectionHeading (group); }
            const juce::String suf = types()[(size_t) t].suffix;
            typeBox.addItem (juce::String (types()[(size_t) t].name) + (suf.isEmpty() || suf.startsWith ("(") ? juce::String() : "   (" + suf + ")"), t + 1);
        }
    }
    typeBox.onChange = [this] { if (! updating && typeBox.getSelectedId() > 0) { const int t = typeBox.getSelectedId() - 1; editSlot ([t] (Slot& s) { s.type = t; }); } };
    voicingBox.addItemList (voicingNames(), 1);
    voicingBox.onChange = [this] { if (! updating) { const int v = voicingBox.getSelectedItemIndex(); editSlot ([v] (Slot& s) { s.voicing = v; }); } };
    bassBox.addItem ("No bass note", 1);
    for (int pc = 0; pc < 12; ++pc) bassBox.addItem ("Bass note: " + pitchName (pc), pc + 2);
    bassBox.onChange = [this] { if (! updating) { const int b = bassBox.getSelectedId() - 2; editSlot ([b] (Slot& s) { s.bass = b; }); } };
    for (auto* c : { &typeBox, &voicingBox, &bassBox })
    {
        c->setWantsKeyboardFocus (false);
        addAndMakeVisible (c);
    }
    slotOctDown.onClick = [this] { editSlot ([] (Slot& s) { s.octave = juce::jmax (-3, s.octave - 1); }); };
    slotOctUp.onClick = [this] { editSlot ([] (Slot& s) { s.octave = juce::jmin (3, s.octave + 1); }); };
    playB.onClick = [this] { if (onAudition) onAudition (selected); };
    playB.setTooltip ("Hear it (or double-click the key)");
    clearB.onClick = [this] { state.bank().slots[(size_t) selected] = Slot(); state.changed(); };
    copyB.onClick = [this] { clipboard = slot(); };
    pasteB.onClick = [this] { if (clipboard.used) { slot() = clipboard; state.changed(); if (onAudition) onAudition (selected); } };
    fillB.onClick = [this] { fillMenu(); };
    fillB.setTooltip ("Fill this key's row with the chords of a key, or clear it");

    entry.setFont (uiFont (14.0f, true));
    entry.setTextToShowWhenEmpty ("type a chord: Am7, F#m7b5, C/G...", theme::textFaint);
    entry.setColour (juce::TextEditor::backgroundColourId, theme::panelRaised);
    entry.setColour (juce::TextEditor::textColourId, theme::text);
    entry.setColour (juce::TextEditor::outlineColourId, theme::outline);
    entry.setColour (juce::TextEditor::focusedOutlineColourId, theme::accent);
    entry.onReturnKey = [this]
    {
        auto parsed = parse (entry.getText());
        if (parsed.used)
        {
            const int tr = transposeNow();
            parsed.root = ((parsed.root - tr) % 12 + 12) % 12;
            if (parsed.bass >= 0) parsed.bass = ((parsed.bass - tr) % 12 + 12) % 12;
            const auto old = slot();
            parsed.octave = old.used ? old.octave : (parsed.root >= 7 ? -1 : 0);
            parsed.voicing = old.voicing;
            slot() = parsed;
            state.changed();
            if (onAudition) onAudition (selected);
        }
        else if (entry.getText().trim().isEmpty()) { slot() = Slot(); state.changed(); }
        else entry.setText (chordName (slot(), transposeNow(), spell()), false);
        if (onFocusBack) onFocusBack();
    };
    entry.onEscapeKey = [this] { entry.setText (chordName (slot(), transposeNow(), spell()), false); if (onFocusBack) onFocusBack(); };
    entry.onFocusLost = [this] { entry.setText (chordName (slot(), transposeNow(), spell()), false); };
    addAndMakeVisible (entry);

    refresh();
}

ChordKeysPanel::~ChordKeysPanel()
{
    state.removeChangeListener (this);
}

void ChordKeysPanel::setHeld (const std::set<int>& slots)
{
    if (slots == held) return;
    held = slots;
    repaint (keyArea.toNearestInt().expanded (4));
}

void ChordKeysPanel::selectSlot (int s)
{
    if (! juce::isPositiveAndBelow (s, numKeys) || s == selected) return;
    selected = s;
    refresh();
}

int ChordKeysPanel::spell() const
{
    const auto k = songKey ? songKey() : std::pair<int, int> { 0, 0 };
    return state.spelling (k.first, k.second);
}

int ChordKeysPanel::transposeNow() const
{
    const auto k = songKey ? songKey() : std::pair<int, int> { 0, 0 };
    return state.effectiveTranspose (k.first, k.second);
}

void ChordKeysPanel::editSlot (std::function<void (Slot&)> change)
{
    auto& s = slot();
    if (! s.used) { s = Slot(); s.used = true; s.root = 0; s.type = 0; }
    change (s);
    state.changed();
    if (onAudition) onAudition (selected);
}

void ChordKeysPanel::refresh()
{
    updating = true;
    onButton.setButtonText (state.enabled ? "Chord Keys: ON" : "Chord Keys: OFF");
    onButton.setToggleState (state.enabled, juce::dontSendNotification);
    bankBox.clear (juce::dontSendNotification);
    for (int i = 0; i < (int) state.banks.size(); ++i) bankBox.addItem (state.banks[(size_t) i].name, i + 1);
    bankBox.setSelectedItemIndex (state.current, juce::dontSendNotification);
    octLabel.setText ("Octave C" + juce::String (state.octave), juce::dontSendNotification);
    const int tr = transposeNow();
    trLabel.setText ("Transpose " + juce::String (state.transpose > 0 ? "+" : "") + juce::String (state.transpose), juce::dontSendNotification);
    followKey.setToggleState (state.followKey, juce::dontSendNotification);
    {
        const auto k = songKey ? songKey() : std::pair<int, int> { 0, 0 };
        followKey.setButtonText ("Follow the song's key (" + pitchName (k.first) + (k.second == 1 ? " minor" : " major") + ")");
    }
    voiceLead.setToggleState (state.voiceLeading, juce::dontSendNotification);
    bassNote.setToggleState (state.bassNote, juce::dontSendNotification);
    latch.setToggleState (state.latch, juce::dontSendNotification);
    velocity.setValue (state.velocity, juce::dontSendNotification);
    strum.setValue (state.strumMs, juce::dontSendNotification);
    strumDir.setSelectedItemIndex (state.strumDir, juce::dontSendNotification);

    const auto& s = state.bank().slots[(size_t) selected];
    for (int pc = 0; pc < 12; ++pc)
    {
        rootButtons[pc]->setToggleState (s.used && s.root == pc, juce::dontSendNotification);
        rootButtons[pc]->setButtonText (pitchName (pc + tr, spell()));
    }
    const int nNotes = s.used ? (int) types()[(size_t) s.type].intervals.size() : 3;
    for (int i = 0; i < 4; ++i)
    {
        invButtons[i]->setToggleState (s.used && s.inversion == i, juce::dontSendNotification);
        invButtons[i]->setEnabled (i < nNotes);
    }
    typeBox.setSelectedId (s.used ? s.type + 1 : 0, juce::dontSendNotification);
    voicingBox.setSelectedItemIndex (s.voicing, juce::dontSendNotification);
    bassBox.setSelectedId (s.bass + 2, juce::dontSendNotification);
    slotOctLabel.setText (s.octave == 0 ? juce::String ("Octave 0") : "Octave " + juce::String (s.octave > 0 ? "+" : "") + juce::String (s.octave), juce::dontSendNotification);
    if (! entry.hasKeyboardFocus (false)) entry.setText (chordName (s, tr, spell()), false);
    updating = false;
    repaint();
}

// ---- layout ------------------------------------------------------------------------------------------------

void ChordKeysPanel::resized()
{
    auto r = getLocalBounds().reduced (10, 6);
    auto row1 = r.removeFromTop (26);
    onButton.setBounds (row1.removeFromLeft (150)); row1.removeFromLeft (10);
    bankBox.setBounds (row1.removeFromLeft (260)); row1.removeFromLeft (4);
    bankButton.setBounds (row1.removeFromLeft (72)); row1.removeFromLeft (16);
    octDown.setBounds (row1.removeFromLeft (24)); octLabel.setBounds (row1.removeFromLeft (92)); octUp.setBounds (row1.removeFromLeft (24)); row1.removeFromLeft (12);
    trDown.setBounds (row1.removeFromLeft (24)); trLabel.setBounds (row1.removeFromLeft (104)); trUp.setBounds (row1.removeFromLeft (24)); row1.removeFromLeft (12);
    followKey.setBounds (row1);
    r.removeFromTop (4);
    auto row2 = r.removeFromTop (24);
    row2.removeFromLeft (60);
    velocity.setBounds (row2.removeFromLeft (170)); row2.removeFromLeft (14);
    row2.removeFromLeft (44);
    strum.setBounds (row2.removeFromLeft (170)); row2.removeFromLeft (4);
    strumDir.setBounds (row2.removeFromLeft (104)); row2.removeFromLeft (14);
    voiceLead.setBounds (row2.removeFromLeft (180));
    bassNote.setBounds (row2.removeFromLeft (150));
    latch.setBounds (row2.removeFromLeft (250));
    r.removeFromTop (8);
    r.removeFromBottom (18);   // hint

    auto side = r.removeFromRight (sideW);
    r.removeFromRight (12);
    keyArea = r.toFloat();

    side.removeFromTop (20);   // title
    entry.setBounds (side.removeFromTop (24)); side.removeFromTop (4);
    for (int row = 0; row < 2; ++row)
    {
        auto rr = side.removeFromTop (20);
        const int w = rr.getWidth() / 6;
        for (int k = 0; k < 6; ++k) rootButtons[row * 6 + k]->setBounds (rr.removeFromLeft (w).reduced (1, 0));
        side.removeFromTop (2);
    }
    side.removeFromTop (2);
    typeBox.setBounds (side.removeFromTop (22)); side.removeFromTop (4);
    {
        auto rr = side.removeFromTop (20);
        const int w = rr.getWidth() / 4;
        for (int k = 0; k < 4; ++k) invButtons[k]->setBounds (rr.removeFromLeft (w).reduced (1, 0));
    }
    side.removeFromTop (4);
    {
        auto rr = side.removeFromTop (22);
        slotOctDown.setBounds (rr.removeFromLeft (24)); slotOctLabel.setBounds (rr.removeFromLeft (80)); slotOctUp.setBounds (rr.removeFromLeft (24));
        rr.removeFromLeft (8);
        voicingBox.setBounds (rr);
    }
    side.removeFromTop (4);
    bassBox.setBounds (side.removeFromTop (22));
    side.removeFromTop (5);
    {
        auto rr = side.removeFromTop (juce::jlimit (16, 24, side.getHeight()));
        const int w = (rr.getWidth() - 4) / 5;
        for (auto* b : { &playB, &clearB, &copyB, &pasteB, &fillB }) b->setBounds (rr.removeFromLeft (b == &fillB ? rr.getWidth() : w).reduced (1, 0));
    }
}

juce::Rectangle<float> ChordKeysPanel::keyBounds (int index) const
{
    const int row = index / keysPerRow, col = index % keysPerRow;
    const float kw = juce::jmin (keyArea.getWidth() / ((float) keysPerRow + rowOffsets[3] + 0.1f), 120.0f);
    const float kh = juce::jmin (keyArea.getHeight() / 4.0f, kw * 0.9f);
    return { keyArea.getX() + (rowOffsets[row] + (float) col) * kw, keyArea.getY() + (float) row * kh, kw, kh };
}

int ChordKeysPanel::keyAt (juce::Point<float> p) const
{
    for (int i = 0; i < numKeys; ++i)
        if (keyBounds (i).reduced (2.0f).contains (p)) return i;
    return -1;
}

void ChordKeysPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    auto r = getLocalBounds().reduced (10, 6);
    g.setFont (uiFont (12.5f));
    g.setColour (theme::textDim);
    g.drawText ("Velocity", juce::Rectangle<int> (r.getX(), r.getY() + 30, 58, 24), juce::Justification::centredLeft);
    g.drawText ("Strum", juce::Rectangle<int> (r.getX() + 60 + 170 + 14, r.getY() + 30, 44, 24), juce::Justification::centredLeft);

    const int tr = transposeNow();
    const auto opts = [&]
    {
        const auto k = songKey ? songKey() : std::pair<int, int> { 0, 0 };
        return state.options (k.first, k.second);
    }();

    // the keyboard
    for (int i = 0; i < numKeys; ++i)
    {
        const auto b = keyBounds (i).reduced (3.0f);
        const auto& s = state.bank().slots[(size_t) i];
        const bool on = held.count (i) > 0;
        const auto qc = qualityColour (s);
        if (dragging && i == dragFrom) continue;
        g.setColour (on ? qc : (s.used ? qc.withAlpha (0.16f).overlaidWith (theme::panelRaised.withAlpha (0.55f)) : theme::panelRaised.withAlpha (0.5f)));
        g.fillRoundedRectangle (b, 6.0f);
        g.setColour (i == selected ? theme::accent : (s.used ? qc.withAlpha (0.55f) : theme::outline));
        g.drawRoundedRectangle (b.reduced (0.5f), 6.0f, i == selected ? 2.2f : 1.0f);
        // key letter
        g.setColour (on ? juce::Colours::black.withAlpha (0.7f) : theme::textFaint);
        g.setFont (uiFont (juce::jlimit (9.0f, 13.0f, b.getHeight() * 0.2f), true));
        g.drawText (keyLabel (i), b.reduced (6.0f, 4.0f), juce::Justification::topLeft);
        if (! s.used) continue;
        // chord name
        const auto name = chordName (s, tr, spell());
        g.setColour (on ? juce::Colours::black : theme::text);
        float fs = juce::jlimit (10.0f, 22.0f, b.getHeight() * 0.32f);
        g.setFont (uiFont (fs, true));
        while (fs > 9.0f && juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), name) > b.getWidth() - 8.0f) { fs -= 1.0f; g.setFont (uiFont (fs, true)); }
        g.drawText (name, b.reduced (3.0f).withTrimmedTop (b.getHeight() * 0.12f), juce::Justification::centred, false);
        if (s.inversion > 0 || s.voicing > 0 || s.octave != 0)
        {
            juce::String tag;
            if (s.inversion > 0) tag << "inv" << s.inversion << " ";
            if (s.octave != 0) tag << (s.octave > 0 ? "+" : "") << s.octave << "oct ";
            if (s.voicing > 0) tag << juce::String (voicingNames()[s.voicing]).upToFirstOccurrenceOf (" ", false, false).toLowerCase();
            g.setFont (uiFont (juce::jlimit (8.0f, 11.0f, b.getHeight() * 0.16f)));
            g.setColour (on ? juce::Colours::black.withAlpha (0.6f) : theme::textFaint);
            g.drawText (tag.trim(), b.reduced (5.0f, 3.0f), juce::Justification::bottomRight);
        }
    }
    if (dragging && juce::isPositiveAndBelow (dragFrom, numKeys))
    {
        const auto& s = state.bank().slots[(size_t) dragFrom];
        auto b = keyBounds (dragFrom).reduced (3.0f).withCentre (dragPos);
        g.setColour (qualityColour (s).withAlpha (0.85f));
        g.fillRoundedRectangle (b, 6.0f);
        g.setColour (juce::Colours::black);
        g.setFont (uiFont (juce::jlimit (10.0f, 20.0f, b.getHeight() * 0.3f), true));
        g.drawText (chordName (s, tr, spell()), b, juce::Justification::centred);
        const int over = keyAt (dragPos);
        if (over >= 0 && over != dragFrom)
        {
            g.setColour (theme::accent);
            g.drawRoundedRectangle (keyBounds (over).reduced (2.0f), 6.0f, 2.5f);
        }
    }

    // the selected key, in words and notes
    auto side = getLocalBounds().reduced (10, 6);
    side.removeFromTop (26 + 4 + 24 + 8);
    side = side.removeFromRight (sideW);
    const auto& s = state.bank().slots[(size_t) selected];
    g.setColour (theme::text);
    g.setFont (uiFont (14.0f, true));
    g.drawText ("Key " + keyLabel (selected) + (s.used ? ":  " + chordName (s, tr, spell()) : juce::String (":  (empty)")), side.removeFromTop (20), juce::Justification::centredLeft);
    g.setColour (theme::textDim);
    g.setFont (uiFont (11.5f));
    if (s.used)
        g.drawText (noteList (notesFor (s, [&] { auto o = opts; o.voiceLeading = false; return o; }())), getLocalBounds().reduced (10, 6).removeFromRight (sideW).withY (side.getY() - 20).withHeight (20),
                    juce::Justification::centredRight);

    g.setColour (theme::textFaint);
    g.setFont (uiFont (11.5f));
    g.drawText (juce::String (state.enabled ? "Chord Keys are ON: " : "Turn Chord Keys on (Ctrl+Shift+K), then ")
                + "each key plays its chord into the selected instrument track.   [ ]  octave    -  =  transpose.   Click a key to edit it, drag it onto another to swap (Alt-drag copies), double-click to hear it, right-click for more.",
                getLocalBounds().reduced (10, 2).removeFromBottom (18), juce::Justification::centredLeft, true);
}

// ---- mouse -------------------------------------------------------------------------------------------------

void ChordKeysPanel::mouseDown (const juce::MouseEvent& e)
{
    const int k = keyAt (e.position);
    if (k < 0) return;
    if (e.mods.isPopupMenu()) { selectSlot (k); slotMenu (k); return; }
    dragFrom = k;
    dragPos = e.position;
    dragging = false;
    selected = k;
    refresh();
    if (onFocusBack) onFocusBack();
}

void ChordKeysPanel::mouseDrag (const juce::MouseEvent& e)
{
    if (dragFrom < 0 || ! state.bank().slots[(size_t) dragFrom].used) return;
    if (e.getDistanceFromDragStart() > 6) dragging = true;
    dragPos = e.position;
    if (dragging) repaint (keyArea.toNearestInt().expanded (60));
}

void ChordKeysPanel::mouseUp (const juce::MouseEvent& e)
{
    if (dragging)
    {
        const int to = keyAt (e.position);
        if (to >= 0 && to != dragFrom)
        {
            auto& slots = state.bank().slots;
            if (e.mods.isAltDown() || e.mods.isCommandDown()) slots[(size_t) to] = slots[(size_t) dragFrom];
            else std::swap (slots[(size_t) to], slots[(size_t) dragFrom]);
            selected = to;
            state.changed();
        }
    }
    dragging = false;
    dragFrom = -1;
    repaint();
}

void ChordKeysPanel::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int k = keyAt (e.position);
    if (k >= 0 && onAudition) onAudition (k);
}

// ---- menus -------------------------------------------------------------------------------------------------

void ChordKeysPanel::slotMenu (int index)
{
    juce::PopupMenu m;
    const bool used = state.bank().slots[(size_t) index].used;
    m.addSectionHeader ("Key " + keyLabel (index));
    m.addItem (1, "Play", used);
    m.addItem (2, "Copy", used);
    m.addItem (3, "Paste", clipboard.used);
    m.addItem (4, "Clear", used);
    m.addSeparator();
    m.addItem (5, "Type a chord name...");
    m.showMenuAsync (juce::PopupMenu::Options(), [safe = juce::Component::SafePointer<ChordKeysPanel> (this), index] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& slots = safe->state.bank().slots;
        switch (r)
        {
            case 1: if (safe->onAudition) safe->onAudition (index); break;
            case 2: safe->clipboard = slots[(size_t) index]; break;
            case 3: slots[(size_t) index] = safe->clipboard; safe->state.changed(); break;
            case 4: slots[(size_t) index] = Slot(); safe->state.changed(); break;
            case 5: safe->entry.grabKeyboardFocus(); safe->entry.selectAll(); break;
            default: break;
        }
    });
}

void ChordKeysPanel::fillMenu()
{
    const int row = selected / keysPerRow;
    juce::PopupMenu m;
    auto& b = state.bank();
    const juce::String home = pitchName (b.tonic) + (b.minor ? " minor" : " major");
    m.addSectionHeader ("Row " + juce::String (row + 1) + " (" + keyLabel (row * keysPerRow) + " to " + keyLabel (row * keysPerRow + 9) + "), in the bank's key, " + home);
    m.addItem (1, "Triads of the key  (I ii iii IV V vi vii, + I V7 IV)");
    m.addItem (2, "Seventh chords of the key");
    m.addItem (3, "Sus, add9 and colour chords of the key");
    m.addSeparator();
    m.addItem (4, "Clear this row");
    m.addItem (5, "Clear the whole bank");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&fillB), [safe = juce::Component::SafePointer<ChordKeysPanel> (this), row] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& bk = safe->state.bank();
        if (r <= 3) fillRow (bk, row, bk.tonic, bk.minor, r - 1);
        else if (r == 4) for (int k = 0; k < keysPerRow; ++k) bk.slots[(size_t) (row * keysPerRow + k)] = Slot();
        else for (auto& s : bk.slots) s = Slot();
        safe->state.changed();
    });
}

void ChordKeysPanel::bankMenu()
{
    juce::PopupMenu m, home;
    m.addItem (1, "New Empty Bank");
    m.addItem (2, "Duplicate This Bank");
    m.addItem (3, "Rename This Bank...");
    m.addItem (4, "Delete This Bank", state.banks.size() > 1);
    for (int minor = 0; minor < 2; ++minor)
        for (int pc = 0; pc < 12; ++pc)
            home.addItem (100 + minor * 12 + pc, pitchName (pc) + (minor ? " minor" : " major"), true, state.bank().tonic == pc && state.bank().minor == (minor == 1));
    m.addSubMenu ("This Bank's Home Key (for Follow the song's key)", home);
    m.addSeparator();
    m.addItem (5, "Save This Bank to a File...");
    m.addItem (6, "Load a Bank from a File...");
    m.addSeparator();
    m.addItem (7, "Reset All Banks to the Factory Banks...");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&bankButton), [safe = juce::Component::SafePointer<ChordKeysPanel> (this)] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& st = safe->state;
        if (r >= 100)
        {
            st.bank().tonic = (r - 100) % 12;
            st.bank().minor = r >= 112;
            st.changed();
            return;
        }
        switch (r)
        {
            case 1: { Bank b; b.name = "My Chords " + juce::String ((int) st.banks.size() + 1); st.banks.push_back (b); st.current = (int) st.banks.size() - 1; st.changed(); break; }
            case 2: { auto b = st.bank(); b.name << " (copy)"; st.banks.insert (st.banks.begin() + st.current + 1, b); ++st.current; st.changed(); break; }
            case 3: safe->renameBank(); break;
            case 4: st.banks.erase (st.banks.begin() + st.current); st.current = juce::jmax (0, st.current - 1); st.changed(); break;
            case 5: safe->saveBank(); break;
            case 6: safe->loadBank(); break;
            case 7:
                juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Reset chord banks",
                                                    "Put back the factory chord banks? Banks you made or changed will be lost (save them to files first if you want to keep them).",
                                                    "Reset", "Cancel", safe.getComponent(),
                                                    juce::ModalCallbackFunction::create ([safe] (int ok)
                                                    {
                                                        if (safe == nullptr || ok == 0) return;
                                                        safe->state.resetFactory();
                                                        safe->state.changed();
                                                    }));
                break;
            default: break;
        }
    });
}

void ChordKeysPanel::renameBank()
{
    auto* w = new juce::AlertWindow ("Rename chord bank", {}, juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", state.bank().name);
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<ChordKeysPanel> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int r)
    {
        if (safe == nullptr || r != 1) return;
        const auto name = w->getTextEditorContents ("name").trim();
        if (name.isEmpty()) return;
        safe->state.bank().name = name;
        safe->state.changed();
        if (safe->onFocusBack) safe->onFocusBack();
    }), true);
}

void ChordKeysPanel::saveBank()
{
    chooser = std::make_unique<juce::FileChooser> ("Save chord bank", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                                       .getChildFile (juce::File::createLegalFileName (state.bank().name) + ".wischords"), "*.wischords");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                          [safe = juce::Component::SafePointer<ChordKeysPanel> (this)] (const juce::FileChooser& fc)
    {
        if (safe == nullptr || fc.getResult() == juce::File()) return;
        auto f = fc.getResult().withFileExtension ("wischords");
        if (auto xml = toTree (safe->state.bank()).createXml()) xml->writeTo (f);
    });
}

void ChordKeysPanel::loadBank()
{
    chooser = std::make_unique<juce::FileChooser> ("Load chord bank", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.wischords;*.xml");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe = juce::Component::SafePointer<ChordKeysPanel> (this)] (const juce::FileChooser& fc)
    {
        if (safe == nullptr || ! fc.getResult().existsAsFile()) return;
        if (auto xml = juce::XmlDocument::parse (fc.getResult()))
        {
            auto t = juce::ValueTree::fromXml (*xml);
            if (! t.hasType ("BANK")) return;
            safe->state.banks.push_back (fromTree (t));
            safe->state.current = (int) safe->state.banks.size() - 1;
            safe->state.changed();
        }
    });
}

} // namespace wis::daw
