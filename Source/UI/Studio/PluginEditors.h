#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Daw/Plugins/BuiltinProcessor.h"

namespace wis::daw
{

/** Registers the editor factories for the built-in plugins (generic knob panels + custom editors). */
void installBuiltinEditors();

/** A floating window that shows any plugin's editor (built-in or VST3). With onDock set, a strip at the top
    has a button that puts the plugin back into the Studio's bottom panel. */
class PluginWindow : public juce::DocumentWindow
{
public:
    PluginWindow (const juce::String& title, juce::AudioProcessor& processor, std::function<void()> onClose,
                  std::function<void()> onDock = {});
    ~PluginWindow() override;
    void closeButtonPressed() override;
    juce::AudioProcessor& processor;

private:
    std::function<void()> onCloseCallback;
};

/** The Studio's bottom panel for plugins: the open instrument / effect editors live here as tabs instead of
    floating over the song. Only one editor exists at a time (the selected tab's). */
class PluginDock : public juce::Component, private juce::ComponentListener
{
public:
    PluginDock();
    ~PluginDock() override;

    /** Looks up a slot's processor (nullptr if it's gone). */
    std::function<juce::AudioProcessor* (const juce::String& slotId)> getProcessor;
    std::function<void (const juce::String& slotId)> onPopOut;   // the user wants this one in its own window
    std::function<void()> onEmpty;                              // the last tab was closed
    std::function<void (int height)> onWantsHeight;             // the panel height that shows the whole editor

    /** Adds a tab (or selects it if it's already there) and shows its editor. */
    void show (const juce::String& slotId, const juce::String& title);
    /** Closes a tab (its editor is deleted before the processor goes away). */
    void remove (const juce::String& slotId);
    void clear();
    bool contains (const juce::String& slotId) const;
    bool isEmpty() const { return entries.empty(); }
    juce::String activeSlot() const { return active >= 0 ? entries[(size_t) active].slotId : juce::String(); }
    /** Height needed to show the current editor without scrolling. */
    int preferredHeight() const;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int tabBarHeight = 32;

private:
    struct Entry { juce::String slotId, title; };
    void select (int index);
    void rebuildTabs();
    void destroyEditor();
    void layoutEditor();
    void componentMovedOrResized (juce::Component&, bool, bool wasResized) override { if (wasResized && ! layingOut) layoutEditor(); }

    std::vector<Entry> entries;
    int active = -1;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    juce::AudioProcessor* editorProcessor = nullptr;
    int editorDefaultWidth = 0;
    bool layingOut = false;
    juce::Viewport viewport;
    juce::Component holder;
    juce::OwnedArray<juce::TextButton> tabs;
    juce::TextButton popOutButton { "Pop out" }, closeButton { "Close" };
};

} // namespace wis::daw
