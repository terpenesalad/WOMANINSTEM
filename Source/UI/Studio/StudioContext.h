#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "Daw/Model/Project.h"
#include "Daw/Engine/DawEngine.h"
#include "Daw/Plugins/PluginHost.h"
#include "UI/LookAndFeel.h"

namespace wis::daw
{

/** Adds a menu item; text after a tab is shown right-aligned as the keyboard shortcut. */
inline void addMenuItem (juce::PopupMenu& m, int id, const juce::String& text, bool enabled = true)
{
    juce::PopupMenu::Item item (text.upToFirstOccurrenceOf ("\t", false, false));
    item.itemID = id;
    item.isEnabled = enabled;
    item.shortcutKeyDescription = text.fromFirstOccurrenceOf ("\t", false, false);
    m.addItem (std::move (item));
}

/** Shared view state for all the Studio panels: selection, zoom, snapping, and actions the panels can trigger. */
class StudioContext : public juce::ChangeBroadcaster
{
public:
    StudioContext (Project& p, DawEngine& e, PluginHost& h) : project (p), engine (e), host (h) {}

    Project& project;
    DawEngine& engine;
    PluginHost& host;

    // ---- selection ----
    int selectedTrack = 0;
    juce::Array<int> selectedClips;
    int editorClip = 0;                       // clip open in the bottom editor

    void selectTrack (int id)
    {
        if (selectedTrack == id) return;
        selectedTrack = id;
        engine.selectedTrackId = id;
        sendChangeMessage();
    }
    void selectClip (int id, bool add)
    {
        if (! add) selectedClips.clearQuick();
        if (! selectedClips.contains (id)) selectedClips.add (id);
        sendChangeMessage();
    }
    void clearClipSelection() { selectedClips.clearQuick(); sendChangeMessage(); }
    bool isClipSelected (int id) const { return selectedClips.contains (id); }

    // ---- view ----
    double pixelsPerBeat = 24.0;              // arrangement zoom
    double scrollBeats = 0.0;                 // left edge of the arrangement
    int snapMode = 0;                         // 0 auto, 1 bar, 2 beat, 3 1/8, 4 1/16, 5 off

    double gridBeats (double ppb) const
    {
        const double bpb = project.beatsPerBar();
        switch (snapMode)
        {
            case 1: return bpb;
            case 2: return 1.0;
            case 3: return 0.5;
            case 4: return 0.25;
            case 5: return 0.0;
            default:
                if (ppb < 6) return bpb * 4;
                if (ppb < 18) return bpb;
                if (ppb < 60) return 1.0;
                if (ppb < 160) return 0.5;
                return 0.25;
        }
    }
    double snap (double beats, double ppb, bool bypass = false) const
    {
        const double g = gridBeats (ppb);
        if (bypass || g <= 0.0) return juce::jmax (0.0, beats);
        return juce::jmax (0.0, std::round (beats / g) * g);
    }

    // ---- actions (wired by StudioPage) ----
    std::function<void (int clipId)> openEditor;
    std::function<void (const juce::ValueTree& pluginNode)> openPluginWindow;
    std::function<void (const juce::String&)> status;
    std::function<void (int trackId, juce::Point<int> screenPos)> showTrackMenu;

    void setStatus (const juce::String& s) { if (status) status (s); }

    // undo helper
    void beginEdit (const juce::String& name) { project.undo().beginNewTransaction (name); }
};

} // namespace wis::daw
