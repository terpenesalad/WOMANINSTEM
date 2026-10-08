#pragma once

#include "Daw/Plugins/BuiltinProcessor.h"
#include <mutex>

namespace wis::daw
{

// ---------------------------------------------------------------------------------------------------------
//  Sample packs (pianos/<id>/pack.json + FLAC files, made by Tools/prep_pianos.py)
// ---------------------------------------------------------------------------------------------------------
struct PianoPack
{
    struct Sample
    {
        int key = 60, layer = 0, frames = 0;
        double rate = 48000.0;
        float gain = 1.0f;                    // evens out loudness between layers (see loudness curve)
        std::vector<juce::int16> data;        // interleaved stereo
    };
    juce::String id, name, credit;
    std::vector<std::pair<int, int>> layers;  // velocity ranges
    std::vector<float> layerDb;               // average loudness of each layer (attack), dB
    std::vector<Sample> samples;
    std::vector<std::vector<int>> byLayer;    // per layer: sample indices sorted by key
    float packGain = 1.0f;                    // brings every pack to the same mezzo-forte level

    /** The sample of `layer` nearest to `note` (falls back to neighbouring layers if that layer lacks the note range). */
    const Sample* find (int layer, int note) const;
    int layerFor (int velocity) const;
};

/** Loads packs on a background thread and keeps them for the life of the app (they're shared by every piano). */
class PianoPackCache
{
public:
    static PianoPackCache& get();
    static juce::File packFolder (const juce::String& id);        // empty if not installed
    static bool isInstalled (const juce::String& id) { return packFolder (id).exists(); }

    /** Never blocks: the pack if it's loaded, otherwise null (and loading starts in the background). Real-time safe. */
    const PianoPack* tryGet (const juce::String& id);
    /** Loads it now if needed (tests, offline rendering). */
    const PianoPack* getBlocking (const juce::String& id);
    bool hasFailed (const juce::String& id);

private:
    PianoPackCache();
    struct Entry { std::atomic<const PianoPack*> pack { nullptr }; std::atomic<int> state { 0 }; };   // 0 idle, 1 wanted, 2 loading, 3 done, 4 failed
    Entry* entryFor (const juce::String& id);
    static std::unique_ptr<PianoPack> load (const juce::String& id);
    void loaderLoop();
    static constexpr int maxPacks = 8;
    juce::String ids[maxPacks];
    Entry entries[maxPacks];
    std::atomic<int> numIds { 0 };
    std::mutex loadMutex;
};

// ---------------------------------------------------------------------------------------------------------
//  Piano Room: sampled grand / vintage grand / upright, a modelled grand and a toy piano, with character,
//  tone and a designed acoustic space around it.
// ---------------------------------------------------------------------------------------------------------
class PianoRoom : public BuiltinProcessor, private juce::Timer
{
public:
    PianoRoom();
    ~PianoRoom() override;

    void prepareToPlay (double sampleRate, int blockSize) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    double getTailLengthSeconds() const override { return 6.0; }
    juce::StringArray getProgramNames() override { return presetNames(); }
    /** Preset names without creating an instance (creating a piano starts loading its samples). */
    static juce::StringArray presetNames();
    void loadProgram (int) override;
    juce::AudioProcessorEditor* createCustomEditor() override { return editorFactory ? editorFactory (*this) : nullptr; }
    static std::function<juce::AudioProcessorEditor* (PianoRoom&)> editorFactory;

    static juce::StringArray modelNames();
    static juce::StringArray micNames();
    static juce::String presetDescription (int program);

    /** For the editor: what is actually playing ("Concert Grand", or the fallback when a pack is missing). */
    juce::String getStatus() const;
    /** Tests: block until the selected model's samples and the room are ready. */
    void waitUntilReady();
    /** Number of sounding voices (UI / tests). */
    int getActiveVoices() const { return activeVoices.load(); }

    static constexpr int maxVoices = 72;

private:
    struct Voice;
    struct Resonator;
    struct NoiseEvent { int left = 0, total = 1; float gain = 0, colour = 0, z1 = 0, z2 = 0; };

    void timerCallback() override;
    void rebuildRoomIfNeeded (bool force);
    void startNote (int note, float velocity, int sampleOffset);
    void stopNote (int note);
    void setPedal (bool down);
    void addNoise (float gain, float colourHz, float seconds);
    const PianoPack* currentPack();
    void renderVoices (float* L, float* R, int n);

    std::vector<std::unique_ptr<Voice>> voices;
    double sampleRate = 48000.0;
    int maxBlock = 512;
    bool pedalDown = false, softPedal = false, lastLatched = false;
    juce::uint32 noteCounter = 0;
    std::atomic<int> activeVoices { 0 };
    juce::Random random;

    // global modulation
    double wobblePhase = 0.0, flutterPhase = 0.0;

    // mechanical noises
    std::array<NoiseEvent, 24> noises;

    // sympathetic strings
    std::vector<std::unique_ptr<Resonator>> resonators;

    // tone / effects
    std::array<juce::dsp::IIR::Filter<float>, 2> lowShelf, midPeak, highShelf, presence;
    float lastTone[5] { -99, -99, -99, -99, -99 };
    struct Comp { float env = 0.0f; } comp;
    std::vector<float> tapeBuf[2];
    int tapePos = 0;
    double tapeWow = 0.0, tapeFlutter = 0.0;
    float tapeLp[2] {}, lofiHp[2] {}, lofiLp[2] {}, lofiHold[2] {};
    int lofiCount = 0;

    // space
    juce::dsp::Convolution convolution { juce::dsp::Convolution::NonUniform { 512 } };
    juce::AudioBuffer<float> wet, dryCopy;
    std::atomic<bool> roomReady { false };
    int roomKey[4] { -1, -1, -1, -1 };
    float dryLp[2] {};
};

} // namespace wis::daw
