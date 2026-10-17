#if defined(_MSC_VER)
 #pragma warning (push, 0)
#elif defined(__clang__)
 #pragma clang diagnostic push
 #pragma clang diagnostic ignored "-Weverything"
#elif defined(__GNUC__)
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wall"
 #pragma GCC diagnostic ignored "-Wextra"
#endif
#include <tsf.h>
#if defined(_MSC_VER)
 #pragma warning (pop)
#elif defined(__clang__)
 #pragma clang diagnostic pop
#elif defined(__GNUC__)
 #pragma GCC diagnostic pop
#endif

#include "SmartDrummer.h"
#include "SoundFontInstrument.h"
#include "DrumSynth.h"
#include "RoomIr.h"
#include <cmath>
#include <mutex>

namespace wis::daw
{

std::function<juce::AudioProcessorEditor* (SmartDrummer&)> SmartDrummer::editorFactory;
std::function<void (SmartDrummer&, bool)> SmartDrummer::onPlaySong;
std::function<void (SmartDrummer&)> SmartDrummer::onRewrite;

using namespace prm;

// ---- pieces and sources ----------------------------------------------------------------------------------
namespace
{
    struct PieceDef { const char* id; const char* name; int note; float seconds; float pan; DrumSynth::Voice synth; float synthTune; };
    const PieceDef pieceDefs[SmartDrummer::numPieces] = {
        { "kick",   "Kick",          36, 1.2f,  0.0f,  DrumSynth::kick,       0.0f },
        { "snare",  "Snare",         38, 1.2f,  0.0f,  DrumSynth::snare,      0.0f },
        { "stick",  "Side Stick",    37, 0.6f,  0.0f,  DrumSynth::rim,        0.0f },
        { "clap",   "Clap",          39, 1.0f,  0.0f,  DrumSynth::clap,       0.0f },
        { "hhc",    "Hi-Hat Closed", 42, 0.6f,  0.3f,  DrumSynth::closedHat,  0.0f },
        { "hhp",    "Hi-Hat Pedal",  44, 0.6f,  0.3f,  DrumSynth::pedalHat,   0.0f },
        { "hho",    "Hi-Hat Open",   46, 2.2f,  0.3f,  DrumSynth::openHat,    0.0f },
        { "tomF",   "Floor Tom",     41, 1.8f, -0.35f, DrumSynth::lowTom,    -4.0f },
        { "tomL",   "Low Tom",       45, 1.6f, -0.15f, DrumSynth::lowTom,     0.0f },
        { "tomM",   "Mid Tom",       47, 1.5f,  0.05f, DrumSynth::midTom,     0.0f },
        { "tomH",   "High Tom",      50, 1.4f,  0.25f, DrumSynth::highTom,    0.0f },
        { "crash",  "Crash",         49, 4.0f,  0.4f,  DrumSynth::crash,      0.0f },
        { "ride",   "Ride",          51, 3.5f, -0.4f,  DrumSynth::ride,       0.0f },
        { "bell",   "Ride Bell",     53, 3.0f, -0.4f,  DrumSynth::ride,       7.0f },
        { "tamb",   "Tambourine",    54, 1.0f,  0.5f,  DrumSynth::tambourine, 0.0f },
        { "shaker", "Shaker",        70, 0.6f, -0.5f,  DrumSynth::maracas,    0.0f },
        { "cowbell","Cowbell",       56, 1.0f,  0.2f,  DrumSynth::cowbell,    0.0f },
    };
    const int sampledPrograms[11] = { 0, 1, 2, 8, 16, 24, 25, 26, 32, 40, 48 };
    const float layerVel[3] = { 0.4f, 0.72f, 1.0f };
}

juce::String SmartDrummer::pieceName (int p) { return juce::isPositiveAndBelow (p, (int) numPieces) ? pieceDefs[p].name : ""; }
juce::String SmartDrummer::pieceId (int p) { return juce::isPositiveAndBelow (p, (int) numPieces) ? pieceDefs[p].id : ""; }
int SmartDrummer::noteForPiece (int p) { return juce::isPositiveAndBelow (p, (int) numPieces) ? pieceDefs[p].note : 36; }

int SmartDrummer::pieceForNote (int n)
{
    switch (n)
    {
        case 35: case 36: return kick;
        case 38: case 40: return snare;
        case 37: return stick;
        case 39: return clap;
        case 42: return hatClosed;
        case 44: return hatPedal;
        case 46: return hatOpen;
        case 41: case 43: return floorTom;
        case 45: return lowTom;
        case 47: case 48: return midTom;
        case 50: return highTom;
        case 49: case 52: case 55: case 57: return crash;
        case 51: case 59: return ride;
        case 53: return rideBell;
        case 54: return tambourine;
        case 69: case 70: case 82: return shaker;
        case 56: return cowbell;
        default: return -1;
    }
}

juce::StringArray SmartDrummer::sourceNames()
{
    return { "Studio Kit 1", "Studio Kit 2", "Studio Kit 3", "Room Kit", "Power Kit", "Electronic Kit", "808 / 909", "Dance Kit",
             "Jazz Kit", "Brushes", "Orchestral", "Synth: Home Keyboard '84", "Synth: Rhythm Unit '78", "Synth: Eight-Oh-Eight", "Synth: Toy Box Lo-Fi" };
}

// ---- the sample cache: every piece of every source, rendered once per sample rate ---------------------------
struct SmartDrummer::Samples
{
    std::array<juce::AudioBuffer<float>, 3> layer;
    double sr = 48000.0;
    float rate = 1.0f;        // playback pitch (synth floor tom / bell are tuned machine voices)
    float gain = 1.0f;        // matches the source's level to the others
};

struct SmartDrummer::Bank : public juce::ReferenceCountedObject
{
    using Ptr = juce::ReferenceCountedObjectPtr<Bank>;
    std::array<const Samples*, numPieces> piece {};
};

namespace
{
    juce::AudioBuffer<float> trimmed (juce::AudioBuffer<float> b)
    {
        int last = 0;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = b.getNumSamples(); --i >= 0;)
                if (std::abs (b.getSample (ch, i)) > 1.0e-4f) { last = juce::jmax (last, i); break; }
        const int len = juce::jmin (b.getNumSamples(), last + 64);
        b.setSize (2, juce::jmax (1, len), true);
        const int fade = juce::jmin (256, b.getNumSamples());
        for (int ch = 0; ch < 2; ++ch) b.applyGainRamp (ch, b.getNumSamples() - fade, fade, 1.0f, 0.0f);
        return b;
    }

    juce::AudioBuffer<float> renderSampled (int program, int note, float velocity, double sr, float seconds)
    {
        const auto sf = SoundFontCache::defaultSoundFont();
        if (! sf.existsAsFile()) return {};
        juce::String err;
        tsf* t = SoundFontCache::get().createInstance (sf, err);
        if (t == nullptr) return {};
        tsf_set_output (t, TSF_STEREO_INTERLEAVED, (int) sr, 0.0f);
        if (! tsf_channel_set_bank_preset (t, 0, 128, program))
            tsf_channel_set_presetnumber (t, 0, program, 1);
        tsf_channel_note_on (t, 0, note, velocity);
        const int total = (int) (sr * seconds);
        juce::AudioBuffer<float> out (2, total);
        std::vector<float> inter (512 * 2);
        for (int done = 0; done < total; done += 512)
        {
            const int n = juce::jmin (512, total - done);
            tsf_render_float (t, inter.data(), n, 0);
            for (int i = 0; i < n; ++i)
            {
                out.setSample (0, done + i, inter[(size_t) (i * 2)]);
                out.setSample (1, done + i, inter[(size_t) (i * 2 + 1)]);
            }
        }
        tsf_close (t);
        return trimmed (std::move (out));
    }

    struct HitCache
    {
        std::mutex lock;
        std::map<juce::String, std::unique_ptr<SmartDrummer::Samples>> items;
        std::map<juce::String, float> sourceGain;

        static HitCache& get() { static HitCache c; return c; }

        static float peakOf (const juce::AudioBuffer<float>& b) { return b.getNumSamples() > 1 ? b.getMagnitude (0, b.getNumSamples()) : 0.0f; }

        SmartDrummer::Samples* render (int source, int piece, double sr)
        {
            auto s = std::make_unique<SmartDrummer::Samples>();
            s->sr = sr;
            const auto& def = pieceDefs[piece];
            for (int l = 0; l < 3; ++l)
            {
                if (SmartDrummer::sourceIsSampled (source))
                    s->layer[(size_t) l] = renderSampled (sampledPrograms[source], def.note, layerVel[l], sr, def.seconds);
                if (s->layer[(size_t) l].getNumSamples() <= 1)
                {
                    // no Sound Library (or a synth machine): the drum machine voices
                    const int kit = SmartDrummer::sourceIsSampled (source) ? (int) DrumSynth::rhythmUnit : source - 11;
                    s->layer[(size_t) l] = trimmed (DrumSynth::renderHit (def.synth, kit, sr, layerVel[l]));
                    s->rate = std::exp2 (def.synthTune / 12.0f);
                }
            }
            return s.release();
        }

        /** The piece, rendering it (and its source's level reference, the kick) if needed. Blocking. */
        const SmartDrummer::Samples* get (int source, int piece, double sr)
        {
            std::lock_guard<std::mutex> l (lock);
            auto key = [sr] (int src, int p) { return juce::String (src) + ":" + juce::String (p) + "@" + juce::String ((int) sr); };
            const auto gk = juce::String (source) + "@" + juce::String ((int) sr);
            if (sourceGain.find (gk) == sourceGain.end())
            {
                auto kickKey = key (source, SmartDrummer::kick);
                if (items.find (kickKey) == items.end()) items[kickKey].reset (render (source, SmartDrummer::kick, sr));
                const float pk = peakOf (items[kickKey]->layer[2]);
                const float g = pk > 1.0e-4f ? 0.6f / pk : 1.0f;
                sourceGain[gk] = juce::jlimit (0.1f, 12.0f, g);
                items[kickKey]->gain = sourceGain[gk];
            }
            auto k = key (source, piece);
            auto it = items.find (k);
            if (it == items.end())
            {
                items[k].reset (render (source, piece, sr));
                items[k]->gain = sourceGain[gk];
                it = items.find (k);
            }
            return it->second.get();
        }
    };
}

// ---- the loader thread ---------------------------------------------------------------------------------------
struct SmartDrummer::Loader : public juce::Thread
{
    explicit Loader (SmartDrummer& o) : juce::Thread ("Drum kit loader"), owner (o) {}
    ~Loader() override { stopThread (10000); }

    void request (const std::array<int, numPieces>& sources, double sr)
    {
        {
            const juce::ScopedLock sl (lock);
            wanted = sources;
            wantedRate = sr;
            pending = true;
            owner.kitReady = false;
        }
        notify();
    }

    void run() override
    {
        while (! threadShouldExit())
        {
            std::array<int, numPieces> src {};
            double sr = 0.0;
            bool got = false;
            {
                const juce::ScopedLock sl (lock);
                if (pending) { src = wanted; sr = wantedRate; pending = false; got = true; }
            }
            if (got && sr > 0.0 && (src != loaded || sr != loadedRate || first))
            {
                first = false;
                Bank::Ptr bank = new Bank();
                for (int p = 0; p < numPieces; ++p)
                {
                    if (threadShouldExit()) return;
                    bank->piece[(size_t) p] = HitCache::get().get (src[(size_t) p], p, sr);
                }
                {
                    const juce::SpinLock::ScopedLockType sl (owner.bankLock);
                    owner.retiredBank = nullptr;   // freed here, never on the audio thread
                    owner.pendingBank = bank;
                }
                loaded = src;
                loadedRate = sr;
            }
            if (got)
            {
                // ready once the newest request is in (not an older one finishing while a newer one waits)
                const juce::ScopedLock sl (lock);
                if (! pending) owner.kitReady = true;
            }
            bool again = false;
            { const juce::ScopedLock sl (lock); again = pending; }
            if (! again) wait (-1);
        }
    }

    SmartDrummer& owner;
    juce::CriticalSection lock;
    std::array<int, numPieces> wanted {}, loaded {};
    double wantedRate = 0.0, loadedRate = 0.0;
    bool pending = false, first = true;
};

// ---- parameters ---------------------------------------------------------------------------------------------
static Layout drummerLayout()
{
    Layout l;
    addChoice  (l, "style", "Style", drummer::styleNames(), 0);
    addPercent (l, "complexity", "Complexity", 0.5f);
    addFloat   (l, "energy", "Energy", -1.0f, 1.0f, 0.0f, {}, 0.0f, 2);
    addPercent (l, "follow", "Follow the Band", 0.6f);
    addPercent (l, "fills", "Fills", 0.5f);
    addBool    (l, "swingAuto", "Swing from the Song", true);
    addPercent (l, "swing", "Swing", 0.0f);
    addPercent (l, "humanize", "Humanize", 0.5f);
    addFloat   (l, "feel", "Push - Lay Back", -1.0f, 1.0f, 0.0f, {}, 0.0f, 2);
    addChoice  (l, "timeFeel", "Time", { "Normal", "Half-time", "Double-time" }, 0);
    addChoice  (l, "cymbals", "Cymbals", { "Style's choice", "Hi-hat", "Ride" }, 0);
    addBool    (l, "endHit", "Final Hit", true);
    l.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "seed", 1 }, "Take", 1, 9999, 1));
    addBool    (l, "matchKit", "Match Kit to Style", true);
    addBool    (l, "autoWrite", "Rewrite as You Change It", true);

    addChoice  (l, "kit", "Kit", SmartDrummer::sourceNames(), 4);
    juce::StringArray srcChoices { "Kit" };
    srcChoices.addArray (SmartDrummer::sourceNames());
    for (int p = 0; p < SmartDrummer::numPieces; ++p)
    {
        const juce::String id = pieceDefs[p].id, name = pieceDefs[p].name;
        addChoice (l, id + "Src", name + " From", srcChoices, 0);
        addFloat  (l, id + "Tune", name + " Tune", -12.0f, 12.0f, 0.0f, "st", 0.0f, 1);
        addPercent (l, id + "Decay", name + " Decay", 1.0f);
        addDb     (l, id + "Level", name + " Level", -30.0f, 6.0f, 0.0f);
        addFloat  (l, id + "Pan", name + " Pan", -1.0f, 1.0f, pieceDefs[p].pan, {}, 0.0f, 2);
        addBool   (l, id + "Mute", name + " Mute", false);
    }
    addChoice  (l, "room", "Room", roomir::spaceNames(), (int) roomir::woodenStudio);
    addPercent (l, "roomMix", "Room Level", 0.2f);
    addPercent (l, "punch", "Punch", 0.4f);
    addPercent (l, "drive", "Drive", 0.1f);
    addFloat   (l, "tone", "Tone", -1.0f, 1.0f, 0.0f, {}, 0.0f, 2);
    addDb      (l, "volume", "Volume", -24.0f, 12.0f, 0.0f);
    return l;
}

namespace
{
    struct KitStyle { int source, room; float roomMix, punch, drive, tone; std::vector<std::pair<const char*, float>> tweaks; };
    const KitStyle& kitStyle (int style)
    {
        static const std::vector<KitStyle> k {
            { 4, (int) roomir::bigLiveRoom, 0.3f, 0.5f, 0.1f, 0.1f, {} },                                           // Rock
            { 0, (int) roomir::woodenStudio, 0.2f, 0.5f, 0.0f, 0.2f, {} },                                         // Pop
            { 3, (int) roomir::bigLiveRoom, 0.35f, 0.3f, 0.25f, 0.0f, {} },                                        // Indie
            { 2, (int) roomir::woodenStudio, 0.12f, 0.7f, 0.2f, -0.35f, { { "kickTune", -2.0f }, { "snareDecay", 0.45f }, { "snareTune", -1.0f }, { "hhcLevel", -3.0f } } },  // Psych
            { 4, (int) roomir::barClub, 0.25f, 0.6f, 0.3f, 0.2f, {} },                                             // Punk
            { 4, (int) roomir::bigLiveRoom, 0.2f, 0.85f, 0.15f, 0.3f, { { "kickDecay", 0.5f } } },                // Metal
            { 1, (int) roomir::woodenStudio, 0.1f, 0.55f, 0.1f, 0.1f, { { "snareTune", 2.0f } } },                // Funk
            { 3, (int) roomir::woodenStudio, 0.25f, 0.35f, 0.15f, -0.15f, { { "tambLevel", 2.0f } } },            // Soul
            { 1, (int) roomir::livingRoom, 0.08f, 0.7f, 0.35f, -0.1f, { { "kickTune", -2.0f } } },                 // Hip-hop
            { 2, (int) roomir::livingRoom, 0.12f, 0.5f, 0.45f, -0.6f, { { "hhcLevel", -4.0f } } },                 // Lo-fi
            { 0, (int) roomir::woodenStudio, 0.15f, 0.5f, 0.0f, 0.2f, {} },                                        // Disco
            { 7, (int) roomir::plate, 0.12f, 0.6f, 0.1f, 0.15f, {} },                                              // House
            { 3, (int) roomir::plate, 0.2f, 0.3f, 0.1f, -0.1f, { { "snareTune", 4.0f }, { "stickLevel", 2.0f }, { "volume", 3.0f } } }, // Reggae
            { 3, (int) roomir::barClub, 0.25f, 0.3f, 0.1f, -0.1f, { { "volume", 2.0f } } },                      // Blues
            { 8, (int) roomir::woodenStudio, 0.2f, 0.1f, 0.0f, 0.0f, { { "kickLevel", -4.0f }, { "volume", 6.0f } } }, // Jazz
            { 9, (int) roomir::livingRoom, 0.18f, 0.1f, 0.0f, -0.1f, {} },                                         // Bossa
            { 9, (int) roomir::woodenStudio, 0.2f, 0.15f, 0.0f, -0.1f, { { "volume", 3.0f } } },                 // Folk
            { 3, (int) roomir::church, 0.35f, 0.3f, 0.1f, -0.25f, { { "kickTune", -1.0f } } },                    // Dream pop
        };
        return k[(size_t) juce::jlimit (0, (int) k.size() - 1, style)];
    }
}

// ---- lifecycle ----------------------------------------------------------------------------------------------
SmartDrummer::SmartDrummer() : BuiltinProcessor ("drummer", "Smart Drummer", true, drummerLayout())
{
    loader = std::make_unique<Loader> (*this);
    loader->startThread();
    requestedSources.fill (-1);
    loadProgram (0);
    startTimerHz (5);
}

SmartDrummer::~SmartDrummer()
{
    stopTimer();
    loader.reset();
}

void SmartDrummer::loadProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) drummer::styles().size())) return;
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            rp->setValueNotifyingHost (rp->getDefaultValue());
    setParam ("style", (float) index);
    applyStyleKit (index);
}

void SmartDrummer::applyStyleKit (int style)
{
    const auto& k = kitStyle (style);
    for (int p = 0; p < numPieces; ++p)
    {
        const juce::String id = pieceDefs[p].id;
        setParam (id + "Src", 0.0f);
        setParam (id + "Tune", 0.0f);
        setParam (id + "Decay", 1.0f);
        setParam (id + "Level", 0.0f);
        setParam (id + "Pan", pieceDefs[p].pan);
        setParam (id + "Mute", 0.0f);
    }
    setParam ("kit", (float) k.source);
    setParam ("room", (float) k.room);
    setParam ("roomMix", k.roomMix);
    setParam ("punch", k.punch);
    setParam ("drive", k.drive);
    setParam ("tone", k.tone);
    setParam ("volume", 0.0f);
    for (auto& [id, v] : k.tweaks) setParam (id, v);
    requestKit();
}

drummer::Settings SmartDrummer::settings() const
{
    drummer::Settings s;
    s.style = (int) param ("style");
    s.complexity = param ("complexity");
    s.energy = param ("energy");
    s.follow = param ("follow");
    s.fills = param ("fills");
    s.swingAuto = param ("swingAuto") > 0.5f;
    s.swing = param ("swing");
    s.humanize = param ("humanize");
    s.feel = param ("feel");
    s.timeFeel = (int) param ("timeFeel");
    s.cymbals = (int) param ("cymbals");
    s.endHit = param ("endHit") > 0.5f;
    s.seed = juce::roundToInt (param ("seed"));
    s.sectionLevel = sectionLevels;
    return s;
}

void SmartDrummer::setSectionLevel (int startBar, int level)
{
    if (level < 0) sectionLevels.erase (startBar);
    else sectionLevels[startBar] = juce::jlimit (0, 4, level);
    sendChangeMessage();
}

int SmartDrummer::getSectionLevel (int startBar) const
{
    auto it = sectionLevels.find (startBar);
    return it != sectionLevels.end() ? it->second : -1;
}

void SmartDrummer::saveExtraState (juce::ValueTree& t)
{
    for (auto& [bar, level] : sectionLevels)
    {
        juce::ValueTree s ("SECTION");
        s.setProperty ("bar", bar, nullptr);
        s.setProperty ("level", level, nullptr);
        t.appendChild (s, nullptr);
    }
}

void SmartDrummer::loadExtraState (const juce::ValueTree& t)
{
    sectionLevels.clear();
    for (auto s : t)
        if (s.hasType ("SECTION")) sectionLevels[(int) s["bar"]] = juce::jlimit (0, 4, (int) s["level"]);
}

std::array<int, SmartDrummer::numPieces> SmartDrummer::wantedSources() const
{
    std::array<int, numPieces> src {};
    const int kit = (int) param ("kit");
    for (int p = 0; p < numPieces; ++p)
    {
        const int s = (int) param ((juce::String (pieceDefs[p].id) + "Src").toRawUTF8());
        src[(size_t) p] = s == 0 ? kit : s - 1;
    }
    return src;
}

void SmartDrummer::requestKit()
{
    const auto src = wantedSources();
    if (src == requestedSources && kitReady.load()) return;
    requestedSources = src;
    loader->request (src, sampleRate);
}

void SmartDrummer::prepareToPlay (double sr, int block)
{
    const bool rateChanged = sr != sampleRate;
    sampleRate = sr;
    maxBlock = juce::jmax (16, block);
    for (auto& v : voices) v.active = false;
    toneLp[0] = toneLp[1] = compEnv = 0.0f;
    wet.setSize (2, maxBlock);
    convolution.prepare ({ sr, (juce::uint32) maxBlock, 2 });
    roomKey[0] = roomKey[1] = -1;
    rebuildRoomIfNeeded (true);
    if (rateChanged) { kitReady = false; requestedSources.fill (-1); }
    requestKit();
}

void SmartDrummer::timerCallback()
{
    requestKit();
    rebuildRoomIfNeeded (false);
    const juce::SpinLock::ScopedTryLockType sl (bankLock);
    if (sl.isLocked()) retiredBank = nullptr;
}

void SmartDrummer::rebuildRoomIfNeeded (bool force)
{
    const int key[2] = { (int) param ("room"), 100 };
    if (! force && key[0] == roomKey[0] && key[1] == roomKey[1]) return;
    roomKey[0] = key[0]; roomKey[1] = key[1];
    auto ir = roomir::design (key[0], sampleRate, 1.0f, 0.5f, 3.0f);
    convolution.loadImpulseResponse (std::move (ir), sampleRate, juce::dsp::Convolution::Stereo::yes,
                                     juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
    roomReady = true;
}

void SmartDrummer::waitUntilReady()
{
    requestKit();
    rebuildRoomIfNeeded (true);
    for (int i = 0; i < 600 && ! kitReady.load(); ++i) juce::Thread::sleep (50);
    juce::Thread::sleep (200);
}

void SmartDrummer::audition (int piece, float velocity)
{
    int s1, n1, s2, n2;
    auditionFifo.prepareToWrite (1, s1, n1, s2, n2);
    if (n1 > 0) auditionQueue[(size_t) s1] = { piece, velocity };
    else if (n2 > 0) auditionQueue[(size_t) s2] = { piece, velocity };
    auditionFifo.finishedWrite (n1 + n2);
}

// ---- playing ------------------------------------------------------------------------------------------------
void SmartDrummer::startVoice (int piece, float velocity)
{
    if (liveBank == nullptr || ! juce::isPositiveAndBelow (piece, (int) numPieces)) return;
    const auto* smp = liveBank->piece[(size_t) piece];
    if (smp == nullptr) return;
    const juce::String id = pieceDefs[piece].id;
    if (param ((id + "Mute").toRawUTF8()) > 0.5f) return;

    // hi-hats: a closed or pedal hat chokes the open one
    if (piece == hatClosed || piece == hatPedal)
        for (auto& v : voices)
            if (v.active && v.piece == hatOpen) v.chokeMul = std::exp (-1.0f / (0.012f * (float) sampleRate));

    // a few voices per piece (cymbals ring over each other), steal the oldest
    int same = 0, oldestSame = -1, freeSlot = -1, oldest = -1;
    juce::uint32 oldestAge = 0xffffffffu, oldestSameAge = 0xffffffffu;
    for (int i = 0; i < (int) voices.size(); ++i)
    {
        auto& v = voices[(size_t) i];
        if (! v.active) { if (freeSlot < 0) freeSlot = i; continue; }
        if (v.age < oldestAge) { oldestAge = v.age; oldest = i; }
        if (v.piece == piece) { ++same; if (v.age < oldestSameAge) { oldestSameAge = v.age; oldestSame = i; } }
    }
    const int maxSame = piece == crash || piece == ride || piece == hatOpen ? 3 : 4;
    const int slot = same >= maxSame ? oldestSame : (freeSlot >= 0 ? freeSlot : oldest);
    if (slot < 0) return;

    auto& v = voices[(size_t) slot];
    const float vel = juce::jlimit (0.01f, 1.0f, velocity);
    v.layer = vel < 0.55f ? 0 : vel < 0.86f ? 1 : 2;
    if (smp->layer[(size_t) v.layer].getNumSamples() <= 1) return;
    v.samples = smp;
    v.piece = piece;
    v.pos = 0.0;
    v.rate = smp->rate * std::exp2 (param ((id + "Tune").toRawUTF8()) / 12.0f) * smp->sr / sampleRate;
    v.gain = std::pow (vel / layerVel[v.layer], 1.3f) * smp->gain * juce::Decibels::decibelsToGain (param ((id + "Level").toRawUTF8()), -29.9f);
    const float pan = param ((id + "Pan").toRawUTF8());
    v.panL = juce::jmin (1.0f, 1.0f - pan);
    v.panR = juce::jmin (1.0f, 1.0f + pan);
    const float d = param ((id + "Decay").toRawUTF8());
    v.decayMul = d >= 0.98f ? 1.0f : std::exp (-1.0f / ((0.03f + 1.5f * d * d) * (float) sampleRate));
    v.env = 1.0f;
    v.choke = 1.0f;
    v.chokeMul = 1.0f;
    v.age = ++voiceCounter;
    v.active = true;
}

void SmartDrummer::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals nd;
    const int total = buffer.getNumSamples();
    buffer.clear();
    {
        const juce::SpinLock::ScopedTryLockType sl (bankLock);
        if (sl.isLocked() && pendingBank != nullptr && retiredBank == nullptr)
        {
            retiredBank = liveBank;
            liveBank = pendingBank;
            pendingBank = nullptr;
        }
    }
    {
        int s1, n1, s2, n2;
        auditionFifo.prepareToRead (auditionFifo.getNumReady(), s1, n1, s2, n2);
        for (int i = 0; i < n1; ++i) startVoice (auditionQueue[(size_t) (s1 + i)].first, auditionQueue[(size_t) (s1 + i)].second);
        for (int i = 0; i < n2; ++i) startVoice (auditionQueue[(size_t) (s2 + i)].first, auditionQueue[(size_t) (s2 + i)].second);
        auditionFifo.finishedRead (n1 + n2);
    }

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    auto renderVoices = [&] (int from, int to)
    {
        for (auto& v : voices)
        {
            if (! v.active) continue;
            const auto& b = v.samples->layer[(size_t) v.layer];
            const int len = b.getNumSamples();
            const float* sl = b.getReadPointer (0);
            const float* sr = b.getReadPointer (juce::jmin (1, b.getNumChannels() - 1));
            for (int i = from; i < to; ++i)
            {
                const int idx = (int) v.pos;
                if (idx + 1 >= len || v.env < 1.0e-4f) { v.active = false; break; }
                const float f = (float) (v.pos - idx);
                const float g = v.gain * v.env * v.choke;
                L[i] += (sl[idx] + (sl[idx + 1] - sl[idx]) * f) * g * v.panL;
                if (R != nullptr) R[i] += (sr[idx] + (sr[idx + 1] - sr[idx]) * f) * g * v.panR;
                v.pos += v.rate;
                v.env *= v.decayMul;
                v.choke *= v.chokeMul;
                if (v.choke < 1.0e-3f) { v.active = false; break; }
            }
        }
    };

    int pos = 0;
    for (const auto meta : midi)
    {
        const int t = juce::jlimit (0, total, meta.samplePosition);
        if (t > pos) { renderVoices (pos, t); pos = t; }
        const auto m = meta.getMessage();
        if (m.isNoteOn())
        {
            const int piece = pieceForNote (m.getNoteNumber());
            if (piece >= 0) startVoice (piece, m.getFloatVelocity());
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            for (auto& v : voices) v.active = false;
    }
    if (pos < total) renderVoices (pos, total);
    int act = 0;
    for (auto& v : voices) if (v.active) ++act;
    activeVoices = act;

    // ---- the bus: tone, punch, drive, room, volume ----
    const float tone = param ("tone"), punch = param ("punch"), drive = param ("drive");
    const float a = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 1200.0f / (float) sampleRate);
    const float atk = 1.0f - std::exp (-1.0f / (0.003f * (float) sampleRate)), rel = 1.0f - std::exp (-1.0f / (0.12f * (float) sampleRate));
    const float thr = juce::Decibels::decibelsToGain (-20.0f), makeup = 1.0f + punch * 1.4f;
    const float dk = 1.0f + 3.0f * drive, dn = 1.0f / std::sqrt (dk);
    const int chans = juce::jmin (2, buffer.getNumChannels());
    for (int i = 0; i < total; ++i)
    {
        float x[2] = { L[i], R != nullptr ? R[i] : L[i] };
        for (int ch = 0; ch < chans; ++ch)
        {
            toneLp[ch] += a * (x[ch] - toneLp[ch]);
            x[ch] = toneLp[ch] + (x[ch] - toneLp[ch]) * (1.0f + tone);
        }
        const float lvl = juce::jmax (std::abs (x[0]), std::abs (x[1]));
        compEnv += (lvl > compEnv ? atk : rel) * (lvl - compEnv);
        const float gr = compEnv > thr ? std::pow (thr / compEnv, 0.75f) : 1.0f;
        for (int ch = 0; ch < chans; ++ch)
        {
            float y = x[ch] + (x[ch] * gr * makeup - x[ch]) * punch;
            if (drive > 0.001f) y = std::tanh (y * dk) * dn;
            (ch == 0 ? L : R)[i] = y;
        }
    }

    const float roomMix = param ("roomMix");
    if (roomReady.load() && roomMix > 0.001f && (int) param ("room") != (int) roomir::dry && R != nullptr)
    {
        if (wet.getNumSamples() < total) wet.setSize (2, total, false, false, true);
        for (int ch = 0; ch < 2; ++ch) wet.copyFrom (ch, 0, buffer, ch, 0, total);
        juce::dsp::AudioBlock<float> blk (wet.getArrayOfWritePointers(), 2, (size_t) total);
        convolution.process (juce::dsp::ProcessContextReplacing<float> (blk));
        for (int ch = 0; ch < 2; ++ch) buffer.addFrom (ch, 0, wet, ch, 0, total, roomMix * 0.8f);
    }

    const float vol = juce::Decibels::decibelsToGain (param ("volume"));
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        float* x = buffer.getWritePointer (ch);
        for (int i = 0; i < total; ++i)
        {
            const float y = x[i] * vol, ay = std::abs (y);
            x[i] = ay < 0.8f ? y : std::copysign (0.8f + 0.2f * std::tanh ((ay - 0.8f) * 5.0f), y);
        }
    }
}

} // namespace wis::daw
