#pragma once

#include "BuiltinProcessor.h"

namespace wis::daw
{

/** "Loop Station": a looper pedal on a track. One button records, then plays, then overdubs layer on layer
    (undo / redo the last layer). Loops can snap to whole bars of the song, play at half speed or reversed,
    and be dropped onto the track as an audio clip. */
class Looper : public BuiltinProcessor
{
public:
    enum State { empty, recording, playing, overdubbing, stopped };

    Looper();
    void prepareToPlay (double sr, int block) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    double getTailLengthSeconds() const override { return 0.0; }
    juce::AudioProcessorEditor* createCustomEditor() override { return editorFactory ? editorFactory (*this) : nullptr; }
    static std::function<juce::AudioProcessorEditor* (Looper&)> editorFactory;

    // pedal buttons (message thread; acted on at the start of the next block, or the next bar when synced)
    void pressMain();     // rec -> play -> dub -> play ...
    void pressStop();
    void pressUndo();
    void pressRedo();
    void pressClear();

    State getState() const          { return (State) state.load(); }
    bool isWaitingForBar() const    { return pending.load() != 0; }
    double getLoopSeconds() const   { return loopLength.load() / juce::jmax (1.0, sampleRate); }
    float getPosition() const       { return loopLength.load() > 0 ? (float) playPos.load() / (float) loopLength.load() : 0.0f; }
    int getLayers() const           { return layers.load(); }
    bool canRedo() const            { return redoAvailable.load(); }
    /** Overview of the loop for drawing (64 points, refreshed as it changes). */
    std::array<float, 64> getOverview() const;

    /** Writes the loop to a WAV file (message thread). */
    juce::String exportLoop (const juce::File& f);

private:
    void apply (int action);
    double sampleRate = 48000.0;
    static constexpr int maxSeconds = 120;
    juce::AudioBuffer<float> loop, undoBuf;
    std::atomic<int> state { empty }, pending { 0 }, layers { 0 };
    std::atomic<int> loopLength { 0 }, playPos { 0 };
    std::atomic<bool> redoAvailable { false };
    int recordLength = 0;
    double readPos = 0.0;
    float fade = 0.0f;
    mutable juce::SpinLock overviewLock;
    std::array<float, 64> overview {};
    juce::int64 lastBar = -1;
};

} // namespace wis::daw
