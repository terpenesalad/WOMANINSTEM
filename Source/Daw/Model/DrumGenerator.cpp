#include "DrumGenerator.h"
#include <cmath>
#include <cstring>

namespace wis::daw::drummer
{

using namespace note;

// Grooves: one bar each, 'X' accent, 'x' normal, 'o' ghost, anything else rest.
// Straight styles use 16 steps per 4/4 bar, triplet styles 12 (each beat = 3 triplet 8ths).
const std::vector<Style>& styles()
{
    static const std::vector<Style> s = [] {
        std::vector<Style> v;
        auto add = [&v] (Style st) { v.push_back (std::move (st)); };
        //                                                      1...2...3...4...
        add ({ "Rock", "Straight-ahead rock: kick and snare locked with the bass, 8th hats, ride or crash-ride in the choruses, tom fills",
               false, 0.0f, false, fillToms, 0.25f, 0.7f, 4,
               { { { kick, "X.......X.x....." }, { stick, "....x.......x..." }, { closedHat, "x.x.x.x.x.x.x.x." } },
                 { { kick, "X.......X.x....." }, { snare, "....X.......X..." }, { closedHat, "x.x.x.x.x.x.x.x." } },
                 { { kick, "X.....x.X.x....." }, { snare, "....X.......X..." }, { closedHat, "X.x.X.x.X.x.X..." }, { openHat, "..............x." } },
                 { { kick, "X.x...x.X.x...x." }, { snare, "....X.......X..." }, { ride, "X.x.X.x.X.x.X.x." } } } });
        add ({ "Pop", "Clean and tight: four-on-the-snare backbeat, 16th hats in the choruses, tambourine and claps on top",
               false, 0.0f, false, fillSnareBuild, 0.2f, 0.6f, 0,
               { { { kick, "X.......X......." }, { stick, "....x.......x..." }, { shaker, "xoxoxoxoxoxoxoxo" } },
                 { { kick, "X......xX.x....." }, { snare, "....X.......X..." }, { closedHat, "x.x.x.x.x.x.x.x." } },
                 { { kick, "X......xX.x....." }, { snare, "....X.......X..." }, { closedHat, "XxxxXxxxXxxxXxxx" }, { tambourine, "....x.......x..." } },
                 { { kick, "X.x....xX.x....." }, { snare, "....X.......X..." }, { clap, "....x.......x..." }, { closedHat, "x...x...x...x..." }, { openHat, "..x...x...x...x." } } } });
        add ({ "Indie / Garage", "Loose and trashy: washy open hats, kick pushing the off-beats, room sound",
               false, 0.0f, false, fillToms, 0.15f, 0.8f, 3,
               { { { kick, "X.......X......." }, { snare, "............o..." }, { closedHat, "x.x.x.x.x.x.x.x." } },
                 { { kick, "X..x....X.x....." }, { snare, "....X.......X..." }, { closedHat, "X.x.X.x.X.x.X.x." } },
                 { { kick, "X..x..x.X.x....." }, { snare, "....X.......X..." }, { openHat, "x.x.x.x.x.x.x.x." } },
                 { { kick, "X.xx..x.X.x..x.." }, { snare, "....X.......X..." }, { ride, "X.x.X.x.X.x.X.x." } } } });
        add ({ "Psych (laid back)", "60s / modern psych: fat, dry and behind the beat, busy 16th hats, ghost notes and big tom fills",
               false, 0.12f, true, fillToms, 0.6f, 0.6f, 2,
               { { { kick, "X.......X......." }, { stick, "....x.......x..." }, { closedHat, "x.x.x.x.x.x.x.x." } },
                 { { kick, "X......xX......." }, { snare, "....X.......X..." }, { closedHat, "xoxoxoxoxoxoxoxo" } },
                 { { kick, "X.....x.X..x...." }, { snare, "....X..o....X..." }, { closedHat, "xoxoXoxoxoxoXoxo" }, { tambourine, "....x.......x..." } },
                 { { kick, "X.x...x.X..x..x." }, { snare, "....X..o....X.o." }, { openHat, "x.x.x.x.x.x.x.x." }, { tambourine, "x.x.x.x.x.x.x.x." } } } });
        add ({ "Punk", "Fast and relentless: kick on every beat, the snare on the off-beats when it takes off",
               false, 0.0f, false, fillToms, 0.0f, 0.4f, 4,
               { { { kick, "X...X...X...X..." }, { closedHat, "x.x.x.x.x.x.x.x." } },
                 { { kick, "X...X.x.X...X.x." }, { snare, "....X.......X..." }, { closedHat, "x.x.x.x.x.x.x.x." } },
                 { { kick, "X.x.X.x.X.x.X.x." }, { snare, "..X...X...X...X." }, { closedHat, "x.x.x.x.x.x.x.x." } },
                 { { kick, "X.x.X.x.X.x.X.x." }, { snare, "..X...X...X...X." }, { ride, "X.x.X.x.X.x.X.x." } } } });
        add ({ "Metal", "Heavy: half-time verses, double-kick choruses, crash-ride",
               false, 0.0f, false, fillToms, 0.0f, 0.5f, 4,
               { { { kick, "X.......X......." }, { snare, "........X......." }, { ride, "x.x.x.x.x.x.x.x." } },
                 { { kick, "X.x.....X.x....." }, { snare, "....X.......X..." }, { closedHat, "x.x.x.x.x.x.x.x." } },
                 { { kick, "XxxxXxxxXxxxXxxx" }, { snare, "....X.......X..." }, { ride, "X...X...X...X..." } },
                 { { kick, "xxxxxxxxxxxxxxxx" }, { snare, "....X.......X..." }, { ride, "X.x.X.x.X.x.X.x." } } } });
        add ({ "Funk", "Syncopated kick, ghost-note snare and 16th hats with accents",
               false, 0.0f, false, fillFunk, 0.8f, 0.8f, 1,
               { { { kick, "X..x......X..x.." }, { stick, "....x.......x..." }, { closedHat, "x.x.x.x.x.x.x.x." } },
                 { { kick, "X..x......X..x.." }, { snare, "....X..o.o..X..o" }, { closedHat, "XxxxXxxxXxxxXxxx" } },
                 { { kick, "X..x..x...X..x.." }, { snare, "....X..o.o..X..o" }, { closedHat, "XxxxXxxxXxxxXx.x" }, { openHat, "..............x." } },
                 { { kick, "X.xx..x...X..x.x" }, { snare, "o...X..o.o.oX.oo" }, { closedHat, "XxxxXxxxXxxxXx.x" }, { openHat, "..............x." } } } });
        add ({ "Soul / Motown", "Snare on every beat, tambourine riding along, a warm room",
               false, 0.0f, false, fillFunk, 0.3f, 0.6f, 3,
               { { { kick, "X.......X......." }, { stick, "....x.......x..." }, { closedHat, "x.x.x.x.x.x.x.x." } },
                 { { kick, "X.x.....X.x....." }, { snare, "....X.......X..." }, { closedHat, "x.x.x.x.x.x.x.x." }, { tambourine, "....x.......x..." } },
                 { { kick, "X.x.....X.x....." }, { snare, "X...X...X...X..." }, { closedHat, "x.x.x.x.x.x.x.x." }, { tambourine, "x.x.x.x.x.x.x.x." } },
                 { { kick, "X.x...x.X.x...x." }, { snare, "X...X...X...X..." }, { tambourine, "xxxxxxxxxxxxxxxx" }, { openHat, "..x...x...x...x." } } } });
        add ({ "Hip-Hop (boom bap)", "Swung 16ths, a heavy kick and a cracking snare, sampled-break feel",
               false, 0.25f, true, fillMinimal, 0.3f, 0.7f, 1,
               { { { kick, "X.........X....." }, { closedHat, "x.x.x.x.x.x.x.x." } },
                 { { kick, "X.........X..x.." }, { snare, "....X.......X..." }, { closedHat, "x.x.x.x.x.x.x.x." } },
                 { { kick, "X..x......X..x.." }, { snare, "....X.......X..." }, { closedHat, "x.x.x.x.x.x.xxx." }, { openHat, "..............x." } },
                 { { kick, "X..x..x...X.xx.." }, { snare, "....X..o....X..." }, { closedHat, "xxxxxxxxxxxxxxxx" } } } });
        add ({ "Lo-fi", "Dusty, swung and lazy: soft hats, a sleepy snare, behind the beat",
               false, 0.35f, true, fillMinimal, 0.3f, 0.5f, 1,
               { { { kick, "X.........X....." }, { stick, "....x.......x..." }, { closedHat, "x.x.x.x.x.x.x.x." } },
                 { { kick, "X.........X....." }, { snare, "....X.......X..." }, { closedHat, "x.x.x.x.x.x.x.x." } },
                 { { kick, "X......x..X....." }, { snare, "....X.......X..o" }, { closedHat, "xoxoxoxoxoxoxoxo" } },
                 { { kick, "X..x...x..X.x..." }, { snare, "....X.......X..o" }, { closedHat, "xoxoXoxoxoxoXoxo" }, { shaker, "x.x.x.x.x.x.x.x." } } } });
        add ({ "Disco", "Four on the floor, open hats on the off-beats, tambourine 16ths",
               false, 0.0f, false, fillSnareBuild, 0.1f, 0.2f, 0,
               { { { kick, "X...X...X...X..." }, { closedHat, "x...x...x...x..." } },
                 { { kick, "X...X...X...X..." }, { snare, "....X.......X..." }, { openHat, "..X...X...X...X." }, { closedHat, "x...x...x...x..." } },
                 { { kick, "X...X...X...X..." }, { snare, "....X.......X..." }, { openHat, "..X...X...X...X." }, { closedHat, "x...x...x...x..." }, { tambourine, "x.x.x.x.x.x.x.x." } },
                 { { kick, "X...X...X...X..." }, { snare, "....X.......X..." }, { clap, "....x.......x..." }, { openHat, "..X...X...X...X." }, { tambourine, "xxxxxxxxxxxxxxxx" } } } });
        add ({ "House / Dance", "Four on the floor with claps, off-beat hats and snare-roll builds",
               false, 0.0f, false, fillSnareBuild, 0.1f, 0.1f, 7,
               { { { kick, "X...X...X...X..." }, { closedHat, "..x...x...x...x." } },
                 { { kick, "X...X...X...X..." }, { clap, "....X.......X..." }, { openHat, "..x...x...x...x." } },
                 { { kick, "X...X...X...X..." }, { clap, "....X.......X..." }, { openHat, "..x...x...x...x." }, { closedHat, "xx.xxx.xxx.xxx.x" } },
                 { { kick, "X...X...X...X..." }, { clap, "....X.......X..." }, { snare, "....x.......x..." }, { openHat, "..x...x...x...x." }, { closedHat, "xx.xxx.xxx.xxx.x" }, { shaker, "x.x.x.x.x.x.x.x." } } } });
        add ({ "Reggae (one drop)", "Nothing on beat 1, kick and rim together on 3, skanking hats; steppers in the peaks",
               false, 0.3f, false, fillTriplet, 0.2f, 0.1f, 3,
               { { { stick, "........X......." }, { closedHat, "x.x.x.x.x.x.x.x." } },
                 { { kick, "........X......." }, { stick, "........X......." }, { closedHat, "x.x.x.x.x.x.x.x." } },
                 { { kick, "........X......." }, { snare, "........X......." }, { closedHat, "x.xxx.xxx.xxx.xx" }, { openHat, "......x........." } },
                 { { kick, "X...X...X...X..." }, { snare, "........X......." }, { closedHat, "x.x.x.x.x.x.x.x." }, { rideBell, "..x...x...x...x." } } } });
        add ({ "Blues / Shuffle", "Triplet shuffle: hats or ride swung hard, backbeat snare, kick walking with the bass",
               true, 0.9f, false, fillTriplet, 0.3f, 0.6f, 3,
               { { { kick, "x.....x....." }, { stick, "...x.....x.." }, { closedHat, "x.xx.xx.xx.x" } },
                 { { kick, "X.....X....." }, { snare, "...X.....X.." }, { closedHat, "x.xx.xx.xx.x" } },
                 { { kick, "X....xX....x" }, { snare, "...X.....X.." }, { ride, "x.xx.xx.xx.x" } },
                 { { kick, "X.x..xX.x..x" }, { snare, "...X..o..X.o" }, { ride, "X.xX.xX.xX.x" } } } });
        add ({ "Jazz (swing)", "Spang-a-lang ride, hi-hat foot on 2 and 4, feathered kick, comping snare",
               true, 1.0f, false, fillJazz, 0.6f, 0.3f, 8,
               { { { ride, "x..x.xx..x.x" }, { pedalHat, "...x.....x.." } },
                 { { ride, "x..x.xx..x.x" }, { pedalHat, "...x.....x.." }, { kick, "o..o..o..o.." }, { snare, "........o..." } },
                 { { ride, "X..x.xX..x.x" }, { pedalHat, "...x.....x.." }, { kick, "o..o..o..o.." }, { snare, ".....o..o..o" } },
                 { { ride, "X..x.xX..x.x" }, { pedalHat, "...x.....x.." }, { kick, "o..o..o.Xo.." }, { snare, "..o..o.o...o" } } } });
        add ({ "Bossa Nova", "Side-stick clave, gentle 16th hats or shaker, the bossa kick",
               false, 0.0f, false, fillLatin, 0.2f, 0.2f, 9,
               { { { stick, "x..x..x...x..x.." }, { shaker, "xoxoxoxoxoxoxoxo" } },
                 { { kick, "X..xX..xX..xX..x" }, { stick, "x..x..x...x..x.." }, { closedHat, "x.x.x.x.x.x.x.x." } },
                 { { kick, "X..xX..xX..xX..x" }, { stick, "x..x..x...x..x.." }, { closedHat, "xoxoxoxoxoxoxoxo" } },
                 { { kick, "X..xX..xX..xX..x" }, { stick, "x..x..x...x..x.." }, { ride, "x.x.x.x.x.x.x.x." } } } });
        add ({ "Folk / Brushes", "Brushes and soft hands: swishes, a gentle backbeat, kick on 1 and 3",
               false, 0.0f, false, fillMinimal, 0.4f, 0.4f, 9,
               { { { kick, "X.......X......." }, { snare, "oooooooooooooooo" } },
                 { { kick, "X.......X......." }, { snare, "o.o.X.o.o.o.X.o." }, { pedalHat, "....x.......x..." } },
                 { { kick, "X.....x.X......." }, { snare, "o.o.X.o.o.o.X.o." }, { closedHat, "x.x.x.x.x.x.x.x." } },
                 { { kick, "X.x...x.X.x....." }, { snare, "....X.......X..." }, { closedHat, "x.x.x.x.x.x.x.x." }, { tambourine, "....x.......x..." } } } });
        add ({ "Dream Pop", "Slow and roomy: rolling floor toms, a soft backbeat, shaker; ride in the swells",
               false, 0.0f, false, fillToms, 0.1f, 0.4f, 3,
               { { { kick, "X.......X......." }, { floorTom, "..x.......x....." }, { stick, "............x..." } },
                 { { kick, "X.......X......." }, { snare, "....X.......X..." }, { floorTom, "..x.......x....." }, { shaker, "x.x.x.x.x.x.x.x." } },
                 { { kick, "X.....x.X......." }, { snare, "....X.......X..." }, { floorTom, "x.x.x.x.x.x.x.x." } },
                 { { kick, "X.....x.X.x....." }, { snare, "....X.......X..." }, { ride, "x.x.x.x.x.x.x.x." }, { floorTom, "..x...x...x...x." } } } });
        return v;
    }();
    return s;
}

juce::StringArray styleNames()
{
    juce::StringArray n;
    for (auto& s : styles()) n.add (s.name);
    return n;
}

namespace
{
    int velocityOf (char c)
    {
        switch (c)
        {
            case 'X': return 112;
            case 'x': return 92;
            case 'o': return 40;
            default:  return 0;
        }
    }

    bool isHatLike (int n) { return n == closedHat || n == openHat || n == pedalHat || n == ride || n == rideBell || n == shaker || n == tambourine; }
    bool isTom (int n) { return n == floorTom || n == lowTom || n == midTom || n == highTom; }

    struct StepHit { int step; int note; int velocity; };

    /** The template for one bar: lanes for a level, with the cymbal choice and half / double time applied. */
    std::vector<StepHit> templateBar (const Style& st, int level, const Settings& set, int steps)
    {
        std::vector<StepHit> out;
        for (auto& lane : st.levels[juce::jlimit (0, 3, level)])
        {
            int n = lane.note;
            if (set.cymbals == 1 && (n == ride || n == rideBell)) n = closedHat;
            if (set.cymbals == 2 && (n == closedHat || n == openHat)) n = n == openHat ? rideBell : ride;
            const int len = (int) std::strlen (lane.steps);
            for (int s = 0; s < len; ++s)
            {
                const int vel = velocityOf (lane.steps[s]);
                if (vel == 0) continue;
                if (set.timeFeel == 1)
                {
                    // half-time: the first half of the bar stretched over the whole bar
                    if (s >= len / 2) continue;
                    out.push_back ({ s * 2 * steps / len, n, vel });
                }
                else if (set.timeFeel == 2)
                {
                    // double-time: the whole bar twice, squeezed (odd steps fold onto the grid)
                    if ((s & 1) != 0 && ! isHatLike (n)) continue;
                    out.push_back ({ (s / 2) * steps / len, n, vel });
                    out.push_back ({ (s / 2 + len / 2) * steps / len, n, vel });
                }
                else out.push_back ({ s * steps / len, n, vel });
            }
        }
        return out;
    }

    /** A bar for meters other than 4/4: kick on 1, backbeats where the meter puts them, 8ths on top. */
    std::vector<StepHit> meterBar (const Style& st, int level, const Settings& set, int tsNum, int tsDen, int steps)
    {
        std::vector<StepHit> out;
        // the style's time-keeper at this level
        int keeper = closedHat;
        for (auto& lane : st.levels[juce::jlimit (0, 3, level)])
            if (lane.note == ride || lane.note == closedHat || lane.note == openHat || lane.note == shaker || lane.note == floorTom) { keeper = lane.note; break; }
        if (set.cymbals == 1) keeper = closedHat;
        if (set.cymbals == 2) keeper = ride;
        const bool compound = tsDen == 8 && tsNum % 3 == 0;          // 6/8, 9/8, 12/8: groups of three 8ths
        const int eighth = 2;                                        // 16th steps per 8th
        const int group = compound ? 3 * eighth : 4;                 // steps per main pulse
        const int pulses = juce::jmax (1, steps / group);
        for (int s = 0; s < steps; s += eighth)
            out.push_back ({ s, keeper, s % group == 0 ? 96 : 78 });
        out.push_back ({ 0, note::kick, 112 });
        if (level >= 2 && pulses >= 4) out.push_back ({ (pulses / 2) * group, note::kick, 98 });
        if (compound)
        {
            for (int p = 1; p < pulses; p += 2) out.push_back ({ p * group, level == 0 ? note::stick : note::snare, 104 });
        }
        else if (tsNum == 3)
        {
            out.push_back ({ group, level == 0 ? note::stick : note::snare, 84 });
            out.push_back ({ 2 * group, level == 0 ? note::stick : note::snare, 84 });
        }
        else
        {
            // 5/4, 7/8...: backbeats on the even pulses, the last one strongest
            for (int p = 1; p < pulses; p += 2) out.push_back ({ p * group, level == 0 ? note::stick : note::snare, p + 2 >= pulses ? 110 : 100 });
        }
        return out;
    }

    /** A fill over the last `beats` beats of a bar that has `steps` steps (4 or 3 per beat). */
    std::vector<StepHit> fill (const Style& st, int steps, int perBeat, int beats, float energy, juce::Random& rng)
    {
        std::vector<StepHit> out;
        const int from = steps - beats * perBeat;
        const int n = beats * perBeat;
        auto vel = [&] (int i) { return juce::jlimit (30, 127, (int) (70.0f + 45.0f * (float) i / (float) juce::jmax (1, n - 1) + energy * 10.0f)); };
        switch (st.fill)
        {
            case fillSnareBuild:
                for (int i = 0; i < n; ++i) out.push_back ({ from + i, note::snare, vel (i) });
                if (beats >= 2) for (int i = 0; i < n; i += perBeat) out.push_back ({ from + i, note::kick, 100 });
                break;
            case fillFunk:
            {
                const char* pat = "X.oxX.xoX.oxXxXx";
                for (int i = 0; i < n; ++i)
                {
                    const int v = velocityOf (pat[(i + 16 - n % 16) % 16]);
                    if (v > 0) out.push_back ({ from + i, (i >= n - perBeat && (i & 1)) ? note::highTom : note::snare, v });
                }
                out.push_back ({ from, note::kick, 100 });
                break;
            }
            case fillMinimal:
                // a pickup: snare flam / open hat on the last off-beat, a kick before the one
                out.push_back ({ steps - perBeat / 2 - 1, note::snare, 70 });
                out.push_back ({ steps - 1, note::snare, 96 });
                out.push_back ({ steps - perBeat, note::kick, 90 });
                if (rng.nextBool()) out.push_back ({ steps - perBeat / 2, note::openHat, 88 });
                break;
            case fillTriplet:
            {
                const int toms[] = { note::snare, note::snare, note::highTom, note::midTom, note::lowTom, note::floorTom };
                for (int i = 0; i < n; ++i)
                    if (perBeat == 3 || (i % 2) == 0)
                        out.push_back ({ from + i, toms[(i * 6) / juce::jmax (1, n)], vel (i) });
                break;
            }
            case fillJazz:
                for (int i = 0; i < n; ++i)
                    if (rng.nextFloat() < 0.45f) out.push_back ({ from + i, note::snare, 50 + rng.nextInt (40) });
                out.push_back ({ steps - 1, note::kick, 108 });   // the "bomb" into the next bar
                break;
            case fillLatin:
                for (int i = 0; i < n; i += 2) out.push_back ({ from + i, i >= n - perBeat ? note::highTom : note::stick, vel (i) });
                break;
            default:
            {
                // around the kit: snare, high, mid, low, floor, with the kick under the last beat
                const int toms[] = { note::snare, note::highTom, note::midTom, note::lowTom, note::floorTom };
                const bool sixteenths = energy > 0.25f || beats <= 1;
                for (int i = 0; i < n; ++i)
                {
                    if (! sixteenths && (i % 2) == 1) continue;
                    const int which = juce::jmin (4, (i * 5) / juce::jmax (1, n));
                    out.push_back ({ from + i, toms[which], vel (i) });
                }
                if (beats >= 2) out.push_back ({ steps - perBeat, note::kick, 104 });
                break;
            }
        }
        return out;
    }
}

int suggestStyle (const SongMap& m)
{
    auto index = [] (const char* name) { return juce::jmax (0, styleNames().indexOf (name)); };
    if (m.isEmpty()) return 0;
    if (m.tripletFeel) return index ("Blues / Shuffle");
    if (m.swing > 0.45f && m.tempo < 105.0) return index ("Hip-Hop (boom bap)");
    if (m.tempo >= 165.0) return index ("Punk");
    if (m.tempo <= 76.0) return index ("Dream Pop");
    return 0;
}

Beat generate (const SongMap& input, const Settings& set)
{
    Beat beat;
    const auto& st = styles()[(size_t) juce::jlimit (0, (int) styles().size() - 1, set.style)];
    SongMap map = input;
    if (map.isEmpty())
    {
        // nothing to listen to: 8 bars of the style
        map.bars = 8;
        map.bar.assign (8, SongMap::Bar());
        for (auto& b : map.bar) { b.active = true; b.energy = 0.6f; b.accents.assign ((size_t) map.stepsPerBar, 0.0f); b.low = b.accents; }
        map.firstActiveBar = 0; map.lastActiveBar = 7;
        map.sections = { { 0, 8, "Groove", 2, 0.6f, false } };
    }
    beat.beatsPerBar = map.beatsPerBar;
    const bool fourFour = map.tsNum == 4 && map.tsDen == 4;
    const int perBeat = fourFour && st.triplet ? 3 : 4;
    const int steps = fourFour ? 4 * perBeat : map.stepsPerBar;
    const double stepBeats = map.beatsPerBar / steps;
    const double msToBeats = map.tempo / 60000.0;
    const float swing = juce::jlimit (0.0f, 1.0f, set.swingAuto ? (input.isEmpty() ? st.swing : juce::jmax (map.swing, st.swing * 0.5f)) : set.swing);
    juce::Random human ((juce::int64) set.seed * 7919 + 13);

    // what the band does, section by section: where the low end and the accents land (for the kick to follow)
    for (size_t si = 0; si < map.sections.size(); ++si)
    {
        const auto& sec = map.sections[si];
        PlayedSection ps;
        ps.section = sec;
        int level = sec.intensity;
        if (auto it = set.sectionLevel.find (sec.startBar); it != set.sectionLevel.end()) level = it->second;
        else if (sec.silent) level = 4;
        if (level < 4) level = juce::jlimit (0, 3, level + juce::roundToInt (set.energy * 1.4f));
        ps.level = level >= 4 ? -1 : level;
        if (level >= 4) { beat.sections.push_back (ps); continue; }

        std::vector<float> lowAvg ((size_t) map.stepsPerBar, 0.0f), accAvg = lowAvg;
        int counted = 0;
        for (int b = sec.startBar; b < sec.startBar + sec.bars && b < map.bars; ++b)
        {
            if (! map.bar[(size_t) b].active) continue;
            for (int s = 0; s < map.stepsPerBar; ++s)
            {
                lowAvg[(size_t) s] += map.bar[(size_t) b].low[(size_t) s];
                accAvg[(size_t) s] += map.bar[(size_t) b].accents[(size_t) s];
            }
            ++counted;
        }
        for (int s = 0; s < map.stepsPerBar; ++s) { lowAvg[(size_t) s] /= (float) juce::jmax (1, counted); accAvg[(size_t) s] /= (float) juce::jmax (1, counted); }
        auto songAt = [&] (const std::vector<float>& v, int step)   // our step -> the song's 16th grid
        {
            const int s16 = juce::jlimit (0, map.stepsPerBar - 1, juce::roundToInt ((double) step * map.stepsPerBar / steps));
            return v[(size_t) s16];
        };

        const bool nextSilentOrEnd = si + 1 >= map.sections.size() || map.sections[si + 1].silent;
        const float velScale = 0.82f + 0.07f * (float) level + 0.1f * set.energy;

        for (int bi = 0; bi < sec.bars; ++bi)
        {
            const int bar = sec.startBar + bi;
            if (bar >= map.bars) break;
            const double barStart = bar * map.beatsPerBar;
            // variations repeat in 2- and 4-bar phrases (the same seed for the same place in the phrase)
            juce::Random rng ((juce::int64) set.seed * 1000003 + (juce::int64) si * 7717 + (bi % 4) * 131 + 7);
            auto hits = fourFour ? templateBar (st, level, set, steps) : meterBar (st, level, set, map.tsNum, map.tsDen, steps);

            // complexity: thin the time-keeping out, or add ghost notes and extra kicks
            const float c = set.complexity;
            if (c < 0.3f)
                hits.erase (std::remove_if (hits.begin(), hits.end(), [&] (const StepHit& h)
                {
                    return isHatLike (h.note) && (h.step % perBeat) != 0 && rng.nextFloat() < (0.3f - c) / 0.3f;
                }), hits.end());
            if (c > 0.35f && fourFour && st.ghosts > 0.0f)
                for (int s = 0; s < steps; ++s)
                {
                    const bool nearBackbeat = std::any_of (hits.begin(), hits.end(), [&] (const StepHit& h) { return h.note == note::snare && std::abs (h.step - s) <= 1; });
                    if (! nearBackbeat && (s % perBeat) != 0 && rng.nextFloat() < (c - 0.35f) * st.ghosts * 0.5f)
                        hits.push_back ({ s, note::snare, 30 + rng.nextInt (16) });
                }
            if (c > 0.55f && (bi % 2) == 1 && fourFour)
            {
                const int s = (rng.nextBool() ? 3 : 7) * perBeat / 2;   // a syncopated kick in the second bar of a pair
                if (rng.nextFloat() < (c - 0.55f) * 1.5f) hits.push_back ({ juce::jmin (steps - 1, s), note::kick, 84 });
            }

            // follow the band: kick where the low end hits, not where it leaves space
            if (set.follow > 0.01f && counted > 0 && st.kickFollow > 0.0f && level > 0)
            {
                const float f = set.follow * st.kickFollow;
                for (int s = 0; s < steps; ++s)
                {
                    const float low = juce::jmax (songAt (lowAvg, s), 0.6f * songAt (map.bar[(size_t) bar].low, s));
                    const bool hasKick = std::any_of (hits.begin(), hits.end(), [&] (const StepHit& h) { return h.step == s && h.note == note::kick; });
                    const bool snareThere = std::any_of (hits.begin(), hits.end(), [&] (const StepHit& h) { return h.step == s && (h.note == note::snare || h.note == note::clap) && h.velocity > 80; });
                    if (! hasKick && ! snareThere && low > 0.55f && rng.nextFloat() < f * (low - 0.3f) * 1.4f)
                        hits.push_back ({ s, note::kick, juce::jlimit (70, 110, (int) (70.0f + 40.0f * low)) });
                    if (hasKick && s != 0 && low < 0.12f && songAt (accAvg, s) < 0.15f && rng.nextFloat() < f * 0.6f)
                        hits.erase (std::remove_if (hits.begin(), hits.end(), [&] (const StepHit& h) { return h.step == s && h.note == note::kick; }), hits.end());
                }
                // a busy band: 16th hats instead of 8ths
                if (map.bar[(size_t) bar].density > 0.6f && c > 0.4f && perBeat == 4)
                    for (int s = 1; s < steps; s += 2)
                        if (std::any_of (hits.begin(), hits.end(), [&] (const StepHit& h) { return h.step == s - 1 && h.note == note::closedHat; })
                            && ! std::any_of (hits.begin(), hits.end(), [&] (const StepHit& h) { return h.step == s && isHatLike (h.note); }))
                            hits.push_back ({ s, note::closedHat, 60 });
            }

            // fills: at the end of a section, and now and then at the end of 4- and 8-bar phrases
            int fillBeats = 0;
            const bool lastBar = bi == sec.bars - 1;
            const int beatsInBar = juce::jmax (1, (int) std::round (map.beatsPerBar));
            if (lastBar && ! nextSilentOrEnd && set.fills > 0.1f)
                fillBeats = set.fills > 0.75f ? juce::jmin (beatsInBar, 2 + (level >= 2 ? 1 : 0)) : set.fills > 0.4f ? 2 : 1;
            else if (lastBar && nextSilentOrEnd && set.fills > 0.3f && si + 1 < map.sections.size())
                fillBeats = 1;
            else if (! lastBar && (bi % 8) == 7 && rng.nextFloat() < set.fills * 0.9f)
                fillBeats = set.fills > 0.6f ? 2 : 1;
            else if (! lastBar && (bi % 4) == 3 && rng.nextFloat() < set.fills * 0.45f)
                fillBeats = 1;
            fillBeats = juce::jmin (fillBeats, beatsInBar);
            if (fillBeats > 0)
            {
                const int from = steps - fillBeats * (steps / beatsInBar);
                hits.erase (std::remove_if (hits.begin(), hits.end(), [&] (const StepHit& h)
                {
                    return h.step >= from && (h.note != note::kick || st.fill == fillMinimal);
                }), hits.end());
                auto f = fill (st, steps, steps / beatsInBar, fillBeats, (float) level / 3.0f + set.energy * 0.3f, rng);
                hits.insert (hits.end(), f.begin(), f.end());
            }

            // crash at the top of a section (and after phrase fills in loud parts)
            const bool sectionTop = bi == 0 && (level >= 1 || si == 0);
            const bool afterFill = bi > 0 && (bi % 8) == 0 && level >= 2;
            if ((sectionTop && level >= 2) || (sectionTop && si > 0 && level >= 1) || afterFill)
            {
                hits.erase (std::remove_if (hits.begin(), hits.end(), [] (const StepHit& h) { return h.step == 0 && (h.note == note::closedHat || h.note == note::ride); }), hits.end());
                hits.push_back ({ 0, note::crash, juce::jlimit (60, 124, 100 + level * 6) });
                if (! std::any_of (hits.begin(), hits.end(), [] (const StepHit& h) { return h.step == 0 && h.note == note::kick; }))
                    hits.push_back ({ 0, note::kick, 108 });
            }

            // into beats: swing, push / lay back, humanise, velocity
            for (auto& h : hits)
            {
                double pos = h.step * stepBeats;
                if (perBeat == 4 && fourFour)
                {
                    const int inBeat = h.step % 4;
                    if (st.swing16) { if (inBeat == 1 || inBeat == 3) pos += swing * stepBeats / 3.0; }
                    else if (inBeat == 2) pos += swing * (2.0 / 3.0 - 0.5);
                    else if (inBeat == 3) pos += swing * (2.0 / 3.0 - 0.5) * 0.5;
                }
                double ms = 0.0;
                if (h.note == note::snare || h.note == note::clap || h.note == note::stick) ms += 16.0 * set.feel;
                else if (isHatLike (h.note)) ms += 6.0 * set.feel;
                ms += (human.nextFloat() + human.nextFloat() - 1.0f) * 7.0f * set.humanize;
                pos = juce::jmax (0.0, pos + ms * msToBeats);
                int v = juce::roundToInt ((float) h.velocity * velScale * (h.step == 0 ? 1.04f : 1.0f));
                v += juce::roundToInt ((human.nextFloat() + human.nextFloat() - 1.0f) * 9.0f * set.humanize);
                ps.hits.push_back ({ barStart + pos, h.note, juce::jlimit (1, 127, v) });
            }
        }
        std::sort (ps.hits.begin(), ps.hits.end(), [] (const Hit& a, const Hit& b) { return a.beat < b.beat; });
        beat.sections.push_back (ps);
    }

    // the last hit: crash and kick on the downbeat after the music stops
    if (set.endHit && ! beat.sections.empty())
    {
        const auto& last = beat.sections.back();
        if (last.level >= 0)
        {
            beat.endingBeat = (last.section.startBar + last.section.bars) * map.beatsPerBar;
            beat.ending = { { beat.endingBeat, note::crash, 118 }, { beat.endingBeat, note::kick, 112 } };
        }
    }
    return beat;
}

int write (Project& p, const Track& track, const Beat& beat, const juce::String& styleName)
{
    // the drummer's earlier clips go; anything else on the track stays
    for (int i = track.clips().getNumChildren(); --i >= 0;)
    {
        auto c = track.clips().getChild (i);
        if ((bool) c.getProperty ("drummer", false)) track.clips().removeChild (i, p.um());
    }
    int written = 0;
    auto addClip = [&] (double start, double length, const juce::String& name, const std::vector<Hit>& hits)
    {
        auto c = p.addMidiClip (track, start, length, name);
        c.v.setProperty ("drummer", true, p.um());
        for (auto& h : hits)
        {
            const double rel = h.beat - start;
            if (rel < -0.05 || rel >= length) continue;
            juce::ValueTree n (ids::NOTE);
            n.setProperty (ids::p, h.note, nullptr);
            n.setProperty (ids::s, juce::jmax (0.0, rel), nullptr);
            n.setProperty (ids::l, 0.12, nullptr);
            n.setProperty (ids::v, h.velocity, nullptr);
            c.v.appendChild (n, nullptr);   // the clip's own undo step brings its notes with it
            ++written;
        }
        return c;
    };
    for (auto& s : beat.sections)
    {
        if (s.level < 0) continue;
        addClip (s.section.startBar * beat.beatsPerBar, s.section.bars * beat.beatsPerBar, s.section.name + "  -  " + styleName, s.hits);
    }
    if (! beat.ending.empty())
        addClip (beat.endingBeat, beat.beatsPerBar, "Ending", beat.ending);
    return written;
}

} // namespace wis::daw::drummer
