#include "MidiEffects.h"

namespace wis::daw
{

// =====================================================================================================
//  Base
// =====================================================================================================
MidiFxBase::MidiFxBase (const juce::String& id, const juce::String& name, juce::AudioProcessorValueTreeState::ParameterLayout layout)
    : BuiltinProcessor (id, name, false, std::move (layout), true)
{
    queue.reserve (2048);
    output.ensureSize (4096);
}

void MidiFxBase::prepareToPlay (double sr, int)
{
    sampleRate = sr;
    queue.clear();
    now = 0;
    resetState();
}

void MidiFxBase::schedule (const juce::MidiMessage& m, juce::int64 at)
{
    if (queue.size() >= 2000) return;
    Scheduled s { at, {}, juce::jmin (3, m.getRawDataSize()) };
    for (int i = 0; i < s.size; ++i) s.data[i] = m.getRawData()[i];
    queue.push_back (s);
}

void MidiFxBase::processBlock (juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
{
    const int n = juce::jmax (1, audio.getNumSamples());
    blockLength = n;
    Clock c;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) c.bpm = *b;
            if (auto p = pos->getPpqPosition()) c.ppq = *p;
            c.playing = pos->getIsPlaying();
        }
    c.beatsPerSample = c.bpm / 60.0 / sampleRate;

    // panic from the engine (stop / seek): forget everything we were holding
    bool panic = false;
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isAllNotesOff() || m.isAllSoundOff()) panic = true;
    }
    output.clear();
    if (panic)
    {
        // release whatever we scheduled to stop later
        for (auto& s : queue)
            if ((s.data[0] & 0xf0) == 0x80) output.addEvent (s.data, s.size, 0);
        queue.clear();
        resetState();
    }

    process (midi, n, c);

    // due scheduled events
    for (size_t i = 0; i < queue.size();)
    {
        if (queue[i].time < now + n)
        {
            output.addEvent (queue[i].data, queue[i].size, (int) juce::jlimit ((juce::int64) 0, (juce::int64) n - 1, queue[i].time - now));
            queue[i] = queue.back();
            queue.pop_back();
        }
        else ++i;
    }
    // pass everything that isn't a note straight through (controllers, pitch bend, panic)
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (! m.isNoteOnOrOff()) output.addEvent (m, meta.samplePosition);
    }
    midi.swapWith (output);
    now += n;
}

juce::StringArray MidiFxBase::scaleNames()
{
    return { "Major", "Minor", "Dorian", "Phrygian", "Lydian", "Mixolydian", "Harmonic Minor", "Melodic Minor",
             "Major Pentatonic", "Minor Pentatonic", "Blues", "Whole Tone", "Japanese (In)", "Chromatic" };
}

const std::vector<int>& MidiFxBase::scaleSteps (int scale)
{
    static const std::vector<std::vector<int>> scales = {
        { 0, 2, 4, 5, 7, 9, 11 }, { 0, 2, 3, 5, 7, 8, 10 }, { 0, 2, 3, 5, 7, 9, 10 }, { 0, 1, 3, 5, 7, 8, 10 },
        { 0, 2, 4, 6, 7, 9, 11 }, { 0, 2, 4, 5, 7, 9, 10 }, { 0, 2, 3, 5, 7, 8, 11 }, { 0, 2, 3, 5, 7, 9, 11 },
        { 0, 2, 4, 7, 9 }, { 0, 3, 5, 7, 10 }, { 0, 3, 5, 6, 7, 10 }, { 0, 2, 4, 6, 8, 10 }, { 0, 1, 5, 7, 8 },
        { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } };
    return scales[(size_t) juce::jlimit (0, (int) scales.size() - 1, scale)];
}

int MidiFxBase::snapToScale (int note, int key, int scale, bool upOnTie)
{
    const auto& steps = scaleSteps (scale);
    int best = note, bestDist = 99;
    for (int d = 0; d <= 6; ++d)
        for (int sign : { upOnTie ? 1 : -1, upOnTie ? -1 : 1 })
        {
            const int cand = note + sign * d;
            const int pc = ((cand - key) % 12 + 12) % 12;
            if (std::find (steps.begin(), steps.end(), pc) != steps.end() && d < bestDist) { best = cand; bestDist = d; }
        }
    return juce::jlimit (0, 127, best);
}

static juce::StringArray keyChoices()
{
    return { "Song Key", "C", "C#/Db", "D", "D#/Eb", "E", "F", "F#/Gb", "G", "G#/Ab", "A", "A#/Bb", "B" };
}

// =====================================================================================================
//  Arpeggiator
// =====================================================================================================
juce::StringArray Arpeggiator::rateNames()
{
    return { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/4 T", "1/8 T", "1/16 T", "1/4 D", "1/8 D", "1/16 D" };
}

double Arpeggiator::rateBeats (int i)
{
    static const double beats[] = { 4.0, 2.0, 1.0, 0.5, 0.25, 0.125, 2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0, 1.5, 0.75, 0.375 };
    return beats[juce::jlimit (0, 11, i)];
}

static prm::Layout arpLayout()
{
    prm::Layout l;
    prm::addChoice (l, "mode", "Mode", { "Up", "Down", "Up / Down", "Down / Up", "As Played", "Random", "Random Walk", "Chord", "Converge", "Diverge", "Thumb Up" }, 0);
    prm::addChoice (l, "rate", "Rate", Arpeggiator::rateNames(), 4);
    prm::addFloat (l, "octaves", "Octaves", 1.0f, 4.0f, 1.0f, "", 0.0f, 0);
    prm::addFloat (l, "gate", "Gate", 0.05f, 2.0f, 0.6f, "x", 1.0f, 2);
    prm::addPercent (l, "swing", "Swing", 0.0f);
    prm::addBool (l, "latch", "Latch", false);
    prm::addChoice (l, "velmode", "Velocity", { "As Played", "Fixed", "Accent Every 4", "Ramp Up", "Random" }, 0);
    prm::addFloat (l, "velocity", "Fixed Velocity", 1.0f, 127.0f, 100.0f, "", 0.0f, 0);
    prm::addChoice (l, "rhythm", "Rhythm", { "Every Step", "x.x.", "xx.x", "x..x..x.", "x.xx.x.x", "xxx.", "x...", "Euclid 5/8", "Euclid 7/16" }, 0);
    prm::addPercent (l, "prob", "Probability", 1.0f);
    prm::addPercent (l, "ratchet", "Ratchet Chance", 0.0f);
    prm::addFloat (l, "transpose", "Transpose", -24.0f, 24.0f, 0.0f, "st", 0.0f, 0);
    return l;
}

Arpeggiator::Arpeggiator() : MidiFxBase ("arp", "Arpeggiator", arpLayout()) {}

juce::StringArray Arpeggiator::getProgramNames()
{
    return { "Classic 16ths Up", "Synthwave 8ths", "Dreamy Up/Down", "Trance Gate", "Random Glitter", "Broken Chords", "Ratchet Madness", "Euclid Bells" };
}

void Arpeggiator::loadProgram (int i)
{
    struct P { int mode, rate; float oct, gate, swing; int vel, rhythm; float prob, ratchet; };
    static const P presets[] = {
        { 0, 4, 1, 0.6f, 0.0f, 0, 0, 1.0f, 0.0f }, { 2, 3, 2, 0.8f, 0.0f, 2, 0, 1.0f, 0.0f }, { 2, 7, 3, 1.4f, 0.2f, 0, 0, 1.0f, 0.0f },
        { 7, 4, 1, 0.3f, 0.0f, 1, 4, 1.0f, 0.0f }, { 5, 5, 3, 0.4f, 0.0f, 4, 0, 0.75f, 0.1f }, { 4, 3, 1, 0.9f, 0.3f, 0, 3, 1.0f, 0.0f },
        { 6, 4, 2, 0.5f, 0.0f, 4, 0, 0.9f, 0.45f }, { 0, 4, 3, 0.7f, 0.0f, 2, 8, 1.0f, 0.05f } };
    const auto& p = presets[juce::jlimit (0, (int) std::size (presets) - 1, i)];
    setParam ("mode", (float) p.mode); setParam ("rate", (float) p.rate); setParam ("octaves", p.oct); setParam ("gate", p.gate);
    setParam ("swing", p.swing); setParam ("velmode", (float) p.vel); setParam ("rhythm", (float) p.rhythm);
    setParam ("prob", p.prob); setParam ("ratchet", p.ratchet);
}

void Arpeggiator::resetState()
{
    held.clear(); latched.clear(); allReleased = true; nextStep = -1.0; stepIndex = 0; sounding = -1;
}

void Arpeggiator::process (const juce::MidiBuffer& in, int n, const Clock& clock)
{
    const bool latch = param ("latch") > 0.5f;
    for (const auto meta : in)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn())
        {
            if (latch && allReleased) latched.clear();
            allReleased = false;
            held.push_back ({ m.getNoteNumber(), m.getVelocity() });
            if (latch) latched.push_back ({ m.getNoteNumber(), m.getVelocity() });
        }
        else if (m.isNoteOff())
        {
            held.erase (std::remove_if (held.begin(), held.end(), [&] (auto& h) { return h.first == m.getNoteNumber(); }), held.end());
            if (held.empty()) allReleased = true;
        }
    }
    if (! latch) latched.clear();
    const auto& notes = latch ? latched : held;

    const double stepBeats = rateBeats ((int) param ("rate"));
    // the clock: the song's when it plays; otherwise our own, started by the first key
    double ppq;
    if (clock.playing)
    {
        if (! wasPlaying) nextStep = -1.0;
        ppq = clock.ppq;
    }
    else
    {
        if (wasPlaying || notes.empty()) { if (notes.empty()) freePpq = 0.0; nextStep = notes.empty() ? -1.0 : nextStep; }
        ppq = freePpq;
    }
    wasPlaying = clock.playing;
    const double end = ppq + n * clock.beatsPerSample;
    if (! clock.playing) freePpq = notes.empty() ? 0.0 : end;

    if (notes.empty()) { nextStep = -1.0; stepIndex = 0; return; }
    if (nextStep < 0.0 || nextStep < ppq - stepBeats || nextStep > ppq + 4.0 * stepBeats)
    {
        nextStep = clock.playing ? std::ceil (ppq / stepBeats - 1.0e-9) * stepBeats : ppq;
        stepIndex = 0;
    }

    // build the note sequence
    std::vector<std::pair<int, int>> base (notes.begin(), notes.end());
    const int mode = (int) param ("mode");
    if (mode != 4) std::sort (base.begin(), base.end());
    const int octaves = juce::jlimit (1, 4, (int) param ("octaves"));
    const int transpose = (int) param ("transpose");
    std::vector<std::pair<int, int>> seq;
    for (int o = 0; o < octaves; ++o)
        for (auto& [note, vel] : base) seq.push_back ({ note + 12 * o + transpose, vel });
    if (seq.empty()) return;

    static const char* rhythms[] = { "x", "x.x.", "xx.x", "x..x..x.", "x.xx.x.x", "xxx.", "x...", "x.xx.xx.", "x.x.x.x.x.x.x..." };
    const juce::String rhythm = rhythms[juce::jlimit (0, 8, (int) param ("rhythm"))];
    const double gate = param ("gate"), swing = param ("swing");
    const float prob = param ("prob"), ratchet = param ("ratchet");
    const int velMode = (int) param ("velmode");

    auto pickNotes = [&] (int step) -> std::vector<std::pair<int, int>>
    {
        const int len = (int) seq.size();
        switch (mode)
        {
            case 0: case 4: return { seq[(size_t) (step % len)] };
            case 1: return { seq[(size_t) (len - 1 - step % len)] };
            case 2: case 3:
            {
                if (len == 1) return { seq[0] };
                const int period = 2 * len - 2, k = step % period;
                int idx = k < len ? k : period - k;
                if (mode == 3) idx = len - 1 - idx;
                return { seq[(size_t) idx] };
            }
            case 5: return { seq[(size_t) rng.nextInt (len)] };
            case 6:
            {
                walk = juce::jlimit (0, len - 1, walk + (rng.nextBool() ? 1 : -1));
                return { seq[(size_t) walk] };
            }
            case 7: return seq;
            case 8: { const int k = step % len, idx = (k % 2 == 0) ? k / 2 : len - 1 - k / 2; return { seq[(size_t) idx] }; }
            case 9: { const int k = step % len, mid = len / 2, idx = juce::jlimit (0, len - 1, (k % 2 == 0) ? mid + k / 2 : mid - 1 - k / 2); return { seq[(size_t) idx] }; }
            case 10: { const int k = step % (2 * len - 1); return { k % 2 == 0 ? seq[0] : seq[(size_t) juce::jmin (len - 1, (k + 1) / 2)] }; }
            default: return { seq[0] };
        }
    };

    while (nextStep < end)
    {
        double at = nextStep;
        if ((stepIndex & 1) == 1) at += swing * stepBeats * 0.5;   // swing: push every second step late
        const int offset = (int) std::floor ((at - ppq) / clock.beatsPerSample);
        if (offset >= n) break;   // swung step falls in the next block
        lastStep = stepIndex;

        const bool on = rhythm[stepIndex % rhythm.length()] == 'x' && rng.nextFloat() <= prob;
        if (on)
        {
            const int repeats = rng.nextFloat() < ratchet ? 2 + rng.nextInt (3) : 1;
            const double sub = stepBeats / repeats;
            for (int r = 0; r < repeats; ++r)
                for (auto [note, vel] : pickNotes (stepIndex))
                {
                    if (! juce::isPositiveAndBelow (note, 128)) continue;
                    int v = vel;
                    if (velMode == 1) v = (int) param ("velocity");
                    else if (velMode == 2) v = stepIndex % 4 == 0 ? 120 : 70;
                    else if (velMode == 3) v = 50 + (stepIndex % 8) * 10;
                    else if (velMode == 4) v = 40 + rng.nextInt (87);
                    const auto startSample = blockStart() + juce::jmax (0, offset) + (juce::int64) (r * sub / clock.beatsPerSample);
                    const auto length = (juce::int64) juce::jmax (sampleRate * 0.005, sub * gate / clock.beatsPerSample);
                    schedule (juce::MidiMessage::noteOn (1, note, (juce::uint8) juce::jlimit (1, 127, v)), startSample);
                    schedule (juce::MidiMessage::noteOff (1, note), startSample + length);
                }
        }
        ++stepIndex;
        nextStep += stepBeats;
    }
}

// =====================================================================================================
//  Chord Trigger
// =====================================================================================================
juce::StringArray ChordTrigger::chordNames()
{
    return { "Major", "Minor", "Dominant 7", "Major 7", "Minor 7", "Sus 2", "Sus 4", "Power (5th)", "Add 9", "Major 9",
             "Minor 9", "Diminished", "Augmented", "Octaves", "Stacked 4ths", "Dream (Maj7 #11)" };
}

static const std::vector<int>& chordIntervals (int i)
{
    static const std::vector<std::vector<int>> c = {
        { 0, 4, 7 }, { 0, 3, 7 }, { 0, 4, 7, 10 }, { 0, 4, 7, 11 }, { 0, 3, 7, 10 }, { 0, 2, 7 }, { 0, 5, 7 }, { 0, 7, 12 },
        { 0, 4, 7, 14 }, { 0, 4, 7, 11, 14 }, { 0, 3, 7, 10, 14 }, { 0, 3, 6 }, { 0, 4, 8 }, { 0, 12, 24 }, { 0, 5, 10, 15 },
        { 0, 4, 7, 11, 18 } };
    return c[(size_t) juce::jlimit (0, (int) c.size() - 1, i)];
}

static prm::Layout chordLayout()
{
    prm::Layout l;
    prm::addChoice (l, "chord", "Chord", ChordTrigger::chordNames(), 0);
    prm::addBool (l, "diatonic", "Follow Song Key", false);
    prm::addChoice (l, "inversion", "Inversion", { "Root", "1st", "2nd", "3rd" }, 0);
    prm::addChoice (l, "spread", "Voicing", { "Close", "Open", "Wide" }, 0);
    prm::addMs (l, "strum", "Strum", 0.0f, 200.0f, 0.0f);
    prm::addFloat (l, "transpose", "Transpose", -24.0f, 24.0f, 0.0f, "st", 0.0f, 0);
    return l;
}

ChordTrigger::ChordTrigger() : MidiFxBase ("chordtrig", "Chord Trigger", chordLayout()) {}

void ChordTrigger::process (const juce::MidiBuffer& in, int, const Clock&)
{
    for (const auto meta : in)
    {
        const auto m = meta.getMessage();
        const int note = m.getNoteNumber();
        if (m.isNoteOn())
        {
            std::vector<int> iv = chordIntervals ((int) param ("chord"));
            if (param ("diatonic") > 0.5f)
            {
                // build a triad (or 7th) from the song's scale instead
                const int key = songKey.load(), sc = songScale.load() == 1 ? 1 : 0;
                const auto& steps = scaleSteps (sc);
                const int pc = ((note - key) % 12 + 12) % 12;
                int degree = 0;
                for (int d = 0; d < (int) steps.size(); ++d) if (steps[(size_t) d] <= pc) degree = d;
                iv.clear();
                const int count = iv.empty() && chordIntervals ((int) param ("chord")).size() >= 4 ? 4 : 3;
                for (int k = 0; k < count; ++k)
                {
                    const int idx = degree + k * 2;
                    const int semis = steps[(size_t) (idx % 7)] + 12 * (idx / 7) - steps[(size_t) degree];
                    iv.push_back (semis);
                }
            }
            const int inversion = juce::jmin ((int) param ("inversion"), (int) iv.size() - 1);
            for (int k = 0; k < inversion; ++k) iv[(size_t) k] += 12;
            const int spread = (int) param ("spread");
            if (spread >= 1 && iv.size() >= 3) iv[1] += 12;
            if (spread == 2 && iv.size() >= 4) iv[3] += 12;
            std::sort (iv.begin(), iv.end());
            const int transpose = (int) param ("transpose");
            const double strum = param ("strum") * 0.001 * sampleRate;
            std::vector<int> sent;
            int k = 0;
            for (int i : iv)
            {
                const int out = note + i + transpose;
                if (! juce::isPositiveAndBelow (out, 128)) continue;
                schedule (juce::MidiMessage::noteOn (m.getChannel(), out, m.getVelocity()), blockStart() + meta.samplePosition + (juce::int64) (strum * k++));
                sent.push_back (out);
            }
            playing[note] = sent;
        }
        else if (m.isNoteOff())
        {
            if (auto it = playing.find (note); it != playing.end())
            {
                for (int out : it->second) schedule (juce::MidiMessage::noteOff (m.getChannel(), out), blockStart() + meta.samplePosition + (juce::int64) (param ("strum") * 0.001 * sampleRate * 4));
                playing.erase (it);
            }
        }
    }
}

// =====================================================================================================
//  Scale Lock
// =====================================================================================================
static prm::Layout scaleLockLayout()
{
    prm::Layout l;
    prm::addChoice (l, "key", "Key", keyChoices(), 0);
    prm::addChoice (l, "scale", "Scale", MidiFxBase::scaleNames(), 0);
    prm::addChoice (l, "mode", "Wrong Notes", { "Snap to Nearest", "Snap Up", "Snap Down", "Drop" }, 0);
    prm::addFloat (l, "transpose", "Scale Steps", -7.0f, 7.0f, 0.0f, "", 0.0f, 0);
    return l;
}

ScaleLock::ScaleLock() : MidiFxBase ("scalelock", "Scale Lock", scaleLockLayout()) {}

void ScaleLock::process (const juce::MidiBuffer& in, int, const Clock&)
{
    const int keyChoice = (int) param ("key");
    const int key = keyChoice == 0 ? songKey.load() : keyChoice - 1;
    int scale = (int) param ("scale");
    if (keyChoice == 0 && scale <= 1) scale = songScale.load() == 1 ? 1 : 0;
    const int mode = (int) param ("mode");
    const int steps = (int) param ("transpose");
    const auto& sc = scaleSteps (scale);
    for (const auto meta : in)
    {
        const auto m = meta.getMessage();
        const int note = m.getNoteNumber();
        if (m.isNoteOn())
        {
            int out = note;
            const int pc = ((note - key) % 12 + 12) % 12;
            const bool inScale = std::find (sc.begin(), sc.end(), pc) != sc.end();
            if (! inScale)
            {
                if (mode == 3) out = -1;
                else if (mode == 1) { out = note; while (std::find (sc.begin(), sc.end(), ((out - key) % 12 + 12) % 12) == sc.end()) ++out; }
                else if (mode == 2) { out = note; while (std::find (sc.begin(), sc.end(), ((out - key) % 12 + 12) % 12) == sc.end()) --out; }
                else out = snapToScale (note, key, scale);
            }
            if (out >= 0 && steps != 0)
            {
                // move by scale degrees
                for (int k = 0; k < std::abs (steps); ++k)
                {
                    int next = out + (steps > 0 ? 1 : -1);
                    while (std::find (sc.begin(), sc.end(), ((next - key) % 12 + 12) % 12) == sc.end()) next += steps > 0 ? 1 : -1;
                    out = next;
                }
            }
            if (out >= 0) out = juce::jlimit (0, 127, out);
            mapped[note] = out;
            if (out >= 0) emit (juce::MidiMessage::noteOn (m.getChannel(), out, m.getVelocity()), meta.samplePosition);
        }
        else if (m.isNoteOff())
        {
            auto it = mapped.find (note);
            const int out = it != mapped.end() ? it->second : note;
            if (it != mapped.end()) mapped.erase (it);
            if (out >= 0) emit (juce::MidiMessage::noteOff (m.getChannel(), out), meta.samplePosition);
        }
    }
}

// =====================================================================================================
//  Note Echo
// =====================================================================================================
static prm::Layout echoLayout()
{
    prm::Layout l;
    prm::addFloat (l, "repeats", "Repeats", 1.0f, 12.0f, 3.0f, "", 0.0f, 0);
    prm::addChoice (l, "rate", "Rate", Arpeggiator::rateNames(), 7);
    prm::addPercent (l, "feedback", "Velocity Decay", 0.3f);
    prm::addFloat (l, "pitch", "Pitch Step", -12.0f, 12.0f, 0.0f, "st", 0.0f, 0);
    prm::addFloat (l, "gate", "Gate", 0.05f, 1.0f, 0.5f, "x", 0.0f, 2);
    prm::addBool (l, "dry", "Play Original", true);
    return l;
}

NoteEcho::NoteEcho() : MidiFxBase ("noteecho", "Note Echo", echoLayout()) {}

void NoteEcho::process (const juce::MidiBuffer& in, int, const Clock& c)
{
    const int repeats = (int) param ("repeats");
    const double step = Arpeggiator::rateBeats ((int) param ("rate")) / c.beatsPerSample;
    const float decay = param ("feedback");
    const int pitch = (int) param ("pitch");
    const double gate = param ("gate");
    const bool dry = param ("dry") > 0.5f;
    for (const auto meta : in)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn())
        {
            if (dry) emit (m, meta.samplePosition);
            float v = m.getVelocity();
            for (int r = 1; r <= repeats; ++r)
            {
                v *= 1.0f - decay;
                if (v < 3.0f) break;
                const int note = m.getNoteNumber() + pitch * r;
                if (! juce::isPositiveAndBelow (note, 128)) break;
                const auto at = blockStart() + meta.samplePosition + (juce::int64) (step * r);
                schedule (juce::MidiMessage::noteOn (m.getChannel(), note, (juce::uint8) juce::jlimit (1, 127, (int) v)), at);
                schedule (juce::MidiMessage::noteOff (m.getChannel(), note), at + (juce::int64) juce::jmax (sampleRate * 0.01, step * gate));
            }
        }
        else if (m.isNoteOff() && dry) emit (m, meta.samplePosition);
    }
}

// =====================================================================================================
//  Randomizer
// =====================================================================================================
static prm::Layout randomLayout()
{
    prm::Layout l;
    prm::addPercent (l, "velocity", "Velocity Spread", 0.15f);
    prm::addMs (l, "timing", "Timing Spread", 0.0f, 60.0f, 8.0f);
    prm::addPercent (l, "drop", "Drop Notes", 0.0f);
    prm::addPercent (l, "octave", "Octave Jumps", 0.0f);
    prm::addPercent (l, "scaleJump", "Scale Wander", 0.0f);
    return l;
}

Randomizer::Randomizer() : MidiFxBase ("randomizer", "Randomizer", randomLayout()) {}

void Randomizer::process (const juce::MidiBuffer& in, int, const Clock&)
{
    const float velSpread = param ("velocity"), drop = param ("drop"), octave = param ("octave"), wander = param ("scaleJump");
    const double timing = param ("timing") * 0.001 * sampleRate;
    for (const auto meta : in)
    {
        const auto m = meta.getMessage();
        const int note = m.getNoteNumber();
        if (m.isNoteOn())
        {
            int out = note;
            if (rng.nextFloat() < drop) out = -1;
            else
            {
                if (rng.nextFloat() < octave) out += rng.nextBool() ? 12 : -12;
                if (rng.nextFloat() < wander)
                    out = snapToScale (out + (rng.nextBool() ? 1 : -1) * (1 + rng.nextInt (4)), songKey.load(), songScale.load() == 1 ? 1 : 0);
                out = juce::jlimit (0, 127, out);
            }
            shifted[note] = out;
            if (out < 0) continue;
            const int v = juce::jlimit (1, 127, (int) (m.getVelocity() * (1.0f + velSpread * (rng.nextFloat() * 2.0f - 1.0f))));
            const auto delay = (juce::int64) (timing * rng.nextFloat());
            schedule (juce::MidiMessage::noteOn (m.getChannel(), out, (juce::uint8) v), blockStart() + meta.samplePosition + delay);
        }
        else if (m.isNoteOff())
        {
            auto it = shifted.find (note);
            const int out = it != shifted.end() ? it->second : note;
            if (it != shifted.end()) shifted.erase (it);
            if (out >= 0) schedule (juce::MidiMessage::noteOff (m.getChannel(), out), blockStart() + meta.samplePosition + (juce::int64) timing);
        }
    }
}

} // namespace wis::daw
