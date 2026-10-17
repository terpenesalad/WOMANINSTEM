#pragma once

#include "Daw/Plugins/BuiltinProcessor.h"
#include "Daw/Model/DrumGenerator.h"

namespace wis::daw
{

// ---------------------------------------------------------------------------------------------------------
//  Smart Drummer: a drummer that listens to the song and plays along, on a kit you can rebuild.
//
//  The drummer half is settings (style, complexity, energy, how closely to follow the band, fills, swing,
//  feel...) plus a hook into the Studio: "Play the Song" analyses everything else in the song (MIDI and
//  audio: where the accents and the low end land, the swing, the sections) and writes a part, one MIDI
//  clip per section, that you can edit like any other.
//
//  The kit half is a drum sampler: 17 pieces (kick, snare, side stick, clap, three hi-hats, four toms,
//  crash, ride, bell, tambourine, shaker, cowbell), each taken from any of 11 sampled kits in the Sound
//  Library or 4 synthesized drum machines, with its own tune, decay, level and pan, then a shared tone,
//  punch, drive and room.
// ---------------------------------------------------------------------------------------------------------
class SmartDrummer : public BuiltinProcessor, public juce::ChangeBroadcaster, private juce::Timer
{
public:
    SmartDrummer();
    ~SmartDrummer() override;

    enum Piece { kick = 0, snare, stick, clap, hatClosed, hatPedal, hatOpen, floorTom, lowTom, midTom, highTom,
                 crash, ride, rideBell, tambourine, shaker, cowbell, numPieces };
    static juce::String pieceName (int piece);
    static juce::String pieceId (int piece);          // parameter prefix
    static int pieceForNote (int midiNote);           // -1 if not a drum
    static int noteForPiece (int piece);

    static constexpr int numSources = 15;             // 11 sampled kits + 4 synth machines
    static juce::StringArray sourceNames();
    static bool sourceIsSampled (int source) { return source < 11; }

    // ---- the drummer (message thread) ----
    drummer::Settings settings() const;
    void setSectionLevel (int startBar, int level);   // -1 = follow the song
    int getSectionLevel (int startBar) const;
    /** Analyse the song and write (or just analyse); the Studio installs this. */
    static std::function<void (SmartDrummer&, bool write)> onPlaySong;
    void playSong (bool write) { if (onPlaySong) onPlaySong (*this, write); }
    /** Write again from the song already heard (after changing settings). */
    static std::function<void (SmartDrummer&)> onRewrite;
    void rewrite() { if (onRewrite && hasMap) onRewrite (*this); }

    /** Results, set by the Studio (message thread). */
    void setSongMap (const SongMap& m) { songMap = m; hasMap = true; sendChangeMessage(); }
    void setStatus (const juce::String& s, bool isBusy) { status = s; busy = isBusy; sendChangeMessage(); }
    const SongMap& getSongMap() const { return songMap; }
    bool hasSongMap() const { return hasMap; }
    juce::String getStatus() const { return status; }
    bool isBusy() const { return busy; }

    /** Load the style's matching kit settings (sources, room, tone...). */
    void applyStyleKit (int style);

    // ---- the kit ----
    /** Plays a piece now (editor buttons). Any thread. */
    void audition (int piece, float velocity = 0.85f);
    bool isKitReady() const { return kitReady.load(); }
    /** Tests: block until the kit (and room) are loaded. */
    void waitUntilReady();
    int getActiveVoices() const { return activeVoices.load(); }
    /** Which page the editor shows (0 drummer, 1 kit); remembered while the plugin lives. */
    int editorPage = 0;

    // BuiltinProcessor
    void prepareToPlay (double sampleRate, int blockSize) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    double getTailLengthSeconds() const override { return 4.0; }
    juce::StringArray getProgramNames() override { return drummer::styleNames(); }
    void loadProgram (int) override;
    void saveExtraState (juce::ValueTree&) override;
    void loadExtraState (const juce::ValueTree&) override;
    juce::AudioProcessorEditor* createCustomEditor() override { return editorFactory ? editorFactory (*this) : nullptr; }
    static std::function<juce::AudioProcessorEditor* (SmartDrummer&)> editorFactory;

    struct Samples;   // one piece from one source: velocity layers
    struct Bank;      // the 17 pieces in use
    struct Loader;

private:
    void timerCallback() override;
    std::array<int, numPieces> wantedSources() const;
    void requestKit();
    void rebuildRoomIfNeeded (bool force);
    void startVoice (int piece, float velocity);

    // results
    SongMap songMap;
    bool hasMap = false, busy = false;
    juce::String status;
    std::map<int, int> sectionLevels;

    // kit loading
    std::unique_ptr<Loader> loader;
    juce::SpinLock bankLock;
    juce::ReferenceCountedObjectPtr<Bank> pendingBank, retiredBank, liveBank;
    std::array<int, numPieces> requestedSources {};
    std::atomic<bool> kitReady { false };
    double sampleRate = 48000.0;
    int maxBlock = 512;

    // voices
    struct Voice
    {
        const Samples* samples = nullptr;
        int layer = 0, piece = -1;
        double pos = 0.0, rate = 1.0;
        float gain = 1.0f, panL = 1.0f, panR = 1.0f, env = 1.0f, decayMul = 1.0f, choke = 1.0f, chokeMul = 1.0f;
        juce::uint32 age = 0;
        bool active = false;
    };
    std::array<Voice, 64> voices;
    juce::uint32 voiceCounter = 0;
    std::atomic<int> activeVoices { 0 };
    juce::AbstractFifo auditionFifo { 64 };
    std::array<std::pair<int, float>, 64> auditionQueue {};
    juce::Random random;

    // bus
    float toneLp[2] {}, compEnv = 0.0f;
    juce::dsp::Convolution convolution { juce::dsp::Convolution::NonUniform { 512 } };
    juce::AudioBuffer<float> wet;
    std::atomic<bool> roomReady { false };
    int roomKey[2] { -1, -1 };
};

} // namespace wis::daw
