#pragma once

#include "Daw/Plugins/BuiltinProcessor.h"
#include "Sampler.h"
#include "DrumSynth.h"

namespace wis::daw
{

/** "Beat Lab": a groovebox for messing around with beats and loops (in the spirit of pocket samplers and
    step-sequencer drum machines). 8 lanes - synth drums, your own samples or chopped loops - on a step
    sequencer with per-step velocity, probability, ratchets (rolls), pitch, micro-timing and reverse; 8 patterns;
    per-lane length (polymeter) and rate; filter / drive / crush per lane, reverb and delay sends; hold-to-play
    performance effects (stutter, tape stop, reverse, filter sweep); and an IDM section (mutate, chaos,
    break shuffle) for glitchy, drill'n'bass-style experiments. */
class BeatLab : public BuiltinProcessor, private juce::AsyncUpdater, private juce::AudioProcessorValueTreeState::Listener
{
public:
    static constexpr int numLanes = 8, numPatterns = 8, maxSteps = 64, firstLaneNote = 36;
    enum Source { synth = 0, sample };
    enum LaneMode { oneShot = 0, loopSlice, loopRepitch };
    static constexpr int glitchKit = DrumSynth::numKits;   // extra kit: synthesized glitch sounds

    BeatLab();
    ~BeatLab() override;

    void prepareToPlay (double sr, int block) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    double getTailLengthSeconds() const override { return 4.0; }
    void saveExtraState (juce::ValueTree&) override;
    void loadExtraState (const juce::ValueTree&) override;
    juce::StringArray getProgramNames() override { return presetNames(); }
    static juce::StringArray presetNames();
    void loadProgram (int index) override;
    juce::AudioProcessorEditor* createCustomEditor() override { return editorFactory ? editorFactory (*this) : nullptr; }
    static std::function<juce::AudioProcessorEditor* (BeatLab&)> editorFactory;

    // ---- steps (any thread; lock-free) ----
    struct Step
    {
        bool on = false;
        int velocity = 100;      // 1..127
        int probability = 100;   // 0..100 %
        int ratchet = 1;         // 1..16 hits in the step
        int ratchetShape = 0;    // 0 even, 1 pitch up, 2 pitch down, 3 fade out
        int pitch = 0;           // -24..+24 semitones
        int nudge = 0;           // -8..+7 (sixteenths of a step)
        bool reverse = false;
        juce::uint32 pack() const;
        static Step unpack (juce::uint32);
    };
    Step getStep (int pattern, int lane, int step) const;
    void setStep (int pattern, int lane, int step, const Step& s);
    void clearPattern (int pattern, int lane = -1);
    void copyPattern (int from, int to);
    /** Fills a lane with a Euclidean rhythm (hits spread as evenly as possible). */
    void euclid (int pattern, int lane, int hits, int rotate = 0);
    /** Random pattern for a lane, from sensible to wild. */
    void randomise (int pattern, int lane, float density, float wildness);
    int getPatternPlaying() const   { return patternPlaying.load(); }
    int getPatternEditing() const   { return juce::jlimit (0, numPatterns - 1, (int) param ("pattern")); }

    // ---- lane sounds (message thread) ----
    juce::String loadLaneFile (int lane, const juce::File& f);
    void loadLaneBuffer (int lane, juce::AudioBuffer<float> b, double sampleRate, const juce::String& name);
    void useSynthSound (int lane);
    juce::String laneName (int lane) const;
    bool laneHasUserSample (int lane) const { return laneRefs[(size_t) lane].isNotEmpty() || laneUserBuffer[(size_t) lane]; }
    SampleData* laneSample (int lane) const { return laneSamples[(size_t) lane].get(); }
    static juce::StringArray voiceNamesForKit (int kit);
    static juce::StringArray kitNames();
    static juce::String laneParam (int lane, const char* name) { return "l" + juce::String (lane) + "_" + name; }
    void rebuildSoundsNow();   // tests / offline

    // ---- transport & performance (message thread) ----
    void startStop()                  { internalRunning = ! internalRunning.load(); if (internalRunning) restartRequested = true; }
    bool isRunning() const            { return internalRunning.load() || hostRolling.load(); }
    void triggerLane (int lane, float velocity) { auditionRequest = lane; auditionVelocity = velocity; }
    enum Perf { perfNone = 0, perfStutter4, perfStutter8, perfStutter16, perfStutter32, perfStutter64, perfTapeStop, perfReverse, perfFill };
    void setPerformance (int perf)    { performance = perf; }
    int getPerformance() const        { return performance.load(); }

    /** The current pattern as MIDI (lane i = note 36 + i), for putting it in the song. Length in beats. */
    juce::MidiMessageSequence patternToMidi (int pattern, double& lengthBeats) const;
    static std::function<void (BeatLab&, const juce::MidiMessageSequence&, double lengthBeats)> onPatternToSong;

    // ---- for the editor ----
    std::array<std::atomic<int>, numLanes> laneStep;       // step under the playhead (-1 = stopped)
    std::array<std::atomic<float>, numLanes> laneFlash;    // 1 on a hit, the editor fades it

    static int stepsForRate (int rateIndex);               // grid columns that make one bar at a rate
    static double rateBeats (int rateIndex);
    static juce::StringArray rateNames();

private:
    void handleAsyncUpdate() override { rebuildSoundsNow(); }
    void parameterChanged (const juce::String& id, float) override;
    void buildLaneSound (int lane);
    void startVoice (int lane, int offset, float velocity, double pitchSemis, bool reverse, int sliceIndex, int sliceCount, bool repitch);
    void runSequencer (int n, double ppqStart, double beatsPerSample, double bpb);
    void renderVoices (int n);
    void renderMaster (juce::AudioBuffer<float>& out, int n, double bpm);

    std::array<std::atomic<juce::uint32>, (size_t) (numPatterns * numLanes * maxSteps)> steps;
    std::array<SampleSlot, numLanes> laneSamples;
    std::array<juce::String, numLanes> laneRefs;           // user sample references ("" = synth sound)
    std::array<bool, numLanes> laneUserBuffer {};          // a user buffer without a file (from a clip)
    std::array<std::atomic<bool>, numLanes> laneDirty;

    struct Voice;
    std::unique_ptr<std::array<Voice, 48>> voices;
    struct LaneFx;
    std::unique_ptr<std::array<LaneFx, numLanes>> laneFx;
    struct Master;
    std::unique_ptr<Master> master;

    double sr = 48000.0;
    int blockSize = 512;
    juce::AudioBuffer<float> laneBuf, revBus, dlyBus;
    juce::Random rng;
    std::atomic<bool> internalRunning { false }, hostRolling { false }, restartRequested { false };
    std::atomic<int> patternPlaying { 0 }, performance { 0 }, auditionRequest { -1 };
    std::atomic<float> auditionVelocity { 0.8f };
    double internalPpq = 0.0, currentBpm = 120.0;
    juce::int64 lastBar = -1;
    int chainPos = 0;
    bool wasRolling = false;
};

} // namespace wis::daw
