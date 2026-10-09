#pragma once

#include "StudioContext.h"
#include "Daw/Instruments/InstrumentRefs.h"
#include "Daw/Instruments/BeatLab.h"
#include "Daw/Instruments/PianoRoom.h"

namespace wis::daw
{

/** Builds plugin choice menus (built-ins + scanned VST3s) and maps results back to PluginRefs. */
struct PluginMenu
{
    juce::PopupMenu menu;
    std::map<int, PluginRef> refs;

    /** Built-in with a factory preset, made only when it's chosen (creating plugins for every menu item would be slow). */
    static PluginRef lazyPreset (const juce::String& id, int preset, const juce::String& name)
    {
        auto r = builtinRef (id);
        r.name = name;
        r.state = "@preset:" + juce::String (preset);
        return r;
    }
    /** Turns a menu reference into one ready to put in a slot. */
    static PluginRef resolve (const PluginRef& r)
    {
        if (r.type == "builtin" && r.state.startsWith ("@preset:"))
            return builtinPresetRef (r.uid, r.state.fromFirstOccurrenceOf (":", false, false).getIntValue());
        return r;
    }

    static juce::String formatTag (const juce::PluginDescription& d)
    {
        return d.pluginFormatName == "VST3" ? juce::String() : "  [" + d.pluginFormatName + "]";
    }

    static void addExternal (juce::PopupMenu& menu, PluginMenu& m, int& id, const juce::Array<juce::PluginDescription>& list, bool byMaker)
    {
        if (list.isEmpty())
        {
            menu.addItem (-1, "(Scan for plugins in Project > Plugin Manager)", false);
            return;
        }
        std::map<juce::String, juce::PopupMenu> groups;
        for (auto& d : list)
        {
            const auto group = byMaker ? (d.manufacturerName.isNotEmpty() ? d.manufacturerName : juce::String ("Other")) : juce::String();
            groups[group].addItem (id, d.name + formatTag (d) + (byMaker || d.manufacturerName.isEmpty() ? juce::String() : "  (" + d.manufacturerName + ")"));
            m.refs[id++] = PluginHost::refFor (d);
        }
        if (! byMaker) { menu = groups[{}]; return; }
        for (auto& [maker, sub] : groups) menu.addSubMenu (maker, sub);
    }

    static PluginMenu effects (PluginHost& host)
    {
        PluginMenu m;
        int id = 10000;
        std::map<juce::String, juce::PopupMenu> cats;
        for (auto& b : builtinPlugins())
        {
            if (b.category == "Hidden" || b.instrument || b.midiFx) continue;
            cats[b.category].addItem (id, b.name);
            m.refs[id++] = builtinRef (b.id);
        }
        for (auto& [cat, sub] : cats) m.menu.addSubMenu (cat, sub);
        m.menu.addSeparator();
        juce::PopupMenu ext;
        addExternal (ext, m, id, host.externalEffects(), true);
        m.menu.addSubMenu ("Plugins (VST3 / VST / CLAP / LV2)", ext);
        return m;
    }

    static PluginMenu midiEffects (PluginHost&)
    {
        PluginMenu m;
        int id = 30000;
        for (auto& b : builtinPlugins())
        {
            if (! b.midiFx) continue;
            auto proto = createBuiltin (b.id);
            const auto presets = proto != nullptr ? proto->getProgramNames() : juce::StringArray();
            if (presets.isEmpty())
            {
                m.menu.addItem (id, b.name);
                m.refs[id++] = builtinRef (b.id);
            }
            else
            {
                juce::PopupMenu sub;
                sub.addItem (id, "Default");
                m.refs[id++] = builtinRef (b.id);
                for (int i = 0; i < presets.size(); ++i)
                {
                    sub.addItem (id, presets[i]);
                    m.refs[id++] = lazyPreset (b.id, i, b.name + ": " + presets[i]);
                }
                m.menu.addSubMenu (b.name, sub);
            }
        }
        return m;
    }

    static PluginMenu instruments (PluginHost& host)
    {
        PluginMenu m;
        int id = 20000;
        juce::PopupMenu sounds;
        const auto families = gmFamilies();
        for (int f = 0; f < 16; ++f)
        {
            juce::PopupMenu fam;
            for (int prog = f * 8; prog < f * 8 + 8; ++prog)
            {
                fam.addItem (id, gmProgramName (prog));
                m.refs[id++] = soundFontRef (0, prog);
            }
            sounds.addSubMenu (families[f], fam);
        }
        juce::PopupMenu kits;
        for (auto [prog, name] : { std::pair<int, const char*> { 0, "Standard Kit" }, { 8, "Room Kit" }, { 16, "Power Kit" }, { 24, "Electronic Kit" },
                                   { 25, "808 Kit" }, { 32, "Jazz Kit" }, { 40, "Brush Kit" }, { 48, "Orchestra Kit" } })
        {
            kits.addItem (id, name);
            m.refs[id] = soundFontRef (128, prog);
            m.refs[id].name = name;
            ++id;
        }
        sounds.addSubMenu ("Drum Kits", kits);
        m.menu.addSubMenu ("Sound Library", sounds);

        juce::PopupMenu synth;
        StudioSynthNames (synth, m, id);
        m.menu.addSubMenu ("Studio Synth", synth);

        auto presetMenu = [&] (const char* builtinId, const juce::StringArray& names, const juce::String& prefix)
        {
            juce::PopupMenu sub;
            for (int i = 0; i < names.size(); ++i)
            {
                sub.addItem (id, names[i]);
                m.refs[id++] = lazyPreset (builtinId, i, prefix + names[i]);
            }
            return sub;
        };
        static const juce::StringArray homeKeys { "Dream Pop Organ (Slow Rock)", "Bedroom Waltz", "Tropical Bossa", "Haunted Music Box", "Cassette Strings",
                                                  "Disco Brass", "Choir in the Attic", "Vibes Lounge" };
        m.menu.addSubMenu ("Piano Room (real pianos in rooms)", presetMenu ("piano", PianoRoom::presetNames(), "Piano Room: "));
        m.menu.addSubMenu ("HomeKeys 20 (80s keyboard)", presetMenu ("homekeys", homeKeys, {}));
        static const juce::StringArray kitNames { "Home Keyboard '84", "Rhythm Unit '78", "Eight-Oh-Eight", "Toy Box Lo-Fi" };
        m.menu.addSubMenu ("Rhythm Box (vintage drums)", presetMenu ("rhythmbox", kitNames, "Rhythm Box: "));
        m.menu.addSubMenu ("Beat Lab (groovebox: beats, loops, glitch)", presetMenu ("beatlab", BeatLab::presetNames(), "Beat Lab: "));
        juce::PopupMenu samplers;
        samplers.addItem (id, "Sampler (load any sound)");
        m.refs[id++] = builtinRef ("sampler");
        samplers.addSubMenu ("Drum Pads", presetMenu ("drumpads", kitNames, "Drum Pads: "));
        m.menu.addSubMenu ("Samplers", samplers);

        m.menu.addSeparator();
        juce::PopupMenu ext;
        addExternal (ext, m, id, host.externalInstruments(), false);
        m.menu.addSubMenu ("Plugin Instruments (VST3 / VST / CLAP / LV2)", ext);
        return m;
    }

    static void StudioSynthNames (juce::PopupMenu& menu, PluginMenu& m, int& id)
    {
        static const char* names[] = { "Init Saw", "Warm Analog Pad", "Fat Bass", "Sub Bass", "Classic Lead", "Soft Pluck", "Brass Stab",
                                       "String Machine", "Bell Keys", "Wobble Bass", "Dreamy Sweep", "Square Chip Lead", "Acid Line",
                                       "Organ-ish", "Glide Lead", "Noise Riser" };
        for (int i = 0; i < (int) std::size (names); ++i)
        {
            menu.addItem (id, names[i]);
            m.refs[id++] = synthRef (i);   // state is built lazily below if needed
        }
    }

    void show (juce::Component* target, std::function<void (const PluginRef&)> chosen) const
    {
        auto copy = refs;
        auto m = menu;
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target),
                            [copy, chosen] (int r) { if (auto it = copy.find (r); it != copy.end() && chosen) chosen (resolve (it->second)); });
    }
};

} // namespace wis::daw
