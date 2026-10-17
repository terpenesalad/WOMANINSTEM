#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "Daw/Model/ChordKeys.h"
#include <set>

namespace wis::daw
{

/** The Studio's Chords panel: your computer keyboard drawn with the chord on every key. Click a key to edit
    its chord (root, type, inversion, octave, voicing, bass note, or type a name like "Am7/G"), drag one key
    onto another to swap them (Alt-drag copies), and pick, make, rename, save and load banks. */
class ChordKeysPanel : public juce::Component, private juce::ChangeListener
{
public:
    explicit ChordKeysPanel (ChordKeysState& state);
    ~ChordKeysPanel() override;

    std::function<void (int slot)> onAudition;
    std::function<void()> onToggleEnabled;
    std::function<std::pair<int, int>()> songKey;   // key (0 = C), scale (0 major, 1 minor)
    std::function<void()> onFocusBack;              // hand the keyboard back to the Studio

    /** Keys that are sounding right now (lit). */
    void setHeld (const std::set<int>& slots);
    void selectSlot (int slot);
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
    int keyAt (juce::Point<float>) const;
    juce::Rectangle<float> keyBounds (int index) const;
    chordkeys::Slot& slot() { return state.bank().slots[(size_t) selected]; }
    void editSlot (std::function<void (chordkeys::Slot&)> change);
    void bankMenu();
    void slotMenu (int index);
    void fillMenu();
    void renameBank();
    void saveBank();
    void loadBank();
    int transposeNow() const;
    int spell() const;

    ChordKeysState& state;
    int selected = 10;
    std::set<int> held;
    int dragFrom = -1;
    juce::Point<float> dragPos;
    bool dragging = false;
    chordkeys::Slot clipboard;
    juce::Rectangle<float> keyArea;

    juce::TextButton onButton, bankButton { "Bank..." }, octDown { "-" }, octUp { "+" }, trDown { "-" }, trUp { "+" };
    juce::ComboBox bankBox;
    juce::Label octLabel, trLabel;
    juce::ToggleButton followKey { "Follow the song's key" }, voiceLead { "Smooth voice leading" }, bassNote { "Root in the bass" }, latch { "Latch (hold until the next chord)" };
    juce::Slider velocity { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight }, strum { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::ComboBox strumDir;

    juce::OwnedArray<juce::TextButton> rootButtons, invButtons;
    juce::ComboBox typeBox, voicingBox, bassBox;
    juce::TextButton slotOctDown { "-" }, slotOctUp { "+" }, playB { "Play" }, clearB { "Clear" }, copyB { "Copy" }, pasteB { "Paste" }, fillB { "Fill Row..." };
    juce::Label slotOctLabel;
    juce::TextEditor entry;
    std::unique_ptr<juce::FileChooser> chooser;
    bool updating = false;
};

} // namespace wis::daw
