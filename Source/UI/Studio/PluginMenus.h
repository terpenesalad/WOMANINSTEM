#pragma once

#include "StudioContext.h"
#include "Daw/Instruments/InstrumentRefs.h"

namespace wis::daw
{

/** Builds plugin choice menus (built-ins + scanned VST3s) and maps results back to PluginRefs. */
struct PluginMenu
{
    juce::PopupMenu menu;
    std::map<int, PluginRef> refs;

    static PluginMenu effects (PluginHost& host)
    {
        PluginMenu m;
        int id = 1000;
        std::map<juce::String, juce::PopupMenu> cats;
        for (auto& b : builtinPlugins())
        {
            if (b.instrument) continue;
            cats[b.category].addItem (id, b.name);
            m.refs[id++] = builtinRef (b.id);
        }
        for (auto& [cat, sub] : cats) m.menu.addSubMenu (cat, sub);

        auto ext = host.externalEffects();
        if (! ext.isEmpty())
        {
            m.menu.addSeparator();
            std::map<juce::String, juce::PopupMenu> byMaker;
            for (auto& d : ext)
            {
                byMaker[d.manufacturerName.isNotEmpty() ? d.manufacturerName : juce::String ("Other")].addItem (id, d.name);
                m.refs[id++] = PluginHost::refFor (d);
            }
            juce::PopupMenu vst;
            for (auto& [maker, sub] : byMaker) vst.addSubMenu (maker, sub);
            m.menu.addSubMenu ("VST3 Plugins", vst);
        }
        else
        {
            m.menu.addSeparator();
            m.menu.addItem (-1, "(Scan for VST3 plugins in Project > Plugin Manager)", false);
        }
        return m;
    }

    static PluginMenu instruments (PluginHost& host)
    {
        PluginMenu m;
        int id = 2000;
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

        auto ext = host.externalInstruments();
        if (! ext.isEmpty())
        {
            m.menu.addSeparator();
            juce::PopupMenu vst;
            for (auto& d : ext)
            {
                vst.addItem (id, d.name + (d.manufacturerName.isNotEmpty() ? "  (" + d.manufacturerName + ")" : juce::String()));
                m.refs[id++] = PluginHost::refFor (d);
            }
            m.menu.addSubMenu ("VST3 Instruments", vst);
        }
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
                            [copy, chosen] (int r) { if (auto it = copy.find (r); it != copy.end() && chosen) chosen (it->second); });
    }
};

} // namespace wis::daw
