#pragma once

#include "Daw/Plugins/BuiltinProcessor.h"
#include "Daw/Model/DaydreamChords.h"
#include "DaydreamSynth.h"
#include "DrumSynth.h"
#include <array>

namespace wis::daw
{

/** "Daydream": a chord machine, an ideas box for songwriting. Hold chord buttons (Maj, Min, Sus, Dim plus
    the colours 6, m7, M7, 9) and press one key to play the chord; Key Mode puts every key's chord in the
    key. The voicing ladder walks a chord up and down through its inversions, the bass voicing walks the
    bass through the chord's notes. Performance modes animate the chord (strum, sloppy strum, arpeggios,
    rhythm patterns, harp sweeps), a separate bass synth plays underneath, three synth engines (analogue,
    FM and a modelled reed electric piano) make the sounds with eight effects, and there are drum grooves
    to play to and a looper to layer ideas.

    On a MIDI keyboard the chord buttons are the eight white keys of one octave (C2..C3 by default):
    C Maj, D Min, E Sus, F Dim, G 6, A m7, B M7, C 9. Every other key is a chord root. With no chord button
    (and Key Mode off) keys play single notes. The on-screen buttons latch. */
class Daydream : public BuiltinProcessor
{
public:
    Daydream();
    ~Daydream() override;

    void prepareToPlay (double sampleRate, int blockSize) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    double getTailLengthSeconds() const override { return 6.0; }
    juce::StringArray getProgramNames() override { return presetNames(); }
    static juce::StringArray presetNames();
    static juce::String presetCategory (int index);
    void loadProgram (int) override;
    void saveExtraState (juce::ValueTree&) override;
    void loadExtraState (const juce::ValueTree&) override;

    static std::function<juce::AudioProcessorEditor* (Daydream&)> editorFactory;
    int editorPage = 0;   // the editor remembers its tab (PLAY, SOUND, EFFECTS, SETTINGS)
    juce::AudioProcessorEditor* createCustomEditor() override { return editorFactory ? editorFactory (*this) : nullptr; }

    enum Perf { perfChord, perfStrum, perfStrum2, perfSlop, perfArp, perfArp2, perfPattern, perfHarp, numPerfModes };
    static juce::StringArray perfNames();
    enum BassMode { bassOff, bassWithChords, bassAlways, bassOnly };
    static juce::StringArray bassModeNames();

    // ---- from the editor (message thread) --------------------------------------------------------------
    /** On-screen chord buttons latch: toggled on until clicked again. */
    void toggleLatched (int button);
    void setLatched (unsigned mask) { latched.store (mask); }
    unsigned getLatched() const     { return latched.load(); }
    /** Buttons held right now (latched or held on the MIDI keyboard). */
    unsigned getHeldButtons() const { return heldButtons.load(); }
    /** Plays / releases a root key from the on-screen keyboard. */
    void uiNote (int note, bool on, float velocity = 0.8f);

    /** The looper. */
    enum LoopState { loopIdle, loopArmed, loopCountIn, loopRecording, loopPlaying, loopOverdub, loopPaused };
    void loopButton();      // record -> play -> pause -> play ...
    void overdubButton();   // toggle overdubbing while it plays
    void loopUndo();        // remove the last layer (the last one clears)
    void loopClear();
    LoopState getLoopState() const      { return (LoopState) loopStateOut.load(); }
    float getLoopProgress() const       { return loopProgressOut.load(); }   // 0..1 through the loop (or the count-in)
    int getLoopLayers() const           { return loopLayersOut.load(); }
    double getLoopBeats() const         { return loopBeatsOut.load(); }

    /** What's playing, for the display. */
    struct Display
    {
        bool chord = false;          // false = single notes
        int root = 0;
        int numIntervals = 0;
        std::array<int, 12> intervals {};
        int numNotes = 0;
        std::array<int, 48> notes {};
        int bass = -1;
        unsigned buttons = 0;
        bool inKey = false;
        juce::uint32 serial = 0;
    };
    Display getDisplay() const;
    /** Notes sounding in the chord synth now. */
    bool isSounding (int note) const { return juce::isPositiveAndBelow (note, 128) && soundingOut[(size_t) note].load (std::memory_order_relaxed); }
    int getBassSounding() const      { return bassSoundingOut.load(); }
    /** The last ~1000 output samples (mono) for the scope view. */
    void getScope (float* dest, int n) const;
    double getCurrentBpm() const     { return currentBpm.load(); }
    /** The key the chords follow (the song's when "follow the song" is on). */
    int getKeyTonic() const;
    int getKeyScale() const;

    // ---- for tests ---------------------------------------------------------------------------------------
    /** The chord a root key would play with these buttons right now (empty intervals = a single note). */
    daydream::Chord chordFor (int note, unsigned buttons) const;
    std::vector<int> notesFor (const daydream::Chord&) const;
    int buttonNote (int button) const;   // the MIDI key of a chord button (-1 when the zone is off)

private:
    // ---- players: the live hands, plus one per loop layer -------------------------------------------------
    struct Pending { juce::int64 at; int note; float vel; bool on; };
    struct Player
    {
        int owner = 0;
        unsigned midiMask = 0;          // the buttons this player is holding (live: MIDI keys + latched)
        unsigned midiMaskRaw = 0;       // live only: chord buttons held on the MIDI keyboard
        std::vector<int> held;          // root keys held, oldest first (as played, before transpose)
        std::vector<int> singles;       // single notes sounding (sounding pitch)
        std::vector<int> sounding;      // chord-synth notes switched on
        std::vector<Pending> events;    // scheduled note ons / offs
        bool chordOn = false, sustain = false;
        int rootKey = -1;
        daydream::Chord chord;
        std::vector<int> notes;         // the placed chord
        int bass = -1;
        float vel = 0.8f;
        // timing for arpeggios and patterns
        juce::int64 gridAt = 0;
        int step = 0;
        std::vector<int> seq;
        int lastRandom = -1;
        bool strumUp = true;
        void reset() { held.clear(); singles.clear(); sounding.clear(); events.clear(); chordOn = false; sustain = false; rootKey = -1; bass = -1; notes.clear(); }
    };
    static constexpr int maxLayers = 12;
    std::array<Player, maxLayers + 1> players;

    void handleRootOn (Player&, int key, float vel, bool record);
    void handleRootOff (Player&, int key, bool record);
    void handleMask (Player&, unsigned mask);
    unsigned effectiveMask (const Player&) const;
    bool makeChord (const Player&, int key, unsigned mask, daydream::Chord& out) const;
    void startChord (Player&, int key, const daydream::Chord&, float vel, bool retrigger);
    void stopChord (Player&);
    void updateChord (Player&, bool revoice = false);   // revoice: same chord, new voicing / position
    void runPlayer (Player&, juce::int64 from, juce::int64 to);
    void scheduleStep (Player&);
    void playStepNotes (Player&, juce::int64 at);
    void synthOn (Player&, int note, float vel);
    void synthOff (Player&, int note);
    void publish (const Player&);

    // the voice pool
    static constexpr int numVoices = 28;
    std::array<daydream::Voice, numVoices> voices;
    daydream::BassSynth bass;
    juce::uint32 voiceCounter = 0;
    daydream::SoundParams sp;
    daydream::FxParams fxp;
    daydream::Effects fx;
    DrumSynth drums;
    void readParams();

    // timing
    double sr = 48000.0;
    juce::int64 now = 0;
    double bpm = 110.0;
    juce::Random rnd;
    juce::int64 samplesPerStep (int rateIndex) const;
    double beatsToSamples (double beats) const { return beats * 60.0 / bpm * sr; }

    // drum grooves
    double grooveBeat = 0.0;
    bool grooveWasOn = false;
    void runGroove (double fromBeat, double toBeat, int chunkStart, int chunkLen);

    // looper
    struct LoopEvent { double beat; int type; int a; float b; };   // 0 on, 1 off, 2 mask, 3 sustain
    std::array<std::vector<LoopEvent>, maxLayers> layers;
    int numLayers = 0;
    LoopState loopState = loopIdle;
    double loopPos = 0.0, loopLength = 0.0, countIn = 0.0;
    int countClicks = 0;
    int recordingLayer = -1;
    std::atomic<int> loopCmd { 0 };
    std::atomic<int> loopStateOut { 0 }, loopLayersOut { 0 };
    std::atomic<float> loopProgressOut { 0.0f };
    std::atomic<double> loopBeatsOut { 0.0 };
    void record (int type, int a, float b);
    void runLoop (double beats);
    void finishRecording();
    void loopCommand (int cmd);

    // the editor's buttons and keys
    std::atomic<unsigned> latched { 0 }, heldButtons { 0 };
    unsigned lastLiveMask = 0;
    std::array<int, 6> lastVoicing {};
    struct UiNote { int note; bool on; float vel; };
    std::array<UiNote, 64> uiQueue;
    std::atomic<int> uiWrite { 0 }, uiRead { 0 };

    // display
    mutable juce::SpinLock displayLock;
    Display display;
    std::array<std::atomic<bool>, 128> soundingOut {};
    std::atomic<int> bassSoundingOut { -1 };
    std::array<std::atomic<float>, 1024> scope {};
    std::atomic<int> scopeWrite { 0 };
    std::atomic<double> currentBpm { 110.0 };
    float bendSemis = 0.0f, modWheel = 0.0f;
    float dcL = 0.0f, dcR = 0.0f, dcInL = 0.0f, dcInR = 0.0f;
    juce::AudioBuffer<float> work;
};

} // namespace wis::daw
