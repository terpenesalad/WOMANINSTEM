#include "JunkLoops.h"
#include "Daw/Instruments/Junkyard.h"

namespace wis::daw
{

namespace
{
    enum Preset { pJunkyard = 0, pSwordfish = 1, pSnapsDry = 2, pSnapsRoom = 3, pTinShed = 4, pCave = 5, pForest = 6, pCarPark = 7, pStairwell = 8 };

    int velocityFor (juce::juce_wchar c)
    {
        switch (c)
        {
            case 'X': return 112;
            case 'x': return 88;
            case 'o': return 52;
            default:  return 0;
        }
    }
}

const std::vector<JunkLoop>& junkLoops()
{
    static const std::vector<JunkLoop> list = []
    {
        using J = Junkyard;
        std::vector<JunkLoop> l;
        auto steps = [&l] (const char* group, const char* name, const char* desc, int spb, double tempo, float swing, int preset,
                           std::vector<std::pair<int, juce::String>> lanes)
        {
            JunkLoop p;
            p.group = group; p.name = name; p.description = desc; p.stepsPerBeat = spb; p.suggestedTempo = tempo; p.swing = swing; p.preset = preset;
            p.lanes = std::move (lanes);
            l.push_back (std::move (p));
        };
        auto space = [&l] (const char* name, const char* desc, int preset, std::vector<JunkLoop::Event> events)
        {
            JunkLoop p;
            p.group = "Sound Spaces"; p.name = name; p.description = desc; p.preset = preset; p.suggestedTempo = 80.0;
            p.events = std::move (events);
            l.push_back (std::move (p));
        };

        // ---- finger snaps & body percussion -----------------------------------------------------------
        const char* fs = "Finger Snaps";
        //                                                                                                  1...2...3...4...
        steps (fs, "Snaps on 2 & 4", "The classic: a fat snap on the backbeat", 4, 100, 0.0f, pSnapsDry, {
            { J::snapFat,     "....X.......X..." } });
        steps (fs, "Snap Every Beat", "Snapping along on every beat, accents on 1 and 3", 4, 110, 0.0f, pSnapsDry, {
            { J::snapTight,   "X...x...X...x..." } });
        steps (fs, "Doo-Wop Snaps", "2 and 4 with a soft pickup snap before each one", 4, 76, 0.0f, pSnapsRoom, {
            { J::snapFat,     "....X.......X..." },
            { J::snapSoft,    "...o.......o...." } });
        steps (fs, "Jazz Club Snaps (swing)", "Cool, swung finger snaps on 2 and 4 with a ghosted skip", 3, 132, 0.0f, pSnapsRoom, {
            { J::snapFat,     "...X.....X.." },
            { J::snapSoft,    "..o.....o..." } });
        steps (fs, "Finger Click Shuffle", "A dry, clicky shuffle in triplets", 3, 96, 0.0f, pSnapsDry, {
            { J::fingerClick, "X.xx.xX.xx.x" },
            { J::snapFat,     "...X.....X.." } });
        steps (fs, "Busy 16th Snaps", "Two hands snapping 16ths, the backbeat louder", 4, 92, 0.12f, pSnapsDry, {
            { J::snapTight,   "x.o.X.o.x.o.X.o." },
            { J::snapBright,  ".o.o.o.o.o.o.o.o" } });
        steps (fs, "Half-Time Snaps", "One big snap on beat 3: slow and moody", 4, 70, 0.0f, pSnapsRoom, {
            { J::snapFat,     "........X......." },
            { J::snapSoft,    "..............o." } });
        steps (fs, "Snaps & Knee Slaps", "Knee slaps on the downbeats, snaps on the backbeat", 4, 98, 0.1f, pSnapsDry, {
            { J::kneeSlap,    "X.......X.x....." },
            { J::thighSlap,   "......o........." },
            { J::snapFat,     "....X.......X..." } });
        steps (fs, "Hand Jive", "Thigh, thigh, clap, clap: the 50s hand jive", 4, 112, 0.0f, pSnapsDry, {
            { J::kneeSlap,    "X.X.....X.X....." },
            { J::handClap,    "....X.X.....X.X." } });
        steps (fs, "Gospel Stomp & Clap", "Stomp the floorboards, clap the backbeat, snap the off-beats", 4, 88, 0.15f, pSnapsRoom, {
            { J::footStomp,   "X.......X.....x." },
            { J::groupClap,   "....X.......X..." },
            { J::snapTight,   "..o...o...o...o." } });
        steps (fs, "Tongue Clicks & Snaps", "Clicks for the hi-hat, snaps for the snare, a chest thump for the kick", 4, 94, 0.08f, pSnapsDry, {
            { J::chestThump,  "X.....x.X......." },
            { J::snapFat,     "....X.......X..." },
            { J::tongueClick, "x.x.x.x.x.x.x.x." } });

        // ---- junkyard grooves -------------------------------------------------------------------------
        const char* jg = "Junkyard Grooves";
        steps (jg, "Bone Machine Stomp", "A thumping oil drum, a brake drum backbeat and a jail door now and then", 4, 92, 0.0f, pJunkyard, {
            { J::oilDrum,     "X.....X.X......." },
            { J::brakeDrum,   "....X.......X..x" },
            { J::chairKnock,  "..o...o...o...o." } });
        steps (jg, "Swordfish March", "A lopsided parade: box drum, planks and a brake drum", 4, 104, 0.0f, pSwordfish, {
            { J::woodBox,     "X...X...X...X..." },
            { J::plank,       "..x...x...x...x." },
            { J::brakeDrum,   "....X.......X..." },
            { J::tinCan,      "x.o.x.o.x.o.x.oo" } });
        steps (jg, "Junkyard Shuffle", "Crates and pipes in a dragging shuffle", 3, 88, 0.0f, pJunkyard, {
            { J::crate,       "X..X..X..X.." },
            { J::pipe,        "..x.....x..." },
            { J::chairKnock,  "...X.....X.." },
            { J::bones,       ".o..o..o..o." } });
        steps (jg, "Clanking Machinery", "Pot lids, an anvil and chains: a factory that never stops", 4, 116, 0.0f, pCarPark, {
            { J::potLid,      "X..x..x.X..x..x." },
            { J::anvil,       "....X.......X..." },
            { J::chains,      "........x......." },
            { J::logLow,      "X.....X.....X..." } });
        steps (jg, "Funeral Procession", "Slow and heavy: a drum, a tolling bell plate, knocks", 4, 60, 0.0f, pJunkyard, {
            { J::oilDrum,     "X.......X......." },
            { J::bellPlate,   "X..............." },
            { J::doorKnock,   "....x.......x..." } });
        steps (jg, "Kitchen Sink Polka", "Oom-pah on tin cans, bottles and a wooden box", 4, 126, 0.0f, pTinShed, {
            { J::woodBox,     "X.......X......." },
            { J::tinCan,      "....X.......X..." },
            { J::glassBottle, "..x...x...x...x." },
            { J::logHigh,     "x...x...x...x..." } });
        steps (jg, "Rattle & Bones", "Bones, a wood block and a rattling chain", 3, 100, 0.0f, pTinShed, {
            { J::bones,       "X.xX.xX.xX.x" },
            { J::woodBlock,   "...x.....x.." },
            { J::chains,      "......o....." },
            { J::crate,       "X.....X....." } });
        steps (jg, "Col Legno Pulse", "Strings struck with the bow's wood and a plucked double bass", 4, 84, 0.0f, pSwordfish, {
            { J::colLegno,    "X.x.X.x.X.x.X.x." },
            { J::bassPizz,    "X.......X...x..." },
            { J::bassBodySlap, "....x.......x..." } });

        // ---- sound spaces (8 bars) ------------------------------------------------------------------------
        space ("Haunted Basement", "A low rumble, drips, creaking boards, a bowed-metal moan and a gong far away", pStairwell, {
            { J::rumble, 0.0, 8.0, 80 }, { J::drips, 0.0, 8.0, 70 }, { J::deepGong, 0.0, 0.5, 70 },
            { J::creak, 2.5, 1.0, 80 }, { J::bowedMetal, 3.0, 2.5, 70 }, { J::creak, 6.0, 1.0, 90 }, { J::doorKnock, 7.25, 0.25, 60 } });
        space ("Storm Shelter", "Wind and rain on a tin roof, the thunder sheet rolling in", pTinShed, {
            { J::wind, 0.0, 8.0, 90 }, { J::rain, 0.0, 8.0, 85 }, { J::thunderSheet, 2.0, 1.0, 100 }, { J::thunderSheet, 6.5, 1.0, 80 },
            { J::rumble, 1.5, 6.0, 60 } });
        space ("Abandoned Factory", "Mains hum, chains, a slamming door and bowed metal in a concrete hall", pCarPark, {
            { J::hum, 0.0, 8.0, 60 }, { J::chains, 1.5, 0.5, 90 }, { J::jailDoor, 3.0, 0.5, 105 }, { J::pipe, 4.5, 0.25, 70 },
            { J::pipe, 5.75, 0.25, 55 }, { J::bowedMetal, 4.0, 4.0, 75 }, { J::springBoing, 7.0, 0.5, 60 } });
        space ("Night Forest", "Wind through the trees, creaking branches and knocks in the distance", pForest, {
            { J::wind, 0.0, 8.0, 70 }, { J::creak, 1.0, 1.0, 70 }, { J::logLow, 2.75, 0.25, 50 }, { J::logLow, 3.0, 0.25, 45 },
            { J::creak, 4.5, 1.0, 85 }, { J::woodBlock, 6.25, 0.25, 40 }, { J::creak, 7.0, 1.0, 60 } });
        space ("Underwater Cave", "A drifting waterphone, drips and a bowed saw in the deep", pCave, {
            { J::waterphone, 0.0, 8.0, 75 }, { J::drips, 0.0, 8.0, 80 }, { J::rumble, 0.0, 8.0, 50 }, { J::bowedSaw, 2.0, 3.0, 65 },
            { J::glassBottle, 5.5, 0.25, 50 } });
        space ("Ghost Ship", "Creaking timbers, a droning bowed bass, rattling chains and wind", pCave, {
            { J::bowedBass, 0.0, 8.0, 70 }, { J::wind, 0.0, 8.0, 55 }, { J::creak, 0.5, 1.0, 85 }, { J::creak, 2.5, 1.0, 75 },
            { J::chains, 3.5, 0.5, 70 }, { J::creak, 4.5, 1.0, 85 }, { J::creak, 6.5, 1.0, 70 }, { J::deepGong, 7.0, 0.5, 60 } });
        space ("Bone Machine Room", "The junk kit left to ring on its own: odd hits in a concrete storeroom", pJunkyard, {
            { J::rumble, 0.0, 8.0, 45 }, { J::brakeDrum, 0.5, 0.25, 80 }, { J::crate, 1.25, 0.25, 70 }, { J::jailDoor, 2.0, 0.5, 90 },
            { J::pipe, 3.5, 0.25, 70 }, { J::chains, 4.25, 0.5, 60 }, { J::oilDrum, 5.0, 0.25, 90 }, { J::bowedSaw, 5.5, 2.0, 55 },
            { J::potLid, 7.5, 0.25, 65 } });
        return l;
    }();
    return list;
}

Clip insertJunkLoop (Project& project, const Track& track, int index, double startBeat, int bars)
{
    const auto& loops = junkLoops();
    if (! juce::isPositiveAndBelow (index, (int) loops.size()) || ! track.isValid())
        return Clip();
    const auto& loop = loops[(size_t) index];
    const double bpb = project.beatsPerBar();
    if (! loop.events.empty()) bars = 8;
    auto clip = project.addMidiClip (track, startBeat, bars * bpb, loop.name);
    juce::Random rng ((juce::int64) index * 131 + 7);

    auto addNote = [&] (int note, double beat, double len, int vel)
    {
        juce::ValueTree n (ids::NOTE);
        n.setProperty (ids::p, note, nullptr);
        n.setProperty (ids::s, juce::jmax (0.0, beat), nullptr);
        n.setProperty (ids::l, len, nullptr);
        n.setProperty (ids::v, juce::jlimit (1, 127, vel), nullptr);
        clip.v.appendChild (n, nullptr);
    };

    if (! loop.events.empty())
    {
        for (auto& e : loop.events)
            addNote (e.note, e.bar * bpb, juce::jmax (0.05, e.lengthBars * bpb - 0.05), e.velocity);
        return clip;
    }

    const int beatsInBar = juce::jmax (1, (int) std::round (bpb));
    const int stepsInBar = loop.stepsPerBeat * beatsInBar;
    const double stepLen = 1.0 / loop.stepsPerBeat;
    for (int bar = 0; bar < bars; ++bar)
        for (auto& [note, pattern] : loop.lanes)
            for (int st = 0; st < stepsInBar; ++st)
            {
                const int vel = velocityFor (pattern[st % pattern.length()]);
                if (vel == 0) continue;
                double b = bar * bpb + st * stepLen;
                if (loop.swing > 0.0f && loop.stepsPerBeat == 4 && (st % 2) == 1) b += stepLen * 0.5 * loop.swing;
                b += (rng.nextDouble() - 0.5) * 0.012;          // human timing, a hair either side
                addNote (note, b, stepLen * 0.5, vel + rng.nextInt (9) - 4);
            }
    return clip;
}

} // namespace wis::daw
