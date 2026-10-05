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
#define TSF_IMPLEMENTATION
#define TSF_NO_STDIO
#include <tsf.h>
#if defined(_MSC_VER)
 #pragma warning (pop)
#elif defined(__clang__)
 #pragma clang diagnostic pop
#elif defined(__GNUC__)
 #pragma GCC diagnostic pop
#endif

#include "SoundFontInstrument.h"
#include "InstrumentRefs.h"
#include "Separation/ModelManager.h"

namespace wis::daw
{

// =====================================================================================================
//  General MIDI names
// =====================================================================================================
static const char* const gmNames[128] = {
    "Acoustic Grand Piano", "Bright Acoustic Piano", "Electric Grand Piano", "Honky-tonk Piano", "Electric Piano 1", "Electric Piano 2", "Harpsichord", "Clavinet",
    "Celesta", "Glockenspiel", "Music Box", "Vibraphone", "Marimba", "Xylophone", "Tubular Bells", "Dulcimer",
    "Drawbar Organ", "Percussive Organ", "Rock Organ", "Church Organ", "Reed Organ", "Accordion", "Harmonica", "Tango Accordion",
    "Nylon Guitar", "Steel Guitar", "Jazz Guitar", "Clean Electric Guitar", "Muted Guitar", "Overdriven Guitar", "Distortion Guitar", "Guitar Harmonics",
    "Acoustic Bass", "Fingered Bass", "Picked Bass", "Fretless Bass", "Slap Bass 1", "Slap Bass 2", "Synth Bass 1", "Synth Bass 2",
    "Violin", "Viola", "Cello", "Contrabass", "Tremolo Strings", "Pizzicato Strings", "Orchestral Harp", "Timpani",
    "String Ensemble 1", "String Ensemble 2", "Synth Strings 1", "Synth Strings 2", "Choir Aahs", "Voice Oohs", "Synth Voice", "Orchestra Hit",
    "Trumpet", "Trombone", "Tuba", "Muted Trumpet", "French Horn", "Brass Section", "Synth Brass 1", "Synth Brass 2",
    "Soprano Sax", "Alto Sax", "Tenor Sax", "Baritone Sax", "Oboe", "English Horn", "Bassoon", "Clarinet",
    "Piccolo", "Flute", "Recorder", "Pan Flute", "Blown Bottle", "Shakuhachi", "Whistle", "Ocarina",
    "Square Lead", "Saw Lead", "Calliope Lead", "Chiff Lead", "Charang Lead", "Voice Lead", "Fifths Lead", "Bass + Lead",
    "New Age Pad", "Warm Pad", "Polysynth Pad", "Choir Pad", "Bowed Pad", "Metallic Pad", "Halo Pad", "Sweep Pad",
    "Rain FX", "Soundtrack FX", "Crystal FX", "Atmosphere FX", "Brightness FX", "Goblins FX", "Echoes FX", "Sci-Fi FX",
    "Sitar", "Banjo", "Shamisen", "Koto", "Kalimba", "Bagpipe", "Fiddle", "Shanai",
    "Tinkle Bell", "Agogo", "Steel Drums", "Woodblock", "Taiko Drum", "Melodic Tom", "Synth Drum", "Reverse Cymbal",
    "Guitar Fret Noise", "Breath Noise", "Seashore", "Bird Tweet", "Telephone Ring", "Helicopter", "Applause", "Gunshot"
};

juce::String gmProgramName (int program) { return gmNames[juce::jlimit (0, 127, program)]; }

juce::StringArray gmFamilies()
{
    return { "Piano", "Chromatic Percussion", "Organ", "Guitar", "Bass", "Strings", "Ensemble & Choir", "Brass",
             "Reed", "Pipe & Flute", "Synth Lead", "Synth Pad", "Synth FX", "World", "Percussive", "Sound FX" };
}

juce::String gmFamilyName (int program) { return gmFamilies()[juce::jlimit (0, 127, program) / 8]; }

juce::String gmDrumName (int note)
{
    static const char* const names[] = {
        "Acoustic Kick", "Kick", "Side Stick", "Snare", "Hand Clap", "Electric Snare", "Low Floor Tom", "Closed Hi-Hat",
        "High Floor Tom", "Pedal Hi-Hat", "Low Tom", "Open Hi-Hat", "Low-Mid Tom", "Hi-Mid Tom", "Crash 1", "High Tom",
        "Ride 1", "China", "Ride Bell", "Tambourine", "Splash", "Cowbell", "Crash 2", "Vibraslap", "Ride 2",
        "Hi Bongo", "Low Bongo", "Mute Hi Conga", "Open Hi Conga", "Low Conga", "High Timbale", "Low Timbale",
        "High Agogo", "Low Agogo", "Cabasa", "Maracas", "Short Whistle", "Long Whistle", "Short Guiro", "Long Guiro",
        "Claves", "Hi Wood Block", "Low Wood Block", "Mute Cuica", "Open Cuica", "Mute Triangle", "Open Triangle"
    };
    if (note < 35 || note > 81) return {};
    return names[note - 35];
}

PluginRef soundFontRef (int bank, int program, const juce::String& sf2Path)
{
    PluginRef r = builtinRef ("soundfont");
    r.name = bank == 128 ? juce::String ("Drum Kit") : gmProgramName (program);

    // Same format as BuiltinProcessor::getStateInformation (params default, extra state carries the preset).
    juce::ValueTree tree ("STATE");
    tree.setProperty ("program", 0, nullptr);
    juce::ValueTree extra ("EXTRA");
    extra.setProperty ("file", sf2Path, nullptr);
    extra.setProperty ("bank", bank, nullptr);
    extra.setProperty ("preset", program, nullptr);
    tree.appendChild (extra, nullptr);
    if (auto xml = tree.createXml())
    {
        juce::MemoryBlock mb;
        juce::AudioProcessor::copyXmlToBinary (*xml, mb);
        r.state = mb.toBase64Encoding();
    }
    return r;
}

// =====================================================================================================
//  SoundFontCache
// =====================================================================================================
SoundFontCache& SoundFontCache::get()
{
    static SoundFontCache instance;
    return instance;
}

juce::File SoundFontCache::defaultSoundFont()
{
    const juce::String name ("GeneralUser-GS.sf2");
    auto exeDir = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
    const juce::File candidates[] = {
        exeDir.getChildFile ("sounds").getChildFile (name),
        exeDir.getChildFile (name),
        exeDir.getParentDirectory().getChildFile ("sounds").getChildFile (name),
        ModelManager::appDataDirectory().getChildFile ("sounds").getChildFile (name),
        juce::File (juce::SystemStats::getEnvironmentVariable ("WIS_SOUNDFONT", {})),
    };
    for (auto& f : candidates)
        if (f.existsAsFile())
            return f;
    return {};
}

tsf* SoundFontCache::master (const juce::File& f, juce::String& error)
{
    const auto key = f.getFullPathName();
    if (auto it = loaded.find (key); it != loaded.end())
        return it->second;

    juce::MemoryBlock data;
    if (! f.loadFileAsData (data) || data.getSize() < 64)
    {
        error = "Couldn't read " + f.getFullPathName();
        return nullptr;
    }

    tsf* t = tsf_load_memory (data.getData(), (int) data.getSize());
    if (t == nullptr)
    {
        error = f.getFileName() + " isn't a valid SoundFont (.sf2) file.";
        return nullptr;
    }
    loaded[key] = t;
    return t;
}

tsf* SoundFontCache::createInstance (const juce::File& f, juce::String& error)
{
    std::lock_guard<std::mutex> l (lock);
    if (auto* m = master (f, error))
        return tsf_copy (m);
    return nullptr;
}

juce::Array<SoundFontCache::PresetInfo> SoundFontCache::presetsFor (const juce::File& f)
{
    std::lock_guard<std::mutex> l (lock);
    juce::Array<PresetInfo> result;
    juce::String err;
    if (auto* m = master (f, err))
    {
        for (int i = 0; i < m->presetNum; ++i)
        {
            PresetInfo p;
            p.bank = m->presets[i].bank;
            p.program = m->presets[i].preset;
            p.name = juce::String (m->presets[i].presetName).trim();
            result.add (p);
        }
        std::sort (result.begin(), result.end(), [] (const PresetInfo& a, const PresetInfo& b)
        {
            return a.bank != b.bank ? a.bank < b.bank : a.program < b.program;
        });
    }
    return result;
}

void SoundFontCache::preloadDefault()
{
    juce::Thread::launch ([this]
    {
        auto f = defaultSoundFont();
        if (! f.existsAsFile()) return;
        std::lock_guard<std::mutex> l (lock);
        juce::String err;
        master (f, err);
    });
}

// =====================================================================================================
//  SoundFontInstrument
// =====================================================================================================
std::function<juce::AudioProcessorEditor* (SoundFontInstrument&)> SoundFontInstrument::editorFactory;

static prm::Layout sfLayout()
{
    prm::Layout l;
    prm::addDb (l, "gain", "Volume", -24.0f, 12.0f, 0.0f);
    prm::addFloat (l, "transpose", "Transpose", -24.0f, 24.0f, 0.0f, "st", 0.0f, 0);
    prm::addFloat (l, "bendrange", "Bend Range", 1.0f, 12.0f, 2.0f, "st", 0.0f, 0);
    return l;
}

SoundFontInstrument::SoundFontInstrument() : BuiltinProcessor ("soundfont", "Sound Library", true, sfLayout())
{
    loadSoundFont ({});
}

SoundFontInstrument::~SoundFontInstrument()
{
    if (synth != nullptr) tsf_close (synth);
}

juce::String SoundFontInstrument::loadSoundFont (const juce::File& requested)
{
    auto f = requested.existsAsFile() ? requested : SoundFontCache::defaultSoundFont();
    if (! f.existsAsFile())
        return "The Sound Library (GeneralUser-GS.sf2) isn't installed next to the app.";

    juce::String err;
    tsf* t = SoundFontCache::get().createInstance (f, err);
    if (t == nullptr) return err;

    tsf_set_output (t, TSF_STEREO_INTERLEAVED, (int) sampleRate, 0.0f);
    tsf_set_max_voices (t, 128);
    tsf_channel_set_bank_preset (t, 0, bank.load(), program.load());   // also allocates channel 0 (off the audio thread)
    tsf_channel_set_pitchrange (t, 0, param ("bendrange"));

    tsf* old;
    {
        const juce::SpinLock::ScopedLockType sl (synthLock);
        old = synth;
        synth = t;
        sfFile = requested.existsAsFile() ? requested : juce::File();
        presetDirty = true;
    }
    if (old != nullptr) tsf_close (old);
    presets = SoundFontCache::get().presetsFor (f);
    return {};
}

void SoundFontInstrument::setPreset (int b, int p)
{
    bank = b;
    program = p;
    presetDirty = true;
}

juce::String SoundFontInstrument::getPresetName() const
{
    for (auto& p : presets)
        if (p.bank == bank.load() && p.program == program.load())
            return p.name;
    return bank.load() == 128 ? juce::String ("Drum Kit") : gmProgramName (program.load());
}

void SoundFontInstrument::applyPreset (tsf* f)
{
    if (! presetDirty.exchange (false)) return;
    tsf_channel_sounds_off_all (f, 0);
    if (! tsf_channel_set_bank_preset (f, 0, bank.load(), program.load()))
        tsf_channel_set_presetnumber (f, 0, program.load(), bank.load() == 128);   // fall back to bank 0 / GM drums
}

void SoundFontInstrument::prepareToPlay (double sr, int block)
{
    sampleRate = sr;
    maxBlock = block;
    interleaved.assign ((size_t) block * 2 + 32, 0.0f);
    const juce::SpinLock::ScopedLockType sl (synthLock);
    if (synth != nullptr)
    {
        tsf_set_output (synth, TSF_STEREO_INTERLEAVED, (int) sr, 0.0f);
        tsf_channel_sounds_off_all (synth, 0);
    }
    presetDirty = true;
}

void SoundFontInstrument::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    buffer.clear();
    const juce::SpinLock::ScopedTryLockType sl (synthLock);
    if (! sl.isLocked() || synth == nullptr)
        return;

    applyPreset (synth);
    tsf_set_volume (synth, juce::Decibels::decibelsToGain (param ("gain")));

    const int transpose = (int) std::round (param ("transpose"));
    const int n = buffer.getNumSamples();
    auto* L = buffer.getWritePointer (0);
    auto* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;

    int pos = 0;
    auto renderTo = [&] (int end)
    {
        while (pos < end)
        {
            const int chunk = juce::jmin (end - pos, maxBlock);
            tsf_render_float (synth, interleaved.data(), chunk, 0);
            for (int i = 0; i < chunk; ++i)
            {
                L[pos + i] = interleaved[(size_t) (2 * i)];
                if (R != nullptr) R[pos + i] = interleaved[(size_t) (2 * i + 1)];
            }
            pos += chunk;
        }
    };

    for (const auto meta : midi)
    {
        renderTo (juce::jlimit (0, n, meta.samplePosition));
        const auto m = meta.getMessage();
        if (m.isNoteOn())
            tsf_channel_note_on (synth, 0, juce::jlimit (0, 127, m.getNoteNumber() + transpose), m.getFloatVelocity());
        else if (m.isNoteOff())
            tsf_channel_note_off (synth, 0, juce::jlimit (0, 127, m.getNoteNumber() + transpose));
        else if (m.isPitchWheel())
            tsf_channel_set_pitchwheel (synth, 0, m.getPitchWheelValue());
        else if (m.isController())
            tsf_channel_midi_control (synth, 0, m.getControllerNumber(), m.getControllerValue());
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            tsf_channel_sounds_off_all (synth, 0);
    }
    renderTo (n);
}

void SoundFontInstrument::saveExtraState (juce::ValueTree& t)
{
    t.setProperty ("file", sfFile.getFullPathName(), nullptr);
    t.setProperty ("bank", bank.load(), nullptr);
    t.setProperty ("preset", program.load(), nullptr);
}

void SoundFontInstrument::loadExtraState (const juce::ValueTree& t)
{
    setPreset ((int) t.getProperty ("bank", 0), (int) t.getProperty ("preset", 0));
    const juce::File f (t.getProperty ("file").toString());
    if (f.existsAsFile() && f != sfFile)
        loadSoundFont (f);
    else
        presetDirty = true;
}

} // namespace wis::daw
