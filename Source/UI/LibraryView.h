#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Library/SongLibrary.h"

namespace wis
{

/** Your separated songs: open instantly, export stems as WAV, or delete. */
class LibraryView : public juce::Component, private juce::ListBoxModel
{
public:
    LibraryView();

    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void (const SongInfo&)> onOpen;
    std::function<void (const SongInfo&)> onExport;

private:
    int getNumRows() override { return songs.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void selectedRowsChanged (int) override;
    void deleteKeyPressed (int) override { removeSelected(); }
    void returnKeyPressed (int row) override;

    void removeSelected();
    const SongInfo* selected() const;

    juce::Array<SongInfo> songs;
    juce::ListBox list { "songs", this };
    juce::TextButton open { "Open" }, exportBtn { "Export stems..." }, remove { "Delete" }, reveal { "Show folder" };
    juce::Label empty;
};

} // namespace wis
