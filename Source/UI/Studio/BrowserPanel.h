#pragma once

#include "StudioContext.h"

namespace wis::daw
{

/** GarageBand-style library: instruments, drum grooves, effects and your separated songs.
    Items can be double-clicked (applies to the selected track) or dragged onto the arrangement. */
class BrowserPanel : public juce::Component
{
public:
    explicit BrowserPanel (StudioContext& ctx);
    ~BrowserPanel() override;

    void resized() override;
    void paint (juce::Graphics&) override;
    void refresh();

    std::function<void (const juce::String& item)> onActivate;

    class Item;

private:
    void showTab (int index);
    void populate();

    StudioContext& ctx;
    static constexpr int numTabs = 5;
    juce::TextButton tabs[numTabs] { juce::TextButton ("Sounds"), juce::TextButton ("Drums"), juce::TextButton ("Loops"), juce::TextButton ("FX"), juce::TextButton ("Songs") };
    juce::TextEditor search;
    juce::TreeView tree;
    std::unique_ptr<juce::TreeViewItem> root;
    juce::Label hint;
    int currentTab = 0;
};

} // namespace wis::daw
