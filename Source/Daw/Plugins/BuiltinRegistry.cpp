#include "BuiltinProcessor.h"
#include "BuiltinEffects.h"
#include "TestEffects.h"
#include "Daw/Instruments/SoundFontInstrument.h"
#include "Daw/Instruments/StudioSynth.h"

namespace wis::daw
{

template <typename T>
static std::function<std::unique_ptr<BuiltinProcessor>()> make() { return [] { return std::make_unique<T>(); }; }

const juce::Array<BuiltinInfo>& builtinPlugins()
{
    static const juce::Array<BuiltinInfo> list = [] {
        juce::Array<BuiltinInfo> l;
        auto add = [&l] (const char* id, const char* name, const char* cat, const char* desc, bool inst, auto factory)
        {
            BuiltinInfo i;
            i.id = id; i.name = name; i.category = cat; i.description = desc; i.instrument = inst; i.create = factory;
            l.add (i);
        };
        add ("soundfont", "Sound Library", "Instrument", "Pianos, keys, guitars, basses, strings, brass, winds, synths and drum kits (General MIDI SoundFont, or load your own .sf2)", true, make<SoundFontInstrument>());
        add ("synth", "Studio Synth", "Instrument", "Polyphonic analogue-style synth: 2 oscillators, sub, noise, resonant filter, envelopes, LFO, glide", true, make<StudioSynth>());

        add ("eq", "Channel EQ", "EQ & Filter", "6-band EQ: low/high cut, shelves and two bells", false, make<ChannelEq>());
        add ("compressor", "Compressor", "Dynamics", "Smooth soft-knee compressor with parallel mix", false, make<Compressor>());
        add ("limiter", "Limiter", "Dynamics", "Brick-wall limiter for loudness and safety", false, make<LimiterFx>());
        add ("gate", "Noise Gate", "Dynamics", "Removes hum and noise between notes", false, make<GateFx>());
        add ("reverb", "Reverb", "Space", "Rooms, halls and plates", false, make<ReverbFx>());
        add ("delay", "Delay", "Space", "Tempo-synced stereo / ping-pong echo", false, make<DelayFx>());
        add ("chorus", "Chorus", "Modulation", "Lush stereo chorus", false, make<ChorusFx>());
        add ("phaser", "Phaser", "Modulation", "Swirling phaser", false, make<PhaserFx>());
        add ("tremolo", "Tremolo / Auto-Pan", "Modulation", "Tempo-synced tremolo and auto-pan", false, make<TremoloFx>());
        add ("saturator", "Saturator", "Distortion", "Tape, tube, clip, fuzz and lo-fi colour", false, make<Saturator>());
        add ("amprig", "Amp & Pedals", "Amps", "The full play-along rig: gate, comp, drive, amps, NAM captures, cabs, IRs, effects", false, make<AmpRigFx>());

        add ("testlatency", "Latency Test", "Hidden", "Used by the automated tests", false, make<LatencyTestFx>());
        return l;
    }();
    return list;
}

} // namespace wis::daw
