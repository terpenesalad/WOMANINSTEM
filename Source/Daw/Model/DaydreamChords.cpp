#include "DaydreamChords.h"
#include "ChordKeys.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace wis::daw::daydream
{

juce::String buttonName (int b)
{
    static const char* n[] = { "Maj", "Min", "Sus", "Dim", "6", "m7", "M7", "9" };
    return juce::isPositiveAndBelow (b, (int) numButtons) ? n[b] : "";
}

juce::String buttonLongName (int b)
{
    static const char* n[] = { "Major", "Minor", "Suspended", "Diminished", "Add 6th", "Minor 7th", "Major 7th", "Add 9th" };
    return juce::isPositiveAndBelow (b, (int) numButtons) ? n[b] : "";
}

std::vector<int> intervalsFor (unsigned buttons, bool sus2)
{
    if ((buttons & typeMask) == 0) return {};
    std::set<int> s;
    if (buttons & (1u << bMaj)) s.insert ({ 0, 4, 7 });
    if (buttons & (1u << bMin)) s.insert ({ 0, 3, 7 });
    if (buttons & (1u << bSus)) s.insert ({ 0, sus2 ? 2 : 5, 7 });
    if (buttons & (1u << bDim)) s.insert ({ 0, 3, 6 });
    if (buttons & (1u << b6))   s.insert (9);
    if (buttons & (1u << bm7))  s.insert (10);
    if (buttons & (1u << bM7))  s.insert (11);
    if (buttons & (1u << b9))   s.insert (14);
    return { s.begin(), s.end() };
}

juce::StringArray scaleNames()
{
    return { "Major", "Minor", "Dorian", "Mixolydian", "Lydian", "Phrygian", "Harmonic Minor", "Melodic Minor" };
}

const std::vector<int>& scaleSteps (int scale)
{
    static const std::vector<std::vector<int>> s {
        { 0, 2, 4, 5, 7, 9, 11 },   // major
        { 0, 2, 3, 5, 7, 8, 10 },   // natural minor
        { 0, 2, 3, 5, 7, 9, 10 },   // dorian
        { 0, 2, 4, 5, 7, 9, 10 },   // mixolydian
        { 0, 2, 4, 6, 7, 9, 11 },   // lydian
        { 0, 1, 3, 5, 7, 8, 10 },   // phrygian
        { 0, 2, 3, 5, 7, 8, 11 },   // harmonic minor
        { 0, 2, 3, 5, 7, 9, 11 },   // melodic minor
    };
    return s[(size_t) juce::jlimit (0, (int) s.size() - 1, scale)];
}

Chord inKey (int pc, int tonic, int scale, unsigned buttons)
{
    const auto& st = scaleSteps (scale);
    const int rel = ((pc - tonic) % 12 + 12) % 12;
    int degree = 0;
    {
        int best = 99;
        for (int d = 0; d < 7; ++d)
        {
            const int down = (rel - st[(size_t) d] + 12) % 12;      // how far the scale note is below
            const int up = (st[(size_t) d] - rel + 12) % 12;
            const int dist = juce::jmin (down, up);
            const int score = dist * 2 + (down <= up ? 0 : 1);       // a tie goes down
            if (score < best) { best = score; degree = d; }
        }
    }
    auto above = [&] (int k)
    {
        const int idx = degree + k;
        return st[(size_t) (idx % 7)] + 12 * (idx / 7) - st[(size_t) degree];
    };
    Chord c;
    c.root = (tonic + st[(size_t) degree]) % 12;
    c.intervals = { 0, above (2), above (4) };
    if (buttons & (1u << b6)) c.intervals.push_back (above (5));
    if (buttons & ((1u << bm7) | (1u << bM7))) c.intervals.push_back (above (6));
    if (buttons & (1u << b9)) c.intervals.push_back (above (8));
    std::sort (c.intervals.begin(), c.intervals.end());
    c.intervals.erase (std::unique (c.intervals.begin(), c.intervals.end()), c.intervals.end());
    return c;
}

juce::String noteName (int midiNote, int keyTonic)
{
    return chordkeys::pitchName (midiNote, keyTonic) + juce::String (midiNote / 12 - 1);
}

juce::String chordName (int rootPc, const std::vector<int>& iv, int keyTonic, int bassPc)
{
    if (iv.empty()) return {};
    bool low[12] {}, high[12] {};
    for (int i : iv)
    {
        if (i < 0) continue;
        if (i < 12) low[i] = true; else high[i % 12] = true;
    }
    bool covered[24] {};
    covered[0] = true;
    auto cov = [&] (int s) { covered[s] = true; };
    const bool m3 = low[3], M3 = low[4], p4 = low[5], s2 = low[2], b5 = low[6], p5 = low[7], s5 = low[8],
               six = low[9], b7 = low[10], M7 = low[11], n9 = high[2];
    if (p5) cov (7);
    if (n9) {}   // decided per chord below

    juce::String sfx;
    auto seventh = [&] (const char* dom, const char* dom9, const char* maj, const char* maj9, bool allowDom = true) -> bool
    {
        if (b7 && allowDom) { cov (10); if (n9 && dom9) { cov (14); sfx << dom9; } else sfx << dom; return true; }
        if (M7) { cov (11); if (n9 && maj9) { cov (14); sfx << maj9; } else sfx << maj; return true; }
        return false;
    };

    if (M3 && ! m3)
    {
        cov (4);
        if (s5 && ! p5)
        {
            cov (8);
            if (! seventh ("aug7", "aug9", "maj7#5", "maj9#5")) sfx << "aug";
        }
        else if (! seventh ("7", "9", "maj7", "maj9"))
        {
            if (six) { cov (9); if (n9) { cov (14); sfx << "6/9"; } else sfx << "6"; }
            else if (n9) { cov (14); sfx << "add9"; }
        }
    }
    else if (m3 && ! M3)
    {
        cov (3);
        if (b5 && ! p5)
        {
            cov (6);
            if (six && ! b7) { cov (9); sfx << "dim7"; }
            else if (b7) { cov (10); sfx << "m7b5"; }
            else sfx << "dim";
        }
        else if (! seventh ("m7", "m9", "m(maj7)", "m(maj9)"))
        {
            if (six) { cov (9); if (n9) { cov (14); sfx << "m6/9"; } else sfx << "m6"; }
            else if (n9) { cov (14); sfx << "m(add9)"; }
            else sfx << "m";
        }
    }
    else if (! m3 && ! M3)
    {
        if (p4 || s2)
        {
            const char* kind = p4 ? "sus4" : "sus2";
            cov (p4 ? 5 : 2);
            if (b7) { cov (10); if (n9) { cov (14); sfx << "9" << kind; } else sfx << "7" << kind; }
            else if (M7) { cov (11); sfx << "maj7" << kind; }
            else if (six) { cov (9); sfx << "6" << kind; }
            else { sfx << kind; if (n9) { cov (14); sfx << "(add9)"; } }
        }
        else if (! seventh ("7(no3)", nullptr, "maj7(no3)", nullptr))
            sfx << (p5 ? "5" : "");
    }
    else
    {
        // both thirds: call it major and list the minor third as a #9
        cov (4);
        if (! seventh ("7", "9", "maj7", "maj9") && six) cov (9), sfx << "6";
    }

    // anything not explained yet
    juce::StringArray adds;
    static const char* lowNames[] = { "", "b9", "9", "#9", "", "11", "b5", "", "#5", "6", "7", "maj7" };
    static const char* highNames[] = { "", "b9", "9", "#9", "10", "11", "#11", "", "b13", "13", "7", "maj7" };
    for (int s = 1; s < 12; ++s)
        if (low[s] && ! covered[s] && lowNames[s][0] != 0) adds.add (lowNames[s]);
    for (int s = 0; s < 12; ++s)
        if (high[s] && ! covered[12 + s] && ! low[s] && highNames[s][0] != 0) adds.add (highNames[s]);
    if (! adds.isEmpty()) sfx << "(" << (sfx.isEmpty() ? "add" : "") << adds.joinIntoString (",") << ")";

    juce::String name = chordkeys::pitchName (rootPc, keyTonic) + sfx;
    if (bassPc >= 0 && ((bassPc - rootPc) % 12 + 12) % 12 != 0)
        name << "/" << chordkeys::pitchName (bassPc, keyTonic);
    return name;
}

juce::StringArray positionNames() { return { "Follow the Key You Press", "Stay in One Place" }; }
juce::StringArray spreadNames()   { return { "Close", "Open (drop 2)", "Open (drop 3)", "Wide (root down)" }; }

void voicingStep (std::vector<int>& n, int dir)
{
    if (n.empty()) return;
    if (dir > 0) { auto it = std::min_element (n.begin(), n.end()); *it += 12; }
    else if (dir < 0) { auto it = std::max_element (n.begin(), n.end()); *it -= 12; }
    std::sort (n.begin(), n.end());
}

std::vector<int> place (const Chord& c, int position, int centre, int spread, int voicing)
{
    std::vector<int> n;
    for (int i : c.intervals) n.push_back (48 + ((c.root % 12) + 12) % 12 + i);
    std::sort (n.begin(), n.end());
    if (n.empty()) return n;

    if (n.size() >= 3)
    {
        switch (spread)
        {
            case drop2: n[n.size() - 2] -= 12; break;
            case drop3: n[n.size() >= 4 ? n.size() - 3 : n.size() - 2] -= 12; break;
            case wide:  n[0] -= 12; if (n.size() >= 4) n[n.size() - 2] -= 12; break;
            default: break;
        }
        std::sort (n.begin(), n.end());
    }

    if (position == stayCentred)
    {
        auto meanOf = [] (const std::vector<int>& v) { double s = 0; for (int x : v) s += x; return s / (double) v.size(); };
        std::vector<int> best = n;
        double bestScore = 1e9;
        for (int k = -8; k <= 8; ++k)
        {
            auto t = n;
            for (int i = 0; i < std::abs (k); ++i) voicingStep (t, k > 0 ? 1 : -1);
            const double score = std::abs (meanOf (t) - centre) + 0.01 * std::abs (k);
            if (score < bestScore) { bestScore = score; best = t; }
        }
        n = best;
    }

    for (int i = 0; i < std::abs (voicing); ++i) voicingStep (n, voicing > 0 ? 1 : -1);
    while (n.back() > 108) for (auto& x : n) x -= 12;
    while (n.front() < 21) for (auto& x : n) x += 12;
    return n;
}

int bassNote (const Chord& c, int bassOctave, int bassVoicing)
{
    bool pcs[12] {};
    for (int i : c.intervals) pcs[((c.root + i) % 12 + 12) % 12] = true;
    const int root = ((c.root % 12) + 12) % 12;
    pcs[root] = true;
    std::vector<int> ladder;
    for (int n = 12; n <= 84; ++n) if (pcs[n % 12]) ladder.push_back (n);
    const int anchor = 12 * (juce::jlimit (0, 4, bassOctave) + 1) + root;
    const int idx = (int) (std::find (ladder.begin(), ladder.end(), anchor) - ladder.begin());
    return ladder[(size_t) juce::jlimit (0, (int) ladder.size() - 1, idx + bassVoicing)];
}

// ---- patterns ---------------------------------------------------------------------------------------------

namespace
{
    /** Tokens per step: "C" whole chord, "B" the bass note, digits = chord-note indices (comma separated),
        ":n" = lasts n steps, "!" accent, "'" soft, "." = nothing starts. */
    Pattern parsePattern (const char* name, const char* text)
    {
        Pattern p { name, 0, {} };
        const auto tokens = juce::StringArray::fromTokens (text, " ", "");
        for (auto t : tokens)
        {
            t = t.trim();
            if (t.isEmpty()) continue;
            const int step = p.steps++;
            if (t == ".") continue;
            PatternStep s;
            s.velocity = 0.82f;
            if (t.endsWithChar ('!')) { s.velocity = 1.0f; t = t.dropLastCharacters (1); }
            else if (t.endsWithChar ('\'')) { s.velocity = 0.55f; t = t.dropLastCharacters (1); }
            if (t.containsChar (':')) { s.length = juce::jmax (1, t.fromFirstOccurrenceOf (":", false, false).getIntValue()); t = t.upToFirstOccurrenceOf (":", false, false); }
            for (auto part : juce::StringArray::fromTokens (t, ",", ""))
            {
                if (part == "C") s.notes.push_back (99);
                else if (part == "B") s.notes.push_back (-1);
                else s.notes.push_back (part.getIntValue());
            }
            p.events.push_back ({ step, s });
        }
        return p;
    }
}

const std::vector<Pattern>& patterns()
{
    static const std::vector<Pattern> p {
        parsePattern ("Eighth Pulse",     "C! . C' . C . C' . C! . C' . C . C' ."),
        parsePattern ("Offbeat Stabs",    ". . C! . . . C . . . C! . . . C ."),
        parsePattern ("Broken Chord",     "0:2! . 2:2 . 1:2 . 3:2 . 0:2! . 2:2 . 1:2 . 3:2 ."),
        parsePattern ("Rolling Sixteenths", "0! 2 1 2 0 2 1 2 0! 2 1 2 0 2 1 2"),
        parsePattern ("Bossa Comp",       "C:2! . . C:2 . . C:2 . . . C:2 . . C:2 . ."),
        parsePattern ("Motorik Chug",     "C! C' C C' C! C' C C' C! C' C C' C! C' C C'"),
        parsePattern ("Rise and Fall",    "0! 1 2 3 4 3 2 1 0! 1 2 3 4 3 2 1"),
        parsePattern ("Gallop",           "C:2! . C C C:2! . C C C:2! . C C C:2 . C C"),
        parsePattern ("Two Hands",        "B:2! . C:2 . 0 2 C:2 . B:2 . C:2 . 1 3 C:2 ."),
        parsePattern ("Slow Swells",      "C:6! . . . . . C:6 . . . . . C:4 . . ."),
        parsePattern ("Ballad Picking",   "B:4! . 0 1 2 3 2 1 B:4 . 0 1 2 3 2 1"),
        parsePattern ("Half-Time Hits",   "C:4! . . . . . . . . . . C:2 . . C:2' ."),
        parsePattern ("Shuffle Strum",    "C:2! . C C:2 . C C:2! . C C:2 . C"),
        parsePattern ("Waltz",            "B:4! . . . C:4 . . . C:4' . . ."),
        parsePattern ("Twinkle Up",       "0' 1' 2' 3' 4 5 6 7! 8 9 10 11 12! . . ."),
        parsePattern ("Push and Pull",    "C:3! . . C:3 . . C:2 . 0 1 C:3! . . C:2 . ."),
    };
    return p;
}

juce::StringArray patternNames()
{
    juce::StringArray n;
    for (auto& p : patterns()) n.add (p.name);
    return n;
}

// ---- grooves ---------------------------------------------------------------------------------------------

const std::vector<Groove>& grooves()
{
    // GM drum notes: 36 kick, 37 rim, 38 snare, 39 clap, 42 closed hat, 46 open hat, 54 tambourine,
    // 56 cowbell, 63/64 congas, 70 shaker, 75 clave
    static const std::vector<Groove> g {
        { "Easy Pop", 16, 0.0f, { { 36, "X.......x.X....." }, { 38, "....X.......X..." }, { 42, "x.o.x.o.x.o.x.o." }, { 54, "....o.......o..." } } },
        { "Laid-Back Hip-Hop", 16, 0.58f, { { 36, "X.....x...x....." }, { 38, "....X.......X..." }, { 42, "x.x.x.x.x.x.x.xo" }, { 37, "...........o...." } } },
        { "Four on the Floor", 16, 0.0f, { { 36, "X...X...X...X..." }, { 39, "....X.......X..." }, { 42, "x.x.x.x.x.x.x.x." }, { 46, "..x...x...x...x." } } },
        { "Motorik", 16, 0.0f, { { 36, "X.x.X.x.X.x.X.x." }, { 38, "....X.......X..." }, { 42, "xoxoxoxoxoxoxoxo" } } },
        { "Breakbeat", 16, 0.12f, { { 36, "X.........X..x.." }, { 38, "....X..o.o..X..o" }, { 42, "x.x.x.x.x.x.x.x." }, { 46, "..............x." } } },
        { "Bossa", 16, 0.0f, { { 36, "X..xX..xX..xX..x" }, { 37, "x..x..x...x..x.." }, { 42, "o.o.o.o.o.o.o.o." }, { 70, "xoxoxoxoxoxoxoxo" } } },
        { "Disco Strut", 16, 0.0f, { { 36, "X...X...X...X..." }, { 38, "....X.......X..." }, { 46, "..X...X...X...X." }, { 42, "x.o.x.o.x.o.x.o." }, { 54, "xoxoxoxoxoxoxoxo" } } },
        { "Slow Jam 12/8", 12, 0.0f, { { 36, "X.....x..x.." }, { 38, "...X.....X.." }, { 42, "xoxxoxxoxxox" } } },
        { "Psych Shuffle", 12, 0.0f, { { 36, "X.....X.x..." }, { 38, "...X.....X.." }, { 42, "x.ox.ox.ox.o" }, { 54, "...o.....o.." } } },
        { "Lo-Fi Dust", 16, 0.62f, { { 36, "X......x..X....." }, { 37, "....X.......X..." }, { 42, "o.o.o.o.o.o.o.oo" }, { 70, "..o...o...o...o." } } },
        { "One Drop", 16, 0.3f, { { 36, "........X......." }, { 37, "........X......." }, { 42, "x.x.x.x.x.x.x.x." }, { 46, "..............o." } } },
        { "Afro Groove", 16, 0.15f, { { 36, "X.....X...X....." }, { 38, "....x.......x..o" }, { 56, "x.x.xx.x.x.xx.x." }, { 63, "..o..o..o..o.o.." }, { 64, "o.....o.....o..." }, { 42, "xxxxxxxxxxxxxxxx" } } },
        { "Half-Time", 16, 0.0f, { { 36, "X.....x...x....." }, { 38, "........X......." }, { 42, "x.x.x.x.x.x.x.x." } } },
        { "Soft Brushes", 16, 0.5f, { { 36, "o.......o......." }, { 38, "....o.......o..." }, { 70, "xoxoxoxoxoxoxoxo" }, { 37, "..........o....." } } },
        { "Krautrock Toms", 16, 0.0f, { { 36, "X...X...X...X..." }, { 45, "..x...x...x..x.x" }, { 38, "....X.......X..." }, { 42, "o.o.o.o.o.o.o.o." } } },
    };
    return g;
}

juce::StringArray grooveNames()
{
    juce::StringArray n { "Off" };
    for (auto& g : grooves()) n.add (g.name);
    return n;
}

} // namespace wis::daw::daydream
