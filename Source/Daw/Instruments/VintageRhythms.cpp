#include "VintageRhythms.h"
#include "Daw/Model/Project.h"

namespace wis::daw
{

// GM drum notes
enum { K = 36, RIM = 37, SN = 38, CLAP = 39, CH = 42, PH = 44, OH = 46, LT = 41, MT = 45, HT = 48, CR = 49, RD = 51,
       TAMB = 54, BELL = 56, HB = 60, LB = 61, HC = 63, LC = 64, MAR = 70, CLV = 75 };

const std::vector<VintageRhythm>& vintageRhythms()
{
    static const std::vector<VintageRhythm> list = {
        // 12/8 ballad feel: the dreamy organ-and-drum-machine sound
        { "Slow Rock", 64.0f, 4, 12,
          { { K, "X.....X....." }, { SN, "...X.....X.." }, { CH, "xooxooxooxoo" } },
          "R.....5.....", "XxxXxxXxxXxx" },
        { "Waltz", 132.0f, 3, 12,
          { { K, "X..........." }, { SN, "....o...o..." }, { CH, "x...x...x..." } },
          "R.......5...", "....X...X..." },
        { "Bossa Nova", 128.0f, 4, 16,
          { { K, "X..xX..xX..xX..x" }, { RIM, "x..x..x...x..x.." }, { CH, "oxoxoxoxoxoxoxox" } },
          "R.....5.R.....5.", "X..X..X...X..X.." },
        { "Rhumba", 108.0f, 4, 16,
          { { K, "X.....X.X......." }, { CLV, "x..x..x...x.x..." }, { MAR, "xoxoxoxoxoxoxoxo" }, { HC, "......xx......x." }, { LC, "x.........x....." } },
          "R..5...R..5.....", "..X...X...X..X.." },
        { "Beguine", 116.0f, 4, 16,
          { { K, "X.......X......." }, { RIM, "x..x..x.x..x..x." }, { CH, "x.x.x.x.x.x.x.x." }, { HB, "......x.......x." } },
          "R.....5.R.....5.", "..X...X...X...X." },
        { "Disco", 120.0f, 4, 16,
          { { K, "X...X...X...X..." }, { SN, "....X.......X..." }, { CH, "x...x...x...x..." }, { OH, "..x...x...x...x." } },
          "R.8.R.8.R.8.R.8.", ".x.x.x.x.x.x.x.x" },
        { "Rock", 112.0f, 4, 16,
          { { K, "X......xX.x....." }, { SN, "....X.......X..." }, { CH, "x.x.x.x.x.x.x.x." } },
          "R.R.R.R.R.R.R.R.", "X.......X......." },
        { "16 Beat", 100.0f, 4, 16,
          { { K, "X.....x...X....." }, { SN, "....X.......X..." }, { CH, "Xoxoxoxoxoxoxoxo" } },
          "R..R..R.R.....5.", "X..X..X.X......." },
        { "Soft Rock", 88.0f, 4, 16,
          { { K, "X.....x.X......." }, { RIM, "....X.......X..." }, { CH, "x.o.x.o.x.o.x.o." } },
          "R.....R.5.......", "X-------X-------" },
        { "Ballad", 72.0f, 4, 16,
          { { K, "X.......X.x....." }, { RIM, "....x.......x..." }, { CH, "x.x.x.x.x.x.x.x." } },
          "R.......5.......", "X---x---X---x---" },
        { "Swing", 140.0f, 4, 12,
          { { RD, "X..x.xX..x.x" }, { PH, "...x.....x.." }, { K, "o..o..o..o.." } },
          "R..3..5..6..", "...x.....x.." },
        { "Shuffle", 120.0f, 4, 12,
          { { K, "X.....X....." }, { SN, "...X.....X.." }, { CH, "x.xx.xx.xx.x" } },
          "R.R3.35.56.6", "...X.....X.." },
        { "March", 116.0f, 4, 16,
          { { K, "X.......X......." }, { SN, "X.x.X.x.XxxxX.x." }, { CR, "X..............." } },
          "R...5...R...5...", "..X...X...X...X." },
        { "Tango", 120.0f, 4, 16,
          { { K, "X...X...X..xX..." }, { SN, "..........x.x..." }, { CH, "x...x...x...x..." } },
          "R...5...R..5R...", "X...X...X..XX..." },
        { "Samba", 104.0f, 4, 16,
          { { K, "X..xX..xX..xX..x" }, { RIM, "x..x..x...x..x.." }, { TAMB, "Xoxxxoxxxoxxxoxx" } },
          "R..5R..5R..5R..5", ".X.X..X.X.X..X.." },
        { "Reggae", 76.0f, 4, 16,
          { { K, "........X......." }, { RIM, "........X......." }, { CH, "x.x.x.x.x.x.x.x." } },
          "R.....R.5...5...", "..X...X...X...X." },
        { "Country", 120.0f, 4, 16,
          { { K, "X.......X......." }, { SN, "....X.......X..." }, { CH, "xoxoxoxoxoxoxoxo" } },
          "R...5...R...5...", "..X...X...X...X." },
        { "Polka", 120.0f, 4, 16,
          { { K, "X...X...X...X..." }, { SN, "..x...x...x...x." }, { CH, "..x...x...x...x." } },
          "R...5...R...5...", "..X...X...X...X." },
        { "Cha-Cha", 120.0f, 4, 16,
          { { K, "X.......X......." }, { BELL, "X...X...X...X..." }, { HC, "......xx......xx" }, { MAR, "x.x.x.x.x.x.x.x." } },
          "R...5...R..5R...", "..X...X...X.X.X." },
        { "Mambo", 128.0f, 4, 16,
          { { K, "X..x....X..x...." }, { BELL, "X.x.X.xxX.x.X.xx" }, { HC, "..x...xx..x...xx" }, { CLV, "x..x..x...x.x..." } },
          "R..5..R.5..R..5.", ".X.X..X..X.X..X." },
    };
    return list;
}

const std::vector<VintageRhythm>& portableRhythms()
{
    static const std::vector<VintageRhythm> list = {
        // ---- March ----
        { "March I", 116.0f, 4, 16,
          { { K, "X.......X......." }, { SN, "....X.......X..." }, { CH, "x.x.x.x.x.x.x.x." } },
          "R.......5.......", "....X.......X..." },
        { "March II", 116.0f, 4, 16,
          { { K, "X.......X......." }, { SN, "..o.X.o...o.X.xx" }, { CH, "x...x...x...x..." } },
          "R...5...R...5...", "..X...X...X...X." },
        // ---- Disco ----
        { "Disco I", 120.0f, 4, 16,
          { { K, "X...X...X...X..." }, { SN, "....X.......X..." }, { CH, "x...x...x...x..." }, { OH, "..x...x...x...x." } },
          "R.8.R.8.R.8.R.8.", "..X...X...X...X." },
        { "Disco II", 120.0f, 4, 16,
          { { K, "X...X...X...X..." }, { SN, "....X..o....X..." }, { CH, "xoxoxoxoxoxoxoxo" } },
          "R..8R..8R..8R..8", ".x.x.x.x.x.x.x.x" },
        // ---- Waltz (3/4) ----
        { "Waltz I", 132.0f, 3, 12,
          { { K, "X..........." }, { SN, "....x...x..." }, { CH, "x...x...x..." } },
          "R.......5...", "....X...X..." },
        { "Waltz II", 132.0f, 3, 12,
          { { K, "X.......o..." }, { SN, "....x...x.o." }, { CH, "x.o.x.o.x.o." } },
          "R...3...5...", "....X...X..." },
        // ---- Rock ----
        { "Rock I", 112.0f, 4, 16,
          { { K, "X.......X......." }, { SN, "....X.......X..." }, { CH, "x.x.x.x.x.x.x.x." } },
          "R.......R.5.....", "X.......X......." },
        { "Rock II", 112.0f, 4, 16,
          { { K, "X......xX.x....." }, { SN, "....X.......X..o" }, { CH, "xoxoxoxoxoxoxoxo" } },
          "R.R.R.R.R.R.5.5.", "X...x...X...x..." },
        // ---- Tango ----
        { "Tango I", 120.0f, 4, 16,
          { { K, "X...X...X...X..." }, { SN, "..........x.x..." }, { CH, "x...x...x...x..." } },
          "R...5...R...5...", "X...X...X..XX..." },
        { "Tango II", 120.0f, 4, 16,
          { { K, "X..xX...X..xX..." }, { SN, "........x.x.x..." }, { CLV, "x...x...x..x...." } },
          "R..5R...R..5R...", "X..XX...X..XX..." },
        // ---- Swing (triplets) ----
        { "Swing I", 140.0f, 4, 12,
          { { RD, "X..x.xX..x.x" }, { PH, "...x.....x.." }, { K, "o.....o....." } },
          "R..3..5..6..", "...x.....x.." },
        { "Swing II", 140.0f, 4, 12,
          { { RD, "X..x.xX..x.x" }, { PH, "...x.....x.." }, { K, "o..o..o..o.." }, { SN, ".....o.....o" } },
          "R..3..5..3..", "...x..x..x.." },
        // ---- Rhumba ----
        { "Rhumba I", 108.0f, 4, 16,
          { { K, "X.....X.X......." }, { CLV, "x..x..x...x.x..." }, { MAR, "xoxoxoxoxoxoxoxo" } },
          "R..5...R..5.....", "..X...X...X..X.." },
        { "Rhumba II", 108.0f, 4, 16,
          { { K, "X.....X.X......." }, { CLV, "x..x..x...x.x..." }, { HB, "..x.....x.x...x." }, { LB, "......x.......x." } },
          "R..5..R...5..R..", "..X..X....X..X.." },
        // ---- Samba ----
        { "Samba I", 104.0f, 4, 16,
          { { K, "X..xX..xX..xX..x" }, { RIM, "x..x..x...x..x.." }, { CH, "xoxoxoxoxoxoxoxo" } },
          "R..5R..5R..5R..5", ".X.X..X.X.X..X.." },
        { "Samba II", 104.0f, 4, 16,
          { { K, "X..xX..xX..xX..x" }, { SN, "..x..x.x..x..x.x" }, { CH, "xoxoxoxoxoxoxoxo" } },
          "R..5R..5R..5R.5.", "X..X..X...X..X.." },
    };
    return list;
}

juce::StringArray portableRhythmNames()
{
    return { "March", "Disco", "Waltz", "Rock", "Tango", "Swing", "Rhumba", "Samba" };
}

std::vector<std::pair<int, juce::String>> vintageFill (const VintageRhythm& r)
{
    const int half = r.steps / 2;
    std::vector<std::pair<int, juce::String>> lanes;
    // keep the first half of the groove, then a snare + tom run
    for (auto& [note, steps] : r.drums)
        lanes.push_back ({ note, steps.substring (0, half) + juce::String::repeatedString (".", r.steps - half) });
    juce::String snare, low, high;
    for (int s = 0; s < r.steps; ++s)
    {
        const bool inFill = s >= half;
        const int k = s - half;
        snare << (inFill && k % 2 == 0 && k < (r.steps - half) / 2 ? (k == 0 ? 'X' : 'x') : '.');
        high  << (inFill && k >= (r.steps - half) / 2 && k % 2 == 0 ? 'x' : '.');
        low   << (inFill && k >= (r.steps - half) / 2 && k % 2 == 1 ? 'X' : '.');
    }
    lanes.push_back ({ SN, snare });
    lanes.push_back ({ HT, high });
    lanes.push_back ({ LT, low });
    return lanes;
}

float stepVelocity (juce::juce_wchar c)
{
    switch (c)
    {
        case 'X': return 1.0f;
        case 'x': return 0.72f;
        case 'o': return 0.42f;
        default:  return 0.0f;
    }
}

int bassInterval (juce::juce_wchar c, bool minor)
{
    switch (c)
    {
        case 'R': return 0;
        case '3': return minor ? 3 : 4;
        case '5': return 7;
        case '6': return minor ? 8 : 9;
        case '7': return 10;
        case '8': return 12;
        default:  return 0;
    }
}

Clip insertVintageRhythm (Project& p, const Track& t, int index, double startBeat, int bars)
{
    const auto& all = vintageRhythms();
    const auto& r = all[(size_t) juce::jlimit (0, (int) all.size() - 1, index)];
    const double stepBeats = (double) r.beats / r.steps;
    auto clip = p.addMidiClip (t, startBeat, (double) r.beats * bars, r.name);
    for (int bar = 0; bar < bars; ++bar)
    {
        const bool fill = bar % 4 == 3;
        const auto lanes = fill ? vintageFill (r) : r.drums;
        for (auto& [note, steps] : lanes)
            for (int s = 0; s < steps.length() && s < r.steps; ++s)
            {
                const float v = stepVelocity (steps[s]);
                if (v > 0.0f)
                    p.addNote (clip, note, bar * r.beats + s * stepBeats, stepBeats * 0.9, juce::jlimit (1, 127, (int) (v * 118.0f)));
            }
        if (bar > 0 && bar % 4 == 0)   // crash after each fill
            p.addNote (clip, CR, bar * r.beats, stepBeats, 100);
    }
    return clip;
}

} // namespace wis::daw
