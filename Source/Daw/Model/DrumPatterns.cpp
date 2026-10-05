#include "DrumPatterns.h"

namespace wis::daw
{

namespace gm
{
    enum { kick = 36, stick = 37, snare = 38, clap = 39, floorTom = 41, closedHat = 42, lowTom = 45, pedalHat = 44,
           openHat = 46, midTom = 47, highTom = 50, crash = 49, ride = 51, rideBell = 53, tambourine = 54, cowbell = 56, shaker = 70 };
}

const juce::Array<DrumPattern>& drumPatterns()
{
    static const juce::Array<DrumPattern> list = [] {
        juce::Array<DrumPattern> l;
        auto add = [&l] (const char* name, const char* genre, int spb, double tempo, std::vector<std::pair<int, juce::String>> lanes)
        {
            DrumPattern p;
            p.name = name; p.genre = genre; p.stepsPerBeat = spb; p.suggestedTempo = tempo; p.lanes = std::move (lanes);
            l.add (p);
        };
        using namespace gm;
        //                                         1...2...3...4...
        add ("Straight Rock", "Rock", 4, 110, {
            { kick,      "X.......X.x....." },
            { snare,     "....X.......X..." },
            { closedHat, "x.x.x.x.x.x.x.x." } });
        add ("Driving Rock", "Rock", 4, 130, {
            { kick,      "X.x.....X.x.x..." },
            { snare,     "....X.......X..." },
            { closedHat, "XxXxXxXxXxXxXxXx" } });
        add ("Arena Ride", "Rock", 4, 120, {
            { kick,      "X.....x.X.x....." },
            { snare,     "....X.......X..." },
            { ride,      "x.x.x.x.x.x.x.x." },
            { rideBell,  "X...X...X...X..." } });
        add ("Punk", "Rock", 4, 175, {
            { kick,      "X...X...X...X..." },
            { snare,     "..X...X...X...X." },
            { closedHat, "x.x.x.x.x.x.x.x." } });
        add ("Pop Groove", "Pop", 4, 105, {
            { kick,      "X......xX.x....." },
            { snare,     "....X.......X..." },
            { closedHat, "x.x.x.x.x.x.x.x." },
            { tambourine,"....x.......x..." } });
        add ("Four on the Floor", "Dance", 4, 124, {
            { kick,      "X...X...X...X..." },
            { clap,      "....X.......X..." },
            { closedHat, "x.x.x.x.x.x.x.x." },
            { openHat,   "..x...x...x...x." } });
        add ("Disco", "Dance", 4, 118, {
            { kick,      "X...X...X...X..." },
            { snare,     "....X.......X..." },
            { closedHat, "x...x...x...x..." },
            { openHat,   "..X...X...X...X." } });
        add ("Funk", "Funk & Soul", 4, 98, {
            { kick,      "X..x..x...X..x.." },
            { snare,     "....X..o.o..X..o" },
            { closedHat, "xxxXxxxXxxxXxxxX" } });
        add ("Motown", "Funk & Soul", 4, 112, {
            { kick,      "X..x....X..x...." },
            { snare,     "X...X...X...X..." },
            { tambourine,"x.x.x.x.x.x.x.x." },
            { closedHat, "x...x...x...x..." } });
        add ("Boom Bap", "Hip Hop", 4, 90, {
            { kick,      "X......x..X....." },
            { snare,     "....X.......X..." },
            { closedHat, "x.x.x.x.x.xxx.x." } });
        add ("Trap", "Hip Hop", 4, 140, {
            { kick,      "X......X..X....." },
            { clap,      "........X......." },
            { closedHat, "x.x.x.xxx.x.xxxx" } });
        add ("Half-Time Ballad", "Ballad", 4, 72, {
            { kick,      "X.......x.x....." },
            { stick,     "........X......." },
            { closedHat, "x.x.x.x.x.x.x.x." } });
        add ("Blues Shuffle", "Blues & Jazz", 3, 96, {
            { kick,      "X.....X....." },
            { snare,     "...X.....X.." },
            { closedHat, "x.xx.xx.xx.x" } });
        add ("Jazz Swing", "Blues & Jazz", 3, 140, {
            { ride,      "X..x.xX..x.x" },
            { pedalHat,  "...x.....x.." },
            { kick,      "o.....o....." } });
        add ("Reggae One Drop", "World", 4, 76, {
            { kick,      "........X......." },
            { stick,     "........X......." },
            { closedHat, "x.x.x.x.x.x.x.x." } });
        add ("Bossa Nova", "World", 4, 130, {
            { kick,      "X..xX..xX..xX..x" },
            { stick,     "X..X..X...X..X.." },
            { closedHat, "xxxxxxxxxxxxxxxx" } });
        add ("Metal Double Kick", "Metal", 4, 160, {
            { kick,      "xxxxxxxxxxxxxxxx" },
            { snare,     "....X.......X..." },
            { crash,     "X...x...x...x..." } });
        add ("Indie Toms", "Rock", 4, 115, {
            { kick,      "X.....x.X......." },
            { floorTom,  "x.x.x.x.x.x.x.x." },
            { snare,     "....X.......X..." },
            { shaker,    "xxxxxxxxxxxxxxxx" } });
        return l;
    }();
    return list;
}

static int velocityFor (juce::juce_wchar c)
{
    switch (c)
    {
        case 'X': return 118;
        case 'x': return 92;
        case 'o': return 48;
        default:  return 0;
    }
}

Clip insertDrumPattern (Project& project, const Track& track, int index, double startBeat, int bars, bool fills)
{
    auto& patterns = drumPatterns();
    if (! juce::isPositiveAndBelow (index, patterns.size()) || ! track.isValid())
        return Clip();

    const auto& pat = patterns.getReference (index);
    const double bpb = project.beatsPerBar();
    const int beatsInBar = (int) std::round (bpb);
    const int stepsInBar = pat.stepsPerBeat * beatsInBar;
    const double stepLen = 1.0 / pat.stepsPerBeat;
    const bool swing = pat.stepsPerBeat == 4 && pat.genre == "Hip Hop";

    auto clip = project.addMidiClip (track, startBeat, bars * bpb, pat.name);
    juce::Random rng ((juce::int64) index * 31 + bars);

    auto addNote = [&] (int note, double beat, int vel)
    {
        // a touch of human feel: small velocity variation
        const int v = juce::jlimit (1, 127, vel + rng.nextInt (9) - 4);
        juce::ValueTree n (ids::NOTE);
        n.setProperty (ids::p, note, nullptr);
        n.setProperty (ids::s, beat, nullptr);
        n.setProperty (ids::l, stepLen * 0.5, nullptr);
        n.setProperty (ids::v, v, nullptr);
        clip.v.appendChild (n, nullptr);
    };

    for (int bar = 0; bar < bars; ++bar)
    {
        const double barStart = bar * bpb;
        const bool fillBar = fills && bars >= 4 && (bar % 4) == 3;

        for (auto& [note, steps] : pat.lanes)
        {
            for (int st = 0; st < stepsInBar; ++st)
            {
                const auto c = steps[st % steps.length()];
                const int vel = velocityFor (c);
                if (vel == 0) continue;
                // fills replace the second half of the bar
                if (fillBar && st >= stepsInBar / 2 && note != gm::kick) continue;
                double b = barStart + st * stepLen;
                if (swing && (st % 2) == 1) b += stepLen * 0.33;
                addNote (note, b, vel);
            }
        }

        if (bar == 0 || (fills && bar % 4 == 0))
            addNote (gm::crash, barStart, 112);

        if (fillBar)
        {
            // descending tom fill across the last two beats
            const int toms[] = { gm::snare, gm::snare, gm::highTom, gm::highTom, gm::midTom, gm::midTom, gm::lowTom, gm::floorTom };
            const double fillStart = barStart + bpb / 2.0;
            const int fillSteps = (int) std::round ((bpb / 2.0) / stepLen);
            for (int i = 0; i < fillSteps; ++i)
                addNote (toms[(i * 8) / juce::jmax (1, fillSteps)], fillStart + i * stepLen, 85 + (i * 30) / juce::jmax (1, fillSteps));
        }
    }
    return clip;
}

} // namespace wis::daw
