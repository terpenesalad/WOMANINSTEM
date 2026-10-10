#include "VocalChains.h"
#include "Daw/Plugins/BuiltinProcessor.h"

namespace wis::daw
{

const std::vector<VocalChain>& vocalChains()
{
    static const std::vector<VocalChain> list = []
    {
        std::vector<VocalChain> l;
        using V = std::vector<std::pair<juce::String, float>>;
        auto add = [&l] (const char* group, const char* name, const char* desc, std::vector<VocalChain::Fx> fx)
        {
            l.push_back ({ name, group, desc, std::move (fx) });
        };
        // a few building blocks
        auto cleanEq = [] (float lowCut, float mud, float presence, float air) -> VocalChain::Fx
        {
            return { "eq", V { { "lc_on", 1 }, { "lc_freq", lowCut }, { "m1_freq", 300 }, { "m1_gain", mud }, { "m1_q", 1.0f },
                               { "m2_freq", 3500 }, { "m2_gain", presence }, { "m2_q", 0.8f }, { "hs_freq", 10000 }, { "hs_gain", air } } };
        };
        auto comp = [] (float thresh, float ratio, float attack, float release, float makeup) -> VocalChain::Fx
        {
            return { "compressor", V { { "thresh", thresh }, { "ratio", ratio }, { "attack", attack }, { "release", release }, { "knee", 6 }, { "makeup", makeup } } };
        };
        auto deess = [] { return VocalChain::Fx { "deesser", V { { "freq", 6500 }, { "threshold", -30 }, { "range", 8 } } }; };
        auto verb = [] (float size, float damp, float predelay, float mix) -> VocalChain::Fx
        {
            return { "reverb", V { { "size", size }, { "damp", damp }, { "predelay", predelay }, { "lowcut", 200 }, { "highcut", 8000 }, { "mix", mix } } };
        };
        auto slap = [] (float ms, float fb, float mix) -> VocalChain::Fx
        {
            return { "delay", V { { "sync", 0 }, { "time", ms }, { "feedback", fb }, { "tone", 3500 }, { "mix", mix }, { "pingpong", 0 } } };
        };
        auto syncDelay = [] (int division, float fb, float tone, float mix) -> VocalChain::Fx
        {
            return { "delay", V { { "sync", 1 }, { "division", (float) division }, { "feedback", fb }, { "tone", tone }, { "mix", mix }, { "pingpong", 1 } } };
        };
        enum { half = 0, quarter, quarterDotted, quarterTriplet, eighth, eighthDotted };

        // ---- studio & pop ---------------------------------------------------------------------------------
        const char* sp = "Studio & Pop";
        add (sp, "Clean Studio Vocal", "Polished and clear: low cut, a little presence and air, gentle compression, a plate-ish room and a soft echo",
             { cleanEq (90, -2, 2.5f, 2), comp (-22, 3.5f, 8, 120, 5), deess(), syncDelay (quarter, 0.15f, 4000, 0.06f), verb (0.45f, 0.5f, 30, 0.14f) });
        add (sp, "Warm Radio Voice", "Close, chesty and compressed like a late-night DJ, with tape warmth",
             { { "eq", V { { "lc_on", 1 }, { "lc_freq", 70 }, { "ls_freq", 120 }, { "ls_gain", 3 }, { "m2_freq", 2500 }, { "m2_gain", 1.5f }, { "hs_gain", -1 } } },
               comp (-28, 5, 4, 90, 8), deess(), { "saturator", V { { "type", 0 }, { "drive", 4 }, { "mix", 0.5f } } }, verb (0.25f, 0.6f, 5, 0.07f) });
        add (sp, "Hard-Tune Pop", "Instant pitch snapping to the song's key (the robotic effect), bright and wide with a synced echo",
             { cleanEq (110, -2, 3, 3), { "autotune", V { { "speed", 0 }, { "amount", 1 } } }, comp (-24, 4, 5, 100, 6), deess(),
               { "width", V { { "width", 140 }, { "haas", 6 } } }, syncDelay (eighthDotted, 0.3f, 5000, 0.12f), verb (0.6f, 0.4f, 40, 0.18f) });
        add (sp, "Soul Ballad", "Warm and full, a touch of tape, a long smooth hall",
             { cleanEq (80, -1, 1.5f, 1.5f), comp (-20, 3, 15, 180, 4), { "saturator", V { { "type", 1 }, { "drive", 3 }, { "mix", 0.4f } } },
               verb (0.75f, 0.45f, 45, 0.22f) });
        add (sp, "Garage Rock Shout", "Driven, mid-forward and squashed, with a short slapback",
             { { "eq", V { { "lc_on", 1 }, { "lc_freq", 150 }, { "m2_freq", 1500 }, { "m2_gain", 5 }, { "m2_q", 0.9f }, { "hs_gain", -2 } } },
               { "saturator", V { { "type", 1 }, { "drive", 12 }, { "tone", 7000 }, { "mix", 0.8f } } }, comp (-26, 8, 3, 80, 6), slap (115, 0.12f, 0.22f),
               verb (0.3f, 0.5f, 5, 0.08f) });

        // ---- folk & roots -----------------------------------------------------------------------------------
        const char* fk = "Folk & Roots";
        add (fk, "Folk Room (live take)", "Natural and unfussy: barely compressed, in a small wooden room",
             { cleanEq (80, -1.5f, 0.5f, 1.5f), comp (-18, 2, 20, 200, 2), verb (0.35f, 0.55f, 12, 0.2f) });
        add (fk, "Old Folk Record (60s)", "Narrow, mid-rich and on tape, with a slapback and a plate",
             { { "eq", V { { "lc_on", 1 }, { "lc_freq", 120 }, { "m1_freq", 1200 }, { "m1_gain", 2 }, { "hc_on", 1 }, { "hc_freq", 9000 } } },
               comp (-24, 4, 10, 150, 4), { "tape", V { { "wow", 0.08f }, { "flutter", 0.1f }, { "saturation", 0.5f }, { "age", 0.5f }, { "hiss", 0.06f } } },
               slap (110, 0.05f, 0.12f), verb (0.5f, 0.5f, 20, 0.15f) });
        add (fk, "Laurel Canyon Double", "Two takes' worth of soft 70s folk-rock: gently doubled and warm",
             { cleanEq (90, -1.5f, 1, 2), comp (-22, 3, 12, 160, 4), { "chorus", V { { "rate", 0.3f }, { "depth", 0.12f }, { "centre", 18 }, { "mix", 0.35f } } },
               verb (0.5f, 0.5f, 25, 0.18f) });
        add (fk, "Campfire", "Outdoors at night: a bit dusty, no walls, just a faint distant bounce",
             { cleanEq (100, -1, 1, 0), comp (-20, 2.5f, 15, 180, 3), { "tape", V { { "wow", 0.1f }, { "flutter", 0.1f }, { "saturation", 0.2f }, { "age", 0.25f }, { "hiss", 0.04f } } },
               slap (240, 0.1f, 0.08f), verb (0.6f, 0.75f, 35, 0.1f) });
        add (fk, "Back-Porch Blues", "Raw and close through a cheap mic: crunchy, boxy and honest",
             { { "eq", V { { "lc_on", 1 }, { "lc_freq", 180 }, { "m1_freq", 900 }, { "m1_gain", 4 }, { "hc_on", 1 }, { "hc_freq", 7000 } } },
               { "saturator", V { { "type", 1 }, { "drive", 7 }, { "mix", 0.6f } } }, comp (-20, 3, 10, 150, 3), verb (0.3f, 0.6f, 10, 0.12f) });

        // ---- psychedelic ---------------------------------------------------------------------------------------
        const char* ps = "Psychedelic";
        add (ps, "Psych ADT (60s double tracking)", "Automatic double tracking: a wobbling second voice a few milliseconds behind, tape and a slapback",
             { cleanEq (110, -1, 2.5f, 0), comp (-26, 5, 6, 120, 6), { "chorus", V { { "rate", 0.6f }, { "depth", 0.25f }, { "centre", 22 }, { "mix", 0.45f } } },
               { "tape", V { { "wow", 0.1f }, { "flutter", 0.15f }, { "saturation", 0.45f }, { "age", 0.35f }, { "hiss", 0.03f } } }, slap (90, 0.1f, 0.15f),
               verb (0.45f, 0.5f, 15, 0.14f) });
        add (ps, "Rotating Speaker Voice", "The vocal through a spinning organ speaker: swirling, throbbing, far away",
             { { "eq", V { { "lc_on", 1 }, { "lc_freq", 180 }, { "hc_on", 1 }, { "hc_freq", 7500 }, { "m1_freq", 1000 }, { "m1_gain", 3 } } },
               { "tremolo", V { { "sync", 0 }, { "rate", 6.2f }, { "depth", 0.45f }, { "shape", 0 }, { "stereo", 0.8f } } },
               { "chorus", V { { "rate", 4.5f }, { "depth", 0.2f }, { "centre", 6 }, { "mix", 0.5f } } },
               { "tape", V { { "saturation", 0.5f }, { "age", 0.4f }, { "hiss", 0.04f } } }, verb (0.55f, 0.5f, 20, 0.22f) });
        add (ps, "Phaser Haze", "Squashed, phased, echoing and huge: modern psych pop",
             { cleanEq (120, -2, 2, 1), comp (-30, 8, 4, 100, 9), { "phaser", V { { "rate", 0.25f }, { "depth", 0.9f }, { "centre", 900 }, { "feedback", 0.6f }, { "mix", 0.5f } } },
               { "tape", V { { "wow", 0.15f }, { "flutter", 0.1f }, { "saturation", 0.4f } } }, syncDelay (eighthDotted, 0.4f, 3000, 0.2f), verb (0.75f, 0.45f, 30, 0.25f) });
        add (ps, "Shoegaze Wash", "The voice dissolving into a shimmering cloud",
             { cleanEq (150, -2, 0, -2), comp (-24, 4, 10, 150, 4), { "chorus", V { { "rate", 0.4f }, { "depth", 0.4f }, { "mix", 0.4f } } },
               syncDelay (quarterDotted, 0.5f, 2500, 0.25f), { "shimmer", V { { "amount", 0.45f }, { "size", 0.9f }, { "mix", 0.5f } } } });
        add (ps, "Space Echo Dub", "Echoes that keep coming back, darker each time",
             { cleanEq (120, -1, 1.5f, 0), comp (-24, 4, 8, 120, 5), { "saturator", V { { "type", 0 }, { "drive", 6 }, { "mix", 0.5f } } },
               syncDelay (quarterDotted, 0.6f, 2200, 0.32f), verb (0.6f, 0.6f, 20, 0.18f) });
        add (ps, "Lo-Fi Bedroom Tape", "Warbly cassette, hiss and crunch: home-recorded dream pop",
             { { "eq", V { { "lc_on", 1 }, { "lc_freq", 150 }, { "hc_on", 1 }, { "hc_freq", 7000 } } }, comp (-26, 5, 8, 140, 6),
               { "tape", V { { "wow", 0.4f }, { "flutter", 0.3f }, { "saturation", 0.5f }, { "age", 0.6f }, { "hiss", 0.25f } } },
               { "bitcrusher", V { { "bits", 12 }, { "down", 2 }, { "mix", 0.25f } } }, verb (0.5f, 0.6f, 15, 0.16f) });
        add (ps, "Cosmic Flanged Choir", "Jet-plane flanging on a doubled voice, an octave of shimmer above",
             { cleanEq (120, -2, 1, 1), comp (-24, 4, 8, 120, 5), { "flanger", V { { "rate", 0.15f }, { "depth", 0.8f }, { "feedback", 60 }, { "mix", 0.5f } } },
               { "pitchshift", V { { "semis", 12 }, { "level2", 0 }, { "spread", 8 }, { "mix", 0.18f } } }, verb (0.85f, 0.4f, 40, 0.3f) });

        // ---- weird & extreme ----------------------------------------------------------------------------------
        const char* wx = "Weird & Extreme";
        add (wx, "Telephone / Megaphone", "A tiny, distorted band of voice through a phone or a bullhorn",
             { { "eq", V { { "lc_on", 1 }, { "lc_freq", 500 }, { "hc_on", 1 }, { "hc_freq", 3500 }, { "m1_freq", 1500 }, { "m1_gain", 6 } } },
               { "saturator", V { { "type", 2 }, { "drive", 14 }, { "mix", 0.7f } } }, comp (-20, 6, 3, 80, 3), verb (0.2f, 0.5f, 0, 0.06f) });
        add (wx, "Haunted Whisper", "Whispers scattered into grains, half of them backwards, in a huge dark space",
             { { "eq", V { { "lc_on", 1 }, { "lc_freq", 200 } } }, comp (-28, 6, 5, 120, 8),
               { "grains", V { { "size", 120 }, { "density", 25 }, { "spray", 400 }, { "reverse", 0.5f }, { "mix", 0.35f } } },
               verb (0.95f, 0.6f, 60, 0.45f) });
        add (wx, "Demon Voice", "An octave down, a fifth below that, driven and dark",
             { { "eq", V { { "lc_on", 1 }, { "lc_freq", 60 } } }, { "pitchshift", V { { "semis", -12 }, { "semis2", -7 }, { "level2", 0.45f }, { "window", 70 }, { "mix", 1.0f } } },
               { "saturator", V { { "type", 1 }, { "drive", 12 }, { "tone", 5000 }, { "mix", 0.7f } } }, comp (-22, 4, 10, 150, 4), verb (0.7f, 0.7f, 20, 0.25f) });
        add (wx, "Helium Chipmunk", "Up a fifth and a bit: squeaky cartoon voices",
             { { "pitchshift", V { { "semis", 8 }, { "window", 30 }, { "mix", 1.0f } } }, cleanEq (150, 0, 0, 0), comp (-20, 3, 8, 120, 3), verb (0.3f, 0.5f, 10, 0.1f) });
        add (wx, "Robot (vocoder)", "The voice speaking through a buzzing synth chord",
             { comp (-24, 4, 5, 100, 6), { "vocoder", V { { "mode", 0 }, { "note", 45 }, { "chord", 1 } } }, verb (0.4f, 0.5f, 10, 0.12f) });
        add (wx, "Ring-Mod Alien", "Metallic, inhuman sidebands sweeping slowly, flanged",
             { comp (-24, 4, 6, 120, 5), { "ringmod", V { { "freq", 90 }, { "lfo", 1.5f }, { "lforate", 0.2f }, { "mix", 0.55f } } },
               { "flanger", V { { "rate", 0.3f }, { "depth", 0.6f }, { "feedback", 40 }, { "mix", 0.4f } } }, verb (0.6f, 0.5f, 20, 0.2f) });
        add (wx, "Extreme Metal Scream", "A gated, distorted, scooped-and-searing scream in a cold cathedral",
             { { "gate", V { { "thresh", -40 }, { "release", 80 } } },
               { "eq", V { { "lc_on", 1 }, { "lc_freq", 250 }, { "m1_freq", 500 }, { "m1_gain", -4 }, { "m2_freq", 3000 }, { "m2_gain", 4 } } },
               { "saturator", V { { "type", 3 }, { "drive", 20 }, { "tone", 8000 }, { "mix", 0.6f } } }, comp (-28, 10, 2, 60, 6),
               verb (0.9f, 0.5f, 30, 0.3f) });
        add (wx, "Cathedral Ambience", "Every word hangs in a vast stone space (choirs, chants, drones)",
             { cleanEq (120, -2, 0, 2), comp (-20, 3, 15, 180, 3), verb (1.0f, 0.4f, 60, 0.45f) });
        return l;
    }();
    return list;
}

std::vector<PluginRef> vocalChainRefs (int index)
{
    std::vector<PluginRef> refs;
    const auto& chains = vocalChains();
    if (! juce::isPositiveAndBelow (index, (int) chains.size())) return refs;
    for (auto& fx : chains[(size_t) index].fx)
    {
        auto ref = builtinRef (fx.id);
        if (auto proto = createBuiltin (fx.id))
        {
            for (auto& [id, v] : fx.values) proto->setParam (id, v);
            ref.state = encodeState (*proto);
        }
        refs.push_back (ref);
    }
    return refs;
}

void applyVocalChain (Project& project, const Track& track, int index)
{
    if (! track.isValid()) return;
    auto inserts = track.inserts();
    if (! inserts.isValid()) return;
    inserts.removeAllChildren (project.um());
    for (auto& ref : vocalChainRefs (index)) project.setPlugin (inserts, -1, ref);
}

} // namespace wis::daw
