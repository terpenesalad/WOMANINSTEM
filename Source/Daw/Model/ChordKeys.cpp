#include "ChordKeys.h"
#include <algorithm>
#include <numeric>

namespace wis::daw
{
namespace chordkeys
{

const char* keyChars() { return "1234567890qwertyuiopasdfghjkl;zxcvbnm,./"; }

int keyIndexForChar (juce::juce_wchar c)
{
    c = juce::CharacterFunctions::toLowerCase (c);
    // shifted number row and punctuation (US / UK layouts)
    switch (c)
    {
        case '!': c = '1'; break;   case '@': case '"': c = '2'; break;  case '#': case 0xa3: c = '3'; break;
        case '$': c = '4'; break;   case '%': c = '5'; break;  case '^': c = '6'; break;
        case '&': c = '7'; break;   case '*': c = '8'; break;  case '(': c = '9'; break;
        case ')': c = '0'; break;   case ':': c = ';'; break;  case '<': c = ','; break;
        case '>': c = '.'; break;   case '?': c = '/'; break;
        default: break;
    }
    if (c == 0) return -1;
    const char* k = keyChars();
    for (int i = 0; i < numKeys; ++i)
        if ((juce::juce_wchar) k[i] == c) return i;
    return -1;
}

juce::String keyLabel (int index)
{
    if (! juce::isPositiveAndBelow (index, numKeys)) return {};
    return juce::String::charToString ((juce::juce_wchar) keyChars()[index]).toUpperCase();
}

const std::vector<ChordType>& types()
{
    static const std::vector<ChordType> t {
        { "Major", "", { 0, 4, 7 }, "Triads" },
        { "Minor", "m", { 0, 3, 7 }, "Triads" },
        { "Diminished", "dim", { 0, 3, 6 }, "Triads" },
        { "Augmented", "aug", { 0, 4, 8 }, "Triads" },
        { "Sus 2", "sus2", { 0, 2, 7 }, "Triads" },
        { "Sus 4", "sus4", { 0, 5, 7 }, "Triads" },
        { "Power chord (5th)", "5", { 0, 7, 12 }, "Triads" },
        { "Dominant 7", "7", { 0, 4, 7, 10 }, "Sevenths" },
        { "Major 7", "maj7", { 0, 4, 7, 11 }, "Sevenths" },
        { "Minor 7", "m7", { 0, 3, 7, 10 }, "Sevenths" },
        { "Minor-major 7", "m(maj7)", { 0, 3, 7, 11 }, "Sevenths" },
        { "Half-diminished (m7b5)", "m7b5", { 0, 3, 6, 10 }, "Sevenths" },
        { "Diminished 7", "dim7", { 0, 3, 6, 9 }, "Sevenths" },
        { "7 sus 4", "7sus4", { 0, 5, 7, 10 }, "Sevenths" },
        { "Augmented 7", "7#5", { 0, 4, 8, 10 }, "Sevenths" },
        { "6", "6", { 0, 4, 7, 9 }, "Sixths & added notes" },
        { "Minor 6", "m6", { 0, 3, 7, 9 }, "Sixths & added notes" },
        { "Add 9", "add9", { 0, 4, 7, 14 }, "Sixths & added notes" },
        { "Minor add 9", "madd9", { 0, 3, 7, 14 }, "Sixths & added notes" },
        { "6/9", "6/9", { 0, 4, 7, 9, 14 }, "Sixths & added notes" },
        { "9", "9", { 0, 4, 7, 10, 14 }, "Extended" },
        { "Major 9", "maj9", { 0, 4, 7, 11, 14 }, "Extended" },
        { "Minor 9", "m9", { 0, 3, 7, 10, 14 }, "Extended" },
        { "11", "11", { 0, 7, 10, 14, 17 }, "Extended" },
        { "Minor 11", "m11", { 0, 3, 7, 10, 14, 17 }, "Extended" },
        { "13", "13", { 0, 4, 10, 14, 21 }, "Extended" },
        { "Major 7 #11", "maj7#11", { 0, 4, 7, 11, 18 }, "Extended" },
        { "7 flat 9", "7b9", { 0, 4, 7, 10, 13 }, "Altered" },
        { "7 sharp 9", "7#9", { 0, 4, 7, 10, 15 }, "Altered" },
        { "Single note", "(note)", { 0 }, "Other" },
        { "Octaves", "(8va)", { 0, 12 }, "Other" },
        { "Fifth + octave", "(5+8)", { 0, 7, 12, 19 }, "Other" },
    };
    return t;
}

int typeForSuffix (const juce::String& suffix)
{
    const auto& t = types();
    for (int i = 0; i < (int) t.size(); ++i)
        if (suffix == t[(size_t) i].suffix) return i;
    // common spellings
    static const std::map<juce::String, juce::String> alias {
        { "M", "" }, { "maj", "" }, { "min", "m" }, { "-", "m" }, { "M7", "maj7" }, { "min7", "m7" }, { "-7", "m7" },
        { "o", "dim" }, { "o7", "dim7" }, { "+", "aug" }, { "sus", "sus4" }, { "add2", "add9" }, { "m(add9)", "madd9" },
        { "mmaj7", "m(maj7)" }, { "M9", "maj9" }, { "dom7", "7" }, { "h7", "m7b5" }, { "maj7(#11)", "maj7#11" }, { "aug7", "7#5" },
    };
    auto it = alias.find (suffix);
    if (it != alias.end()) return typeForSuffix (it->second);
    return -1;
}

juce::StringArray voicingNames() { return { "Close", "Open (spread the middle)", "Spread (root below)", "Wide (low root, high top)" }; }

juce::String pitchName (int pc, int keyTonic)
{
    static const char* mixed[] = { "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    pc = ((pc % 12) + 12) % 12;
    if (keyTonic < 0) return mixed[pc];
    keyTonic %= 12;
    static const juce::String letters = "CDEFGAB";
    static const int naturals[] = { 0, 2, 4, 5, 7, 9, 11 };
    static const int degree[] = { 0, 1, 1, 2, 2, 3, 3, 4, 5, 5, 6, 6 };   // b2 -> 2nd, #4 -> 4th, b6 -> 6th...
    const int tonicLetter = letters.indexOfChar (mixed[keyTonic][0]);
    const int letter = (tonicLetter + degree[((pc - keyTonic) % 12 + 12) % 12]) % 7;
    const int acc = ((pc - naturals[letter] + 18) % 12) - 6;
    juce::String name = juce::String::charToString (letters[letter]);
    for (int i = 0; i < acc; ++i) name << "#";
    for (int i = 0; i > acc; --i) name << "b";
    return name;
}

juce::String chordName (const Slot& s, int transpose, int keyTonic)
{
    if (! s.used) return {};
    const auto& t = types()[(size_t) juce::jlimit (0, (int) types().size() - 1, s.type)];
    juce::String name = pitchName (s.root + transpose, keyTonic);
    juce::String suffix = t.suffix;
    if (suffix.startsWith ("(")) name += " " + suffix; else name += suffix;
    if (s.bass >= 0 && ((s.bass - s.root) % 12 + 12) % 12 != 0) name += "/" + pitchName (s.bass + transpose, keyTonic);
    return name;
}

Slot parse (const juce::String& text)
{
    Slot s;
    auto t = text.trim();
    if (t.isEmpty()) return s;
    static const juce::String letters = "CDEFGAB";
    static const int naturals[] = { 0, 2, 4, 5, 7, 9, 11 };
    auto pitchAt = [] (const juce::String& str, int& len) -> int
    {
        len = 0;
        if (str.isEmpty()) return -1;
        const int li = letters.indexOfChar (juce::CharacterFunctions::toUpperCase (str[0]));
        if (li < 0) return -1;
        int pc = naturals[li];
        len = 1;
        if (str.length() > 1 && (str[1] == '#' || str[1] == 0x266f)) { ++pc; ++len; }
        else if (str.length() > 1 && (str[1] == 'b' || str[1] == 0x266d)) { --pc; ++len; }
        return (pc + 12) % 12;
    };
    int len = 0;
    const int root = pitchAt (t, len);
    if (root < 0) return s;
    auto rest = t.substring (len);
    int bass = -1;
    const int slash = rest.lastIndexOfChar ('/');
    if (slash >= 0 && rest.substring (slash) != "/9")
    {
        int bl = 0;
        const int b = pitchAt (rest.substring (slash + 1).trim(), bl);
        if (b >= 0 && bl > 0) { bass = b; rest = rest.substring (0, slash); }
    }
    rest = rest.trim();
    const int type = typeForSuffix (rest);
    if (type < 0) return s;
    s.used = true;
    s.root = root;
    s.type = type;
    s.bass = bass;
    return s;
}

std::vector<int> notesFor (const Slot& slot, const PlayOptions& o, const std::vector<int>& previous)
{
    std::vector<int> out;
    if (! slot.used) return out;
    const auto& iv = types()[(size_t) juce::jlimit (0, (int) types().size() - 1, slot.type)].intervals;
    const int rootPitch = o.baseNote + slot.root + o.transpose + slot.octave * 12;

    auto build = [&] (int inversion, int shift)
    {
        std::vector<int> n;
        for (int i : iv) n.push_back (rootPitch + i + shift);
        std::sort (n.begin(), n.end());
        const int inv = juce::jlimit (0, juce::jmax (0, (int) n.size() - 1), inversion);
        for (int k = 0; k < inv; ++k) { n.push_back (n.front() + 12); n.erase (n.begin()); }
        std::sort (n.begin(), n.end());
        switch (slot.voicing)
        {
            case open:
                if (n.size() >= 4) { n[n.size() - 2] -= 12; }
                else if (n.size() == 3) { n[1] += 12; }
                break;
            case spread: if (n.size() >= 2) n[0] -= 12; break;
            case wide:   if (n.size() >= 3) { n[0] -= 12; n.back() += 12; } break;
            default: break;
        }
        std::sort (n.begin(), n.end());
        return n;
    };

    if (o.voiceLeading && ! previous.empty() && iv.size() > 1)
    {
        const float prevMean = (float) std::accumulate (previous.begin(), previous.end(), 0) / (float) previous.size();
        float best = 1.0e9f;
        for (int inv = 0; inv < (int) iv.size(); ++inv)
            for (int shift = -12; shift <= 12; shift += 12)
            {
                auto n = build (inv, shift);
                if (n.front() < o.baseNote - 8 || n.back() > o.baseNote + 26) continue;   // stay in the chord octave's neighbourhood
                float moved = 0.0f;
                for (int x : n)
                {
                    int nearest = 1000;
                    for (int p : previous) nearest = juce::jmin (nearest, std::abs (x - p));
                    moved += (float) nearest;
                }
                const float mean = (float) std::accumulate (n.begin(), n.end(), 0) / (float) n.size();
                const float cost = moved + 1.5f * std::abs (mean - prevMean) + (inv == slot.inversion && shift == 0 ? -0.5f : 0.0f);
                if (cost < best) { best = cost; out = n; }
            }
    }
    if (out.empty()) out = build (slot.inversion, 0);

    // the bass: a slash chord's own bass note, or the root an octave below
    const int lowest = out.front();
    if (slot.bass >= 0 && ((slot.bass - slot.root) % 12 + 12) % 12 != 0)
    {
        int b = lowest - 1;
        while (((b - (slot.bass + o.transpose)) % 12 + 12) % 12 != 0) --b;
        if (lowest - b < 5) b -= 12;
        out.insert (out.begin(), b);
    }
    else if (o.bassNote && iv.size() > 1)
    {
        int b = lowest - 1;
        while (((b - (slot.root + o.transpose)) % 12 + 12) % 12 != 0) --b;
        if (lowest - b < 5) b -= 12;
        out.insert (out.begin(), b);
    }
    std::vector<int> clean;
    for (int x : out)
        if (x >= 0 && x <= 127 && std::find (clean.begin(), clean.end(), x) == clean.end()) clean.push_back (x);
    std::sort (clean.begin(), clean.end());
    return clean;
}

namespace
{
    Bank makeBank (const char* name, int tonic, bool minor, std::initializer_list<const char*> chords)
    {
        Bank b;
        b.name = name;
        b.tonic = tonic;
        b.minor = minor;
        int i = 0;
        for (const char* c : chords)
        {
            if (i >= numKeys) break;
            b.slots[(size_t) i] = parse (c);
            // keep chords near the middle: roots above F# sit an octave lower so the bank doesn't climb
            if (b.slots[(size_t) i].used && b.slots[(size_t) i].root >= 7) b.slots[(size_t) i].octave = -1;
            ++i;
        }
        return b;
    }
}

std::vector<Bank> factoryBanks()
{
    std::vector<Bank> b;
    b.push_back (makeBank ("Pop & Rock (C major)", 0, false, {
        "C", "Dm", "Em", "F", "G", "Am", "Bdim", "Bb", "Ab", "Eb",
        "Cmaj7", "Dm7", "Em7", "Fmaj7", "G7", "Am7", "Bm7b5", "C7", "D7", "E7",
        "Csus2", "Dsus4", "Esus4", "Fadd9", "Gsus4", "Amadd9", "Fm", "Gm", "D", "E",
        "C/E", "C/G", "F/A", "G/B", "Am/G", "Fm6", "G6", "Cadd9", "Dm9", "Gsus2" }));
    b.push_back (makeBank ("Minor Moods (A minor)", 9, true, {
        "Am", "Bdim", "C", "Dm", "Em", "F", "G", "E", "E7", "Dm6",
        "Am7", "Bm7b5", "Cmaj7", "Dm7", "Em7", "Fmaj7", "G7", "Am(maj7)", "A7", "D",
        "Asus2", "Asus4", "Am9", "Dm9", "Esus4", "Fadd9", "G6", "Bb", "Bbmaj7", "Fm",
        "Am/G", "Am/F#", "F/A", "G/B", "E/G#", "Dm/F", "C/E", "Am/E", "E7b9", "Bb/D" }));
    b.push_back (makeBank ("Jazz & Soul (C major)", 0, false, {
        "Cmaj7", "Dm7", "Em7", "Fmaj7", "G7", "Am7", "Bm7b5", "C6", "Dm6", "G13",
        "Cmaj9", "Dm9", "Em9", "Fmaj9", "G9", "Am9", "E7b9", "A7b9", "D9", "Db7",
        "C6/9", "Dm11", "Ebdim7", "F6/9", "G7sus4", "Bb13", "Ab7", "Gb7", "Cm7", "F7",
        "Fm7", "Bb7", "Ebmaj7", "Abmaj7", "Dbmaj7", "Gm7", "C9", "B7", "E7#9", "Fmaj7#11" }));
    b.push_back (makeBank ("Blues (A)", 9, false, {
        "A7", "D7", "E7", "A9", "D9", "E9", "A6", "D6", "E7#9", "B7",
        "A", "D", "E", "Am7", "Dm7", "Em7", "C", "G", "F", "Bb7",
        "A5", "D5", "E5", "G5", "C5", "F5", "B5", "Bb5", "F#5", "Ab5",
        "Am", "Dm", "Em", "A13", "D13", "E13", "F7", "C7", "G7", "Edim7" }));
    b.push_back (makeBank ("Neo-Soul & Lo-fi (C major)", 0, false, {
        "Cmaj9", "Am9", "Dm9", "G13", "Fmaj9", "Em9", "Bbmaj7", "Abmaj7", "Ebmaj9", "Dbmaj7",
        "Cmaj7", "Am7", "Dm7", "G9", "Fmaj7", "Em7", "A7#9", "D9", "Gm9", "C9",
        "C6/9", "Am11", "Dm11", "G7sus4", "F6/9", "E7b9", "Fm9", "Bb9", "Ebmaj7", "Abmaj9",
        "Cmaj7/E", "Am7/G", "Fmaj7/A", "G/F", "Em7/D", "Dm7/C", "Bbmaj7/D", "Fm6", "Gb7", "B7#9" }));
    b.push_back (makeBank ("Psychedelic 60s (C major)", 0, false, {
        "C", "D", "Bb", "F", "G", "Am", "E7", "Fm", "Ab", "Eb",
        "C7", "D7", "Bbmaj7", "Fmaj7", "G7sus4", "Am7", "E", "A", "B7", "Cmaj7",
        "Csus4", "Dsus4", "Bb6", "Fm6", "Gm", "Em", "A7", "Db", "Gb", "Dm",
        "C/B", "C/Bb", "Am/G", "D/F#", "F/C", "Fm/Ab", "Eaug", "Caug", "Gdim7", "Dm7" }));
    b.push_back (makeBank ("Dream Pop (C major)", 0, false, {
        "Cmaj7", "Am7", "Fmaj7", "Em7", "Dm9", "Gsus4", "Asus2", "Fadd9", "Cadd9", "E7",
        "C", "Am", "F", "Em", "Dm", "G", "Bb", "Abmaj7", "Fm", "Gm7",
        "Csus2", "Amadd9", "Fmaj7#11", "Em9", "Dm7", "G6", "Bbmaj9", "Ebmaj7", "Db", "E",
        "C/G", "Am/E", "F/C", "Em/B", "Dm/A", "G/D", "Fm/C", "Bb/F", "Ab/Eb", "Gsus2" }));
    b.push_back (makeBank ("Folk & Open Chords (G major)", 7, false, {
        "G", "C", "D", "Em", "Am", "Bm", "Cadd9", "Dsus4", "Gsus4", "Em7",
        "G7", "Cmaj7", "D7", "Em9", "Am7", "Bm7", "F", "Bb", "A", "E",
        "Gsus2", "Csus2", "Dsus2", "Asus4", "Asus2", "Esus4", "A7", "E7", "B7", "Fmaj7",
        "G/B", "D/F#", "C/G", "C/E", "Am/C", "Em/G", "D/A", "G/D", "F/C", "D/C" }));
    {
        // every major and minor triad plus sevenths, chromatic
        std::vector<juce::String> names;
        for (int r = 0; r < 10; ++r) names.push_back (pitchName (r));
        for (int r = 0; r < 10; ++r) names.push_back (pitchName (r) + "m");
        names.push_back ("Bb"); names.push_back ("B"); names.push_back ("Bbm"); names.push_back ("Bm");
        for (const char* r : { "C", "D", "E", "F", "G", "A" }) names.push_back (juce::String (r) + "7");
        for (const char* c : { "Cmaj7", "Dmaj7", "Ebmaj7", "Fmaj7", "Gmaj7", "Abmaj7", "Am7", "Bm7", "Dm7", "Em7" }) names.push_back (c);
        Bank all;
        all.name = "Every Major & Minor (chromatic)";
        for (int i = 0; i < numKeys && i < (int) names.size(); ++i)
        {
            all.slots[(size_t) i] = parse (names[(size_t) i]);
            if (all.slots[(size_t) i].root >= 7) all.slots[(size_t) i].octave = -1;
        }
        b.push_back (all);
    }
    Bank empty;
    empty.name = "Empty (make your own)";
    b.push_back (empty);
    return b;
}

void fillRow (Bank& b, int row, int tonic, bool minor, int kind)
{
    if (! juce::isPositiveAndBelow (row, 4)) return;
    static const int majorSteps[] = { 0, 2, 4, 5, 7, 9, 11 };
    static const int minorSteps[] = { 0, 2, 3, 5, 7, 8, 10 };
    // chord quality per degree
    static const char* majTriads[] = { "", "m", "m", "", "", "m", "dim" };
    static const char* majSevenths[] = { "maj7", "m7", "m7", "maj7", "7", "m7", "m7b5" };
    static const char* majSus[] = { "add9", "m7", "sus4", "add9", "sus4", "madd9", "m7b5" };
    static const char* minTriads[] = { "m", "dim", "", "m", "m", "", "" };
    static const char* minSevenths[] = { "m7", "m7b5", "maj7", "m7", "m7", "maj7", "7" };
    static const char* minSus[] = { "madd9", "m7b5", "add9", "m7", "sus4", "maj7", "sus4" };
    const int* steps = minor ? minorSteps : majorSteps;
    const char** q = minor ? (kind == 0 ? minTriads : kind == 1 ? minSevenths : minSus)
                           : (kind == 0 ? majTriads : kind == 1 ? majSevenths : majSus);
    for (int k = 0; k < keysPerRow; ++k)
    {
        auto& s = b.slots[(size_t) (row * keysPerRow + k)];
        if (k >= 7)
        {
            // the last three keys: the tonic an octave up, the V7 and the IV (minor: the major V)
            const int extra[3][2] = { { 0, 0 }, { 7, 1 }, { 5, 2 } };
            s = Slot();
            s.used = true;
            s.root = (tonic + extra[k - 7][0]) % 12;
            s.type = extra[k - 7][1] == 0 ? typeForSuffix (minor ? "m" : "")
                   : extra[k - 7][1] == 1 ? typeForSuffix ("7") : typeForSuffix (minor ? "m" : "");
            s.octave = k == 7 ? 1 : 0;
            if (s.root >= 7) s.octave -= 1;
            continue;
        }
        s = Slot();
        s.used = true;
        s.root = (tonic + steps[k]) % 12;
        s.type = juce::jmax (0, typeForSuffix (q[k]));
        s.octave = s.root >= 7 ? -1 : 0;
    }
}

juce::ValueTree toTree (const Bank& b)
{
    juce::ValueTree t ("BANK");
    t.setProperty ("name", b.name, nullptr);
    t.setProperty ("tonic", b.tonic, nullptr);
    t.setProperty ("minor", b.minor, nullptr);
    for (int i = 0; i < numKeys; ++i)
    {
        const auto& s = b.slots[(size_t) i];
        if (! s.used) continue;
        juce::ValueTree k ("KEY");
        k.setProperty ("i", i, nullptr);
        k.setProperty ("root", s.root, nullptr);
        k.setProperty ("type", types()[(size_t) juce::jlimit (0, (int) types().size() - 1, s.type)].suffix, nullptr);
        if (s.inversion != 0) k.setProperty ("inv", s.inversion, nullptr);
        if (s.octave != 0) k.setProperty ("oct", s.octave, nullptr);
        if (s.bass >= 0) k.setProperty ("bass", s.bass, nullptr);
        if (s.voicing != 0) k.setProperty ("voicing", s.voicing, nullptr);
        t.appendChild (k, nullptr);
    }
    return t;
}

Bank fromTree (const juce::ValueTree& t)
{
    Bank b;
    b.name = t.getProperty ("name", "Chords").toString();
    b.tonic = juce::jlimit (0, 11, (int) t.getProperty ("tonic", 0));
    b.minor = t.getProperty ("minor", false);
    for (auto k : t)
    {
        const int i = k["i"];
        if (! k.hasType ("KEY") || ! juce::isPositiveAndBelow (i, numKeys)) continue;
        Slot s;
        const int type = typeForSuffix (k["type"].toString());
        if (type < 0) continue;
        s.used = true;
        s.root = juce::jlimit (0, 11, (int) k["root"]);
        s.type = type;
        s.inversion = juce::jlimit (0, 5, (int) k.getProperty ("inv", 0));
        s.octave = juce::jlimit (-3, 3, (int) k.getProperty ("oct", 0));
        s.bass = juce::jlimit (-1, 11, (int) k.getProperty ("bass", -1));
        s.voicing = juce::jlimit (0, numVoicings - 1, (int) k.getProperty ("voicing", 0));
        b.slots[(size_t) i] = s;
    }
    return b;
}

} // namespace chordkeys

// ---- state ----------------------------------------------------------------------------------------------------
ChordKeysState::ChordKeysState (juce::PropertiesFile* s) : settings (s)
{
    load();
}

chordkeys::Bank& ChordKeysState::bank()
{
    if (banks.empty()) banks = chordkeys::factoryBanks();
    current = juce::jlimit (0, (int) banks.size() - 1, current);
    return banks[(size_t) current];
}

int ChordKeysState::effectiveTranspose (int songKey, int songScale) const
{
    int t = transpose;
    if (followKey && ! banks.empty())
    {
        const auto& b = banks[(size_t) juce::jlimit (0, (int) banks.size() - 1, current)];
        const bool songMinor = songScale == 1;
        // a major bank in a minor song plays from the relative major, and the other way round
        const int target = b.minor == songMinor ? songKey : (b.minor ? songKey + 9 : songKey + 3);
        t += target - b.tonic;
    }
    t = ((t % 12) + 12) % 12;
    return t > 5 ? t - 12 : t;
}

chordkeys::PlayOptions ChordKeysState::options (int songKey, int songScale) const
{
    chordkeys::PlayOptions o;
    o.baseNote = juce::jlimit (12, 108, (octave + 1) * 12);
    o.transpose = effectiveTranspose (songKey, songScale);
    o.voiceLeading = voiceLeading;
    o.bassNote = bassNote;
    return o;
}

juce::ValueTree ChordKeysState::toTree() const
{
    juce::ValueTree t ("CHORDKEYS");
    t.setProperty ("version", 1, nullptr);
    t.setProperty ("current", current, nullptr);
    t.setProperty ("octave", octave, nullptr);
    t.setProperty ("transpose", transpose, nullptr);
    t.setProperty ("followKey", followKey, nullptr);
    t.setProperty ("velocity", velocity, nullptr);
    t.setProperty ("strumMs", strumMs, nullptr);
    t.setProperty ("strumDir", strumDir, nullptr);
    t.setProperty ("voiceLeading", voiceLeading, nullptr);
    t.setProperty ("bassNote", bassNote, nullptr);
    t.setProperty ("latch", latch, nullptr);
    for (auto& b : banks) t.appendChild (chordkeys::toTree (b), nullptr);
    return t;
}

void ChordKeysState::fromTree (const juce::ValueTree& t)
{
    std::vector<chordkeys::Bank> loaded;
    for (auto b : t)
        if (b.hasType ("BANK")) loaded.push_back (chordkeys::fromTree (b));
    banks = loaded.empty() ? chordkeys::factoryBanks() : loaded;
    current = juce::jlimit (0, (int) banks.size() - 1, (int) t.getProperty ("current", 0));
    octave = juce::jlimit (1, 7, (int) t.getProperty ("octave", 4));
    transpose = juce::jlimit (-11, 11, (int) t.getProperty ("transpose", 0));
    followKey = t.getProperty ("followKey", true);
    velocity = juce::jlimit (1, 127, (int) t.getProperty ("velocity", 100));
    strumMs = juce::jlimit (0.0f, 150.0f, (float) t.getProperty ("strumMs", 0.0f));
    strumDir = juce::jlimit (0, 2, (int) t.getProperty ("strumDir", 0));
    voiceLeading = t.getProperty ("voiceLeading", true);
    bassNote = t.getProperty ("bassNote", false);
    latch = t.getProperty ("latch", false);
}

int ChordKeysState::spelling (int songKey, int songScale) const
{
    if (banks.empty()) return -1;
    const auto& b = banks[(size_t) juce::jlimit (0, (int) banks.size() - 1, current)];
    return (((b.minor ? b.tonic + 3 : b.tonic) + effectiveTranspose (songKey, songScale)) % 12 + 12) % 12;
}

void ChordKeysState::save()
{
    if (settings == nullptr) return;
    if (auto xml = toTree().createXml())
        settings->setValue ("chordKeys", xml.get());
}

void ChordKeysState::load()
{
    if (settings != nullptr)
        if (auto xml = settings->getXmlValue ("chordKeys"))
        {
            fromTree (juce::ValueTree::fromXml (*xml));
            return;
        }
    banks = chordkeys::factoryBanks();
    current = 0;
}

void ChordKeysState::resetFactory()
{
    banks = chordkeys::factoryBanks();
    current = 0;
}

// ---- player ---------------------------------------------------------------------------------------------------
ChordPlayer::ChordPlayer (juce::MidiKeyboardState& s) : keys (s) {}

ChordPlayer::~ChordPlayer()
{
    stopTimer();
    releaseAll();
}

void ChordPlayer::noteOnNow (int note, float vel)
{
    if (! juce::isPositiveAndBelow (note, 128)) return;
    if (refs[(size_t) note]++ > 0) keys.noteOff (1, note, 0.0f);   // retrigger a note another chord holds
    keys.noteOn (1, note, vel);
}

void ChordPlayer::noteOffNow (int note)
{
    if (! juce::isPositiveAndBelow (note, 128) || refs[(size_t) note] <= 0) return;
    if (--refs[(size_t) note] == 0) keys.noteOff (1, note, 0.0f);   // otherwise another chord still holds it
}

void ChordPlayer::press (int owner, const std::vector<int>& notes, float velocity, float strumMs, int strumDir)
{
    const juce::ScopedLock sl (lock);
    if (sounding.count (owner) > 0) return;    // key repeat
    auto& on = sounding[owner];
    std::vector<int> order (notes);
    if (strumDir == 1) std::reverse (order.begin(), order.end());
    const double now = juce::Time::getMillisecondCounterHiRes();
    for (size_t i = 0; i < order.size(); ++i)
    {
        // a little velocity shading across the strum, the way fingers or a pick hit the strings
        const float v = juce::jlimit (0.05f, 1.0f, velocity * (strumMs > 0.0f ? 1.0f - 0.04f * (float) i : 1.0f));
        if (strumMs <= 0.0f || i == 0) { noteOnNow (order[i], v); on.push_back (order[i]); }
        else pending.push_back ({ now + strumMs * (double) i, order[i], v, owner });
    }
    if (! pending.empty() && ! isTimerRunning()) startTimer (1);
}

void ChordPlayer::release (int owner)
{
    const juce::ScopedLock sl (lock);
    pending.erase (std::remove_if (pending.begin(), pending.end(), [owner] (const Pending& p) { return p.owner == owner; }), pending.end());
    auto it = sounding.find (owner);
    if (it == sounding.end()) return;
    for (int n : it->second) noteOffNow (n);
    sounding.erase (it);
}

void ChordPlayer::releaseAll()
{
    const juce::ScopedLock sl (lock);
    pending.clear();
    for (auto& [owner, notes] : sounding)
        for (int n : notes) noteOffNow (n);
    sounding.clear();
    for (int n = 0; n < 128; ++n)
        if (refs[(size_t) n] > 0) { refs[(size_t) n] = 0; keys.noteOff (1, n, 0.0f); }
}

bool ChordPlayer::isHeld (int owner) const
{
    const juce::ScopedLock sl (lock);
    return sounding.count (owner) > 0;
}

std::vector<int> ChordPlayer::owners() const
{
    const juce::ScopedLock sl (lock);
    std::vector<int> o;
    for (auto& [owner, notes] : sounding) o.push_back (owner);
    return o;
}

void ChordPlayer::hiResTimerCallback()
{
    const juce::ScopedLock sl (lock);
    const double now = juce::Time::getMillisecondCounterHiRes();
    for (auto it = pending.begin(); it != pending.end();)
    {
        if (it->due <= now)
        {
            noteOnNow (it->note, it->velocity);
            sounding[it->owner].push_back (it->note);
            it = pending.erase (it);
        }
        else ++it;
    }
    if (pending.empty()) stopTimer();
}

} // namespace wis::daw
