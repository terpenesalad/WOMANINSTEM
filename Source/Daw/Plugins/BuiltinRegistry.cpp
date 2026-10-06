#include "BuiltinProcessor.h"
#include "BuiltinEffects.h"
#include "TestEffects.h"
#include "CreativeEffects.h"
#include "MidiEffects.h"
#include "VocalTune.h"
#include "Looper.h"
#include "Daw/Instruments/HomeKeys.h"
#include "Daw/Instruments/RhythmBox.h"
#include "Daw/Instruments/Sampler.h"
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
            i.midiFx = juce::String (cat) == "MIDI FX";
            l.add (i);
        };
        add ("soundfont", "Sound Library", "Instrument", "Pianos, keys, guitars, basses, strings, brass, winds, synths and drum kits (General MIDI SoundFont, or load your own .sf2)", true, make<SoundFontInstrument>());
        add ("synth", "Studio Synth", "Instrument", "Polyphonic analogue-style synth: 2 oscillators, sub, noise, resonant filter, envelopes, LFO, glide", true, make<StudioSynth>());
        add ("homekeys", "HomeKeys 20", "Instrument", "80s home keyboard: cheesy organs, toy strings, vibes and flutes, a built-in rhythm box with 20 rhythms and auto bass + chord accompaniment", true, make<HomeKeys>());
        add ("rhythmbox", "Rhythm Box", "Instrument", "Vintage drum machine kits (home keyboard, compact rhythm unit, 808-style, toy lo-fi) on the standard drum map", true, make<RhythmBox>());
        add ("sampler", "Sampler", "Instrument", "Play any sample across the keyboard, as a one-shot or chopped into slices; loops, envelopes, filter, glide", true, make<Sampler>());
        add ("drumpads", "Drum Pads", "Instrument", "16 pads of your own samples (or synth drums) with tune, decay, filter, reverse and choke groups", true, make<DrumPads>());

        add ("arp", "Arpeggiator", "MIDI FX", "Up/down/random/chord arpeggios synced to the song, with octaves, swing, latch, rhythms, probability and ratchets", false, make<Arpeggiator>());
        add ("chordtrig", "Chord Trigger", "MIDI FX", "Play whole chords from a single key", false, make<ChordTrigger>());
        add ("scalelock", "Scale Lock", "MIDI FX", "Keeps every note in key (snap or drop wrong notes)", false, make<ScaleLock>());
        add ("noteecho", "Note Echo", "MIDI FX", "Synced MIDI echoes with fading velocity and pitch steps", false, make<NoteEcho>());
        add ("randomizer", "Randomizer", "MIDI FX", "Humanise or mangle: random velocity, timing, octave jumps and dropped notes", false, make<Randomizer>());

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
        add ("autotune", "Vocal Tune", "Vocal", "Automatic pitch correction to the song's key, from natural to the hard robotic effect", false, make<VocalTune>());
        add ("deesser", "De-Esser", "Vocal", "Tames harsh S and T sounds", false, make<DeEsser>());
        add ("vocoder", "Vocoder", "Vocal", "Robot voice, or make a synth talk with a side-chained vocal", false, make<Vocoder>());
        add ("pitchshift", "Pitch Shifter", "Pitch", "Shift up or down up to two octaves, with a second harmony voice", false, make<PitchShifter>());
        add ("shimmer", "Shimmer Reverb", "Space", "Huge reverb with octave-up shimmer in the tail", false, make<ShimmerVerb>());
        add ("tape", "Tape Warble", "Lo-Fi", "Wow, flutter, saturation, hiss and dropouts: warped cassette dreaminess", false, make<TapeWarble>());
        add ("bitcrusher", "Bitcrusher", "Lo-Fi", "Bit depth and sample-rate reduction", false, make<Bitcrusher>());
        add ("autofilter", "Auto Filter", "EQ & Filter", "Resonant filter swept by a synced LFO or the input level (wah, wobble, swells)", false, make<AutoFilter>());
        add ("flanger", "Flanger", "Modulation", "Jet-plane through-zero style flanging", false, make<Flanger>());
        add ("ringmod", "Ring Modulator", "Modulation", "Metallic, bell-like and robotic tones", false, make<RingMod>());
        add ("width", "Stereo Width", "Utility", "Widen or narrow the stereo image, Haas delay, mono bass", false, make<StereoWidth>());
        add ("grains", "Grain Cloud", "Experimental", "Granular clouds: sprays of tiny pitched, reversed, frozen fragments", false, make<GrainCloud>());
        add ("beatrepeat", "Beat Repeat", "Experimental", "Synced stutters, rolls and glitch fills", false, make<BeatRepeat>());
        add ("looper", "Loop Station", "Experimental", "Looper pedal: record, overdub layers, undo, half-speed, reverse, and drop the loop on the track", false, make<Looper>());
        add ("amprig", "Amp & Pedals", "Amps", "The full play-along rig: gate, comp, drive, amps, NAM captures, cabs, IRs, effects", false, make<AmpRigFx>());

        add ("testlatency", "Latency Test", "Hidden", "Used by the automated tests", false, make<LatencyTestFx>());
        return l;
    }();
    return list;
}

} // namespace wis::daw
