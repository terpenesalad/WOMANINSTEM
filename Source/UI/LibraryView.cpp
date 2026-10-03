#include "LibraryView.h"
#include "LookAndFeel.h"

namespace wis
{

LibraryView::LibraryView()
{
    list.setRowHeight (52);
    list.setColour (juce::ListBox::backgroundColourId, theme::panel);
    addAndMakeVisible (list);

    open.onClick = [this] { if (auto* s = selected()) if (onOpen) onOpen (*s); };
    exportBtn.onClick = [this] { if (auto* s = selected()) if (onExport) onExport (*s); };
    remove.onClick = [this] { removeSelected(); };
    reveal.onClick = [this]
    {
        if (auto* s = selected()) s->folder.revealToUser();
        else SongLibrary::libraryDirectory().revealToUser();
    };
    open.setColour (juce::TextButton::buttonColourId, theme::accent);

    for (auto* b : { &open, &exportBtn, &remove, &reveal })
        addAndMakeVisible (b);

    empty.setText ("No songs yet. Open an MP3 or FLAC and it will appear here once it's been split.", juce::dontSendNotification);
    empty.setJustificationType (juce::Justification::centred);
    empty.setColour (juce::Label::textColourId, theme::textDim);
    empty.setFont (uiFont (14.0f));
    addChildComponent (empty);

    setSize (640, 480);
    refresh();
}

void LibraryView::refresh()
{
    songs = SongLibrary::listSongs();
    list.updateContent();
    list.repaint();
    empty.setVisible (songs.isEmpty());
    selectedRowsChanged (list.getSelectedRow());
}

const SongInfo* LibraryView::selected() const
{
    const int row = list.getSelectedRow();
    return juce::isPositiveAndBelow (row, songs.size()) ? &songs.getReference (row) : nullptr;
}

void LibraryView::selectedRowsChanged (int)
{
    const bool any = selected() != nullptr;
    open.setEnabled (any);
    exportBtn.setEnabled (any);
    remove.setEnabled (any);
}

void LibraryView::removeSelected()
{
    auto* s = selected();
    if (s == nullptr) return;

    const auto id = s->id;
    const auto name = s->displayName();
    juce::Component::SafePointer<LibraryView> safe (this);
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Delete song",
        "Delete the stems for \"" + name + "\"? Your original audio file is not touched.", "Delete", "Cancel", this,
        juce::ModalCallbackFunction::create ([safe, id] (int result)
        {
            if (safe == nullptr || result == 0) return;
            SongLibrary::removeSong (id);
            safe->refresh();
        }));
}

void LibraryView::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool isSelected)
{
    if (! juce::isPositiveAndBelow (row, songs.size())) return;
    auto& s = songs.getReference (row);

    auto r = juce::Rectangle<int> (0, 0, width, height).reduced (6, 3).toFloat();
    g.setColour (isSelected ? theme::accent.withAlpha (0.18f) : theme::panelRaised);
    g.fillRoundedRectangle (r, 6.0f);
    if (isSelected)
    {
        g.setColour (theme::accent);
        g.drawRoundedRectangle (r, 6.0f, 1.0f);
    }

    auto inner = r.reduced (12.0f, 6.0f);

    // stem dots
    auto dots = inner.removeFromRight (7 * 12.0f);
    for (auto& st : allStems())
    {
        auto d = dots.removeFromLeft (12.0f).withSizeKeepingCentre (7.0f, 7.0f);
        g.setColour (s.present[(size_t) st.id] ? stemColour (st.id) : theme::ledOff);
        g.fillEllipse (d);
    }

    g.setColour (theme::text);
    g.setFont (uiFont (14.5f, true));
    g.drawText (s.displayName(), inner.removeFromTop (inner.getHeight() * 0.55f), juce::Justification::bottomLeft);

    const int secs = (int) s.durationSeconds;
    juce::String meta = juce::String (secs / 60) + ":" + juce::String (secs % 60).paddedLeft ('0', 2)
                      + "   " + s.added.formatted ("%d %b %Y")
                      + (s.quality == SeparationQuality::maximum ? "   Max quality" : "");
    g.setColour (theme::textDim);
    g.setFont (uiFont (12.0f));
    g.drawText (meta, inner, juce::Justification::topLeft);
}

void LibraryView::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    returnKeyPressed (row);
}

void LibraryView::returnKeyPressed (int row)
{
    if (juce::isPositiveAndBelow (row, songs.size()) && onOpen)
        onOpen (songs.getReference (row));
}

void LibraryView::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
}

void LibraryView::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto buttons = r.removeFromBottom (34);
    open.setBounds (buttons.removeFromRight (100));
    buttons.removeFromRight (8);
    exportBtn.setBounds (buttons.removeFromRight (130));
    buttons.removeFromRight (8);
    remove.setBounds (buttons.removeFromRight (90));
    reveal.setBounds (buttons.removeFromLeft (110));
    r.removeFromBottom (10);
    list.setBounds (r);
    empty.setBounds (r);
}

} // namespace wis
