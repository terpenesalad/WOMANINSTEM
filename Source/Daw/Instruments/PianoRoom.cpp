#include "PianoRoom.h"
#include "RoomIr.h"
#include "Separation/ModelManager.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <thread>

namespace wis::daw
{

using namespace prm;

// =====================================================================================================
//  Packs
// =====================================================================================================
int PianoPack::layerFor (int velocity) const
{
    for (int l = 0; l < (int) layers.size(); ++l)
        if (velocity <= layers[(size_t) l].second) return l;
    return juce::jmax (0, (int) layers.size() - 1);
}

const PianoPack::Sample* PianoPack::find (int layer, int note) const
{
    // the layer itself first, then the nearest layers (a few notes are missing in some layers)
    for (int d = 0; d < (int) byLayer.size(); ++d)
    {
        for (int l : { layer - d, layer + d })
        {
            if (! juce::isPositiveAndBelow (l, (int) byLayer.size()) || byLayer[(size_t) l].empty()) continue;
            const Sample* best = nullptr;
            int bestDist = 1000;
            for (int idx : byLayer[(size_t) l])
            {
                const auto& s = samples[(size_t) idx];
                const int dist = std::abs (s.key - note) * 2 + (s.key < note ? 1 : 0);   // tie: prefer pitching down
                if (dist < bestDist) { bestDist = dist; best = &s; }
            }
            if (best != nullptr && std::abs (best->key - note) <= 4) return best;
            if (d > 0 && best != nullptr) return best;
            if (d == 0 && best != nullptr && (int) byLayer.size() == 1) return best;
        }
    }
    return nullptr;
}

PianoPackCache& PianoPackCache::get()
{
    static PianoPackCache* instance = new PianoPackCache();   // intentionally never freed: the audio thread may hold packs until exit
    return *instance;
}

PianoPackCache::PianoPackCache()
{
    for (auto* id : { "grand", "steinway", "upright" })
        ids[numIds++] = id;
    std::thread ([this] { loaderLoop(); }).detach();
}

juce::File PianoPackCache::packFolder (const juce::String& id)
{
    auto exeDir = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
    const juce::File candidates[] = {
        exeDir.getChildFile ("pianos").getChildFile (id),
        exeDir.getParentDirectory().getChildFile ("pianos").getChildFile (id),
        exeDir.getParentDirectory().getParentDirectory().getChildFile ("pianos").getChildFile (id),
        ModelManager::appDataDirectory().getChildFile ("pianos").getChildFile (id),
        juce::File (juce::SystemStats::getEnvironmentVariable ("WIS_PIANOS", {})).getChildFile (id),
    };
    for (auto& f : candidates)
        if (f.getChildFile ("pack.json").existsAsFile())
            return f;
    return {};
}

PianoPackCache::Entry* PianoPackCache::entryFor (const juce::String& id)
{
    for (int i = 0; i < numIds.load(); ++i)
        if (ids[i] == id) return &entries[i];
    return nullptr;
}

const PianoPack* PianoPackCache::tryGet (const juce::String& id)
{
    auto* e = entryFor (id);
    if (e == nullptr) return nullptr;
    if (auto* p = e->pack.load()) return p;
    int idle = 0;
    e->state.compare_exchange_strong (idle, 1);
    return nullptr;
}

bool PianoPackCache::hasFailed (const juce::String& id)
{
    auto* e = entryFor (id);
    return e == nullptr || e->state.load() == 4;
}

const PianoPack* PianoPackCache::getBlocking (const juce::String& id)
{
    auto* e = entryFor (id);
    if (e == nullptr) return nullptr;
    if (auto* p = e->pack.load()) return p;
    const std::lock_guard<std::mutex> lock (loadMutex);
    if (auto* p = e->pack.load()) return p;
    if (e->state.load() == 4) return nullptr;
    auto loaded = load (id);
    e->state = loaded != nullptr ? 3 : 4;
    e->pack = loaded.release();
    return e->pack.load();
}

void PianoPackCache::loaderLoop()
{
    for (;;)
    {
        for (int i = 0; i < numIds.load(); ++i)
        {
            auto& e = entries[i];
            int wanted = 1;
            if (! e.state.compare_exchange_strong (wanted, 2)) continue;
            const std::lock_guard<std::mutex> lock (loadMutex);
            if (e.pack.load() != nullptr) { e.state = 3; continue; }
            auto loaded = load (ids[i]);
            const bool ok = loaded != nullptr;
            e.pack = loaded.release();
            e.state = ok ? 3 : 4;
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (40));
    }
}

std::unique_ptr<PianoPack> PianoPackCache::load (const juce::String& id)
{
    const auto folder = packFolder (id);
    if (! folder.exists()) return nullptr;
    const auto json = juce::JSON::parse (folder.getChildFile ("pack.json"));
    if (! json.isObject()) return nullptr;

    auto pack = std::make_unique<PianoPack>();
    pack->id = id;
    pack->name = json["name"].toString();
    pack->credit = json["credit"].toString();
    if (auto* layers = json["layers"].getArray())
        for (auto& l : *layers) pack->layers.push_back ({ (int) l["lo"], (int) l["hi"] });
    if (pack->layers.empty()) return nullptr;

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::vector<double> layerSum (pack->layers.size(), 0.0);
    std::vector<int> layerCount (pack->layers.size(), 0);

    if (auto* samples = json["samples"].getArray())
    {
        for (auto& s : *samples)
        {
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (folder.getChildFile (s["file"].toString())));
            if (reader == nullptr) continue;
            const int frames = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) 48000 * 20);
            juce::AudioBuffer<float> buf (2, frames);
            reader->read (&buf, 0, frames, 0, true, reader->numChannels > 1);
            if (reader->numChannels == 1) buf.copyFrom (1, 0, buf, 0, 0, frames);

            PianoPack::Sample smp;
            smp.key = (int) s["key"];
            smp.layer = juce::jlimit (0, (int) pack->layers.size() - 1, (int) s["layer"]);
            smp.rate = reader->sampleRate;
            smp.frames = frames;
            smp.data.resize ((size_t) frames * 2);
            for (int i = 0; i < frames; ++i)
                for (int c = 0; c < 2; ++c)
                    smp.data[(size_t) (i * 2 + c)] = (juce::int16) juce::jlimit (-32767, 32767, (int) std::lround (buf.getSample (c, i) * 32767.0f));

            // loudness of the attack (first 150 ms) for the velocity curve, from the middle of the keyboard
            const int attackN = juce::jmin (frames, (int) (0.15 * smp.rate));
            if (smp.key >= 45 && smp.key <= 80 && attackN > 0)
            {
                const float rms = 0.5f * (buf.getRMSLevel (0, 0, attackN) + buf.getRMSLevel (1, 0, attackN));
                layerSum[(size_t) smp.layer] += juce::Decibels::gainToDecibels (rms, -100.0f);
                layerCount[(size_t) smp.layer]++;
            }
            pack->samples.push_back (std::move (smp));
        }
    }
    if (pack->samples.empty()) return nullptr;

    pack->byLayer.assign (pack->layers.size(), {});
    for (int i = 0; i < (int) pack->samples.size(); ++i)
        pack->byLayer[(size_t) pack->samples[(size_t) i].layer].push_back (i);

    // loudness of each layer; missing ones interpolated, then forced to rise with velocity
    pack->layerDb.assign (pack->layers.size(), -30.0f);
    for (size_t l = 0; l < pack->layers.size(); ++l)
        if (layerCount[l] > 0) pack->layerDb[l] = (float) (layerSum[l] / layerCount[l]);
    for (size_t l = 1; l < pack->layerDb.size(); ++l)
        pack->layerDb[l] = juce::jmax (pack->layerDb[l], pack->layerDb[l - 1] + 0.5f);

    // every pack plays a mezzo-forte (velocity 80) at about -20 dBFS RMS
    const int mf = pack->layerFor (80);
    pack->packGain = juce::Decibels::decibelsToGain (-20.0f - pack->layerDb[(size_t) mf]);
    return pack;
}

// =====================================================================================================
//  Parameters & presets
// =====================================================================================================
juce::StringArray PianoRoom::modelNames() { return { "Concert Grand", "Vintage Grand", "Upright", "Modelled Grand", "Toy Piano" }; }
juce::StringArray PianoRoom::micNames()   { return { "Close (inside the lid)", "Player", "Audience", "Back of the room" }; }

static const char* packIdFor (int model)
{
    switch (model) { case 0: return "grand"; case 1: return "steinway"; case 2: return "upright"; default: return ""; }
}

static Layout pianoLayout()
{
    Layout l;
    addChoice  (l, "model", "Piano", PianoRoom::modelNames(), 0);
    addDb      (l, "volume", "Volume", -24.0f, 12.0f, 0.0f);
    addPercent (l, "dynamics", "Dynamics", 0.5f);
    addFloat   (l, "hammer", "Hammers", -1.0f, 1.0f, 0.0f, "", 0.0f, 2);
    addPercent (l, "felt", "Felt", 0.0f);
    addPercent (l, "lid", "Lid", 0.8f);
    addFloat   (l, "tune", "Tuning (A4)", 415.0f, 466.0f, 440.0f, "Hz", 0.0f, 1);
    addPercent (l, "stretch", "Stretch Tuning", 0.5f);
    addFloat   (l, "detune", "Honky-Tonk", 0.0f, 40.0f, 0.0f, "ct", 0.0f, 1);
    addPercent (l, "tack", "Tacks", 0.0f);
    addPercent (l, "age", "Age", 0.0f);
    addPercent (l, "mechanics", "Key Noise", 0.3f);
    addPercent (l, "resonance", "String Resonance", 0.35f);
    addFloat   (l, "release", "Damper Release", 0.05f, 3.0f, 0.35f, "s", 0.5f, 2);
    addBool    (l, "pedal", "Sustain Pedal (latched)", false);

    addDb      (l, "bass", "Bass", -12.0f, 12.0f, 0.0f);
    addDb      (l, "mid", "Body", -12.0f, 12.0f, 0.0f);
    addDb      (l, "treble", "Treble", -12.0f, 12.0f, 0.0f);
    addChoice  (l, "mic", "Mics", PianoRoom::micNames(), 1);
    addFloat   (l, "width", "Width", 0.0f, 1.5f, 1.0f, "", 0.0f, 2);
    addPercent (l, "drive", "Drive", 0.0f);
    addPercent (l, "comp", "Compressor", 0.0f);
    addPercent (l, "tape", "Tape", 0.0f);
    addPercent (l, "lofi", "Lo-Fi", 0.0f);

    addChoice  (l, "space", "Space", roomir::spaceNames(), (int) roomir::bigLiveRoom);
    addPercent (l, "spaceMix", "Space Amount", 0.2f);
    addFloat   (l, "size", "Space Size", 0.5f, 2.0f, 1.0f, "x", 1.0f, 2);
    addPercent (l, "spaceTone", "Space Tone", 0.5f);
    addPercent (l, "distance", "Distance", 0.3f);
    addMs      (l, "predelay", "Pre-Delay", 0.0f, 150.0f, 8.0f);
    return l;
}

namespace
{
    struct PianoPreset
    {
        const char* name;
        const char* description;
        std::vector<std::pair<const char*, float>> values;
    };

    const std::vector<PianoPreset>& pianoPresets()
    {
        using S = roomir::Space;
        static const std::vector<PianoPreset> p {
            { "Concert Grand", "A clean concert grand in a big studio live room", { } },
            { "Intimate Ballad Grand", "Soft hammers, close and warm in a wooden studio: quiet late-night ballads (in the spirit of Nick Cave's 'The Boatman's Call')",
              { { "model", 1 }, { "hammer", -0.35f }, { "space", S::woodenStudio }, { "spaceMix", 0.22f }, { "comp", 0.25f }, { "bass", 1.5f }, { "treble", -1.5f }, { "mechanics", 0.45f }, { "distance", 0.15f } } },
            { "Murder Ballad Grand", "Heavy, dark and close to the low strings, in a big stone church (Nick Cave & the Bad Seeds territory)",
              { { "model", 1 }, { "hammer", 0.25f }, { "bass", 3.0f }, { "mid", -1.0f }, { "treble", -2.5f }, { "drive", 0.15f }, { "age", 0.1f }, { "space", S::church }, { "spaceMix", 0.35f }, { "distance", 0.4f }, { "resonance", 0.6f } } },
            { "Rain Dog Upright", "A battered, out-of-tune bar upright with clacking keys, through a tired mic (Tom Waits-style)",
              { { "model", 2 }, { "age", 0.55f }, { "tack", 0.25f }, { "detune", 8.0f }, { "mechanics", 0.85f }, { "space", S::barClub }, { "spaceMix", 0.25f }, { "lofi", 0.25f }, { "tape", 0.45f }, { "drive", 0.2f }, { "mid", 3.0f }, { "bass", -3.0f }, { "treble", -2.0f }, { "width", 0.45f }, { "mic", 0 } } },
            { "Junkyard Parlour", "Forty years in a damp parlour: wildly out of tune, rattling, megaphone-thin (more Tom Waits)",
              { { "model", 2 }, { "age", 0.9f }, { "detune", 18.0f }, { "tack", 0.4f }, { "mechanics", 1.0f }, { "lofi", 0.6f }, { "tape", 0.6f }, { "space", S::livingRoom }, { "spaceMix", 0.3f }, { "width", 0.25f }, { "mid", 4.0f } } },
            { "Bohemian Rock Grand", "A bright, hard-hammered grand, close-miked, compressed and wide: 70s rock opera (Queen-style)",
              { { "model", 0 }, { "hammer", 0.6f }, { "lid", 1.0f }, { "comp", 0.55f }, { "treble", 3.0f }, { "mid", 1.0f }, { "width", 1.25f }, { "mic", 0 }, { "space", S::bigLiveRoom }, { "spaceMix", 0.15f }, { "drive", 0.1f }, { "stretch", 0.7f } } },
            { "Stadium Rock Grand", "Brighter and bigger still, for anthems that fill a hall",
              { { "model", 0 }, { "hammer", 0.8f }, { "lid", 1.0f }, { "comp", 0.7f }, { "treble", 4.0f }, { "drive", 0.2f }, { "width", 1.4f }, { "space", S::concertHall }, { "spaceMix", 0.25f } } },
            { "Swedish Psych Upright", "A warm 70s upright on saturated tape with a spring reverb: Scandinavian psych rock (Dungen-style)",
              { { "model", 2 }, { "age", 0.35f }, { "tape", 0.7f }, { "drive", 0.3f }, { "comp", 0.4f }, { "space", S::spring }, { "spaceMix", 0.3f }, { "mid", 2.0f }, { "treble", -3.0f }, { "width", 0.7f }, { "tack", 0.1f }, { "detune", 4.0f } } },
            { "Forest Cabin Psych", "The same upright carried out of the cabin into the trees",
              { { "model", 2 }, { "tape", 0.5f }, { "age", 0.25f }, { "felt", 0.15f }, { "space", S::forest }, { "spaceMix", 0.45f }, { "distance", 0.45f } } },
            { "Honky-Tonk Saloon", "Two strings per note, deliberately apart: the saloon sound",
              { { "model", 2 }, { "detune", 22.0f }, { "tack", 0.3f }, { "space", S::barClub }, { "spaceMix", 0.2f }, { "treble", 2.0f }, { "mechanics", 0.6f } } },
            { "Tack Piano", "Thumbtacks in the hammers: bright, metallic, ragtime and 60s pop sessions",
              { { "model", 2 }, { "tack", 0.85f }, { "detune", 6.0f }, { "space", S::woodenStudio }, { "spaceMix", 0.15f }, { "treble", 3.0f } } },
            { "Felt Piano (late night)", "Felt between the hammers and strings: soft, muffled and intimate, with every mechanical sound",
              { { "model", 1 }, { "felt", 0.8f }, { "hammer", -0.5f }, { "mechanics", 0.75f }, { "space", S::livingRoom }, { "spaceMix", 0.25f }, { "comp", 0.2f }, { "tape", 0.2f }, { "release", 0.6f } } },
            { "Forest Clearing Grand", "A grand piano outdoors in a forest clearing: the trees answer, nothing else does",
              { { "model", 0 }, { "space", S::forest }, { "spaceMix", 0.5f }, { "distance", 0.4f } } },
            { "Cathedral Grand", "Endless stone: every chord hangs in the air",
              { { "model", 1 }, { "space", S::cathedral }, { "spaceMix", 0.45f }, { "distance", 0.5f }, { "treble", -1.0f } } },
            { "Canyon Echo", "Played at the edge of a canyon: the far walls answer back",
              { { "model", 0 }, { "space", S::canyon }, { "spaceMix", 0.4f }, { "distance", 0.3f } } },
            { "Car Park Piano", "Concrete, flutter and a long grey tail",
              { { "model", 2 }, { "space", S::carPark }, { "spaceMix", 0.45f }, { "distance", 0.45f }, { "age", 0.2f } } },
            { "Bathroom Upright", "Tiles all round: short, bright and ringing",
              { { "model", 2 }, { "space", S::bathroom }, { "spaceMix", 0.35f }, { "distance", 0.3f } } },
            { "Lo-Fi Cassette Keys", "Bounced to a worn cassette and back: wobbly, dusty and warm",
              { { "model", 2 }, { "lofi", 0.45f }, { "tape", 0.9f }, { "age", 0.3f }, { "comp", 0.5f }, { "space", S::livingRoom }, { "spaceMix", 0.2f } } },
            { "Modelled Grand", "A grand piano built from physics (no samples): inharmonic strings, hammer and soundboard",
              { { "model", 3 }, { "space", S::bigLiveRoom }, { "spaceMix", 0.2f } } },
            { "Toy Piano", "Metal rods struck by little hammers: music boxes and nursery rhymes gone strange",
              { { "model", 4 }, { "space", S::livingRoom }, { "spaceMix", 0.2f }, { "mechanics", 0.5f } } },
            { "Dry Close Grand", "Mics inside the lid, no room at all: for mixing yourself",
              { { "model", 0 }, { "mic", 0 }, { "space", S::dry }, { "spaceMix", 0.0f } } },
        };
        return p;
    }
}

juce::StringArray PianoRoom::presetNames()
{
    juce::StringArray n;
    for (auto& p : pianoPresets()) n.add (p.name);
    return n;
}

juce::String PianoRoom::presetDescription (int i)
{
    return juce::isPositiveAndBelow (i, (int) pianoPresets().size()) ? juce::String (pianoPresets()[(size_t) i].description) : juce::String();
}

void PianoRoom::loadProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) pianoPresets().size())) return;
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            rp->setValueNotifyingHost (rp->getDefaultValue());
    for (auto& [id, v] : pianoPresets()[(size_t) index].values)
        setParam (id, v);
}

// =====================================================================================================
//  Voices
// =====================================================================================================
struct PianoRoom::Voice
{
    static constexpr int maxPartials = 24;
    bool active = false, keyDown = false, sustained = false, releasing = false, fading = false, damped = true, modelled = false;
    int note = 60;
    float velocity = 0.0f;
    juce::uint32 order = 0;

    // sampled
    const PianoPack::Sample* smp = nullptr;
    double pos = 0.0, pos2 = 0.0, baseInc = 1.0, baseInc2 = 1.0;
    float g2 = 0.0f;

    // modelled
    int partials = 0;
    float re[maxPartials] {}, im[maxPartials] {}, cr[maxPartials] {}, ci[maxPartials] {};
    float a1[maxPartials] {}, a2[maxPartials] {}, d1[maxPartials] {}, d2[maxPartials] {};
    float panL = 1.0f, panR = 1.0f;

    float gain = 1.0f, env = 1.0f, relMul = 1.0f, decayMul = 1.0f;
    float attack = 1.0f, attackInc = 1.0f;
    float lpA = 1.0f, lpz[2] {};

    float level() const noexcept { return env * gain * velocity; }
};

struct PianoRoom::Resonator
{
    std::vector<float> buf;
    int pos = 0;
    float delay = 100.0f, g = 0.0f, lp = 0.0f, panL = 1.0f, panR = 1.0f;
    float process (float in) noexcept
    {
        const int n = (int) buf.size();
        float rp = (float) pos - delay;
        while (rp < 0.0f) rp += (float) n;
        const int i0 = (int) rp;
        const float fr = rp - (float) i0;
        const float y = buf[(size_t) i0] + fr * (buf[(size_t) ((i0 + 1) % n)] - buf[(size_t) i0]);
        lp += 0.35f * (y - lp);                      // string losses: the highs die first
        buf[(size_t) pos] = in + g * lp;
        pos = (pos + 1) % n;
        return y;
    }
};

std::function<juce::AudioProcessorEditor* (PianoRoom&)> PianoRoom::editorFactory;

PianoRoom::PianoRoom() : BuiltinProcessor ("piano", "Piano Room", true, pianoLayout())
{
    for (int i = 0; i < maxVoices; ++i) voices.push_back (std::make_unique<Voice>());
    loadProgram (0);
    PianoPackCache::get().tryGet (packIdFor ((int) param ("model")));   // start loading the default piano now
    startTimerHz (8);
}

PianoRoom::~PianoRoom()
{
    stopTimer();
}

const PianoPack* PianoRoom::currentPack()
{
    const char* id = packIdFor ((int) param ("model"));
    if (*id == 0) return nullptr;
    auto& cache = PianoPackCache::get();
    if (isNonRealtime()) return cache.getBlocking (id);
    return cache.tryGet (id);
}

juce::String PianoRoom::getStatus() const
{
    const int model = (int) param ("model");
    const char* id = packIdFor (model);
    if (*id == 0) return modelNames()[model];
    auto& cache = PianoPackCache::get();
    if (! PianoPackCache::isInstalled (id) || cache.hasFailed (id))
        return modelNames()[model] + ": samples not found, playing the modelled grand (reinstall to get them back)";
    if (cache.tryGet (id) == nullptr) return "Loading the " + modelNames()[model] + "...";
    return modelNames()[model];
}

void PianoRoom::waitUntilReady()
{
    const char* id = packIdFor ((int) param ("model"));
    if (*id != 0) PianoPackCache::get().getBlocking (id);
    rebuildRoomIfNeeded (true);
    // let the convolution engine's background loader install the new impulse response
    for (int i = 0; i < 100 && ! roomReady.load(); ++i) juce::Thread::sleep (10);
    juce::Thread::sleep (100);
}

void PianoRoom::prepareToPlay (double sr, int block)
{
    sampleRate = sr;
    maxBlock = juce::jmax (16, block);
    for (auto& v : voices) v->active = false;
    for (auto& n : noises) n = {};

    // sympathetic strings: two octaves of combs from A1 up
    resonators.clear();
    for (int k = 0; k < 24; ++k)
    {
        auto r = std::make_unique<Resonator>();
        const double f = 440.0 * std::pow (2.0, (33 + k - 69) / 12.0);
        r->delay = (float) (sr / f);
        r->buf.assign ((size_t) (r->delay + 4), 0.0f);
        r->panL = (k & 1) ? 0.6f : 1.0f;
        r->panR = (k & 1) ? 1.0f : 0.6f;
        resonators.push_back (std::move (r));
    }

    for (auto* arr : { &lowShelf, &midPeak, &highShelf, &presence })
        for (auto& f : *arr) { f.coefficients = new juce::dsp::IIR::Coefficients<float> (1, 0, 1, 0); f.reset(); }
    std::fill (std::begin (lastTone), std::end (lastTone), -99.0f);
    for (auto& t : tapeBuf) t.assign ((size_t) (0.05 * sr) + 8, 0.0f);
    tapePos = 0;

    wet.setSize (2, maxBlock);
    dryCopy.setSize (2, maxBlock);
    convolution.prepare ({ sr, (juce::uint32) maxBlock, 2 });
    std::fill (std::begin (roomKey), std::end (roomKey), -1);
    rebuildRoomIfNeeded (true);
}

void PianoRoom::timerCallback()
{
    rebuildRoomIfNeeded (false);
    PianoPackCache::get().tryGet (packIdFor ((int) param ("model")));   // keep the selected piano loading
}

void PianoRoom::rebuildRoomIfNeeded (bool force)
{
    const int key[4] = { (int) param ("space"), juce::roundToInt (param ("size") * 100.0f), juce::roundToInt (param ("spaceTone") * 100.0f),
                         juce::roundToInt (param ("predelay")) };
    if (! force && std::equal (std::begin (key), std::end (key), std::begin (roomKey))) return;
    std::copy (std::begin (key), std::end (key), std::begin (roomKey));
    auto ir = roomir::design (key[0], sampleRate, (float) key[1] / 100.0f, (float) key[2] / 100.0f, (float) key[3]);
    convolution.loadImpulseResponse (std::move (ir), sampleRate, juce::dsp::Convolution::Stereo::yes,
                                     juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
    roomReady = true;
}

void PianoRoom::addNoise (float gain, float colourHz, float seconds)
{
    if (gain <= 1.0e-5f) return;
    for (auto& n : noises)
    {
        if (n.left > 0) continue;
        n.total = n.left = juce::jmax (1, (int) (seconds * sampleRate));
        n.gain = gain;
        n.colour = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * colourHz / sampleRate));
        n.z1 = n.z2 = 0.0f;
        return;
    }
}

static float lerpFc (float a, float b, float t) { return a + (b - a) * t; }

/** A real piano spans ~35 dB from pianissimo to fortissimo; 0 dB at velocity 80. */
static float velocityDb (float vel) { return 34.0f * std::log10 (juce::jmax (0.02f, vel) / 0.63f); }

static float hashNote (int note, int salt)
{
    juce::uint32 h = (juce::uint32) (note * 2654435761u) ^ (juce::uint32) (salt * 40503u);
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return (float) (h & 0xffff) / 32767.5f - 1.0f;   // -1 .. 1, the same every time for this key
}

void PianoRoom::startNote (int note, float vel, int)
{
    const int model = (int) param ("model");
    const auto* pack = currentPack();
    const bool useSamples = pack != nullptr && model <= 2;

    // a key that's still ringing is struck again: the old sound gets out of the way quickly
    for (auto& v : voices)
        if (v->active && v->note == note && ! v->fading) { v->fading = true; v->relMul = (float) std::exp (-6.9 / (0.06 * sampleRate)); v->releasing = true; }

    Voice* voice = nullptr;
    for (auto& v : voices) if (! v->active) { voice = v.get(); break; }
    if (voice == nullptr)
    {
        // steal the quietest
        float quietest = 1.0e9f;
        for (auto& v : voices)
            if (v->level() < quietest) { quietest = v->level(); voice = v.get(); }
    }
    auto& v = *voice;
    v = Voice();
    v.active = true;
    v.keyDown = true;
    v.note = note;
    v.velocity = vel;
    v.order = ++noteCounter;
    v.damped = note < 89;   // the top keys have no dampers

    const float hammer = param ("hammer"), felt = juce::jlimit (0.0f, 1.0f, param ("felt") + (softPedal ? 0.3f : 0.0f));
    const int vel127 = juce::jlimit (1, 127, juce::roundToInt (vel * 127.0f));
    const float age = param ("age"), tack = param ("tack"), detune = param ("detune");

    // tuning: A4 reference, stretch (low notes flat, high notes sharp, like a real tuner does), age
    const float stretchCents = param ("stretch") * 2.0f * (note > 60 ? 0.0085f * (float) ((note - 60) * (note - 60)) : -0.012f * (float) ((60 - note) * (60 - note)));
    const float cents = 1200.0f * std::log2 (param ("tune") / 440.0f) + stretchCents + age * 22.0f * hashNote (note, 1);

    // a second string slightly apart: honky-tonk detune, tacks, and age (unisons drifting apart)
    const float cents2 = juce::jmax (detune, tack * 9.0f, age * 7.0f) * (hashNote (note, 2) > -0.3f ? 1.0f : -1.0f);
    v.g2 = juce::jmax (detune > 0.5f ? 0.85f : 0.0f, tack * 0.6f, age * 0.35f);

    // dynamics: the velocity curve (and Dynamics widens or narrows it)
    const float dynDb = (param ("dynamics") - 0.5f) * 24.0f * (vel - 0.75f);
    float gainDb = dynDb + felt * 4.0f - (softPedal ? 3.0f : 0.0f);

    if (useSamples)
    {
        // harder hammers pick brighter layers; the loudness still follows how hard you played
        const int layerVel = juce::jlimit (1, 127, vel127 + juce::roundToInt (hammer * 28.0f));
        const int layer = pack->layerFor (layerVel);
        v.smp = pack->find (layer, note);
        if (v.smp == nullptr) { v.active = false; return; }
        // every layer is evened out to the mezzo-forte layer's level, then the velocity curve sets the loudness
        // (some sample sets are normalised per layer, so their own levels can't be trusted for dynamics)
        const int mf = pack->layerFor (80);
        gainDb += velocityDb (vel) - (pack->layerDb[(size_t) v.smp->layer] - pack->layerDb[(size_t) mf]);
        v.gain = pack->packGain * juce::Decibels::decibelsToGain (gainDb) / 32767.0f;
        const double semis = (double) (note - v.smp->key) + cents / 100.0;
        v.baseInc = std::pow (2.0, semis / 12.0) * v.smp->rate / sampleRate;
        v.baseInc2 = v.baseInc * std::pow (2.0, cents2 / 1200.0);
        v.pos = v.pos2 = 0.0;
    }
    else
    {
        // ---- modelled: inharmonic partials with two decay stages, two strings (or a toy piano's metal rods) ----
        v.modelled = true;
        const bool toy = model == 4;
        const double f0 = 440.0 * std::pow (2.0, (note - 69 + cents / 100.0 + (toy ? 12.0 : 0.0)) / 12.0);
        const float bright = juce::jlimit (0.2f, 1.6f, 0.55f + 0.9f * vel + 0.4f * hammer);
        const double B = 0.00008 * std::pow (2.0, (note - 21) / 16.0);
        const float tau = (float) (toy ? 1.4 * std::pow (2.0, -(note - 60) / 24.0) : 9.0 * std::pow (2.0, -(note - 21) / 17.0) + 0.4);
        static const double rod[] = { 1.0, 2.756, 5.404, 8.933, 13.34, 18.64 };
        const int perString = toy ? 6 : 11, strings = toy ? 1 : 2;
        int k = 0;
        float norm = 0.0f;
        for (int s = 0; s < strings; ++s)
        {
            const double det = s == 0 ? 1.0 : std::pow (2.0, (1.2 + std::abs (cents2) * 0.5 + age * 6.0) / 1200.0);
            for (int n = 1; n <= perString && k < Voice::maxPartials; ++n)
            {
                const double fn = toy ? f0 * rod[n - 1] : f0 * n * std::sqrt (1.0 + B * n * n);
                const double f = fn * det;
                if (f > sampleRate * 0.45) break;
                float amp = std::pow (1.0f / (float) n, 2.3f - 1.2f * bright);
                float ampRef = std::pow (1.0f / (float) n, 2.3f - 1.2f * 1.0f);
                if (toy) { amp = n == 1 ? 1.0f : 0.35f / (float) (n * n); ampRef = amp; }
                else
                {
                    const float strike = std::abs (std::sin (juce::MathConstants<float>::pi * (float) n / 7.6f)) + 0.08f;   // struck at ~1/8 of the string
                    amp *= strike;
                    ampRef *= strike;
                }
                const double w = juce::MathConstants<double>::twoPi * f / sampleRate;
                v.re[k] = 1.0f; v.im[k] = 0.0f;
                const float ph = juce::MathConstants<float>::twoPi * (float) random.nextFloat();
                v.re[k] = std::cos (ph); v.im[k] = std::sin (ph);
                v.cr[k] = (float) std::cos (w); v.ci[k] = (float) std::sin (w);
                const float tauN = tau / (1.0f + 0.22f * (float) (n - 1) * (toy ? 2.0f : 1.0f));
                v.a1[k] = amp * 0.7f; v.a2[k] = amp * 0.3f;
                v.d1[k] = (float) std::exp (-1.0 / (tauN * 0.12 * sampleRate));     // prompt sound
                v.d2[k] = (float) std::exp (-1.0 / (tauN * sampleRate));            // aftersound
                norm += ampRef;
                ++k;
            }
        }
        v.partials = k;
        const float p = juce::jlimit (-0.6f, 0.6f, (float) (note - 64) / 70.0f);
        v.panL = std::sqrt (0.5f * (1.0f - p)) * 1.41f;
        v.panR = std::sqrt (0.5f * (1.0f + p)) * 1.41f;
        v.gain = juce::Decibels::decibelsToGain (gainDb + velocityDb (vel) - 12.0f) / juce::jmax (0.5f, norm * 0.5f) * (toy ? 0.7f : 1.0f);
        addNoise (vel * 0.03f * (1.0f + hammer), 2500.0f, 0.004f);   // the hammer's knock
    }

    // hammers / felt: how much top end the strike has
    float fc = 20000.0f;
    if (hammer < 0.0f) fc = juce::jmin (fc, 20000.0f * std::pow (2.0f, hammer * 3.0f * (1.2f - vel)));
    if (felt > 0.0f)
    {
        const float f0 = 440.0f * std::pow (2.0f, (float) (note - 69) / 12.0f);
        fc = juce::jmin (fc, lerpFc (20000.0f, 500.0f + 3.0f * f0 + 2000.0f * vel, felt));
    }
    v.lpA = fc >= 19000.0f ? 1.0f : (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * fc / sampleRate));
    const float attackMs = felt * 10.0f;
    v.attack = attackMs > 0.2f ? 0.0f : 1.0f;
    v.attackInc = attackMs > 0.2f ? (float) (1.0 / (attackMs * 0.001 * sampleRate)) : 1.0f;
    v.decayMul = (float) std::exp (-(tack * 0.9 + age * 0.15) / sampleRate);   // tacks and old strings die sooner

    // the mechanism: key and hammer thump, tacks' click
    const float mech = param ("mechanics");
    addNoise (mech * vel * 0.02f, 220.0f, 0.03f);
    addNoise (tack * vel * 0.06f, 5000.0f, 0.005f);
}

void PianoRoom::stopNote (int note)
{
    for (auto& v : voices)
    {
        if (! v->active || v->note != note || ! v->keyDown) continue;
        v->keyDown = false;
        if (pedalDown) { v->sustained = true; continue; }
        if (v->damped && ! v->releasing)
        {
            v->releasing = true;
            const float rel = param ("release") * (1.6f - (float) (note - 21) / 87.0f);
            v->relMul = (float) std::exp (-6.9 / (juce::jmax (0.02f, rel) * sampleRate));
            addNoise (param ("mechanics") * 0.008f * (0.3f + v->velocity), 700.0f, 0.05f);   // the damper landing
        }
    }
}

void PianoRoom::setPedal (bool down)
{
    if (down == pedalDown) return;
    pedalDown = down;
    addNoise (param ("mechanics") * 0.035f, 120.0f, 0.08f);
    if (! down)
        for (auto& v : voices)
            if (v->active && v->sustained && ! v->keyDown)
            {
                v->sustained = false;
                if (v->damped && ! v->releasing)
                {
                    v->releasing = true;
                    const float rel = param ("release") * (1.6f - (float) (v->note - 21) / 87.0f);
                    v->relMul = (float) std::exp (-6.9 / (juce::jmax (0.02f, rel) * sampleRate));
                }
            }
}

void PianoRoom::renderVoices (float* L, float* R, int n)
{
    // slow wobble (age, tape) and the per-note tuning are applied as a speed factor
    const float age = param ("age");
    const double wob = std::pow (2.0, (age * 5.0 * std::sin (wobblePhase)) / 1200.0);
    wobblePhase += juce::MathConstants<double>::twoPi * 0.37 * n / sampleRate;
    if (wobblePhase > 1000.0) wobblePhase -= juce::MathConstants<double>::twoPi * 100.0;

    int active = 0;
    for (auto& vp : voices)
    {
        auto& v = *vp;
        if (! v.active) continue;
        ++active;

        if (v.modelled)
        {
            for (int i = 0; i < n; ++i)
            {
                float s = 0.0f;
                for (int k = 0; k < v.partials; ++k)
                {
                    const float r = v.re[k] * v.cr[k] - v.im[k] * v.ci[k];
                    v.im[k] = v.re[k] * v.ci[k] + v.im[k] * v.cr[k];
                    v.re[k] = r;
                    s += v.im[k] * (v.a1[k] + v.a2[k]);
                    v.a1[k] *= v.d1[k];
                    v.a2[k] *= v.d2[k];
                }
                if (v.attack < 1.0f) v.attack = juce::jmin (1.0f, v.attack + v.attackInc);
                v.env *= v.releasing ? v.relMul : v.decayMul;
                const float x = s * v.gain * v.env * v.attack;
                v.lpz[0] += v.lpA * (x - v.lpz[0]);
                L[i] += v.lpz[0] * v.panL;
                R[i] += v.lpz[0] * v.panR;
            }
            // keep the oscillators on the unit circle
            for (int k = 0; k < v.partials; ++k)
            {
                const float m = 1.0f / std::sqrt (v.re[k] * v.re[k] + v.im[k] * v.im[k] + 1.0e-20f);
                v.re[k] *= m; v.im[k] *= m;
            }
            float total = 0.0f;
            for (int k = 0; k < v.partials; ++k) total += v.a1[k] + v.a2[k];
            if (total * v.env < 1.0e-4f) v.active = false;
            continue;
        }

        const auto& smp = *v.smp;
        const juce::int16* d = smp.data.data();
        const int frames = smp.frames;
        const double inc = v.baseInc * wob, inc2 = v.baseInc2 * wob;
        auto read = [d, frames] (double p, int c) noexcept
        {
            // cubic (Hermite) interpolation between the int16 frames
            const int i = (int) p;
            const float t = (float) (p - (double) i);
            const float xm1 = (float) d[(size_t) (juce::jmax (0, i - 1) * 2 + c)];
            const float x0 = (float) d[(size_t) (i * 2 + c)];
            const float x1 = (float) d[(size_t) (juce::jmin (frames - 1, i + 1) * 2 + c)];
            const float x2 = (float) d[(size_t) (juce::jmin (frames - 1, i + 2) * 2 + c)];
            const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            return ((c3 * t + c2) * t + c1) * t + x0;
        };
        const bool second = v.g2 > 0.001f;
        for (int i = 0; i < n; ++i)
        {
            if (v.pos >= frames - 2 || (second && v.pos2 >= frames - 2)) { v.active = false; break; }
            float l = read (v.pos, 0), r = read (v.pos, 1);
            if (second)
            {
                l = (l + v.g2 * read (v.pos2, 0)) * (1.0f / (1.0f + 0.6f * v.g2));
                r = (r + v.g2 * read (v.pos2, 1)) * (1.0f / (1.0f + 0.6f * v.g2));
                v.pos2 += inc2;
            }
            v.pos += inc;
            if (v.attack < 1.0f) v.attack = juce::jmin (1.0f, v.attack + v.attackInc);
            v.env *= v.releasing ? v.relMul : v.decayMul;
            const float g = v.gain * v.env * v.attack;
            if (v.lpA < 1.0f)
            {
                v.lpz[0] += v.lpA * (l * g - v.lpz[0]);
                v.lpz[1] += v.lpA * (r * g - v.lpz[1]);
                L[i] += v.lpz[0]; R[i] += v.lpz[1];
            }
            else
            {
                L[i] += l * g; R[i] += r * g;
            }
        }
        if (v.env < 1.0e-4f) v.active = false;
    }
    activeVoices = active;
}

// =====================================================================================================
//  Processing
// =====================================================================================================
void PianoRoom::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals nd;
    const int total = buffer.getNumSamples();
    buffer.clear();
    if (buffer.getNumChannels() < 2 || total == 0) return;
    if (isNonRealtime()) rebuildRoomIfNeeded (false);

    const bool latched = param ("pedal") > 0.5f;
    if (latched != lastLatched) { lastLatched = latched; setPedal (latched); }

    // ---- MIDI + voices, split at each event ----
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);
    int pos = 0;
    auto renderTo = [&] (int end)
    {
        for (int at = pos; at < end; at += maxBlock)
            renderVoices (L + at, R + at, juce::jmin (maxBlock, end - at));
        pos = end;
    };
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        renderTo (juce::jlimit (pos, total, meta.samplePosition));
        if (m.isNoteOn()) startNote (m.getNoteNumber(), m.getFloatVelocity(), meta.samplePosition);
        else if (m.isNoteOff()) stopNote (m.getNoteNumber());
        else if (m.isSustainPedalOn()) setPedal (true);
        else if (m.isSustainPedalOff() && ! latched) setPedal (false);
        else if (m.isSoftPedalOn()) softPedal = true;
        else if (m.isSoftPedalOff()) softPedal = false;
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            for (auto& v : voices) if (v->active) { v->keyDown = false; v->sustained = false; v->releasing = true; v->relMul = (float) std::exp (-6.9 / (0.1 * sampleRate)); }
            if (! latched) setPedal (false);
        }
    }
    renderTo (total);

    // ---- mechanical noises ----
    for (auto& nz : noises)
    {
        if (nz.left <= 0) continue;
        for (int i = 0; i < total && nz.left > 0; ++i, --nz.left)
        {
            const float e = (float) nz.left / (float) nz.total;
            const float w = random.nextFloat() * 2.0f - 1.0f;
            nz.z1 += nz.colour * (w - nz.z1);
            nz.z2 += nz.colour * (nz.z1 - nz.z2);
            const float x = (nz.colour > 0.3f ? (w - nz.z2) : nz.z2) * nz.gain * e * e;
            L[i] += x; R[i] += x;
        }
    }

    // ---- sympathetic strings: open strings ring along (much more with the pedal down) ----
    const float res = param ("resonance");
    if (res > 0.001f && ! resonators.empty())
    {
        const float decay = pedalDown ? 3.2f : 0.35f;
        for (auto& r : resonators)
            r->g = (float) std::pow (10.0, -3.0 * r->delay / (decay * sampleRate));
        const float drive = (pedalDown ? 0.05f : 0.012f), outGain = res * 0.35f;
        for (int i = 0; i < total; ++i)
        {
            const float in = (L[i] + R[i]) * drive;
            float sl = 0.0f, sr = 0.0f;
            for (auto& r : resonators)
            {
                const float y = r->process (in);
                sl += y * r->panL; sr += y * r->panR;
            }
            L[i] += sl * outGain; R[i] += sr * outGain;
        }
    }

    // ---- tone: bass, body, treble (+ the lid), and the mic position ----
    const int mic = (int) param ("mic");
    const float micTreble[] = { 1.5f, 0.0f, -2.5f, -4.0f }, micWidth[] = { 1.15f, 0.9f, 0.65f, 0.5f }, micDistance[] = { 0.0f, 0.0f, 0.25f, 0.5f };
    const float tone[5] = { param ("bass"), param ("mid"), param ("treble") + micTreble[mic] + (param ("lid") - 0.8f) * 9.0f, mic == 0 ? 2.0f : 0.0f, (float) sampleRate };
    if (! std::equal (std::begin (tone), std::end (tone), std::begin (lastTone)))
    {
        std::copy (std::begin (tone), std::end (tone), std::begin (lastTone));
        using C = juce::dsp::IIR::Coefficients<float>;
        for (int c = 0; c < 2; ++c)
        {
            lowShelf[(size_t) c].coefficients = C::makeLowShelf (sampleRate, 140.0, 0.7, juce::Decibels::decibelsToGain (tone[0]));
            midPeak[(size_t) c].coefficients = C::makePeakFilter (sampleRate, 650.0, 0.8, juce::Decibels::decibelsToGain (tone[1]));
            highShelf[(size_t) c].coefficients = C::makeHighShelf (sampleRate, 3500.0, 0.7, juce::Decibels::decibelsToGain (tone[2]));
            presence[(size_t) c].coefficients = C::makePeakFilter (sampleRate, 5500.0, 0.9, juce::Decibels::decibelsToGain (tone[3]));
        }
    }
    for (int c = 0; c < 2; ++c)
    {
        float* d = buffer.getWritePointer (c);
        for (int i = 0; i < total; ++i)
            d[i] = presence[(size_t) c].processSample (highShelf[(size_t) c].processSample (midPeak[(size_t) c].processSample (lowShelf[(size_t) c].processSample (d[i]))));
    }

    // ---- drive: a warm tube stage ----
    const float drive = param ("drive");
    if (drive > 0.001f)
    {
        const float pre = 1.0f + drive * 7.0f, post = 1.0f / std::tanh (pre * 0.5f) * 0.5f;
        for (int c = 0; c < 2; ++c)
        {
            float* d = buffer.getWritePointer (c);
            for (int i = 0; i < total; ++i)
            {
                const float x = d[i] * pre;
                d[i] = (std::tanh (x + 0.15f * drive) - std::tanh (0.15f * drive)) * post * (1.0f - 0.3f * drive) + d[i] * 0.3f * drive;
            }
        }
    }

    // ---- compressor (stereo linked) ----
    const float cmp = param ("comp");
    if (cmp > 0.001f)
    {
        const float thrDb = -12.0f - 18.0f * cmp, ratio = 1.0f + 5.0f * cmp;
        const float makeup = juce::Decibels::decibelsToGain (-thrDb * (1.0f - 1.0f / ratio) * 0.45f);
        const float att = (float) std::exp (-1.0 / (0.008 * sampleRate)), rel = (float) std::exp (-1.0 / (0.16 * sampleRate));
        for (int i = 0; i < total; ++i)
        {
            const float lvl = juce::jmax (std::abs (L[i]), std::abs (R[i]));
            comp.env = lvl > comp.env ? att * comp.env + (1.0f - att) * lvl : rel * comp.env + (1.0f - rel) * lvl;
            const float envDb = juce::Decibels::gainToDecibels (comp.env, -100.0f);
            const float over = envDb - thrDb;
            const float g = over > 0.0f ? juce::Decibels::decibelsToGain (-over * (1.0f - 1.0f / ratio)) : 1.0f;
            L[i] *= g * makeup; R[i] *= g * makeup;
        }
    }

    // ---- tape: wow and flutter, saturation, a little hiss, softened top ----
    const float tape = param ("tape");
    if (tape > 0.001f && ! tapeBuf[0].empty())
    {
        const int n = (int) tapeBuf[0].size();
        const double wowDepth = tape * 0.0009 * sampleRate, flutDepth = tape * 0.00012 * sampleRate;
        const float lpA = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * (18000.0 - 10000.0 * tape) / sampleRate));
        const float sat = 1.0f + 2.5f * tape;
        for (int i = 0; i < total; ++i)
        {
            tapeWow += juce::MathConstants<double>::twoPi * 0.55 / sampleRate;
            tapeFlutter += juce::MathConstants<double>::twoPi * 6.3 / sampleRate;
            const double delay = 4.0 + wowDepth * (1.0 + std::sin (tapeWow)) + flutDepth * (1.0 + std::sin (tapeFlutter));
            for (int c = 0; c < 2; ++c)
            {
                float* d = buffer.getWritePointer (c);
                tapeBuf[c][(size_t) tapePos] = d[i];
                double rp = tapePos - delay;
                while (rp < 0) rp += n;
                const int i0 = (int) rp;
                const float fr = (float) (rp - i0);
                const float y = tapeBuf[c][(size_t) i0] + fr * (tapeBuf[c][(size_t) ((i0 + 1) % n)] - tapeBuf[c][(size_t) i0]);
                const float s = std::tanh (y * sat) / sat * (1.0f + 0.3f * tape);
                tapeLp[c] += lpA * (s - tapeLp[c]);
                d[i] = tapeLp[c] + (random.nextFloat() - 0.5f) * 0.0015f * tape;
            }
            tapePos = (tapePos + 1) % n;
            if (tapeWow > 1000.0) tapeWow -= juce::MathConstants<double>::twoPi * 100.0;
            if (tapeFlutter > 1000.0) tapeFlutter -= juce::MathConstants<double>::twoPi * 100.0;
        }
    }

    // ---- lo-fi: a narrowing band, sample-rate and bit reduction (old radio, megaphone, bad recorder) ----
    const float lofi = param ("lofi");
    if (lofi > 0.001f)
    {
        const float hpA = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * (60.0 + 600.0 * lofi) / sampleRate));
        const float lpA = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * (16000.0 * std::pow (0.15, (double) lofi)) / sampleRate));
        const int hold = 1 + (int) (lofi * lofi * 5.0f);
        const float steps = std::pow (2.0f, 16.0f - 10.0f * lofi);
        for (int i = 0; i < total; ++i)
        {
            const bool take = (lofiCount++ % hold) == 0;
            for (int c = 0; c < 2; ++c)
            {
                float* d = buffer.getWritePointer (c);
                if (take) lofiHold[c] = std::round (d[i] * steps) / steps;
                float x = lofiHold[c];
                lofiHp[c] += hpA * (x - lofiHp[c]);
                x -= lofiHp[c];
                lofiLp[c] += lpA * (x - lofiLp[c]);
                d[i] = std::tanh (lofiLp[c] * (1.0f + lofi * 2.0f)) * (1.0f + lofi);
            }
        }
    }

    // ---- stereo width ----
    const float width = param ("width") * micWidth[mic];
    for (int i = 0; i < total; ++i)
    {
        const float m = 0.5f * (L[i] + R[i]), s = 0.5f * (L[i] - R[i]) * width;
        L[i] = m + s; R[i] = m - s;
    }

    // ---- the space ----
    const float mix = param ("spaceMix");
    const float distance = juce::jlimit (0.0f, 1.0f, param ("distance") + micDistance[mic]);
    const bool roomOn = (int) param ("space") != roomir::dry && mix > 0.001f && total <= wet.getNumSamples();
    if (roomOn)
    {
        for (int c = 0; c < 2; ++c) wet.copyFrom (c, 0, buffer, c, 0, total);
        juce::dsp::AudioBlock<float> blk (wet.getArrayOfWritePointers(), 2, (size_t) total);
        convolution.process (juce::dsp::ProcessContextReplacing<float> (blk));
        // further away: less direct sound, duller, more room
        const float dry = std::cos (mix * juce::MathConstants<float>::halfPi * (0.45f + 0.55f * distance));
        const float wetG = std::sin (mix * juce::MathConstants<float>::halfPi) * (0.75f + 0.6f * distance);
        const float dA = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * (20000.0 * std::pow (0.3, (double) distance)) / sampleRate));
        for (int c = 0; c < 2; ++c)
        {
            float* d = buffer.getWritePointer (c);
            const float* w = wet.getReadPointer (c);
            for (int i = 0; i < total; ++i)
            {
                dryLp[c] += dA * (d[i] - dryLp[c]);
                d[i] = dryLp[c] * dry + w[i] * wetG;
            }
        }
    }

    // ---- volume, safety ----
    const float vol = juce::Decibels::decibelsToGain (param ("volume"));
    for (int c = 0; c < 2; ++c)
    {
        float* d = buffer.getWritePointer (c);
        for (int i = 0; i < total; ++i)
        {
            const float x = d[i] * vol;
            d[i] = std::isfinite (x) ? juce::jlimit (-4.0f, 4.0f, x) : 0.0f;
        }
    }
}

} // namespace wis::daw
