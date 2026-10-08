#include "BrowserPanel.h"
#include "Daw/Instruments/InstrumentRefs.h"
#include "Daw/Instruments/SoundFontInstrument.h"
#include "Daw/Model/DrumPatterns.h"
#include "Daw/Model/MidiLoops.h"
#include "Daw/Instruments/VintageRhythms.h"
#include "Daw/Instruments/BeatLab.h"
#include "Daw/Instruments/PianoRoom.h"
#include "Daw/Instruments/YetiVoice.h"
#include "Library/SongLibrary.h"

namespace wis::daw
{

static const juce::StringArray& homeKeysPresets()
{
    static const juce::StringArray names = [] { auto p = createBuiltin ("homekeys"); return p != nullptr ? p->getProgramNames() : juce::StringArray(); }();
    return names;
}

class BrowserPanel::Item : public juce::TreeViewItem
{
public:
    Item (BrowserPanel& o, const juce::String& t, const juce::String& d = {}, const juce::String& sub = {})
        : owner (o), text (t), desc (d), subtitle (sub) {}

    bool mightContainSubItems() override { return desc.isEmpty(); }
    int getItemHeight() const override { return desc.isEmpty() ? 26 : (subtitle.isNotEmpty() ? 34 : 24); }
    juce::String getUniqueName() const override { return text + desc; }
    bool canBeSelected() const override { return true; }

    void paintItem (juce::Graphics& g, int w, int h) override
    {
        if (isSelected())
        {
            g.setColour (theme::accent.withAlpha (0.25f));
            g.fillRoundedRectangle (0.0f, 1.0f, (float) w - 4.0f, (float) h - 2.0f, 4.0f);
        }
        g.setColour (desc.isEmpty() ? theme::text : theme::text.withAlpha (0.9f));
        g.setFont (uiFont (desc.isEmpty() ? 13.0f : 12.5f, desc.isEmpty()));
        if (subtitle.isEmpty())
            g.drawText (text, 4, 0, w - 8, h, juce::Justification::centredLeft, true);
        else
        {
            g.drawText (text, 4, 2, w - 8, 16, juce::Justification::centredLeft, true);
            g.setColour (theme::textFaint);
            g.setFont (uiFont (10.5f));
            g.drawText (subtitle, 4, 18, w - 8, 14, juce::Justification::centredLeft, true);
        }
    }

    void itemClicked (const juce::MouseEvent&) override
    {
        if (desc.isEmpty()) setOpen (! isOpen());
    }

    void itemDoubleClicked (const juce::MouseEvent&) override
    {
        if (desc.isNotEmpty() && owner.onActivate) owner.onActivate (desc);
    }

    juce::var getDragSourceDescription() override { return desc; }
    juce::String getTooltip() override
    {
        return desc.isEmpty() ? juce::String() : "Double-click to use on the selected track, or drag onto a track / empty space";
    }

    BrowserPanel& owner;
    juce::String text, desc, subtitle;
};

BrowserPanel::BrowserPanel (StudioContext& c) : ctx (c)
{
    for (int i = 0; i < numTabs; ++i)
    {
        tabs[i].setClickingTogglesState (true);
        tabs[i].setRadioGroupId (771);
        tabs[i].setColour (juce::TextButton::buttonOnColourId, theme::accent);
        tabs[i].onClick = [this, i] { showTab (i); };
        addAndMakeVisible (tabs[i]);
    }
    tabs[0].setToggleState (true, juce::dontSendNotification);

    search.setTextToShowWhenEmpty ("Search...", theme::textFaint);
    search.onTextChange = [this] { populate(); };
    addAndMakeVisible (search);

    tree.setRootItemVisible (false);
    tree.setDefaultOpenness (false);
    tree.setColour (juce::TreeView::backgroundColourId, theme::panel);
    tree.setIndentSize (14);
    addAndMakeVisible (tree);

    hint.setFont (uiFont (11.0f));
    hint.setColour (juce::Label::textColourId, theme::textFaint);
    hint.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (hint);

    populate();
}

BrowserPanel::~BrowserPanel()
{
    tree.setRootItem (nullptr);
}

void BrowserPanel::showTab (int index)
{
    currentTab = index;
    populate();
}

void BrowserPanel::refresh() { populate(); }

void BrowserPanel::populate()
{
    tree.setRootItem (nullptr);
    root = std::make_unique<Item> (*this, "root");
    const auto filter = search.getText().trim();
    const bool filtering = filter.isNotEmpty();
    auto matches = [&] (const juce::String& s) { return ! filtering || s.containsIgnoreCase (filter); };

    auto addGroup = [&] (const juce::String& name) -> juce::TreeViewItem*
    {
        auto* g = new Item (*this, name);
        root->addSubItem (g);
        return g;
    };
    auto addLeaf = [&] (juce::TreeViewItem* parent, const juce::String& name, const juce::String& d, const juce::String& sub = {})
    {
        if (! matches (name) && ! matches (sub)) return;
        parent->addSubItem (new Item (*this, name, d, sub));
    };

    if (currentTab == 0)
    {
        hint.setText ("Double-click a sound to put it on the selected instrument track (or create one). Drag onto a track to swap its instrument.", juce::dontSendNotification);
        auto sf = SoundFontCache::defaultSoundFont();
        auto presets = sf.existsAsFile() ? SoundFontCache::get().presetsFor (sf) : juce::Array<SoundFontCache::PresetInfo>();
        const auto families = gmFamilies();

        auto addBuiltinGroup = [&] (const char* id, const juce::StringArray& names, const juce::String& title, const juce::String& sub)
        {
            auto* grp = addGroup (title);
            for (int i = 0; i < names.size(); ++i) addLeaf (grp, names[i], "inst:" + juce::String (id) + ":" + juce::String (i), sub);
        };
        addBuiltinGroup ("piano", PianoRoom::presetNames(), "Piano Room (real pianos in rooms)", "Piano Room: grands and uprights, from a living room to a forest or a canyon");
        addBuiltinGroup ("yeti", YetiVoice::presetNames(), "Yodel Yeti (singing yeti)", "Yodel Yeti: a singing voice with a vowel pad, choir and delay");

        auto* homeKeys = addGroup ("HomeKeys 20 (80s Keyboard)");
        for (int i = 0; i < homeKeysPresets().size(); ++i)
            addLeaf (homeKeys, homeKeysPresets()[i], "inst:homekeys:" + juce::String (i), "with a built-in rhythm box and auto accompaniment");
        auto* machines = addGroup ("Vintage Drum Machines");
        const juce::StringArray kitNames { "Home Keyboard '84", "Rhythm Unit '78", "Eight-Oh-Eight", "Toy Box Lo-Fi" };
        for (int i = 0; i < kitNames.size(); ++i) addLeaf (machines, kitNames[i], "inst:rhythmbox:" + juce::String (i), "Rhythm Box");
        auto* beatLab = addGroup ("Beat Lab (groovebox: beats, loops, glitch)");
        {
            const auto names = BeatLab::presetNames();
            for (int i = 0; i < names.size(); ++i)
                addLeaf (beatLab, names[i], "inst:beatlab:" + juce::String (i), "Beat Lab: step sequencer, your samples and loops, stutter and glitch");
        }
        auto* samplers = addGroup ("Samplers");
        addLeaf (samplers, "Sampler", "inst:sampler:-1", "load any sound: play it, one-shot it, or slice a loop");
        for (int i = 0; i < kitNames.size(); ++i) addLeaf (samplers, "Drum Pads - " + kitNames[i], "inst:drumpads:" + juce::String (i), "16 pads, drop your own samples");

        std::map<juce::String, juce::TreeViewItem*> groups;
        for (auto& f : families) groups[f] = addGroup (f);
        auto* kits = addGroup ("Drum Kits");

        if (presets.isEmpty())
        {
            for (int p = 0; p < 128; ++p) addLeaf (groups[gmFamilyName (p)], gmProgramName (p), "sound:0:" + juce::String (p));
            addLeaf (kits, "Standard Kit", "sound:128:0");
        }
        else
        {
            for (auto& p : presets)
            {
                const auto d = "sound:" + juce::String (p.bank) + ":" + juce::String (p.program);
                if (p.bank >= 120) addLeaf (kits, p.name, d);
                else addLeaf (groups[gmFamilyName (p.program)], p.name, d, p.bank != 0 ? "variation " + juce::String (p.bank) : juce::String());
            }
        }

        auto* synth = addGroup ("Studio Synth");
        const juce::StringArray synthNames { "Init Saw", "Warm Analog Pad", "Fat Bass", "Sub Bass", "Classic Lead", "Soft Pluck", "Brass Stab",
                                             "String Machine", "Bell Keys", "Wobble Bass", "Dreamy Sweep", "Square Chip Lead", "Acid Line",
                                             "Organ-ish", "Glide Lead", "Noise Riser" };
        for (int i = 0; i < synthNames.size(); ++i) addLeaf (synth, synthNames[i], "synth:" + juce::String (i));

        auto ext = ctx.host.externalInstruments();
        if (! ext.isEmpty())
        {
            auto* vst = addGroup ("Plugin Instruments");
            for (auto& d : ext) addLeaf (vst, d.name, "vsti:" + d.createIdentifierString(), d.manufacturerName + "  [" + d.pluginFormatName + "]");
        }
    }
    else if (currentTab == 1)
    {
        hint.setText ("Double-click a groove to drop 8 bars (with fills) at the playhead on a drum track. Drag to place it anywhere.", juce::dontSendNotification);
        auto* vintage = addGroup ("Vintage Rhythm Box (80s keyboard)");
        const auto& rhythms = vintageRhythms();
        for (int i = 0; i < (int) rhythms.size(); ++i)
            addLeaf (vintage, rhythms[(size_t) i].name, "rhythm:" + juce::String (i), juce::String ((int) rhythms[(size_t) i].tempo) + " bpm feel" + (rhythms[(size_t) i].beats == 3 ? ", 3/4" : ""));
        std::map<juce::String, juce::TreeViewItem*> groups;
        auto& pats = drumPatterns();
        for (int i = 0; i < pats.size(); ++i)
        {
            auto& p = pats.getReference (i);
            if (groups.count (p.genre) == 0) groups[p.genre] = addGroup (p.genre);
            addLeaf (groups[p.genre], p.name, "pattern:" + juce::String (i), juce::String ((int) p.suggestedTempo) + " bpm feel");
        }
    }
    else if (currentTab == 2)
    {
        hint.setText ("MIDI loops in your song's key (" + Project::keyName (ctx.project.key(), ctx.project.scale()) + ", set in the control bar). "
                      "Double-click to drop 8 bars on the selected instrument track; edit the notes in the piano roll.", juce::dontSendNotification);
        std::map<juce::String, juce::TreeViewItem*> groups;
        const auto& styles = loopStyles();
        for (int st = 0; st < (int) styles.size(); ++st)
        {
            auto* g = addGroup (styles[(size_t) st].category + ": " + styles[(size_t) st].name);
            for (int pr = 0; pr < (int) loopProgressions().size(); ++pr)
                addLeaf (g, loopProgressions()[(size_t) pr].name, "loop:" + juce::String (pr) + ":" + juce::String (st), describeProgression (ctx.project, pr));
        }
    }
    else if (currentTab == 3)
    {
        hint.setText ("Double-click an effect to add it to the selected track (MIDI effects go before the instrument). Drag onto any track.", juce::dontSendNotification);
        std::map<juce::String, juce::TreeViewItem*> groups;
        for (auto& b : builtinPlugins())
        {
            if (b.category == "Hidden") continue;
            if (b.instrument) continue;
            const auto cat = b.midiFx ? juce::String ("MIDI Effects (arpeggiator...)") : b.category;
            if (groups.count (cat) == 0) groups[cat] = addGroup (cat);
            addLeaf (groups[cat], b.name, "fx:" + b.id, b.description);
        }
        auto ext = ctx.host.externalEffects();
        if (! ext.isEmpty())
        {
            auto* vst = addGroup ("Plugins (VST3 / VST / CLAP / LV2)");
            for (auto& d : ext) addLeaf (vst, d.name, "vstfx:" + d.createIdentifierString(), d.manufacturerName + "  [" + d.pluginFormatName + "]");
        }
    }
    else
    {
        hint.setText ("Songs you've split in Play Along mode. Double-click a song to bring all its stems in as tracks, or drag single stems.", juce::dontSendNotification);
        for (auto& song : SongLibrary::listSongs())
        {
            if (! matches (song.displayName())) continue;
            auto* g = addGroup (song.displayName());
            g->addSubItem (new Item (*this, "All stems", "song:" + song.id, formatSeconds (song.durationSeconds).upToLastOccurrenceOf (".", false, false)));
            for (auto& st : wis::allStems())
                if (song.present[(size_t) st.id])
                    g->addSubItem (new Item (*this, st.displayName, "stem:" + song.id + ":" + st.key));
        }
        if (root->getNumSubItems() == 0)
            hint.setText ("No separated songs yet. Switch to PLAY ALONG, open a song, and its stems will show up here.", juce::dontSendNotification);
    }

    // drop empty groups when searching
    for (int i = root->getNumSubItems(); --i >= 0;)
        if (root->getSubItem (i)->getNumSubItems() == 0 && dynamic_cast<Item*> (root->getSubItem (i))->desc.isEmpty())
            root->removeSubItem (i);

    tree.setRootItem (root.get());
    if (filtering)
        for (int i = 0; i < root->getNumSubItems(); ++i) root->getSubItem (i)->setOpen (true);
}

void BrowserPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    g.setColour (theme::outline);
    g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());
}

void BrowserPanel::resized()
{
    auto r = getLocalBounds().reduced (8, 8).withTrimmedRight (1);
    auto tabRow = r.removeFromTop (28);
    const int w = tabRow.getWidth() / numTabs;
    for (int i = 0; i < numTabs; ++i)
        tabs[i].setBounds (tabRow.removeFromLeft (i == numTabs - 1 ? tabRow.getWidth() : w).reduced (1, 0));
    r.removeFromTop (6);
    search.setBounds (r.removeFromTop (26));
    r.removeFromTop (6);
    hint.setBounds (r.removeFromBottom (54));
    tree.setBounds (r);
}

} // namespace wis::daw
