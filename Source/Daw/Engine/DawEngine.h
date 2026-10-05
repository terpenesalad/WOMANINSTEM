#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Daw/Model/Project.h"
#include "Daw/Plugins/PluginHost.h"
#include "AudioCache.h"
#include <bitset>

namespace wis::daw
{

// ---- Real-time building blocks (built on the message thread, used by the audio thread) -----------------

/** One plugin instance in a track's instrument slot or insert chain. */
struct Slot : public juce::ReferenceCountedObject
{
    using Ptr = juce::ReferenceCountedObjectPtr<Slot>;

    juce::String slotId;                       // PLUGIN node "id"
    juce::String uid;                          // what was instantiated (re-create if it changes)
    std::unique_ptr<juce::AudioProcessor> proc;
    juce::String error;
    bool instrument = false;
    std::atomic<bool> bypass { false };
    juce::AudioBuffer<float> scratch;
    int channels = 2;

    void prepare (double sr, int block);
    /** Processes `n` samples of the stereo buffer in place. */
    void process (juce::AudioBuffer<float>& stereo, int n, juce::MidiBuffer& midi);
};

/** Per-track state shared by snapshots (mixer values, meters, audio-thread scratch). */
struct TrackRT : public juce::ReferenceCountedObject
{
    using Ptr = juce::ReferenceCountedObjectPtr<TrackRT>;

    int id = 0;
    std::atomic<float> volumeDb { 0.0f }, pan { 0.0f };
    std::atomic<bool> mute { false }, solo { false }, arm { false }, monitor { false }, isInstrument { false };
    std::atomic<int> input { 0 };
    std::atomic<bool> inputStereo { false };
    std::atomic<float> meterL { 0.0f }, meterR { 0.0f }, inputMeter { 0.0f };

    // audio thread only
    juce::AudioBuffer<float> buffer;
    juce::MidiBuffer midi;
    std::bitset<128> playingNotes;
    bool sendNotesOff = false;
    float lastGainL = 1.0f, lastGainR = 1.0f;
};

/** Immutable picture of the arrangement in samples, swapped atomically into the audio thread. */
struct Snapshot : public juce::ReferenceCountedObject
{
    using Ptr = juce::ReferenceCountedObjectPtr<Snapshot>;

    struct AudioClipR
    {
        AudioData::Ptr data;
        juce::int64 start = 0, length = 0, offset = 0;   // timeline start, length, offset into the file (may be < 0)
        float gain = 1.0f;
        int fadeIn = 0, fadeOut = 0;
    };
    struct MidiEv { juce::int64 time; juce::uint8 bytes[3]; juce::uint8 size; };
    struct MidiClipR { juce::int64 start = 0, end = 0; std::vector<MidiEv> events; };
    struct Lane { std::vector<std::pair<juce::int64, float>> points; float valueAt (juce::int64 t) const; };
    struct TrackR
    {
        TrackRT::Ptr rt;
        bool instrument = false;
        Slot::Ptr inst;
        juce::ReferenceCountedArray<Slot> inserts;
        std::vector<AudioClipR> audio;
        std::vector<MidiClipR> midi;
        Lane volume, pan;
    };

    double sampleRate = 48000.0, tempo = 120.0, samplesPerBeat = 24000.0;
    int tsNum = 4, tsDen = 4;
    std::vector<TrackR> tracks;
    juce::ReferenceCountedArray<Slot> masterInserts;
    bool anySolo = false;
    bool cycleOn = false;
    juce::int64 cycleStart = 0, cycleEnd = 0;
    int missingAudio = 0;
};

/** A take being recorded. */
struct RecordingTrack
{
    int trackId = 0;
    bool audio = true;
    int input = 0;
    bool stereo = false;
    juce::File file;
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer;
    std::vector<float> peaks;               // live waveform (one value per 512 samples)
    std::atomic<int> numPeaks { 0 };
    float peakAcc = 0.0f; int peakCount = 0; // audio thread
};

struct LiveNote { int pitch; int velocity; juce::int64 on, off; };   // off < 0 while held

// ---- The engine ----------------------------------------------------------------------------------------
class DawEngine : public juce::AudioIODeviceCallback,
                  public juce::AudioPlayHead,
                  private juce::ValueTree::Listener,
                  private juce::Timer,
                  private juce::ChangeListener,
                  private juce::MidiInputCallback
{
public:
    DawEngine (Project& project, PluginHost& host);
    ~DawEngine() override;

    /** Attach to / detach from the audio device (the app switches between Play-Along and Studio). */
    void attach (juce::AudioDeviceManager& dm);
    void detach();
    bool isAttached() const { return deviceManager != nullptr; }

    /** Prepares for offline use without a device (tests / command line). */
    void prepareOffline (double sampleRate, int blockSize);

    // ---- transport (message thread) ----
    void play();
    void stop();
    void togglePlay()                { isPlaying() || isCountingIn() ? stop() : play(); }
    void record();
    bool isPlaying() const           { return playing.load(); }
    bool isRecording() const         { return recording.load(); }
    bool isCountingIn() const        { return countInRemaining.load() > 0; }
    double getPositionBeats() const;
    double getPositionSeconds() const;
    void setPositionBeats (double beats);
    double getSampleRate() const     { return sampleRate; }

    std::atomic<int> selectedTrackId { 0 };
    std::atomic<float> metronomeVolumeDb { -6.0f };
    juce::MidiKeyboardState keyboardState;

    // ---- meters / state for the UI ----
    std::pair<float, float> readTrackMeter (int trackId);
    float readTrackInputMeter (int trackId);
    std::pair<float, float> readMasterMeter() { return { masterL.exchange (0.0f), masterR.exchange (0.0f) }; }
    bool readClipIndicator() { return clipped.exchange (false); }

    struct LiveRecording { int trackId; bool audio; double startBeat, endBeat; std::vector<float> peaks; double peakSeconds; std::vector<LiveNote> notes; };
    std::vector<LiveRecording> getLiveRecordings();

    // ---- plugins ----
    juce::AudioProcessor* getProcessor (const juce::String& slotId);
    juce::String getSlotError (const juce::String& slotId);
    /** Writes every plugin's current state into its PLUGIN node (before saving / removing). */
    void flushPluginStates();
    void flushPluginState (const juce::ValueTree& pluginNode);
    std::function<void (const juce::String& slotId)> onSlotRemoved;   // close editor windows
    std::function<void (const juce::String& message)> onError;
    std::function<void()> onRecordingFinished;

    AudioCache& getCache() { return cache; }
    PluginHost& getHost() { return host; }

    // ---- export ----
    struct ExportOptions
    {
        juce::File file;
        int format = 0;                 // 0 WAV, 1 FLAC, 2 OGG
        int bitDepth = 24;
        double sampleRate = 0;          // 0 = engine rate
        double startBeat = 0, endBeat = 0;
        double tailSeconds = 2.0;
        bool normalise = false;
        juce::Array<int> onlyTracks;    // empty = full mix
    };
    /** Convenience: beginExport + renderExport + endExport. */
    juce::String exportAudio (const ExportOptions& o, const std::function<bool (float)>& progress);
    /** Message thread: detaches from the device and prepares the song for offline rendering. */
    void beginExport();
    /** Any thread (between begin/end): renders and writes the file. */
    juce::String renderExport (const ExportOptions& o, const std::function<bool (float)>& progress);
    /** Message thread: reattaches to the device. */
    void endExport();

    /** Forces an immediate snapshot rebuild (normally done on a timer after edits). */
    void rebuildNow();

    // AudioPlayHead
    juce::Optional<PositionInfo> getPosition() const override;

    // AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut, int n,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;

    /** Renders one block of the whole song (also used offline). */
    void renderBlock (const float* const* in, int numIn, float* const* out, int numOut, int n);

private:
    // ValueTree::Listener
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override       { markDirty(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { markDirty(); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override        { markDirty(); }
    void valueTreeRedirected (juce::ValueTree&) override                          { markDirty(); }
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override { markDirty(); }
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;

    void markDirty() { dirty = true; }
    void rebuild();
    void syncMixValues();
    TrackRT::Ptr runtimeFor (int trackId);
    Slot::Ptr slotFor (const juce::ValueTree& pluginNode, bool instrument);
    void prepareAll();

    void renderSegment (Snapshot& s, juce::int64 t0, int len, int outOffset, bool rolling, const float* const* in, int numIn);
    void renderClick (const Snapshot& s, int outOffset, int len, juce::int64 t0);
    void sendNoteOffsToAll (Snapshot& s);
    void startRecordingSession();
    void finishRecording();
    void drainMidiFifo();

    Project& project;
    PluginHost& host;
    AudioCache cache;
    juce::AudioDeviceManager* deviceManager = nullptr;
    juce::AudioDeviceManager* exportDevice = nullptr;

    double sampleRate = 48000.0;
    int maxBlock = 1024;
    int inputLatency = 0, outputLatency = 0;

    // snapshots
    juce::SpinLock snapLock;
    Snapshot::Ptr pending, audioSnap;
    juce::ReferenceCountedArray<Snapshot> graveyard;
    std::map<int, TrackRT::Ptr> runtimes;
    std::map<juce::String, Slot::Ptr> slots;
    juce::ReferenceCountedArray<Slot> slotGraveyard;
    std::atomic<bool> dirty { true };
    juce::CriticalSection processLock;

    // transport
    std::atomic<bool> playing { false }, recording { false };
    std::atomic<juce::int64> position { 0 }, seekRequest { -1 };
    std::atomic<juce::int64> countInRemaining { 0 };
    std::atomic<bool> stopRequest { false };
    std::atomic<bool> offline { false };
    juce::int64 countInTotal = 0;
    std::atomic<float> masterVolume { 1.0f };
    std::atomic<bool> metronomeOn { false };
    juce::int64 playheadForPlugins = 0;
    bool playheadRolling = false;

    // click
    float clickPhase = 0, clickFreq = 1000, clickAmp = 0;
    int clickRemaining = 0;

    // buffers
    juce::AudioBuffer<float> master, clickBuf;
    juce::MidiBuffer liveMidi;
    juce::MidiMessageCollector midiCollector;

    // meters
    std::atomic<float> masterL { 0 }, masterR { 0 };
    std::atomic<bool> clipped { false };

    // recording
    struct RecMidi { juce::int64 time; int trackId; juce::uint8 b0, b1, b2; };
    juce::AbstractFifo midiFifo { 8192 };
    std::vector<RecMidi> midiFifoData = std::vector<RecMidi> (8192);
    std::vector<std::unique_ptr<RecordingTrack>> recTracks;     // message thread owns; audio thread reads while recording
    std::atomic<bool> recSessionActive { false };
    juce::int64 recStartSample = 0;
    std::atomic<juce::int64> recSamples { 0 };
    std::map<int, std::vector<LiveNote>> liveNotes;
    juce::CriticalSection liveLock;
    bool recCycle = false;
    juce::int64 recCycleStart = 0, recCycleEnd = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DawEngine)
};

} // namespace wis::daw
