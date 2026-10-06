#include "MidiLoops.h"
#include "Daw/Instruments/InstrumentRefs.h"
#include "Daw/Plugins/BuiltinProcessor.h"

namespace wis::daw
{

enum ChordType { maj, minr, maj7, min7, dom7, sus2, sus4, dim, add9 };

const std::vector<int>& chordIntervals (int type)
{
    static const std::vector<std::vector<int>> v = { { 0, 4, 7 }, { 0, 3, 7 }, { 0, 4, 7, 11 }, { 0, 3, 7, 10 }, { 0, 4, 7, 10 },
                                                     { 0, 2, 7 }, { 0, 5, 7 }, { 0, 3, 6 }, { 0, 4, 7, 14 } };
    return v[(size_t) juce::jlimit (0, (int) v.size() - 1, type)];
}

juce::String chordTypeSuffix (int type)
{
    static const char* s[] = { "", "m", "maj7", "m7", "7", "sus2", "sus4", "dim", "add9" };
    return s[juce::jlimit (0, 8, type)];
}

const std::vector<LoopProgression>& loopProgressions()
{
    static const std::vector<LoopProgression> p = {
        { "Pop Anthem (I-V-vi-IV)",           false, { { 0, maj }, { 7, maj }, { 9, minr }, { 5, maj } } },
        { "Dream Pop Drift (maj7 / m7)",      false, { { 0, maj7 }, { 4, min7 }, { 9, min7 }, { 5, maj7 } } },
        { "Hazy Borrowed iv (I-IV-iv-I)",     false, { { 0, maj }, { 5, maj }, { 5, minr }, { 0, maj } } },
        { "Fifties (I-vi-IV-V)",              false, { { 0, maj }, { 9, minr }, { 5, maj }, { 7, maj } } },
        { "Jazz Turn (ii7-V7-Imaj7)",         false, { { 2, min7 }, { 7, dom7 }, { 0, maj7 }, { 0, maj7 } } },
        { "Sus Shimmer (I-IV-vi-V, sus)",     false, { { 0, sus2 }, { 5, sus2 }, { 9, minr }, { 7, sus4 } } },
        { "Organ Lullaby (I-iii-IV-I)",       false, { { 0, add9 }, { 4, minr }, { 5, maj7 }, { 0, maj } } },
        { "Minor Epic (i-VI-III-VII)",        true,  { { 0, minr }, { 8, maj }, { 3, maj }, { 10, maj } } },
        { "Slow Ballad (i-iv-VI-V)",          true,  { { 0, minr }, { 5, minr }, { 8, maj }, { 7, maj } } },
        { "Andalusian (i-VII-VI-V)",          true,  { { 0, minr }, { 10, maj }, { 8, maj }, { 7, maj } } },
        { "Late Night (i7-iv7-v7-i7)",        true,  { { 0, min7 }, { 5, min7 }, { 7, min7 }, { 0, min7 } } },
        { "Moody Lift (i-III-VII-iv)",        true,  { { 0, minr }, { 3, maj7 }, { 10, maj }, { 5, min7 } } },
    };
    return p;
}

static PluginRef padSound()    { return synthRef (1); }
static PluginRef guitarSound() { return soundFontRef (0, 25); }
static PluginRef pianoSound()  { return soundFontRef (0, 0); }
static PluginRef organSound()  { return builtinPresetRef ("homekeys", 0); }
static PluginRef bassSound()   { return soundFontRef (0, 33); }
static PluginRef synthBass()   { return synthRef (2); }
static PluginRef pluckSound()  { return synthRef (5); }
static PluginRef bellSound()   { return synthRef (8); }

const std::vector<LoopStyle>& loopStyles()
{
    static const std::vector<LoopStyle> s = {
        { "Pads (whole notes)",       "Chords",   padSound },
        { "Strummed 8ths",            "Chords",   guitarSound },
        { "Piano Ballad",             "Chords",   pianoSound },
        { "Dreamy Organ",             "Chords",   organSound },
        { "Roots",              "Bass",     bassSound },
        { "Disco Octaves",      "Bass",     synthBass },
        { "Root-Fifth Walk",    "Bass",     bassSound },
        { "Up 16ths",            "Arpeggio", pluckSound },
        { "Dreamy Broken 8ths",  "Arpeggio", bellSound },
        { "Up-Down Triplets",    "Arpeggio", pluckSound },
    };
    return s;
}

/** Root pitch class for a progression in the song's key (minor progressions sit on the relative minor of a major song, etc.). */
static int rootFor (const Project& p, const LoopProgression& prog)
{
    const bool songMinor = p.scale() == 1;
    int root = p.key();
    if (prog.minor && ! songMinor) root += 9;
    if (! prog.minor && songMinor) root += 3;
    return root % 12;
}

juce::String describeProgression (const Project& p, int index)
{
    const auto& prog = loopProgressions()[(size_t) juce::jlimit (0, (int) loopProgressions().size() - 1, index)];
    static const char* names[] = { "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    const int root = rootFor (p, prog);
    juce::StringArray s;
    for (auto [deg, type] : prog.chords) s.add (juce::String (names[(root + deg) % 12]) + chordTypeSuffix (type));
    return s.joinIntoString (" - ");
}

static std::vector<int> voicing (int pc, int type, int lo, int hi)
{
    std::vector<int> notes;
    for (int iv : chordIntervals (type))
    {
        int n = lo - ((lo - (pc + iv)) % 12 + 12) % 12 + 12 * (iv >= 12 ? 1 : 0);
        while (n < lo) n += 12;
        while (n >= hi) n -= 12;
        notes.push_back (n);
    }
    std::sort (notes.begin(), notes.end());
    notes.erase (std::unique (notes.begin(), notes.end()), notes.end());
    return notes;
}

Clip insertMidiLoop (Project& p, const Track& t, int progIndex, int styleIndex, double startBeat)
{
    const auto& prog = loopProgressions()[(size_t) juce::jlimit (0, (int) loopProgressions().size() - 1, progIndex)];
    const auto& style = loopStyles()[(size_t) juce::jlimit (0, (int) loopStyles().size() - 1, styleIndex)];
    const double bar = p.beatsPerBar();
    const int bars = 8;
    auto c = p.addMidiClip (t, startBeat, bars * bar, prog.name.upToFirstOccurrenceOf (" (", false, false) + " - "
                                                     + (style.category == "Chords" ? style.name : style.category + " " + style.name));
    const int root = rootFor (p, prog);

    for (int b = 0; b < bars; ++b)
    {
        const auto [deg, type] = prog.chords[(size_t) (b % (int) prog.chords.size())];
        const int pc = (root + deg) % 12;
        const double s = b * bar;
        const auto chord = voicing (pc, type, 55, 74);
        const int bassNote = 28 + (pc + 8) % 12;   // E1 .. D#2, the bass guitar's home
        auto addChord = [&] (double at, double len, int vel) { for (int n : chord) p.addNote (c, n, s + at, len, vel); };

        switch (styleIndex)
        {
            case 0: addChord (0.0, bar - 0.05, 78); break;
            case 1:
                for (double x = 0; x < bar - 0.01; x += 0.5)
                {
                    const bool down = std::fmod (x, 1.0) < 0.01;
                    int i = 0;
                    for (int n : chord) p.addNote (c, n, s + x + i++ * 0.012, 0.42, down ? 96 : 70);   // a little strum
                }
                break;
            case 2:
                p.addNote (c, bassNote + 12, s, bar - 0.1, 80);
                p.addNote (c, bassNote + 24, s, bar - 0.1, 70);
                addChord (0.0, bar * 0.5 - 0.05, 82);
                addChord (bar * 0.5, bar * 0.5 - 0.05, 72);
                break;
            case 3:
                addChord (0.0, bar - 0.02, 84);
                p.addNote (c, bassNote + 12, s, bar - 0.02, 76);
                break;
            case 4:
                for (double x = 0; x < bar - 0.01; x += 1.0) p.addNote (c, bassNote, s + x, 0.9, x < 0.01 ? 104 : 88);
                break;
            case 5:
                for (double x = 0; x < bar - 0.01; x += 0.5)
                    p.addNote (c, bassNote + (std::fmod (x, 1.0) < 0.01 ? 0 : 12), s + x, 0.4, std::fmod (x, 1.0) < 0.01 ? 104 : 90);
                break;
            case 6:
            {
                const int fifth = bassNote + 7, octave = bassNote + 12;
                const int seq[] = { bassNote, fifth, octave, fifth };
                for (int q = 0; q < (int) bar; ++q) p.addNote (c, seq[q % 4], s + q, 0.9, q == 0 ? 102 : 86);
                break;
            }
            case 7:
            {
                std::vector<int> tones = chord;
                for (int n : chord) tones.push_back (n + 12);
                for (int k = 0; k < (int) (bar * 4); ++k) p.addNote (c, tones[(size_t) (k % (int) tones.size())], s + k * 0.25, 0.22, k % 4 == 0 ? 96 : 76);
                break;
            }
            case 8:
            {
                const std::vector<int> pat = { chord[0] - 12, chord[(size_t) juce::jmin (2, (int) chord.size() - 1)], chord[0], chord[1] + 12,
                                               chord[(size_t) juce::jmin (2, (int) chord.size() - 1)] + 12, chord[1] + 12, chord[0], chord[1] };
                for (int k = 0; k < (int) (bar * 2); ++k) p.addNote (c, pat[(size_t) (k % (int) pat.size())], s + k * 0.5, 0.9, k % 2 == 0 ? 88 : 72);
                break;
            }
            default:
            {
                std::vector<int> tones = chord;
                tones.push_back (chord[0] + 12);
                std::vector<int> updown = tones;
                for (int i = (int) tones.size() - 2; i > 0; --i) updown.push_back (tones[(size_t) i]);
                for (int k = 0; k < (int) (bar * 3); ++k) p.addNote (c, updown[(size_t) (k % (int) updown.size())], s + k / 3.0, 0.3, k % 3 == 0 ? 94 : 74);
                break;
            }
        }
    }
    return c;
}

} // namespace wis::daw
