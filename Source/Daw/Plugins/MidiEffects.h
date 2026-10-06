#pragma once

#include "BuiltinProcessor.h"

namespace wis::daw
{

/** Shared plumbing for MIDI effects: a sample-accurate queue of future events and the song's clock. */
class MidiFxBase : public BuiltinProcessor
{
public:
    MidiFxBase (const juce::String& id, const juce::String& name, juce::AudioProcessorValueTreeState::ParameterLayout layout);
    bool isMidiFx() const override { return true; }
    double getTailLengthSeconds() const override { return 0.0; }
    void prepareToPlay (double sr, int) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    /** Scale helpers shared with Scale Lock / Auto-Tune. */
    static juce::StringArray scaleNames();
    static const std::vector<int>& scaleSteps (int scale);
    static int snapToScale (int note, int key, int scale, bool upOnTie = true);

protected:
    struct Clock { double ppq = 0, bpm = 120, beatsPerSample = 0; bool playing = false; };
    /** Called once per block with the incoming MIDI and the clock at the block start. Write events with emit()/schedule(). */
    virtual void process (const juce::MidiBuffer& in, int numSamples, const Clock& clock) = 0;
    virtual void resetState() {}   // forget held notes (on stop / panic)

    void emit (const juce::MidiMessage& m, int offset)             { out->addEvent (m, juce::jlimit (0, blockLength - 1, offset)); }
    void schedule (const juce::MidiMessage& m, juce::int64 atSample);   // absolute engine sample time
    juce::int64 blockStart() const { return now; }
    double sampleRate = 48000.0;

private:
    struct Scheduled { juce::int64 time; juce::uint8 data[3]; int size; };
    std::vector<Scheduled> queue;
    juce::MidiBuffer output;
    juce::MidiBuffer* out = &output;
    juce::int64 now = 0;
    int blockLength = 1;
};

/** Arpeggiator: up/down/random/as-played/chord patterns synced to the song, with octaves, gate, swing,
    latch, rhythm patterns, probability and ratchets for the more experimental stuff. */
class Arpeggiator : public MidiFxBase
{
public:
    Arpeggiator();
    juce::StringArray getProgramNames() override;
    void loadProgram (int) override;
    static juce::StringArray rateNames();
    static double rateBeats (int index);
    std::atomic<int> lastStep { -1 };
private:
    void process (const juce::MidiBuffer& in, int n, const Clock& c) override;
    void resetState() override;
    std::vector<std::pair<int, int>> held, latched;   // note, velocity (in the order played)
    bool allReleased = true;
    double nextStep = -1.0, freePpq = 0.0;
    int stepIndex = 0, sounding = -1, upDownDir = 1, walk = 0;
    juce::Random rng;
    bool wasPlaying = false;
};

/** Chord Trigger: one key plays a whole chord (with inversions and spread). */
class ChordTrigger : public MidiFxBase
{
public:
    ChordTrigger();
    static juce::StringArray chordNames();
private:
    void process (const juce::MidiBuffer& in, int n, const Clock& c) override;
    void resetState() override { playing.clear(); }
    std::map<int, std::vector<int>> playing;   // trigger note -> notes sent
};

/** Scale Lock: keeps every note in a key (snap to the nearest note, or drop wrong ones). */
class ScaleLock : public MidiFxBase
{
public:
    ScaleLock();
private:
    void process (const juce::MidiBuffer& in, int n, const Clock& c) override;
    void resetState() override { mapped.clear(); }
    std::map<int, int> mapped;   // input note -> output note (-1 dropped)
};

/** Note Echo: repeats each note, synced, with decaying velocity and an optional pitch step per repeat. */
class NoteEcho : public MidiFxBase
{
public:
    NoteEcho();
private:
    void process (const juce::MidiBuffer& in, int n, const Clock& c) override;
};

/** Randomizer: humanise velocity and timing, randomly drop notes or jump octaves. */
class Randomizer : public MidiFxBase
{
public:
    Randomizer();
private:
    void process (const juce::MidiBuffer& in, int n, const Clock& c) override;
    void resetState() override { shifted.clear(); }
    juce::Random rng;
    std::map<int, int> shifted;   // input note -> output note (-1 dropped)
};

} // namespace wis::daw
